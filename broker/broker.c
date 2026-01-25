// SPDX-FileCopyrightText: The vmnet-broker authors
// SPDX-License-Identifier: Apache-2.0

#include <dispatch/dispatch.h>
#include <signal.h>
#include <stdlib.h>

#include "broker-network.h"
#include "broker-xpc.h"
#include "common.h"
#include "log.h"
#include "version.h"
#include "vmnet-broker.h"

bool verbose = true;

// The context used for main() and signal handlers.
const struct broker_context main_context = {.name = "main"};

// Time to wait in seconds before shutting down idle network, or shutting done
// idle broker. We want to keep the network reservation in case a user want to
// use the same network soon.
// TODO: Read from user preferences.
const int idle_timeout_sec = 120;

// Number of connected peers, used to prevent termination when peers are
// connected. Using signed int to make it easy to detect incorrect counting.
static int connected_peers;

// Used to shutdown if the broker is idle for idle_timeout_sec.
static dispatch_source_t idle_timer;

static void on_peer_request(struct broker_context *ctx, xpc_object_t event) {
    const char *command = xpc_dictionary_get_string(event, REQUEST_COMMAND);
    if (command == NULL) {
        WARNF("[%s] invalid request: missing command key", ctx->name);
        send_xpc_error(ctx, event, VMNET_BROKER_INVALID_REQUEST);
        return;
    }
    if (strcmp(command, COMMAND_ACQUIRE) != 0) {
        WARNF("[%s] invalid request: unknown command '%s'", ctx->name, command);
        send_xpc_error(ctx, event, VMNET_BROKER_INVALID_REQUEST);
        return;
    }

    const char *network_name = xpc_dictionary_get_string(
        event, REQUEST_NETWORK_NAME
    );
    if (network_name == NULL) {
        WARNF("[%s] invalid request: missing network_name", ctx->name);
        send_xpc_error(ctx, event, VMNET_BROKER_INVALID_REQUEST);
        return;
    }

    int error = 0;
    xpc_object_t network_serialization = acquire_network(
        ctx, network_name, &error
    );
    if (network_serialization == NULL) {
        send_xpc_error(ctx, event, error);
        return;
    }

    send_xpc_network(ctx, event, network_name, network_serialization);
    xpc_release(network_serialization);
}

static void shutdown_later(const struct broker_context *ctx) {
    DEBUGF("[%s] shutting down in %d seconds", ctx->name, idle_timeout_sec);

    // This is impossible since the first connected peer canceled the timer, and
    // shutdown_later is called when the last peer has disconnected.
    assert(idle_timer == NULL && "idle timer running in shutdown_later");

    idle_timer = dispatch_source_create(
        DISPATCH_SOURCE_TYPE_TIMER, 0, 0, dispatch_get_main_queue()
    );

    assert(idle_timer != NULL && "failed to create idle timer");

    dispatch_time_t start = dispatch_time(
        DISPATCH_TIME_NOW, idle_timeout_sec * NSEC_PER_SEC
    );
    // Allow the system up to 1 second leeway if this can improve power
    // consumption and system performance.
    uint64_t leeway = 1 * NSEC_PER_SEC;

    dispatch_source_set_timer(idle_timer, start, DISPATCH_TIME_FOREVER, leeway);

    dispatch_source_set_event_handler(idle_timer, ^{
        INFOF("[%s] idle timeout - shutting down", main_context.name);
        shutdown_networks(&main_context);
        exit(EXIT_SUCCESS);
    });

    dispatch_resume(idle_timer);
}

static void on_peer_connect(struct broker_context *ctx) {
    connected_peers++;

    INFOF("[%s] connected (connected peers %d)", ctx->name, connected_peers);

    if (connected_peers == 1) {
        // Create a transaction so launchd will know that we are active and will
        // not try to stop the service to free resources.
        DEBUGF(
            "[%s] starting transaction to prevent termination while peers "
            "are connected",
            ctx->name
        );
        xpc_transaction_begin();

        if (idle_timer) {
            DEBUGF("[%s] canceling idle shutdown", ctx->name);
            dispatch_source_cancel(idle_timer);
            dispatch_release(idle_timer);
            idle_timer = NULL;
        }
    }
}

static void on_peer_disconnect(struct broker_context *ctx) {
    connected_peers--;

    INFOF("[%s] disconnected (connected peers %d)", ctx->name, connected_peers);

    release_peer_networks(ctx);

    if (connected_peers == 0) {
        // This is the last peer - end the transaction so launchd will be able
        // stop the broker quickly if needed.
        DEBUGF("[%s] ending transaction - broker can be stopped", ctx->name);
        xpc_transaction_end();

        // Shut down if we are idle for long time.
        shutdown_later(ctx);
    }
}

static const struct broker_ops broker_ops = {
    .on_peer_connect = on_peer_connect,
    .on_peer_disconnect = on_peer_disconnect,
    .on_peer_request = on_peer_request,
};

static void setup_signal_handlers(void) {
    DEBUGF("[%s] setting up signal handlers", main_context.name);

    int signals[] = {SIGINT, SIGTERM};

    for (unsigned i = 0; i < ARRAY_SIZE(signals); i++) {
        int sig = signals[i];

        // Ignore the signal so we can handle it on the runloop.
        signal(sig, SIG_IGN);

        dispatch_source_t source = dispatch_source_create(
            DISPATCH_SOURCE_TYPE_SIGNAL, sig, 0, dispatch_get_main_queue()
        );

        // 5. Set the event handler (the "block" that runs when signal is
        // received)
        dispatch_source_set_event_handler(source, ^{
            INFOF("[%s] received signal %d", main_context.name, sig);

            // IMPORTANT: terminating the broker when clients are connected will
            // destroy the bridge.
            if (connected_peers > 0) {
                WARNF(
                    "[%s] %d peers connected - ignoring termination signal",
                    main_context.name,
                    connected_peers
                );
                return;
            }

            INFOF("[%s] no active clients - shutting down", main_context.name);
            shutdown_networks(&main_context);
            exit(EXIT_SUCCESS);
        });

        dispatch_resume(source);
    }
}

int main() {
    INFOF(
        "[%s] starting version=%s commit=%s pid=%d",
        main_context.name,
        GIT_VERSION,
        GIT_COMMIT,
        getpid()
    );

    setup_signal_handlers();

    if (start_xpc_listener(&main_context, &broker_ops) != 0) {
        ERRORF("[%s] failed to start XPC listener", main_context.name);
        exit(EXIT_FAILURE);
    }

    dispatch_main();
}
