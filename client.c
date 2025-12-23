#include <stdlib.h>
#include <time.h>
#include <vmnet/vmnet.h>
#include <xpc/xpc.h>
#include <uuid/uuid.h>

#include "vmnet-broker.h"
#include "log.h"

bool verbose = true;

// The connection must be kept open during the lifetime of the client. The
// kernel invalidates the broker connection after the client terminates.
static xpc_connection_t connection;

// Client starts a vmnet interface using the network returned by the broker.
static interface_ref interface;

static void connect_to_broker(void) {
    INFO("connecting to broker");

    connection = xpc_connection_create_mach_service(MACH_SERVICE_NAME, NULL, 0);

    // Must set the event handler but we don't use it. Errors are logged when we
    // receive a reply.
    xpc_connection_set_event_handler(connection, ^(xpc_object_t event) {
    });

    xpc_connection_resume(connection);
}

// Get a network from the broker.
static vmnet_network_ref request_network_from_broker(void) {
    INFO("requesting network");

    xpc_object_t message = xpc_dictionary_create_empty();
    xpc_dictionary_set_string(message, REQUEST_COMMAND, COMMAND_GET);

    xpc_object_t reply = xpc_connection_send_message_with_reply_sync(connection, message);
    xpc_clear(&message);

    vmnet_network_ref network = NULL;

    if (xpc_get_type(reply) == XPC_TYPE_ERROR) {
        const char *desc = xpc_dictionary_get_string(reply, XPC_ERROR_KEY_DESCRIPTION);
        ERRORF("request failed: %s", desc);
        goto out;
    }

    if (xpc_get_type(reply) != XPC_TYPE_DICTIONARY) {
        char *desc = xpc_copy_description(reply);
        ERRORF("unexpected reply: %s", desc);
        free(desc);
        goto out;
    }

    xpc_object_t error = xpc_dictionary_get_value(reply, REPLY_ERROR);
    if (error) {
        int64_t code = xpc_dictionary_get_int64(error, ERROR_CODE);
        const char *message = xpc_dictionary_get_string(error, ERROR_MESSAGE);
        ERRORF("broker error: (%lld) %s", code, message);
        goto out;
    }

    xpc_object_t serialization = xpc_dictionary_get_value(reply, REPLY_NETWORK);
    if (serialization == NULL) {
        char *desc = xpc_copy_description(reply);
        ERRORF("invalid reply: %s", desc);
        free(desc);
        goto out;
    }

    vmnet_return_t status;
    network = vmnet_network_create_with_serialization(serialization, &status);
    if (network == NULL) {
        ERRORF("failed to create network from serialization: (%d) %s", status, vmnet_strerror(status));
        goto out;
    }

    INFO("network received");

out:
    xpc_release(reply);
    return network;
}

static void write_vmnet_info(xpc_object_t param)
{
    __block int count = 0;

#define print_item(fmt, key, value) \
    do { \
        if (count++ > 0) \
            printf(","); \
        printf(fmt, key, value); \
    } while (0)

    printf("{");

    xpc_dictionary_apply(param, ^bool(const char *key, xpc_object_t value) {
        xpc_type_t t = xpc_get_type(value);
        if (t == XPC_TYPE_UINT64) {
            print_item("\"%s\":%llu", key, xpc_uint64_get_value(value));
        } else if (t == XPC_TYPE_INT64) {
            print_item("\"%s\":%lld", key, xpc_int64_get_value(value));
        } else if (t == XPC_TYPE_STRING) {
            print_item("\"%s\":\"%s\"", key, xpc_string_get_string_ptr(value));
        } else if (t == XPC_TYPE_UUID) {
            char uuid_str[36 + 1];
            uuid_unparse(xpc_uuid_get_bytes(value), uuid_str);
            print_item("\"%s\":\"%s\"", key, uuid_str);
        }
        return true;
    });

    printf("}\n");
    fflush(stdout);
}

static void start_interface_from_network(vmnet_network_ref network) {
    DEBUG("starting vmnet interface from network");

    xpc_object_t desc = xpc_dictionary_create_empty();
    dispatch_queue_t queue = dispatch_queue_create("com.github.nirs.vmnet-client", DISPATCH_QUEUE_SERIAL);
    dispatch_semaphore_t completed = dispatch_semaphore_create(0);

    interface = vmnet_interface_start_with_network(network, desc, queue, ^(vmnet_return_t status, xpc_object_t param){
        if (status != VMNET_SUCCESS) {
            ERRORF("failed to start vment interface with network: (%d) %s", status, vmnet_strerror(status));
            exit(EXIT_FAILURE);
        }

        INFO("vmnet interface started");
        write_vmnet_info(param);
        dispatch_semaphore_signal(completed);
    });

    dispatch_semaphore_wait(completed, DISPATCH_TIME_FOREVER);

    dispatch_release(completed);
    dispatch_release(queue);
    xpc_release(desc);
}

int main(int argc, char *argv[]) {
    connect_to_broker();

    vmnet_network_ref network = request_network_from_broker();
    if (network == NULL) {
        exit(EXIT_FAILURE);
    }

    // NOTE: This requires root. Real client will create a virtual machine with
    // a VZVmnetNetworkDeviceAttachment, which does not require root or the
    // "com.apple.networking" entitlement. The virtualization framework service
    // for the vm have the privileges to start the vment interface.
    // https://developer.apple.com/documentation/virtualization/vzvmnetnetworkdeviceattachment?language=objc
    start_interface_from_network(network);

    // Simulate starting a VM. The connection is alive while the process is running.
    INFO("waiting for termination");
    sleep(300);

    return 0;
}
