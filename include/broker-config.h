// SPDX-FileCopyrightText: The vmnet-broker authors
// SPDX-License-Identifier: Apache-2.0

#ifndef BROKER_CONFIG_H
#define BROKER_CONFIG_H

#include <vmnet/vmnet.h>

#include "broker-xpc.h"

// Create a network configuration for the named network.
// Returns a vmnet_network_configuration_ref on success, or NULL on failure.
// The caller is responsible for releasing the returned object using
// CFRelease(). On failure, *error is set to the error code if error is not
// NULL.
vmnet_network_configuration_ref create_network_configuration(
    const struct broker_context *ctx, const char *name, int *error
);

#endif // BROKER_CONFIG_H
