// SPDX-FileCopyrightText: The vmnet-broker authors
// SPDX-License-Identifier: Apache-2.0

#include <dispatch/dispatch.h>
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/event.h>
#include <time.h>
#include <uuid/uuid.h>
#include <xpc/xpc.h>

#include "vmnet-broker.h"
#include "common.h"
#include "log.h"

bool verbose = true;

#define NANOSECONDS_PER_SECOND 1000000000ULL

// Test result output for test runners (stdout).
static void ok(void) {
    fflush(stderr);
    printf("ok\n");
    exit(EXIT_SUCCESS);
}

static void fail(const char *step, int code) {
    fflush(stderr);
    printf("fail %s %d\n", step, code);
    exit(EXIT_FAILURE);
}

// Command line options
static struct {
    const char *network_name;
    bool quick;
} opt = {
    .network_name = "shared",
    .quick = false,
};

// Start with ':' to enable detection of missing argument.
static const char *short_options = ":hq";

static struct option long_options[] = {
    {"help",  no_argument, 0, 'h'},
    {"quick", no_argument, 0, 'q'},
    {0,       0,           0, 0}
};

static void usage(int code)
{
    fputs(
        "\n"
        "Test vmnet-broker client\n"
        "\n"
        "    test-c [-q|--quick] [-h|--help] [network_name]\n"
        "\n"
        "Options:\n"
        "    -q, --quick    Run quick test and exit immediately\n"
        "    -h, --help     Show this help message\n"
        "\n"
        "Arguments:\n"
        "    network_name   Network to acquire (default: shared)\n"
        "\n"
        "Output (stdout):\n"
        "    ok                 Test passed\n"
        "    fail <step> <code> Test failed at step with error code\n"
        "\n",
        stderr);

    exit(code);
}

static void parse_options(int argc, char *argv[])
{
    const char *optname;
    int c;

    // Silence getopt_long error messages.
    opterr = 0;

    while (1) {
        optname = argv[optind];
        c = getopt_long(argc, argv, short_options, long_options, NULL);

        if (c == -1) {
            break;
        }

        switch (c) {
        case 'h':
            usage(0);
            break;
        case 'q':
            opt.quick = true;
            break;
        case ':':
            ERRORF("Option %s requires an argument", optname);
            usage(1);
            break;
        case '?':
        default:
            ERRORF("Invalid option: %s", optname);
            usage(1);
        }
    }

    // Parse positional arguments.
    if (optind < argc) {
        opt.network_name = argv[optind++];
    }
}

static uint64_t gettime(void) {
    struct timespec ts;
    // CLOCK_UPTIME_RAW: monotonic clock that increments even while system is asleep
    clock_gettime(CLOCK_UPTIME_RAW, &ts);
    return (uint64_t)ts.tv_sec * NANOSECONDS_PER_SECOND + ts.tv_nsec;
}

// Client starts a vmnet interface using the network returned by the broker.
static interface_ref interface;

// Used to start and stop the interface.
static dispatch_queue_t vmnet_queue;

// Used to wait for signals.
static int kq = -1;

static void start_interface_from_network(vmnet_network_ref network) {
    INFO("starting vmnet interface from network");

    vmnet_queue = dispatch_queue_create("com.github.nirs.vmnet-client", DISPATCH_QUEUE_SERIAL);

    xpc_object_t desc = xpc_dictionary_create_empty();
    dispatch_semaphore_t completed = dispatch_semaphore_create(0);

    interface = vmnet_interface_start_with_network(
        network, desc, vmnet_queue, ^(vmnet_return_t start_status, xpc_object_t param){
        if (start_status != VMNET_SUCCESS) {
            ERRORF("failed to start vmnet interface with network: (%d) %s", start_status, vmnet_strerror(start_status));
            fail("start_interface", start_status);
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
            fail("stop_interface", stop_status);
        }
        dispatch_semaphore_signal(completed);
    });

    if (vmnet_status != VMNET_SUCCESS) {
        ERRORF("failed to stop vmnet interface: (%d) %s", vmnet_status, vmnet_strerror(vmnet_status));
        fail("stop_interface", vmnet_status);
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

// Returns 0 on success (signal received), or errno on error.
static int wait_for_termination(void)
{
    INFO("waiting for termination");

    struct kevent events[1];

    while (1) {
        int n = kevent(kq, NULL, 0, events, 1, NULL);
        if (n < 0) {
            int err = errno;
            ERRORF("kevent: %s", strerror(err));
            return err;
        }
        if (n > 0 && events[0].filter == EVFILT_SIGNAL) {
            INFOF("received signal %s", strsignal(events[0].ident));
            return 0;
        }
    }
}

static void test_network(const char *network_name) {
    INFOF("testing network '%s'", network_name);

    uint64_t start_time = gettime();
    vmnet_broker_return_t broker_status;
    xpc_object_t serialization = vmnet_broker_acquire_network(network_name, &broker_status);
    uint64_t end_time = gettime();

    if (serialization == NULL) {
        ERRORF("failed to acquire network '%s': (%d) %s",
            network_name, broker_status, vmnet_broker_strerror(broker_status));
        fail("acquire_network", broker_status);
    }

    uint64_t elapsed_nanos = end_time - start_time;
    double elapsed_seconds = (double)elapsed_nanos / NANOSECONDS_PER_SECOND;
    INFOF("acquired network '%s' from broker: status=%d (%s) in %.6f s",
        network_name, broker_status, vmnet_broker_strerror(broker_status), elapsed_seconds);

    vmnet_return_t vmnet_status;
    vmnet_network_ref network = vmnet_network_create_with_serialization(serialization, &vmnet_status);
    xpc_release(serialization);

    if (network == NULL) {
        ERRORF("failed to create network from serialization: (%d) %s",
            vmnet_status, vmnet_strerror(vmnet_status));
        fail("create_network", vmnet_status);
    }

    INFOF("created network from serialization: status=%d (%s)",
        vmnet_status, vmnet_strerror(vmnet_status));

    struct network_info info;
    network_info(network, &info);
    INFOF("received network subnet '%s' mask '%s' ipv6_prefix '%s' prefix_len %d",
        info.subnet, info.mask, info.ipv6_prefix, info.prefix_len);

    // NOTE: This requires root or com.apple.security.virtualization entitlement.
    start_interface_from_network(network);
    CFRelease(network);

    int wait_error = 0;
    if (!opt.quick) {
        wait_error = wait_for_termination();
    }

    stop_interface();

    if (wait_error) {
        fail("kevent", wait_error);
    }
}

int main(int argc, char *argv[]) {
    parse_options(argc, argv);

    if (opt.quick) {
        INFOF("running in quick mode with network '%s'", opt.network_name);
    }

    setup_kq();
    test_network(opt.network_name);
    ok();
}
