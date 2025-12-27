#include <signal.h>
#include <stdlib.h>
#include <sys/event.h>
#include <time.h>
#include <uuid/uuid.h>

#include "vmnet-broker.h"
#include "common.h"
#include "log.h"

bool verbose = true;

// The connection must be kept open during the lifetime of the client. The
// kernel invalidates the broker connection after the client terminates.
static xpc_connection_t connection;

// Client starts a vmnet interface using the network returned by the broker.
static interface_ref interface;

// Used to start and stop the interface.
static dispatch_queue_t vmnet_queue;

// Used to wait for signals.
static int kq = -1;

// Exit status.
static int status;

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

    struct network_info info;
    network_info(network, &info);
    INFOF("received network subnet '%s' mask '%s' ipv6_prefix '%s' prefix_len %d",
        info.subnet, info.mask, info.ipv6_prefix, info.prefix_len );

out:
    xpc_release(reply);
    return network;
}

static void start_interface_from_network(vmnet_network_ref network) {
    INFO("starting vmnet interface from network");

    vmnet_queue = dispatch_queue_create("com.github.nirs.vmnet-client", DISPATCH_QUEUE_SERIAL);

    xpc_object_t desc = xpc_dictionary_create_empty();
    dispatch_semaphore_t completed = dispatch_semaphore_create(0);

    interface = vmnet_interface_start_with_network(
        network, desc, vmnet_queue, ^(vmnet_return_t status, xpc_object_t param){
        if (status != VMNET_SUCCESS) {
            ERRORF("failed to start vment interface with network: (%d) %s", status, vmnet_strerror(status));
            exit(EXIT_FAILURE);
        }

        xpc_dictionary_apply(param, ^bool(const char *key, xpc_object_t value) {
            xpc_type_t t = xpc_get_type(value);
            if (t == XPC_TYPE_UINT64) {
                DEBUGF("%s: %llu", key, xpc_uint64_get_value(value));
            } else if (t == XPC_TYPE_INT64) {
                DEBUGF("%s: %lld", key, xpc_int64_get_value(value));
            } else if (t == XPC_TYPE_STRING) {
                DEBUGF("%s: '%s'", key, xpc_string_get_string_ptr(value));
            } else if (t == XPC_TYPE_UUID) {
                char uuid_str[36 + 1];
                uuid_unparse(xpc_uuid_get_bytes(value), uuid_str);
                DEBUGF("%s: '%s'", key, uuid_str);
            }
            return true;
        });

        dispatch_semaphore_signal(completed);
    });

    dispatch_semaphore_wait(completed, DISPATCH_TIME_FOREVER);

    dispatch_release(completed);
    xpc_release(desc);

    INFO("vmnet interface started");
}

static void stop_interface(void)
{
    if (interface == NULL) {
        return;
    }

    INFO("[main] stopping vmnet interface");

    dispatch_semaphore_t completed = dispatch_semaphore_create(0);
    vmnet_return_t status = vmnet_stop_interface(
        interface, vmnet_queue, ^(vmnet_return_t status){
        if (status != VMNET_SUCCESS) {
            ERRORF("[main] failed to stop vmnet interface: (%d) %s", status, vmnet_strerror(status));
            exit(EXIT_FAILURE);
        }
        dispatch_semaphore_signal(completed);
    });

    if (status != VMNET_SUCCESS) {
        ERRORF("[main] failed to stop vment interface: (%d) %s", status, vmnet_strerror(status));
        exit(EXIT_FAILURE);
    }

    dispatch_semaphore_wait(completed, DISPATCH_TIME_FOREVER);
    dispatch_release(completed);

    dispatch_release(vmnet_queue);
    vmnet_queue = NULL;

    INFO("[main] vmnet interface stopped");
}

static void setup_kq(void)
{
    kq = kqueue();
    if (kq == -1) {
        ERRORF("[main] kqueue: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }

    struct kevent changes[] = {
        {.ident=SIGTERM, .filter=EVFILT_SIGNAL, .flags=EV_ADD},
        {.ident=SIGINT, .filter=EVFILT_SIGNAL, .flags=EV_ADD},
    };

    sigset_t mask;
    sigemptyset(&mask);
    for (size_t i = 0; i < ARRAY_SIZE(changes); i++) {
        if (changes[i].filter == EVFILT_SIGNAL) {
            sigaddset(&mask, changes[i].ident);
        }
    }
    if (sigprocmask(SIG_BLOCK, &mask, NULL) != 0) {
        ERRORF("[main] sigprocmask: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }

    // We will receive EPIPE on the socket.
    signal(SIGPIPE, SIG_IGN);

    if (kevent(kq, changes, ARRAY_SIZE(changes), NULL, 0, NULL) != 0) {
        ERRORF("[main] kevent: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }
}

static void wait_for_termination(void)
{
    INFO("[main] waiting for termination");

    struct kevent events[1];

    while (1) {
        int n = kevent(kq, NULL, 0, events, 1, NULL);
        if (n < 0) {
            ERRORF("[main] kevent: %s", strerror(errno));
            status |= EXIT_FAILURE;
            break;
        }
        if (n > 0) {
            if (events[0].filter == EVFILT_SIGNAL) {
                INFOF("[main] received signal %s", strsignal(events[0].ident));
                status = EXIT_SUCCESS;
                break;
            }
        }
    }
}

int main(int argc, char *argv[]) {
    setup_kq();
    connect_to_broker();

    vmnet_network_ref network = request_network_from_broker();
    if (network == NULL) {
        exit(EXIT_FAILURE);
    }

    // NOTE: This requires root or com.apple.security.virtualization entitlement.
    start_interface_from_network(network);

    wait_for_termination();
    stop_interface();

    return status;
}
