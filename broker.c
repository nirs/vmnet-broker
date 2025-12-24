#include <arpa/inet.h>
#include <dispatch/dispatch.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <time.h>
#include <vmnet/vmnet.h>
#include <xpc/xpc.h>

#include "vmnet-broker.h"
#include "log.h"

struct peer {
    xpc_connection_t connection;
    pid_t pid;
};

// Shared network used by one of more clients.
// TODO: Keep reference count.
struct network {
    vmnet_network_ref ref;
    xpc_object_t serialization;
};

bool verbose = true;

// Accepting XPC connections.
static xpc_connection_t listener;

// Shared network vended to clients. Created when the first client request a
// network.
// TODO: destroy when the last client disconnects.
static struct network *shared_network;

// Number of conected peers, used to prevent termination when peers are
// connected. Using signed int to make it easy to detect incorrect counting.
static int connected_peers;

static void free_network(const struct peer *peer, struct network *network) {
    if (network) {
        INFOF("[peer %d] deleting network", peer->pid);

        if (network->ref) {
            CFRelease(network->ref);
        }
        if (network->serialization) {
            xpc_release(network->serialization);
        }
        free(network);
    }
}

static vmnet_network_configuration_ref network_config(const struct peer *peer) {
    vmnet_return_t status;

    // TODO: Use mode from broker network configuration.
    vmnet_network_configuration_ref config = vmnet_network_configuration_create(VMNET_SHARED_MODE, &status);
    if (config == NULL) {
        WARNF("[peer %d] failed to create network configuration: (%d) %s", peer->pid, status, vmnet_strerror(status));
        return NULL;
    }

    // TODO: Add configuration options from broker network config.
    // TODO: Log network configuration, showing the defaults we get from vmnet.

    return config;
}

static struct network *create_network(const struct peer *peer) {
    vmnet_return_t status;

    INFOF("[peer %d] creating network", peer->pid);

    struct network *network = calloc(1, sizeof(*network));
    if (network == NULL) {
        WARNF("[peer %d] failed to allocate network: %s", peer->pid, strerror(errno));
        return NULL;
    }

    vmnet_network_configuration_ref config = network_config(peer);
    if (config == NULL) {
        goto error;
    }

    network->ref = vmnet_network_create(config, &status);
    if (network->ref == NULL) {
        WARNF("[peer %d] failed to create network ref: (%d) %s", peer->pid, status, vmnet_strerror(status));
        goto error;
    }

    network->serialization = vmnet_network_copy_serialization(network->ref, &status);
    if (network->serialization == NULL) {
        WARNF("[peer %d] failed to create network serialization: (%d) %s", peer->pid, status, vmnet_strerror(status));
        goto error;
    }

    return network;

error:
    if (config) {
        CFRelease(config);
    }
    free_network(peer, network);
    return NULL;
}

static void send_error(const struct peer *peer, xpc_object_t event, int code, const char *message) {
    WARNF("[peer %d] send error: (%d) %s", peer->pid, code, message);

    xpc_object_t reply = xpc_dictionary_create_reply(event);
    if (reply == NULL) {
        // Event does not include the return address.
        char *desc = xpc_copy_description(event);
        WARNF("[peer %d] failed create reply for event: %s", peer->pid, desc);
        free(desc);
        return;
    }

    xpc_object_t error = xpc_dictionary_create_empty();
    xpc_dictionary_set_int64(error, ERROR_CODE, code);
    xpc_dictionary_set_string(error, ERROR_MESSAGE, message);

    xpc_dictionary_set_value(reply, REPLY_ERROR, error);
    xpc_clear(&error);

    if (verbose) {
        char *desc = xpc_copy_description(reply);
        DEBUGF("[peer %d] send error reply: %s", peer->pid, desc);
        free(desc);
    }

    xpc_connection_send_message(peer->connection, reply);

    xpc_release(reply);
}

static void send_network(const struct peer *peer, xpc_object_t event, struct network *network) {
    INFOF("[peer %d] send reply", peer->pid);

    xpc_object_t reply = xpc_dictionary_create_reply(event);
    if (reply == NULL) {
        // Event does not include the return address.
        char *desc = xpc_copy_description(event);
        WARNF("[peer %d] failed create reply for event: %s", peer->pid, desc);
        free(desc);
        return;
    }

    xpc_dictionary_set_value(reply, REPLY_NETWORK, network->serialization);
    xpc_connection_send_message(peer->connection, reply);

    xpc_release(reply);
}

static void handle_error(const struct peer *peer, xpc_object_t event) {
    if (event == XPC_ERROR_CONNECTION_INVALID) {
        // Client connection is dead - invalidate resources owned by client.
        connected_peers--;
        INFOF("[peer %d] disconnected (connected peers %d)", peer->pid, connected_peers);
    } else if (event == XPC_ERROR_CONNECTION_INTERRUPTED) {
        // Temporary interruption, may recover.
        INFOF("[peer %d] temporary interruption", peer->pid);
    } else {
        // Unexpected error.
        const char *desc = xpc_dictionary_get_string(event, XPC_ERROR_KEY_DESCRIPTION);
        WARNF("[peer %d] unexpected error: %s", peer->pid, desc);
    }
}

static void handle_request(const struct peer *peer, xpc_object_t event) {
    const char* command = xpc_dictionary_get_string(event, REQUEST_COMMAND);
    if (command == NULL) {
        send_error(peer, event, ERROR_INVALID_REQUEST, "no command");
        return;
    }
    if (strcmp(command, COMMAND_GET) != 0) {
        send_error(peer, event, ERROR_INVALID_REQUEST, "unknown command");
        return;
    }

    if (shared_network == NULL) {
        shared_network = create_network(peer);
        if (shared_network == NULL) {
            send_error(peer, event, ERROR_CREATE_NETWORK, "failed to create network");
            return;
        }
    }
    send_network(peer, event, shared_network);
}

static void handle_connection(xpc_connection_t connection) {
    // Until we manage list of active peers, this is a good place to keep the
    // peer. It is shared with all requests on this connection via the handler
    // block. We need to capture the pid now since it is not available when the
    // connection invalidates.
    struct peer peer = {
        .connection = connection,
        .pid = xpc_connection_get_pid(connection),
    };

    connected_peers++;
    INFOF("[peer %d] connected (connected peers %d)", peer.pid, connected_peers);

    // TODO: register the peer and extract the audit token
    // TODO: authorize the client using the audit token

    xpc_connection_set_event_handler(peer.connection, ^(xpc_object_t event) {
        xpc_type_t type = xpc_get_type(event);
        if (type == XPC_TYPE_ERROR) {
            handle_error(&peer, event);
        } else if (type == XPC_TYPE_DICTIONARY) {
            handle_request(&peer, event);
        }
    });

    xpc_connection_resume(connection);
}

static void setup_listener(void) {
    DEBUG("[main] setting up listener");

    // We use the main queue to minimize memory. The broker is mosly idle and
    // handle few clients in its lifetime. There is no reason to have more than
    // one thread.
    listener = xpc_connection_create_mach_service(
        MACH_SERVICE_NAME,
        dispatch_get_main_queue(),
        XPC_CONNECTION_MACH_SERVICE_LISTENER
    );

    xpc_connection_set_event_handler(listener, ^(xpc_object_t event) {
        xpc_type_t type = xpc_get_type(event);
        if (type == XPC_TYPE_ERROR) {
            // We don't expect any non fatal errors.
            ERRORF("[main] listener failed: %s", xpc_dictionary_get_string(event, XPC_ERROR_KEY_DESCRIPTION));
            exit(EXIT_FAILURE);
        } else if (type == XPC_TYPE_CONNECTION) {
            // Use the same queue for all peers. This ensures that we don't need
            // any locks when modfying internal state, and all events are
            // serialized.
            xpc_connection_set_target_queue(event, dispatch_get_main_queue());

            handle_connection(event);
        }
    });

    xpc_connection_resume(listener);
}

static void setup_signal_handlers(void) {
    DEBUG("[main] setting up signal handlers");

    int signals[] = {SIGINT, SIGTERM};

    for (int i = 0; i < ARRAY_SIZE(signals); i++) {
        int sig = signals[i];

        // Ignore the signal so we can handle it on the runloop.
        signal(sig, SIG_IGN);

        dispatch_source_t source = dispatch_source_create(
            DISPATCH_SOURCE_TYPE_SIGNAL,
            sig,
            0,
            dispatch_get_main_queue()
        );

        // 5. Set the event handler (the "block" that runs when signal is received)
        dispatch_source_set_event_handler(source, ^{
            INFOF("[main] received signal %d", sig);

            // IMPORTANT: terminating the broker when clients are connected will
            // cause the network to become orphaned, and we will not be able to
            // create it again. The next time the broker is restarted, creating
            // the same network will fail. If we use dymanic subnet, we will get
            // the next available subnet, which will change the VM IP address.
            if (connected_peers > 0) {
                INFOF("[main] %d peer connected - ignoring termination signal", connected_peers);
                return;
            }

            INFO("[main] no active clients - terminating");
            exit(EXIT_SUCCESS);
        });

        dispatch_resume(source);
    }
}

int main() {
    INFOF("[main] starting pid=%d", getpid());

    setup_signal_handlers();
    setup_listener();

    dispatch_main();
}
