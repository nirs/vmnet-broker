// SPDX-FileCopyrightText: The vmnet-broker authors
// SPDX-License-Identifier: Apache-2.0

#ifndef BROKER_NETWORK_H
#define BROKER_NETWORK_H

#include "broker-xpc.h"

// Acquire a network by name, creating it if necessary.
// Returns a retained xpc_object_t serialization on success, or NULL on failure.
// The caller is responsible for releasing the returned object using
// xpc_release(). On failure, *error is set to the error code if error is not
// NULL. Increments the network peer count; call release_network when done.
xpc_object_t acquire_network(
    struct broker_context *ctx, const char *network_name, int *error
);

// Release all networks acquired by a peer.
// Decrements the peer count for each network.
// When no peers are using a network, the network is deleted.
void release_peer_networks(struct broker_context *ctx);

// Shutdown all networks in the registry.
void shutdown_networks(const struct broker_context *ctx);

#endif // BROKER_NETWORK_H
