// SPDX-FileCopyrightText: The vmnet-broker authors
// SPDX-License-Identifier: Apache-2.0

#include <dispatch/dispatch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "broker-xpc.h"
#include "log.h"
#include "vmnet-broker.h"

static xpc_connection_t listener;
static const struct broker_ops *ops;

static void
init_context(struct broker_context *ctx, xpc_connection_t connection) {
    ctx->connection = connection;
    snprintf(
        ctx->name,
        sizeof(ctx->name),
        "peer %d",
        xpc_connection_get_pid(connection)
    );
    memset(ctx->networks, 0, sizeof(ctx->networks));
    ctx->network_count = 0;
}

static void handle_connection(xpc_connection_t connection) {
    // Allocate context on stack - it will be captured by the block below
    // Use __block to ensure &ctx always refers to the same instance,
    // even if ctx is moved to heap when the block is copied
    __block struct broker_context ctx;
    init_context(&ctx, connection);

    // Notify broker of new peer
    if (ops->on_peer_connect) {
        ops->on_peer_connect(&ctx);
    }

    // NOTE: The block captures ctx, so it lives as long as the connection
    xpc_connection_set_event_handler(connection, ^(xpc_object_t event) {
        xpc_type_t type = xpc_get_type(event);
        if (type == XPC_TYPE_ERROR) {
            if (event == XPC_ERROR_CONNECTION_INVALID) {
                // Client connection is dead
                if (ops->on_peer_disconnect) {
                    ops->on_peer_disconnect(&ctx);
                }
            } else {
                const char *desc = xpc_dictionary_get_string(
                    event, XPC_ERROR_KEY_DESCRIPTION
                );
                WARNF("[%s] unexpected error: %s", ctx.name, desc);
            }
        } else if (type == XPC_TYPE_DICTIONARY) {
            // Forward request to broker
            if (ops->on_peer_request) {
                ops->on_peer_request(&ctx, event);
            }
        }
    });

    xpc_connection_resume(connection);
}

static xpc_object_t
create_reply(const struct broker_context *ctx, xpc_object_t event) {
    xpc_object_t reply = xpc_dictionary_create_reply(event);
    if (reply == NULL) {
        // Event does not include the return address.
        char *desc = xpc_copy_description(event);
        WARNF("[%s] failed create reply for event: %s", ctx->name, desc);
        free(desc);
    }
    return reply;
}

void send_xpc_error(
    const struct broker_context *ctx, xpc_object_t event, int code
) {
    DEBUGF("[%s] send error to peer: code=%d", ctx->name, code);

    xpc_object_t reply = create_reply(ctx, event);
    if (reply == NULL) {
        return;
    }

    xpc_dictionary_set_int64(reply, REPLY_ERROR, code);

    xpc_connection_send_message(ctx->connection, reply);
    xpc_release(reply);
}

void send_xpc_network(
    const struct broker_context *ctx,
    xpc_object_t event,
    const char *network_name,
    xpc_object_t network_serialization
) {
    DEBUGF("[%s] send network '%s' to peer", ctx->name, network_name);

    xpc_object_t reply = create_reply(ctx, event);
    if (reply == NULL) {
        return;
    }

    xpc_dictionary_set_value(reply, REPLY_NETWORK, network_serialization);
    xpc_connection_send_message(ctx->connection, reply);
    xpc_release(reply);
}

int start_xpc_listener(
    const struct broker_context *ctx, const struct broker_ops *broker_ops
) {
    if (broker_ops == NULL) {
        return -1;
    }

    ops = broker_ops;

    DEBUGF("[%s] setting up listener", ctx->name);

    // We use the main queue to minimize memory. The broker is mostly idle and
    // handles few clients in its lifetime. There is no reason to have more than
    // one thread.
    listener = xpc_connection_create_mach_service(
        MACH_SERVICE_NAME,
        dispatch_get_main_queue(),
        XPC_CONNECTION_MACH_SERVICE_LISTENER
    );

    if (listener == NULL) {
        ERRORF("[%s] failed to create listener", ctx->name);
        return -1;
    }

    xpc_connection_set_event_handler(listener, ^(xpc_object_t event) {
        xpc_type_t type = xpc_get_type(event);
        if (type == XPC_TYPE_ERROR) {
            // We don't expect any non fatal errors.
            const char *desc = xpc_dictionary_get_string(
                event, XPC_ERROR_KEY_DESCRIPTION
            );
            ERRORF("[%s] listener failed: %s", ctx->name, desc);
            exit(EXIT_FAILURE);
        } else if (type == XPC_TYPE_CONNECTION) {
            xpc_connection_t connection = (xpc_connection_t)event;
            // Use the same queue for all peers. This ensures that we don't need
            // any locks when modifying internal state, and all events are
            // serialized.
            xpc_connection_set_target_queue(
                connection, dispatch_get_main_queue()
            );
            handle_connection(connection);
        }
    });

    xpc_connection_resume(listener);

    return 0;
}
