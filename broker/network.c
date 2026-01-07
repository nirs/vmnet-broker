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

// Network registry - keeps track of acquired networks.
// TODO: Support multiple networks, lookup by name.
static struct network *shared_network;

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

// Acquire a network, creating it if necessary.
// TODO: Accept network name parameter.
xpc_object_t acquire_network(const struct broker_context *ctx) {
    if (shared_network == NULL) {
        // TODO: lookup network by name.
        shared_network = create_network(ctx, &builtin_networks[0]);
        if (shared_network == NULL) {
            return NULL;
        }
    }
    // Return a retained copy of the serialization for the caller.
    return xpc_retain(shared_network->serialization);
}

// Shutdown all networks in the registry.
void shutdown_networks(const struct broker_context *ctx) {
    if (shared_network) {
        DEBUGF("[%s] shutdown shared network", ctx->name);
        free_network(ctx, shared_network);
        shared_network = NULL;
    }
}
