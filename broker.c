#include <stdlib.h>
#include <dispatch/dispatch.h>
#include <xpc/xpc.h>
#include <stdatomic.h>

#include "vmnet-broker.h"
#include "log.h"

struct peer {
    xpc_connection_t connection;
    pid_t pid;
};

bool verbose = true;

// Concurrent clients run can access this in parallel.
static atomic_uint_least64_t global_counter = 0;

static void send_error(const struct peer *peer, xpc_object_t event, int code, const char *message) {
    WARNF("[peer %d] send error: (%d) %s", peer->pid, code, message);

    xpc_object_t reply = xpc_dictionary_create_reply(event);
    if (reply == NULL) {
        // Event does not include the return address.
        char *desc = xpc_copy_description(event);
        WARNF("[peer %d] failed create reply for event: %s", peer->pid, desc);
        free(desc);
        return;
    }

    xpc_object_t error = xpc_dictionary_create(NULL, NULL, 0);
    if (error == NULL) {
        // Should not happen.
        ERRORF("[peer %d] failed to create error", peer->pid);
        goto out;
    }

    xpc_dictionary_set_int64(error, ERROR_CODE, code);
    xpc_dictionary_set_string(error, ERROR_MESSAGE, message);

    xpc_dictionary_set_value(reply, REPLY_ERROR, error);
    xpc_release(error);

    if (verbose) {
        char *desc = xpc_copy_description(reply);
        DEBUGF("[peer %d] send error reply: %s", peer->pid, desc);
        free(desc);
    }

    xpc_connection_send_message(peer->connection, reply);

out:
    xpc_release(reply);
}

static void send_reply(const struct peer *peer, xpc_object_t event, uint64_t value) {
    INFOF("[peer %d] send reply: %llu", peer->pid, value);

    xpc_object_t reply = xpc_dictionary_create_reply(event);
    if (reply == NULL) {
        // Event does not include the return address.
        char *desc = xpc_copy_description(event);
        WARNF("[peer %d] failed create reply for event: %s", peer->pid, desc);
        free(desc);
        return;
    }

    xpc_dictionary_set_uint64(reply, REPLY_VALUE, value);
    xpc_connection_send_message(peer->connection, reply);

    xpc_release(reply);
}

static void handle_error(const struct peer *peer, xpc_object_t event) {
    if (event == XPC_ERROR_CONNECTION_INVALID) {
        // Client connection is dead - invalidate resources owned by client.
        INFOF("[peer %d] disconnected", peer->pid);
    } else if (event == XPC_ERROR_CONNECTION_INTERRUPTED) {
        // Temporary interruption, may recover.
        INFOF("[peer %d] temporary interruption", peer->pid);
    } else {
        // Unexpected error, log all the details.
        char *desc = xpc_copy_description(event);
        WARNF("[peer %d] unexpected error: %s", peer->pid, desc);
        free(desc);
    }
}

static void handle_request(const struct peer *peer, xpc_object_t event) {
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

static void handle_event(xpc_connection_t connection) {
    // Until we manage list of active connectons, this is a good place to keep
    // the peer. It is shared with all requests on this connection via the
    // handler block. We need to capture now the pid since it is not available
    // when the connecton invalidates.
    struct peer peer = {
        .connection = connection,
        .pid = xpc_connection_get_pid(connection),
    };

    INFOF("[peer %d] connected", peer.pid);

    // TODO: register the peer and extract the audit token
    // TODO: authorize the client using the audit token

    xpc_connection_set_event_handler(peer.connection, ^(xpc_object_t event) {
        xpc_type_t type = xpc_get_type(event);
        if (type == XPC_TYPE_ERROR) {
            handle_error(&peer, event);
        } else if (type == XPC_TYPE_DICTIONARY) {
            handle_request(&peer, event);
        }
    });

    xpc_connection_resume(connection);
}

int main() {
    xpc_connection_t listener = xpc_connection_create_mach_service(MACH_SERVICE_NAME, NULL, XPC_CONNECTION_MACH_SERVICE_LISTENER);
    if (!listener) {
        ERROR("[main] failed to create mach service listener");
        exit(EXIT_FAILURE);
    }

    xpc_connection_set_event_handler(listener, ^(xpc_object_t connection) {
        handle_event(connection);
    });

    xpc_connection_resume(listener);
    dispatch_main();
}
