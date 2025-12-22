#include <stdlib.h>
#include <xpc/xpc.h>

#include "vmnet-broker.h"
#include "log.h"

bool verbose = true;

// The connection must be kept open during the lifetime of the client. The
// kernel invalidates the broker connection after the client terminates.
static xpc_connection_t connection;

static void connect_to_broker(void) {
    INFO("connecting to broker");

    connection = xpc_connection_create_mach_service(MACH_SERVICE_NAME, NULL, 0);
    if (connection == NULL) {
        ERROR("faild to create connection");
        exit(EXIT_FAILURE);
    }

    // Must set the event handler but we don't use it. Errors are logged when we
    // recive a reply.
    xpc_connection_set_event_handler(connection, ^(xpc_object_t event) {
    });

    xpc_connection_resume(connection);
}

    INFO("requesting network");

    xpc_object_t reply = xpc_connection_send_message_with_reply_sync(connection, message);
    if (reply == NULL) {
        ERROR("failed to get a reply");
        exit(EXIT_FAILURE);
    }

    if (xpc_get_type(reply) == XPC_TYPE_ERROR) {
        char *desc = xpc_copy_description(reply);
        ERRORF("request failed: %s", desc);
        free(desc);
        xpc_release(reply);
        exit(EXIT_FAILURE);
    }

    if (xpc_get_type(reply) != XPC_TYPE_DICTIONARY) {
        char *desc = xpc_copy_description(reply);
        ERRORF("unexpected reply: %s", desc);
        free(desc);
        xpc_release(reply);
        exit(EXIT_FAILURE);
    }

    xpc_object_t error = xpc_dictionary_get_value(reply, REPLY_ERROR);
    if (error) {
        char *desc = xpc_copy_description(error);
        ERRORF("broker error: %s", desc);
        free(desc);
        xpc_release(reply);
        exit(EXIT_FAILURE);
    }

    xpc_object_t serialization = xpc_dictionary_get_value(reply, REPLY_NETWORK);
    if (serialization == NULL) {
        char *desc = xpc_copy_description(reply);
        ERRORF("invalid reply: %s", desc);
        free(desc);
        xpc_release(reply);
        exit(EXIT_FAILURE);
    }

    vmnet_return_t status;
    vmnet_network_ref network = vmnet_network_create_with_serialization(serialization, &status);
    if (status != VMNET_SUCCESS) {
        ERRORF("failed to create network from serialization: (%d) %s", status, vmnet_strerror(status));
        xpc_release(reply);
int main(int argc, char *argv[]) {
    connect_to_broker();

        exit(EXIT_FAILURE);
    }

    // TODO: Log network details.
    INFO("network received");

    CFRelease(network);
    xpc_release(reply);

    return 0;
}
