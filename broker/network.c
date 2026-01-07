// SPDX-FileCopyrightText: The vmnet-broker authors
// SPDX-License-Identifier: Apache-2.0

#include <CoreFoundation/CFBase.h>
#include <arpa/inet.h>
#include <errno.h>
#include <stdlib.h>

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

// Network registry - keeps track of acquired networks by name.
static CFMutableDictionaryRef registry;

// External reference to main context (defined in broker.c)
extern const struct broker_context main_context;

// MARK: - Configuration functions

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

static const struct network_config *find_network_config(const char *name) {
    for (size_t i = 0; i < ARRAY_SIZE(builtin_networks); i++) {
        if (strcmp(builtin_networks[i].name, name) == 0) {
            return &builtin_networks[i];
        }
    }
    return NULL;
}

// MARK: - Network functions

static void free_network(struct network *_Nonnull network, const struct broker_context *_Nonnull ctx) {
    if (network == NULL) {
        return;
    }

    if (network->ref) {
        struct network_info info;
        network_info(network->ref, &info);
        INFOF("[%s] deleted network subnet '%s' mask '%s' ipv6_prefix '%s' prefix_len %d",
            ctx->name, info.subnet, info.mask, info.ipv6_prefix, info.prefix_len);
        CFRelease(network->ref);
    }
    if (network->serialization) {
        xpc_release(network->serialization);
    }
    free(network);
}

static struct network *create_network(
    const struct broker_context *ctx,
    const char *network_name
) {
    const struct network_config *config = find_network_config(network_name);
    if (config == NULL) {
        WARNF("[%s] network '%s' not found", ctx->name, network_name);
        return NULL;
    }
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
    free_network(network, ctx);
    return NULL;
}

// MARK: - Network registry functions

// Called only by CFDictionary when adding to registry. The network is already
// allocated so this does nothing.
static const void *registry_retain(CFAllocatorRef allocator, const void *value) {
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
            NULL,
            0,
            &kCFTypeDictionaryKeyCallBacks,
            &registry_value_callbacks);
    }
}

static void release_registry(const struct broker_context *ctx) {
    if (registry) {
        DEBUGF("[%s] shutdown all networks", ctx->name);
        // Releasing the registry will call network_registry_release for each value
        CFRelease(registry);
        registry = NULL;
    }
}

// MARK: - Public API

// Acquire a network, creating it if necessary. Return a retained xpc_object_t
// serialization on success, or NULL on failure. The caller is responsible for
// releasing the returned object using xpc_release().
// TODO: Accept network name parameter.
xpc_object_t acquire_network(const struct broker_context *ctx) {
    xpc_object_t result = NULL;
    CFStringRef key = NULL;

    init_registry();

    // For now, use "shared" as the default network name
    const char *network_name = "shared";
    key = CFStringCreateWithCString(NULL, network_name, kCFStringEncodingUTF8);

    struct network *net = (struct network *)CFDictionaryGetValue(registry, key);

    if (net == NULL) {
        net = create_network(ctx, network_name);
        if (net == NULL) {
            goto cleanup;
        }

        CFDictionarySetValue(registry, key, net);
    }

    result = xpc_retain(net->serialization);

cleanup:
    if (key) {
        CFRelease(key);
    }
    return result;
}

// Shutdown all networks in the registry.
void shutdown_networks(const struct broker_context *ctx) {
    release_registry(ctx);
}
