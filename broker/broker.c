// SPDX-FileCopyrightText: The vmnet-broker authors
// SPDX-License-Identifier: Apache-2.0

#include <CoreFoundation/CFBase.h>
#include <arpa/inet.h>
#include <dispatch/dispatch.h>
#include <errno.h>
#include <stdlib.h>

#include "vmnet-broker.h"
#include "broker-xpc.h"
#include "common.h"
#include "log.h"

// Network configuration.
struct network_config {
    const char *name;

    // VMNET_SHARED_MODE | VMNET_HOST_MODE
    vmnet_mode_t mode;

    // IPv4 subnet: a /24 under 192.168/16
    const char *subnet;
    const char *mask;

    // TODO: Add rest of options:
    // - External interface: default interface per the routing table
    // - NAT44: enabled
    // - NAT66: enabled
    // - DHCP: enabled
    // - DNS proxy: enabled
    // - Router advertisement: enabled
    // - IPv6 prefix: random ULA prefix
    // - Port forwarding rule: none
    // - DHCP reservation: none
    // - MTU: 1500
};

// Shared network used by one of more clients.
// TODO: Keep reference count.
struct network {
    vmnet_network_ref ref;
    xpc_object_t serialization;
};

bool verbose = true;

static const struct network_config builtin_networks[] = {
    {
        .name = "shared",
        .mode = VMNET_SHARED_MODE,
    },
    {
        .name = "host",
        .mode = VMNET_HOST_MODE,
    },
};

// The context used for main() and signal handlers.
static const struct broker_context main_context = { .name = "main" };

// Time to wait in seconds before shutting down after the broker became idle. We
// want to keep the network reservation in case a user want to use the same
// network soon.
// TODO: Read from user preferences.
static const int idle_timeout_sec = 30;

// Shared network vended to clients. Created when the first client request a
// network. Automatically released by shutting down after idle timeout.
static struct network *shared_network;

// Number of connected peers, used to prevent termination when peers are
// connected. Using signed int to make it easy to detect incorrect counting.
static int connected_peers;

// Used to shutdown if the broker is idle for idle_timeout_sec.
static dispatch_source_t idle_timer;

static void free_network(const struct broker_context *ctx, struct network *network) {
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

static vmnet_network_configuration_ref create_network_configuration(
    const struct broker_context *ctx,
    const struct network_config *network_config
) {
    vmnet_return_t status;
    vmnet_network_configuration_ref configuration = vmnet_network_configuration_create(
        network_config->mode, &status);
    if (configuration == NULL) {
        WARNF("[%s] failed to create network configuration: (%d) %s",
            ctx->name, status, vmnet_strerror(status));
        return NULL;
    }

    // When subnet and mask are NULL, vmnet allocates both dynamically.
    // This is the most reliable way to avoid conflicts with other programs
    // allocating the same network, and to prevent orphaned networks if the
    // broker is killed while VMs are using the network.
    // TODO: Investigate if vmnet supports partial allocation (e.g., set subnet
    // and let vmnet allocate mask, or vice versa). This would allow more
    // flexible network configuration.

    if (network_config->subnet && network_config->mask) {
        struct in_addr subnet_addr;
        if (inet_pton(AF_INET, network_config->subnet, &subnet_addr) == 0) {
            WARNF("[%s] failed to parse subnet '%s': %s",
                ctx->name, network_config->subnet, strerror(errno));
            goto error;
        }

        struct in_addr subnet_mask;
        if (inet_pton(AF_INET, network_config->mask, &subnet_mask) == 0) {
            WARNF("[%s] failed to parse mask '%s': %s",
                ctx->name, network_config->mask, strerror(errno));
            goto error;
        }

        status = vmnet_network_configuration_set_ipv4_subnet(
            configuration, &subnet_addr, &subnet_mask);
        if (status != VMNET_SUCCESS) {
            WARNF("[%s] failed to set ipv4 subnet: (%d) %s",
                ctx->name, status, vmnet_strerror(status));
            goto error;
        }
    }

    // TODO: set rest of options.

    return configuration;

error:
    CFRelease(configuration);
    return NULL;
}

static struct network *create_network(
    const struct broker_context *ctx,
    const struct network_config *config
) {
    vmnet_return_t status;

    struct network *network = calloc(1, sizeof(*network));
    if (network == NULL) {
        WARNF("[%s] failed to allocate network: %s", ctx->name, strerror(errno));
        return NULL;
    }

    vmnet_network_configuration_ref configuration = create_network_configuration(ctx, config);
    if (configuration == NULL) {
        goto error;
    }

    network->ref = vmnet_network_create(configuration, &status);
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
    if (configuration) {
        CFRelease(configuration);
    }
    free_network(ctx, network);
    return NULL;
}

// TODO: Accept network name.
static struct network *get_network(const struct broker_context *ctx) {
    if (shared_network == NULL) {
        // TODO: lookup network by name.
        shared_network = create_network(ctx, &builtin_networks[0]);
    }
    return shared_network;
}

static void on_peer_request(const struct broker_context *ctx, xpc_object_t event) {
    const char* command = xpc_dictionary_get_string(event, REQUEST_COMMAND);
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

    struct network *network = get_network(ctx);
    if (network == NULL) {
        send_xpc_error(ctx, event, VMNET_BROKER_CREATE_FAILURE);
        return;
    }

    send_xpc_network(ctx, event, network->serialization);
}

// Avoid orphaned network if broker is stopped before clients.
static void shutdown_shared_network(const struct broker_context *ctx) {
    if (shared_network) {
        DEBUGF("[%s] shutdown shared network", ctx->name);
        free_network(ctx, shared_network);
        shared_network = NULL;
    }
}

static void shutdown_later(const struct broker_context *ctx) {
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

static void on_peer_connect(const struct broker_context *ctx) {
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

static void on_peer_disconnect(const struct broker_context *ctx) {
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

    if (start_xpc_listener(&broker_ops) != 0) {
        ERRORF("[%s] failed to start XPC listener", main_context.name);
        exit(EXIT_FAILURE);
    }

    dispatch_main();
}
