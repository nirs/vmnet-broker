#include <arpa/inet.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdlib.h>
#include <dispatch/dispatch.h>
#include <sys/socket.h>
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

// Mutex protecting shared data accessed by concurrent peers event handlers.
static pthread_mutex_t shared_mutex = PTHREAD_MUTEX_INITIALIZER;

// Shared network vended to clients. Created when the first client request a
// network.
// TODO: destroy when the last client disconnects.
static struct network *shared_network;

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

    const char *subnet = "192.168.32.1";
    const char *mask = "255.255.255.0";
    struct in_addr subnet_addr;
    struct in_addr subnet_mask;

    if (inet_pton(AF_INET, subnet, &subnet_addr) == 0) {
        WARNF("[peer %d] failed to parse subnet '%s': %s", peer->pid, subnet, strerror(errno));
        goto error;
    }
    if (inet_pton(AF_INET, mask, &subnet_mask) == 0) {
        WARNF("[peer %d] failed to parse mask '%s': %s", peer->pid, mask, strerror(errno));
        goto error;
    }

    status = vmnet_network_configuration_set_ipv4_subnet(config, &subnet_addr, &subnet_mask);
    if (status != VMNET_SUCCESS) {
        WARNF("[peer %d] failed to set ipv4 subnet: (%d) %s", peer->pid, status, vmnet_strerror(status));
        goto error;
    }

    return config;

error:
    CFRelease(config);
    return NULL;
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
        INFOF("[peer %d] disconnected", peer->pid);
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

    pthread_mutex_lock(&shared_mutex);

    if (shared_network == NULL) {
        shared_network = create_network(peer);
        if (shared_network == NULL) {
            pthread_mutex_unlock(&shared_mutex);
            send_error(peer, event, ERROR_CREATE_NETWORK, "failed to create network");
            return;
        }
    }
    send_network(peer, event, shared_network);

    pthread_mutex_unlock(&shared_mutex);
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

    INFOF("[peer %d] connected", peer.pid);

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

int main() {
    INFOF("[main] starting pid=%d", getpid());
    xpc_connection_t listener = xpc_connection_create_mach_service(MACH_SERVICE_NAME, NULL, XPC_CONNECTION_MACH_SERVICE_LISTENER);

    xpc_connection_set_event_handler(listener, ^(xpc_object_t event) {
        xpc_type_t type = xpc_get_type(event);
        if (type == XPC_TYPE_ERROR) {
            // We don't expect any non fatal errors.
            ERRORF("[main] listener failed: %s", xpc_dictionary_get_string(event, XPC_ERROR_KEY_DESCRIPTION));
            exit(EXIT_FAILURE);
        } else if (type == XPC_TYPE_CONNECTION) {
            handle_connection(event);
        }
    });

    xpc_connection_resume(listener);
    dispatch_main();
}
