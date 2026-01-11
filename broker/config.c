// SPDX-FileCopyrightText: The vmnet-broker authors
// SPDX-License-Identifier: Apache-2.0

#include <arpa/inet.h>
#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "broker-config.h"
#include "common.h"
#include "log.h"
#include "vmnet-broker.h"

// Network configuration - private to this module.
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

static const struct network_config *find_network_config(
    const struct broker_context *ctx, const char *name, int *error
) {
    for (size_t i = 0; i < ARRAY_SIZE(builtin_networks); i++) {
        if (strcmp(builtin_networks[i].name, name) == 0) {
            return &builtin_networks[i];
        }
    }
    WARNF("[%s] network '%s' not found", ctx->name, name);
    if (error) {
        *error = VMNET_BROKER_NOT_FOUND;
    }
    return NULL;
}

static vmnet_network_configuration_ref create_vmnet_configuration(
    const struct broker_context *ctx,
    const struct network_config *config,
    int *error
) {
    vmnet_return_t status;
    vmnet_network_configuration_ref
        configuration = vmnet_network_configuration_create(
            config->mode, &status
        );
    if (configuration == NULL) {
        WARNF(
            "[%s] failed to create network configuration for '%s': (%d) %s",
            ctx->name,
            config->name,
            status,
            vmnet_strerror(status)
        );
        if (error) {
            *error = VMNET_BROKER_CREATE_FAILURE;
        }
        return NULL;
    }

    // When subnet and mask are NULL, vmnet allocates both dynamically.
    // This is the most reliable way to avoid conflicts with other programs
    // allocating the same network, and to prevent orphaned networks if the
    // broker is killed while VMs are using the network.
    // TODO: Investigate if vmnet supports partial allocation (e.g., set subnet
    // and let vmnet allocate mask, or vice versa). This would allow more
    // flexible network configuration.

    if (config->subnet && config->mask) {
        struct in_addr subnet_addr;
        if (inet_pton(AF_INET, config->subnet, &subnet_addr) == 0) {
            WARNF(
                "[%s] failed to parse subnet '%s' for network '%s': %s",
                ctx->name,
                config->subnet,
                config->name,
                strerror(errno)
            );
            goto error;
        }

        struct in_addr subnet_mask;
        if (inet_pton(AF_INET, config->mask, &subnet_mask) == 0) {
            WARNF(
                "[%s] failed to parse mask '%s' for network '%s': %s",
                ctx->name,
                config->mask,
                config->name,
                strerror(errno)
            );
            goto error;
        }

        status = vmnet_network_configuration_set_ipv4_subnet(
            configuration, &subnet_addr, &subnet_mask
        );
        if (status != VMNET_SUCCESS) {
            WARNF(
                "[%s] failed to set ipv4 subnet for network '%s': (%d) %s",
                ctx->name,
                config->name,
                status,
                vmnet_strerror(status)
            );
            goto error;
        }
    }

    // TODO: set rest of options.

    return configuration;

error:
    CFRelease(configuration);
    if (error) {
        *error = VMNET_BROKER_CREATE_FAILURE;
    }
    return NULL;
}

vmnet_network_configuration_ref create_network_configuration(
    const struct broker_context *ctx, const char *name, int *error
) {
    const struct network_config *config = find_network_config(ctx, name, error);
    if (config == NULL) {
        return NULL;
    }

    return create_vmnet_configuration(ctx, config, error);
}
