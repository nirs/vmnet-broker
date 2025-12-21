#include <xpc/xpc.h>
#include <stdio.h>

int main() {
    xpc_connection_t conn = xpc_connection_create_mach_service("com.github.nirs.vmnet-broker", NULL, 0);
    xpc_connection_set_event_handler(conn, ^(xpc_object_t event) { /* handle errors */ });
    xpc_connection_resume(conn);

    xpc_object_t msg = xpc_dictionary_create(NULL, NULL, 0);
    xpc_dictionary_set_string(msg, "network_name", "shared");

    printf("Requesting network...\n");
    xpc_object_t reply = xpc_connection_send_message_with_reply_sync(conn, msg);

    if (xpc_get_type(reply) != XPC_TYPE_DICTIONARY) {
        char *desc = xpc_copy_description(reply);
        printf("Error: Unexpected reply: %s\n", desc);
        free(desc);
        return 1;
    }

    xpc_object_t handle = xpc_dictionary_get_value(reply, "handle");
    if (handle) {
        char *desc = xpc_copy_description(handle);
        printf("Success! Received vmnet handle: %s\n", desc);
        free(desc);
    } else {
        printf("Error: %s\n", xpc_dictionary_get_string(reply, "error"));
    }
    return 0;
}
