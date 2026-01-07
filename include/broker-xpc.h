// SPDX-FileCopyrightText: The vmnet-broker authors
// SPDX-License-Identifier: Apache-2.0

#ifndef BROKER_XPC_H
#define BROKER_XPC_H

#include <xpc/xpc.h>

// Context structure managed by XPC layer
// Allocated on stack in handle_connection and captured by the event handler block
struct broker_context {
    xpc_connection_t connection;
    char name[sizeof("peer 9223372036854775807")];
};

// Broker operations interface - called by XPC layer when events occur
struct broker_ops {
    // Called when a new peer connects
    // Context is allocated and managed by XPC layer (captured in block)
    // The same context instance is used for all operations on this connection
    void (*on_peer_connect)(const struct broker_context *ctx);

    // Called when a peer disconnects
    void (*on_peer_disconnect)(const struct broker_context *ctx);

    // Called when a peer sends a request
    void (*on_peer_request)(const struct broker_context *ctx, xpc_object_t event);
};

// Start the XPC listener with the given broker operations
// Returns 0 on success, -1 on failure
int start_xpc_listener(const struct broker_ops *ops);

// XPC protocol helpers for sending replies
// Send an error reply to a peer
void send_xpc_error(const struct broker_context *ctx, xpc_object_t event, int code);

// Send a network serialization reply to a peer
void send_xpc_network(const struct broker_context *ctx, xpc_object_t event, xpc_object_t network_serialization);

#endif // BROKER_XPC_H
