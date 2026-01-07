// SPDX-FileCopyrightText: The vmnet-broker authors
// SPDX-License-Identifier: Apache-2.0

#include <signal.h>
#include <stdlib.h>
#include <sys/event.h>
#include <time.h>
#include <uuid/uuid.h>
#include <xpc/xpc.h>

#include "vmnet-broker.h"
#include "common.h"
#include "log.h"

bool verbose = true;

// Client starts a vmnet interface using the network returned by the broker.
static interface_ref interface;

// Used to start and stop the interface.
static dispatch_queue_t vmnet_queue;

// Used to wait for signals.
static int kq = -1;

// Exit status.
static int status;

static void start_interface_from_network(vmnet_network_ref network) {
    INFO("starting vmnet interface from network");

    vmnet_queue = dispatch_queue_create("com.github.nirs.vmnet-client", DISPATCH_QUEUE_SERIAL);

    xpc_object_t desc = xpc_dictionary_create_empty();
    dispatch_semaphore_t completed = dispatch_semaphore_create(0);

    interface = vmnet_interface_start_with_network(
        network, desc, vmnet_queue, ^(vmnet_return_t start_status, xpc_object_t param){
        if (start_status != VMNET_SUCCESS) {
            ERRORF("failed to start vmnet interface with network: (%d) %s", start_status, vmnet_strerror(start_status));
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

    INFO("stopping vmnet interface");

    dispatch_semaphore_t completed = dispatch_semaphore_create(0);
    vmnet_return_t vmnet_status = vmnet_stop_interface(
        interface, vmnet_queue, ^(vmnet_return_t stop_status){
        if (stop_status != VMNET_SUCCESS) {
            ERRORF("failed to stop vmnet interface: (%d) %s", stop_status, vmnet_strerror(stop_status));
            exit(EXIT_FAILURE);
        }
        dispatch_semaphore_signal(completed);
    });

    if (vmnet_status != VMNET_SUCCESS) {
        ERRORF("failed to stop vment interface: (%d) %s", vmnet_status, vmnet_strerror(vmnet_status));
        exit(EXIT_FAILURE);
    }

    dispatch_semaphore_wait(completed, DISPATCH_TIME_FOREVER);
    dispatch_release(completed);

    dispatch_release(vmnet_queue);
    vmnet_queue = NULL;

    INFO("vmnet interface stopped");
}

static void setup_kq(void)
{
    kq = kqueue();
    if (kq == -1) {
        ERRORF("kqueue: %s", strerror(errno));
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
        ERRORF("sigprocmask: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }

    // We will receive EPIPE on the socket.
    signal(SIGPIPE, SIG_IGN);

    if (kevent(kq, changes, ARRAY_SIZE(changes), NULL, 0, NULL) != 0) {
        ERRORF("kevent: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }
}

static void wait_for_termination(void)
{
    INFO("waiting for termination");

    struct kevent events[1];

    while (1) {
        int n = kevent(kq, NULL, 0, events, 1, NULL);
        if (n < 0) {
            ERRORF("kevent: %s", strerror(errno));
            status = EXIT_FAILURE;
            break;
        }
        if (n > 0) {
            if (events[0].filter == EVFILT_SIGNAL) {
                INFOF("received signal %s", strsignal(events[0].ident));
                status = EXIT_SUCCESS;
                break;
            }
        }
    }
}

int main() {
    setup_kq();

    vmnet_broker_return_t broker_status;
    xpc_object_t serialization = vmnet_broker_acquire_network("default", &broker_status);
    if (serialization == NULL) {
        ERRORF("failed to start broker session: (%d) %s", broker_status, vmnet_broker_strerror(broker_status));
        exit(EXIT_FAILURE);
    }

    vmnet_return_t vmnet_status;
    vmnet_network_ref network = vmnet_network_create_with_serialization(serialization, &vmnet_status);
    xpc_release(serialization);

    if (network == NULL) {
        ERRORF("failed to create network from serialization: (%d) %s", vmnet_status, vmnet_strerror(vmnet_status));
        exit(EXIT_FAILURE);
    }

    struct network_info info;
    network_info(network, &info);
    INFOF("received network subnet '%s' mask '%s' ipv6_prefix '%s' prefix_len %d",
        info.subnet, info.mask, info.ipv6_prefix, info.prefix_len );

    // NOTE: This requires root or com.apple.security.virtualization entitlement.
    start_interface_from_network(network);
    CFRelease(network);

    wait_for_termination();
    stop_interface();

    return status;
}
