// SPDX-FileCopyrightText: The vmnet-broker authors
// SPDX-License-Identifier: Apache-2.0

#include <CoreFoundation/CFBase.h>
#include <dispatch/dispatch.h>
#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>

#include "broker-config.h"
#include "broker-xpc.h"
#include "common.h"
#include "log.h"
#include "vmnet-broker.h"

extern const int idle_timeout_sec;

// Shared network used by one or more peers.
struct network {
    char *name;
    int peers; // Number of peers using this network
    vmnet_network_ref ref;
    xpc_object_t serialization;
    dispatch_source_t idle_timer;
};

// Network registry - keeps track of acquired networks by name.
static CFMutableDictionaryRef registry;

// External reference to main context (defined in broker.c)
extern const struct broker_context main_context;

// MARK: - Network functions

static void free_network(
    struct network *_Nonnull network, const struct broker_context *_Nonnull ctx
) {
    if (network == NULL) {
        return;
    }

    if (network->ref) {
        struct network_info info;
        network_info(network->ref, &info);
        INFOF(
            "[%s] deleted network '%s' subnet '%s' mask '%s' ipv6_prefix "
            "'%s' prefix_len %d",
            ctx->name,
            network->name,
            info.subnet,
            info.mask,
            info.ipv6_prefix,
            info.prefix_len
        );
        CFRelease(network->ref);
    }
    if (network->serialization) {
        xpc_release(network->serialization);
    }
    if (network->idle_timer) {
        dispatch_source_cancel(network->idle_timer);
        dispatch_release(network->idle_timer);
    }
    free(network->name);
    free(network);
}

static struct network *create_network(
    const struct broker_context *ctx,
    const char *name,
    vmnet_network_configuration_ref config,
    int *error
) {
    vmnet_return_t status;
    struct network *network = NULL;

    network = calloc(1, sizeof(*network));
    if (network == NULL) {
        WARNF(
            "[%s] failed to allocate network: %s", ctx->name, strerror(errno)
        );
        goto failure;
    }

    network->name = strdup(name);
    if (network->name == NULL) {
        WARNF(
            "[%s] failed to allocate network name: %s",
            ctx->name,
            strerror(errno)
        );
        goto failure;
    }

    network->ref = vmnet_network_create(config, &status);
    if (network->ref == NULL) {
        WARNF(
            "[%s] failed to create network ref: (%d) %s",
            ctx->name,
            status,
            vmnet_strerror(status)
        );
        goto failure;
    }

    struct network_info info;
    network_info(network->ref, &info);
    INFOF(
        "[%s] created network '%s' subnet '%s' mask '%s' ipv6_prefix '%s' "
        "prefix_len %d",
        ctx->name,
        name,
        info.subnet,
        info.mask,
        info.ipv6_prefix,
        info.prefix_len
    );

    network->serialization = vmnet_network_copy_serialization(
        network->ref, &status
    );
    if (network->serialization == NULL) {
        WARNF(
            "[%s] failed to create network serialization: (%d) %s",
            ctx->name,
            status,
            vmnet_strerror(status)
        );
        goto failure;
    }

    return network;

failure:
    free_network(network, ctx);
    if (error) {
        *error = VMNET_BROKER_CREATE_FAILURE;
    }
    return NULL;
}

// MARK: - Network registry functions

// Called only by CFDictionary when adding to registry. The network is already
// allocated so this does nothing.
static const void *
registry_retain(CFAllocatorRef allocator, const void *value) {
    (void)allocator;
    return value;
}

// Called only by CFDictionary when removing from registry. The network will be
// freed.
static void registry_release(CFAllocatorRef allocator, const void *value) {
    (void)allocator;
    free_network((struct network *)value, &main_context);
}

static const CFDictionaryValueCallBacks registry_value_callbacks = {
    .retain = registry_retain,
    .release = registry_release,
};

static void init_registry(void) {
    if (registry == NULL) {
        registry = CFDictionaryCreateMutable(
            NULL, 0, &kCFTypeDictionaryKeyCallBacks, &registry_value_callbacks
        );
    }
}

static void release_registry(const struct broker_context *ctx) {
    if (registry) {
        DEBUGF("[%s] shutdown all networks", ctx->name);
        // Releasing the registry will call network_registry_release for each
        // value
        CFRelease(registry);
        registry = NULL;
    }
}

static struct network *registry_get(const char *name) {
    CFStringRef key = CFStringCreateWithCString(
        NULL, name, kCFStringEncodingUTF8
    );
    struct network *net = (struct network *)CFDictionaryGetValue(registry, key);
    CFRelease(key);
    return net;
}

static void registry_set(const char *name, struct network *net) {
    CFStringRef key = CFStringCreateWithCString(
        NULL, name, kCFStringEncodingUTF8
    );
    CFDictionarySetValue(registry, key, net);
    CFRelease(key);
}

static void registry_remove(const char *name) {
    CFStringRef key = CFStringCreateWithCString(
        NULL, name, kCFStringEncodingUTF8
    );
    CFDictionaryRemoveValue(registry, key);
    CFRelease(key);
}

// Schedule removal of the network after idle_timeout_sec seconds. To cancel the
// removal call cancel_remove_later().
static void
remove_later(const struct broker_context *ctx, struct network *net) {
    DEBUGF(
        "[%s] removing network '%s' in %d seconds",
        ctx->name,
        net->name,
        idle_timeout_sec
    );

    // This is impossible since the first connected peer canceled the timer, and
    // shutdown_later is called when the last peer has disconnected.
    assert(
        net->idle_timer == NULL && "idle timer running in registry_remove_later"
    );

    net->idle_timer = dispatch_source_create(
        DISPATCH_SOURCE_TYPE_TIMER, 0, 0, dispatch_get_main_queue()
    );

    assert(net->idle_timer != NULL && "failed to create idle timer");

    dispatch_time_t start = dispatch_time(
        DISPATCH_TIME_NOW, idle_timeout_sec * NSEC_PER_SEC
    );
    // Allow the system up to 1 second leeway if this can improve power
    // consumption and system performance.
    uint64_t leeway = 1 * NSEC_PER_SEC;

    dispatch_source_set_timer(
        net->idle_timer, start, DISPATCH_TIME_FOREVER, leeway
    );

    dispatch_source_set_event_handler(net->idle_timer, ^{
        INFOF(
            "[%s] idle timeout - removing network '%s'",
            main_context.name,
            net->name
        );
        registry_remove(net->name);
    });

    dispatch_resume(net->idle_timer);
}

// Cancel network removal if the removal is scheduled.
static void
cancel_remove_later(struct broker_context *ctx, struct network *net) {
    if (net->idle_timer != NULL) {
        DEBUGF("[%s] canceled remove network '%s'", ctx->name, net->name);
        dispatch_source_cancel(net->idle_timer);
        dispatch_release(net->idle_timer);
        net->idle_timer = NULL;
    }
}

// MARK: - Peer ownership helpers

// Check if peer already owns a network.
static bool
peer_owns_network(const struct broker_context *ctx, const struct network *net) {
    for (int i = 0; i < ctx->network_count; i++) {
        if (ctx->networks[i] == net) {
            return true;
        }
    }
    return false;
}

// Assumes peer does not already own the network (caller must check first).
static bool can_add_network_to_peer(struct broker_context *ctx, int *error) {
    if (ctx->network_count >= MAX_PEER_NETWORKS) {
        WARNF(
            "[%s] peer has too many networks (%d)",
            ctx->name,
            ctx->network_count
        );
        if (error) {
            *error = VMNET_BROKER_INTERNAL_ERROR;
        }
        return false;
    }
    return true;
}

// Ensure peer owns the network, adding it if not already owned.
// Returns true on success, false on failure.
static bool update_peer_ownership(
    struct broker_context *ctx, struct network *net, int *error
) {
    if (peer_owns_network(ctx, net)) {
        return true;
    }

    if (!can_add_network_to_peer(ctx, error)) {
        return false;
    }

    ctx->networks[ctx->network_count++] = net;
    net->peers++;
    INFOF(
        "[%s] acquired network '%s' (peers %d)",
        ctx->name,
        net->name,
        net->peers
    );

    return true;
}

// MARK: - Public API

xpc_object_t acquire_network(
    struct broker_context *ctx, const char *network_name, int *error
) {
    init_registry();

    struct network *net = registry_get(network_name);

    if (net == NULL) {
        if (!can_add_network_to_peer(ctx, error)) {
            return NULL;
        }

        vmnet_network_configuration_ref config = create_network_configuration(
            ctx, network_name, error
        );
        if (config == NULL) {
            return NULL;
        }

        net = create_network(ctx, network_name, config, error);
        CFRelease(config);
        config = NULL;
        if (net == NULL) {
            return NULL;
        }

        registry_set(network_name, net);
    }

    if (!update_peer_ownership(ctx, net, error)) {
        return NULL;
    }

    cancel_remove_later(ctx, net);

    return xpc_retain(net->serialization);
}

void release_peer_networks(struct broker_context *ctx) {
    while (ctx->network_count) {
        struct network *net = ctx->networks[--ctx->network_count];

        net->peers--;
        INFOF(
            "[%s] released network '%s' (peers %d)",
            ctx->name,
            net->name,
            net->peers
        );

        if (net->peers == 0) {
            remove_later(ctx, net);
        }
    }
}

// Shutdown all networks in the registry.
void shutdown_networks(const struct broker_context *ctx) {
    release_registry(ctx);
}
