#include "vmnet-broker.h"

// The connection must be kept open during the lifetime of the client. The
// kernel invalidates the broker connection after the client terminates.
static xpc_connection_t connection;

static void connect_to_broker(void) {
    connection = xpc_connection_create_mach_service(MACH_SERVICE_NAME, NULL, 0);

    // Must set the event handler but we don't use it. Errors are logged when we
    // receive a reply.
    xpc_connection_set_event_handler(connection, ^(xpc_object_t event) {
    });

    xpc_connection_resume(connection);
}

static void init_error(struct vmnet_broker_error *error, int code, const char *format, ...) {
    if (error == NULL) {
        return;
    }
    error->code = code;
    va_list args;
    va_start(args, format);
    vsnprintf(error->message, sizeof(error->message), format, args);
    va_end(args);
}

xpc_object_t vmnet_broker_start_session(const char *network_name, struct vmnet_broker_error *err) {
    if (connection == NULL) {
        connect_to_broker();
    }

    xpc_object_t message = xpc_dictionary_create_empty();
    xpc_dictionary_set_string(message, REQUEST_COMMAND, COMMAND_GET);
    xpc_dictionary_set_string(message, REQUEST_NETWORK_NAME, network_name);

    xpc_object_t reply = xpc_connection_send_message_with_reply_sync(connection, message);
    xpc_release(message);
    message = NULL;

    xpc_object_t serialization = NULL;
    xpc_type_t reply_type = xpc_get_type(reply);

    if (reply_type == XPC_TYPE_ERROR) {
        const char *reason = xpc_dictionary_get_string(reply, XPC_ERROR_KEY_DESCRIPTION);
        init_error(err, ERROR_XPC_REQUEST, "failed to send xpc message: %s", reason);
        goto out;
    }

    if (reply_type != XPC_TYPE_DICTIONARY) {
        const char *type_name = xpc_type_get_name(reply_type);
        init_error(err, ERROR_INVALID_REPLY, "broker returned invalid reply type: %s", type_name);
        goto out;
    }

    xpc_object_t error = xpc_dictionary_get_value(reply, REPLY_ERROR);
    if (error) {
        int64_t code = xpc_dictionary_get_int64(error, ERROR_CODE);
        const char *message = xpc_dictionary_get_string(error, ERROR_MESSAGE);
        init_error(err, code, "broker retruned an error: %s", message);
        goto out;
    }

    serialization = xpc_dictionary_get_value(reply, REPLY_NETWORK);
    if (serialization == NULL) {
        init_error(err, ERROR_INVALID_REPLY, "broker retruned invalid reply: missing 'network' key");
        goto out;
    }

    xpc_retain(serialization);

out:
    xpc_release(reply);
    return serialization;
}
