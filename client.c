#include <stdlib.h>
#include <xpc/xpc.h>

#include "vmnet-broker.h"
#include "log.h"

bool verbose = true;

int main(int argc, char *argv[]) {
    xpc_connection_t connection = xpc_connection_create_mach_service(MACH_SERVICE_NAME, NULL, 0);
    if (connection == NULL) {
        ERROR("faild to create connection");
        exit(EXIT_FAILURE);
    }

    xpc_connection_set_event_handler(connection, ^(xpc_object_t event) {
        if (xpc_get_type(event) == XPC_TYPE_ERROR) {
            char *desc = xpc_copy_description(event);
            ERRORF("connecton failed: %s", desc);
            free(desc);
            exit(EXIT_FAILURE);
        }
    });

    xpc_connection_resume(connection);

    xpc_object_t message = xpc_dictionary_create(NULL, NULL, 0);
    xpc_dictionary_set_string(message, REQUEST_COMMAND, COMMAND_GET);

    INFO("sending request");

    xpc_object_t reply = xpc_connection_send_message_with_reply_sync(connection, message);
    if (reply == NULL) {
        ERROR("failed to get a reply");
        exit(EXIT_FAILURE);
    }

    if (xpc_get_type(reply) != XPC_TYPE_DICTIONARY) {
        char *desc = xpc_copy_description(reply);
        ERRORF("unexpected reply: %s", desc);
        free(desc);
        exit(EXIT_FAILURE);
    }

    xpc_object_t error = xpc_dictionary_get_value(reply, REPLY_ERROR);
    if (error) {
        char *desc = xpc_copy_description(error);
        ERRORF("request failed: %s", desc);
        free(desc);
        exit(EXIT_FAILURE);
    }

    xpc_object_t value = xpc_dictionary_get_value(reply, REPLY_VALUE);
    if (value == NULL) {
        char *desc = xpc_copy_description(reply);
        ERRORF("invalid reply: %s", desc);
        free(desc);
        exit(EXIT_FAILURE);
    }

    INFOF("recived value %llu", xpc_uint64_get_value(value));

    return 0;
}
