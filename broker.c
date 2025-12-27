#include <dispatch/dispatch.h>
#include <errno.h>
#include <stdlib.h>

#include "vmnet-broker.h"
#include "common.h"
#include "log.h"

struct context {
    char name[sizeof("peer 9223372036854775807")];
    xpc_connection_t connection;
};

// Shared network used by one of more clients.
// TODO: Keep reference count.
struct network {
    vmnet_network_ref ref;
    xpc_object_t serialization;
};

bool verbose = true;

// The context used for main() and signal handlers.
static const struct context main_context = { .name = "main" };

// Time to wait in seconds before shutting down after the broker became idle. We
// want to keep the network reservation in case a user want to use the same
// network soon.
// TODO: Read from user perferences.
static const int idle_timeout_sec = 30;

// Accepting XPC connections.
static xpc_connection_t listener;

// Shared network vended to clients. Created when the first client request a
// network. Automatically released by shutting down after idle timeout.
static struct network *shared_network;

// Number of conected peers, used to prevent termination when peers are
// connected. Using signed int to make it easy to detect incorrect counting.
static int connected_peers;

// Used to shutdown if the broker is idle for idle_timeout_sec.
static dispatch_source_t idle_timer;

static void init_context(struct context *ctx, xpc_connection_t connection) {
    snprintf(ctx->name, sizeof(ctx->name), "peer %d", xpc_connection_get_pid(connection));
    ctx->connection = connection;
}

static void free_network(const struct context *ctx, struct network *network) {
    if (network) {
        if (network->ref) {
            struct network_info info;
            network_info(network->ref, &info);
            INFOF("[%s] deleted network subnet '%s' mask '%s' ipv6_prefix '%s' prefix_len %d",
                ctx->name, info.subnet, info.mask, info.ipv6_prefix, info.prefix_len );
            CFRelease(network->ref);
        }
        if (network->serialization) {
            xpc_release(network->serialization);
        }
        free(network);
    }
}

static vmnet_network_configuration_ref network_config(const struct context *ctx) {
    vmnet_return_t status;

    // TODO: Use mode from broker network configuration.
    vmnet_network_configuration_ref config = vmnet_network_configuration_create(VMNET_SHARED_MODE, &status);
    if (config == NULL) {
        WARNF("[%s] failed to create network configuration: (%d) %s", ctx->name, status, vmnet_strerror(status));
        return NULL;
    }

    // TODO: Add configuration options from broker network config.
    // TODO: Log network configuration, showing the defaults we get from vmnet.

    return config;
}

static struct network *create_network(const struct context *ctx) {
    vmnet_return_t status;

    struct network *network = calloc(1, sizeof(*network));
    if (network == NULL) {
        WARNF("[%s] failed to allocate network: %s", ctx->name, strerror(errno));
        return NULL;
    }

    vmnet_network_configuration_ref config = network_config(ctx);
    if (config == NULL) {
        goto error;
    }

    network->ref = vmnet_network_create(config, &status);
    if (network->ref == NULL) {
        WARNF("[%s] failed to create network ref: (%d) %s", ctx->name, status, vmnet_strerror(status));
        goto error;
    }

    struct network_info info;
    network_info(network->ref, &info);
    INFOF("[%s] created network subnet '%s' mask '%s' ipv6_prefix '%s' prefix_len %d",
        ctx->name, info.subnet, info.mask, info.ipv6_prefix, info.prefix_len );

    network->serialization = vmnet_network_copy_serialization(network->ref, &status);
    if (network->serialization == NULL) {
        WARNF("[%s] failed to create network serialization: (%d) %s", ctx->name, status, vmnet_strerror(status));
        goto error;
    }

    return network;

error:
    if (config) {
        CFRelease(config);
    }
    free_network(ctx, network);
    return NULL;
}

static void send_error(const struct context *ctx, xpc_object_t event, int code, const char *message) {
    WARNF("[%s] send error: (%d) %s", ctx->name, code, message);

    xpc_object_t reply = xpc_dictionary_create_reply(event);
    if (reply == NULL) {
        // Event does not include the return address.
        char *desc = xpc_copy_description(event);
        WARNF("[%s] failed create reply for event: %s", ctx->name, desc);
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
        DEBUGF("[%s] send error reply: %s", ctx->name, desc);
        free(desc);
    }

    xpc_connection_send_message(ctx->connection, reply);

    xpc_release(reply);
}

static void send_network(const struct context *ctx, xpc_object_t event, struct network *network) {
    DEBUGF("[%s] send network to peer", ctx->name);

    xpc_object_t reply = xpc_dictionary_create_reply(event);
    if (reply == NULL) {
        // Event does not include the return address.
        char *desc = xpc_copy_description(event);
        WARNF("[%s] failed create reply for event: %s", ctx->name, desc);
        free(desc);
        return;
    }

    xpc_dictionary_set_value(reply, REPLY_NETWORK, network->serialization);
    xpc_connection_send_message(ctx->connection, reply);

    xpc_release(reply);
}

// TODO: Accept network name.
static struct network *get_network(const struct context *ctx) {
    if (shared_network == NULL) {
        shared_network = create_network(ctx);
    }
    return shared_network;
}

static void handle_request(const struct context *ctx, xpc_object_t event) {
    const char* command = xpc_dictionary_get_string(event, REQUEST_COMMAND);
    if (command == NULL) {
        send_error(ctx, event, ERROR_INVALID_REQUEST, "no command");
        return;
    }
    if (strcmp(command, COMMAND_GET) != 0) {
        send_error(ctx, event, ERROR_INVALID_REQUEST, "unknown command");
        return;
    }

    struct network *network = get_network(ctx);
    if (network == NULL) {
        send_error(ctx, event, ERROR_CREATE_NETWORK, "failed to create network");
        return;
    }

    send_network(ctx, event, network);
}

// Avoid orphaned network if broker is stopped before clients.
static void shutdown_shared_network(const struct context *ctx) {
    if (shared_network) {
        DEBUGF("[%s] shutdown shared network", ctx->name);
        free_network(ctx, shared_network);
        shared_network = NULL;
    }
}

static void shutdown_later(const struct context *ctx) {
    DEBUGF("[%s] shutting down in %d seconds", ctx->name, idle_timeout_sec);

    // This is impossible since the first connected peer canceled the timer, and
    // shutdown_later is called when the last peer has disconnected.
    assert(idle_timer == NULL && "idle timer running in shutdown_later");

    idle_timer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, dispatch_get_main_queue());

    assert(idle_timer != NULL && "failed to create idle timer");

    dispatch_time_t start = dispatch_time(DISPATCH_TIME_NOW, idle_timeout_sec * NSEC_PER_SEC);
    // Allow the system up to 1 second leeway if this can improve power
    // consumption and system performance.
    uint64_t leeway = 1 * NSEC_PER_SEC;

    dispatch_source_set_timer(idle_timer, start, DISPATCH_TIME_FOREVER, leeway);

    dispatch_source_set_event_handler(idle_timer, ^{
        INFOF("[%s] idle timeout - shutting down", main_context.name);
        shutdown_shared_network(&main_context);
        exit(EXIT_SUCCESS);
    });

    dispatch_resume(idle_timer);
}

static void add_peer(const struct context *ctx) {
    connected_peers++;

    INFOF("[%s] connected (connected peers %d)", ctx->name, connected_peers);

    if (connected_peers == 1) {
        // Create a transaction so launchd will know that we are active and will
        // not try to stop the service to free resources.
        DEBUGF("[%s] starting transaction to prevent termination while peers are connected", ctx->name);
        xpc_transaction_begin();

        if (idle_timer) {
            DEBUGF("[%s] canceling idle shutdown", ctx->name);
            dispatch_source_cancel(idle_timer);
            idle_timer = NULL;
        }
    }
}

static void remove_peer(const struct context *ctx) {
    connected_peers--;

    INFOF("[%s] disconnected (connected peers %d)", ctx->name, connected_peers);

    if (connected_peers == 0) {
        // This is the last peer - end the transaction so launchd will be able
        // stop the broker quickly if needed.
        DEBUGF("[%s] ending transaction - broker can be stopped", ctx->name);
        xpc_transaction_end();

        // Shut down if we are idle for long time.
        shutdown_later(ctx);
    }
}

static void handle_connection(xpc_connection_t connection) {
    // TODO: authorize the peer using the audit token

    struct context ctx;
    init_context(&ctx, connection);

    add_peer(&ctx);

    xpc_connection_set_event_handler(ctx.connection, ^(xpc_object_t event) {
        xpc_type_t type = xpc_get_type(event);
        if (type == XPC_TYPE_ERROR) {
            if (event == XPC_ERROR_CONNECTION_INVALID) {
                // Client connection is dead.
                remove_peer(&ctx);
            } else if (event == XPC_ERROR_CONNECTION_INTERRUPTED) {
                INFOF("[%s] temporary interruption", ctx.name);
            } else {
                const char *desc = xpc_dictionary_get_string(event, XPC_ERROR_KEY_DESCRIPTION);
                WARNF("[%s] unexpected error: %s", ctx.name, desc);
            }
        } else if (type == XPC_TYPE_DICTIONARY) {
            handle_request(&ctx, event);
        }
    });

    xpc_connection_resume(connection);
}

static void setup_listener(void) {
    DEBUGF("[%s] setting up listener", main_context.name);

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
            xpc_connection_t connection = (xpc_connection_t)event;
            xpc_connection_set_target_queue(connection, dispatch_get_main_queue());
            handle_connection(connection);
        }
    });

    xpc_connection_resume(listener);
}

static void setup_signal_handlers(void) {
    DEBUGF("[%s] setting up signal handlers", main_context.name);

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
            INFOF("[%s] received signal %d", main_context.name, sig);

            // IMPORTANT: terminating the broker when clients are connected will
            // destroy the bridge.
            if (connected_peers > 0) {
                WARNF("[%s] %d peers connected - ignoring termination signal", main_context.name, connected_peers);
                return;
            }

            INFOF("[%s] no active clients - shutting down", main_context.name);
            shutdown_shared_network(&main_context);
            exit(EXIT_SUCCESS);
        });

        dispatch_resume(source);
    }
}

int main() {
    INFOF("[%s] starting pid=%d", main_context.name, getpid());

    setup_signal_handlers();
    setup_listener();

    dispatch_main();
}
