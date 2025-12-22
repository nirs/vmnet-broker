#include <stdlib.h>
#include <dispatch/dispatch.h>
#include <xpc/xpc.h>
#include <stdatomic.h>

#include "vmnet-broker.h"
#include "log.h"

bool verbose = true;

// Concurrent clients run can access this in parallel.
static atomic_uint_least64_t global_counter = 0;

static void send_error(xpc_connection_t peer, xpc_object_t event, int code, const char *message) {
    WARNF("send error: (%d) %s", code, message);

    xpc_object_t reply = xpc_dictionary_create_reply(event);
    if (reply == NULL) {
        // Event does not include the return address.
        char *desc = xpc_copy_description(event);
        WARNF("failed create reply for event: %s", desc);
        free(desc);
        return;
    }

    xpc_object_t error = xpc_dictionary_create(NULL, NULL, 0);
    if (error == NULL) {
        // Should not happen.
        ERROR("failed to create error");
        goto out;
    }

    xpc_dictionary_set_int64(error, ERROR_CODE, code);
    xpc_dictionary_set_string(error, ERROR_MESSAGE, message);

    xpc_dictionary_set_value(reply, REPLY_ERROR, error);
    xpc_release(error);

    if (verbose) {
        char *desc = xpc_copy_description(reply);
        DEBUGF("send error reply: %s", desc);
        free(desc);
    }

    xpc_connection_send_message(peer, reply);

out:
    xpc_release(reply);
}

static void send_reply(xpc_connection_t peer, xpc_object_t event, uint64_t value) {
    INFOF("send reply: %llu", value);

    xpc_object_t reply = xpc_dictionary_create_reply(event);
    if (reply == NULL) {
        // Event does not include the return address.
        char *desc = xpc_copy_description(event);
        WARNF("failed create reply for event: %s", desc);
        free(desc);
        return;
    }

    xpc_dictionary_set_uint64(reply, REPLY_VALUE, value);
    xpc_connection_send_message(peer, reply);

    xpc_release(reply);
}

static void handle_error(xpc_connection_t peer, xpc_object_t event) {
    if (event == XPC_ERROR_CONNECTION_INVALID) {
        // Client connection is dead - invalidate resources owned by client.
        INFOF("peer %p has disconnected", (void *)peer);
    } else if (event == XPC_ERROR_CONNECTION_INTERRUPTED) {
        // Temporary interruption, may recover.
        INFOF("peer %p temporary interruption", (void *)peer);
    } else {
        // Unexpected error, log all the details.
        char *desc = xpc_copy_description(event);
        WARNF("peer %p unexpected error: %s", (void *)peer, desc);
        free(desc);
    }
}

static void handle_request(xpc_connection_t peer, xpc_object_t event) {
    const char* command = xpc_dictionary_get_string(event, REQUEST_COMMAND);
    if (command == NULL) {
        send_error(peer, event, ERROR_INVALID_REQUEST, "no command");
        return;
    }
    if (strcmp(command, COMMAND_GET) != 0) {
        send_error(peer, event, ERROR_INVALID_REQUEST, "unknown command");
        return;
    }
    send_reply(peer, event, ++global_counter);
}

static void handle_event(xpc_connection_t peer) {
    // TODO: register the peer and extract the audit token and pid
    // TODO: authorize the client using the audit token

    xpc_connection_set_event_handler(peer, ^(xpc_object_t event) {
        xpc_type_t type = xpc_get_type(event);
        if (type == XPC_TYPE_ERROR) {
            handle_error(peer, event);
        } else if (type == XPC_TYPE_DICTIONARY) {
            handle_request(peer, event);
        }
    });

    xpc_connection_resume(peer);
}

int main() {
    xpc_connection_t listener = xpc_connection_create_mach_service(MACH_SERVICE_NAME, NULL, XPC_CONNECTION_MACH_SERVICE_LISTENER);
    if (!listener) {
        ERROR("failed to create mach service listener");
        exit(EXIT_FAILURE);
    }

    xpc_connection_set_event_handler(listener, ^(xpc_object_t peer) {
        handle_event(peer);
    });

    xpc_connection_resume(listener);
    dispatch_main();
}
