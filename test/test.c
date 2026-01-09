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

#include "common.h"
#include "log.h"
#include "vmnet-broker.h"

#define NANOSECONDS_PER_SECOND 1000000000ULL

bool verbose = true;

// Interfaces started by start_interface().
#define MAX_INTERFACES 16

struct interface {
    const char *network_name;
    interface_ref iface;
};

static struct interface interfaces[MAX_INTERFACES];
static int interface_count = 0;

// Used to start and stop interfaces.
static dispatch_queue_t vmnet_queue;

// Used to wait for signals.
static int kq = -1;

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
    const char *network_names[MAX_INTERFACES];
    int network_count;
    bool quick;
} opt = {
    .network_count = 0,
    .quick = false,
};

// Start with ':' to enable detection of missing argument.
static const char *short_options = ":hq";

static struct option long_options[] = {
    {
        .name = "help",
        .has_arg = no_argument,
        .flag = 0,
        .val = 'h',
    },
    {
        .name = "quick",
        .has_arg = no_argument,
        .flag = 0,
        .val = 'q',
    },
    {0},
};

static void usage(int code) {
    fputs(
        "\n"
        "Test vmnet-broker client\n"
        "\n"
        "    test-c [-q|--quick] [-h|--help] [network_name ...]\n"
        "\n"
        "Options:\n"
        "    -q, --quick    Run quick test and exit immediately\n"
        "    -h, --help     Show this help message\n"
        "\n"
        "Arguments:\n"
        "    network_name   Networks to acquire (default: shared, max: 8)\n"
        "\n"
        "Output (stdout):\n"
        "    ok                 Test passed\n"
        "    fail <step> <code> Test failed at step with error code\n"
        "\n",
        stderr
    );

    exit(code);
}

static void parse_options(int argc, char *argv[]) {
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
    while (optind < argc) {
        if (opt.network_count >= MAX_INTERFACES) {
            ERRORF("too many networks (max %d)", MAX_INTERFACES);
            fail("parse_options", E2BIG);
        }
        opt.network_names[opt.network_count++] = argv[optind++];
    }

    // Default to "shared" if no networks specified.
    if (opt.network_count == 0) {
        opt.network_names[opt.network_count++] = "shared";
    }

    // Log the networks to test (8 networks * 64 bytes each).
    char networks[512] = "";
    size_t pos = 0;
    for (int i = 0; i < opt.network_count; i++) {
        int n = snprintf(
            networks + pos, sizeof(networks) - pos, "%s ", opt.network_names[i]
        );
        if (n > 0 && pos + n < sizeof(networks)) {
            pos += n;
        }
    }
    INFOF("testing networks: %s", networks);

    if (opt.quick) {
        INFO("running in quick mode");
    }
}

static uint64_t gettime(void) {
    struct timespec ts;
    // CLOCK_UPTIME_RAW: monotonic clock that increments even while system is
    // asleep
    clock_gettime(CLOCK_UPTIME_RAW, &ts);
    return (uint64_t)ts.tv_sec * NANOSECONDS_PER_SECOND + ts.tv_nsec;
}

// Acquire network from broker and create vmnet_network_ref.
static vmnet_network_ref acquire_network(const char *network_name) {
    INFOF("acquiring network '%s'", network_name);

    uint64_t start_time = gettime();
    vmnet_broker_return_t broker_status;
    xpc_object_t serialization = vmnet_broker_acquire_network(
        network_name, &broker_status
    );
    uint64_t end_time = gettime();

    if (serialization == NULL) {
        ERRORF(
            "failed to acquire network '%s': (%d) %s",
            network_name,
            broker_status,
            vmnet_broker_strerror(broker_status)
        );
        fail("acquire_network", broker_status);
    }

    uint64_t elapsed_nanos = end_time - start_time;
    double elapsed_seconds = (double)elapsed_nanos / NANOSECONDS_PER_SECOND;
    INFOF(
        "acquired network '%s' from broker: status=%d (%s) in %.6f s",
        network_name,
        broker_status,
        vmnet_broker_strerror(broker_status),
        elapsed_seconds
    );

    vmnet_return_t vmnet_status;
    vmnet_network_ref network = vmnet_network_create_with_serialization(
        serialization, &vmnet_status
    );
    xpc_release(serialization);

    if (network == NULL) {
        ERRORF(
            "failed to create network from serialization: (%d) %s",
            vmnet_status,
            vmnet_strerror(vmnet_status)
        );
        fail("create_network", vmnet_status);
    }

    INFOF(
        "created network from serialization: status=%d (%s)",
        vmnet_status,
        vmnet_strerror(vmnet_status)
    );

    struct network_info info;
    network_info(network, &info);
    INFOF(
        "received network subnet '%s' mask '%s' ipv6_prefix '%s' prefix_len %d",
        info.subnet,
        info.mask,
        info.ipv6_prefix,
        info.prefix_len
    );

    return network;
}

// Start interface from network and add to interfaces list.
static void
start_interface(vmnet_network_ref network, const char *network_name) {
    int index = interface_count;
    INFOF("starting vmnet interface %d for network '%s'", index, network_name);

    if (interface_count >= MAX_INTERFACES) {
        ERRORF("too many interfaces (max %d)", MAX_INTERFACES);
        fail("start_interface", ENOMEM);
    }

    xpc_object_t desc = xpc_dictionary_create_empty();
    dispatch_semaphore_t completed = dispatch_semaphore_create(0);

    interface_ref iface = vmnet_interface_start_with_network(
        network,
        desc,
        vmnet_queue,
        ^(vmnet_return_t start_status, xpc_object_t param) {
            if (start_status != VMNET_SUCCESS) {
                ERRORF(
                    "failed to start vmnet interface: (%d) %s",
                    start_status,
                    vmnet_strerror(start_status)
                );
                fail("start_interface", start_status);
            }

            xpc_dictionary_apply(
                param, ^bool(const char *key, xpc_object_t value) {
                    xpc_type_t t = xpc_get_type(value);
                    if (t == XPC_TYPE_UINT64) {
                        DEBUGF("%s: %llu", key, xpc_uint64_get_value(value));
                    } else if (t == XPC_TYPE_INT64) {
                        DEBUGF("%s: %lld", key, xpc_int64_get_value(value));
                    } else if (t == XPC_TYPE_STRING) {
                        DEBUGF(
                            "%s: '%s'", key, xpc_string_get_string_ptr(value)
                        );
                    } else if (t == XPC_TYPE_UUID) {
                        char uuid_str[36 + 1];
                        uuid_unparse(xpc_uuid_get_bytes(value), uuid_str);
                        DEBUGF("%s: '%s'", key, uuid_str);
                    }
                    return true;
                }
            );

            dispatch_semaphore_signal(completed);
        }
    );

    dispatch_semaphore_wait(completed, DISPATCH_TIME_FOREVER);

    dispatch_release(completed);
    xpc_release(desc);

    struct interface *interface = &interfaces[interface_count++];
    interface->network_name = network_name;
    interface->iface = iface;

    INFOF("vmnet interface %d for network '%s' started", index, network_name);
}

// Stop all interfaces in reverse order.
static void stop_interfaces(void) {
    while (interface_count > 0) {
        int index = --interface_count;
        struct interface interface = interfaces[index];
        INFOF(
            "stopping vmnet interface %d for network '%s'",
            index,
            interface.network_name
        );

        dispatch_semaphore_t completed = dispatch_semaphore_create(0);
        vmnet_return_t vmnet_status = vmnet_stop_interface(
            interface.iface, vmnet_queue, ^(vmnet_return_t stop_status) {
                if (stop_status != VMNET_SUCCESS) {
                    ERRORF(
                        "failed to stop vmnet interface: (%d) %s",
                        stop_status,
                        vmnet_strerror(stop_status)
                    );
                    fail("stop_interface", stop_status);
                }
                dispatch_semaphore_signal(completed);
            }
        );

        if (vmnet_status != VMNET_SUCCESS) {
            ERRORF(
                "failed to stop vmnet interface: (%d) %s",
                vmnet_status,
                vmnet_strerror(vmnet_status)
            );
            fail("stop_interface", vmnet_status);
        }

        dispatch_semaphore_wait(completed, DISPATCH_TIME_FOREVER);
        dispatch_release(completed);

        INFOF(
            "vmnet interface %d for network '%s' stopped",
            index,
            interface.network_name
        );
    }

    if (vmnet_queue) {
        dispatch_release(vmnet_queue);
        vmnet_queue = NULL;
    }
}

static void setup_vmnet(void) {
    vmnet_queue = dispatch_queue_create(
        "com.github.nirs.vmnet-client", DISPATCH_QUEUE_SERIAL
    );
}

static void setup_kq(void) {
    kq = kqueue();
    if (kq == -1) {
        ERRORF("kqueue: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }

    struct kevent changes[] = {
        {.ident = SIGTERM, .filter = EVFILT_SIGNAL, .flags = EV_ADD},
        {.ident = SIGINT, .filter = EVFILT_SIGNAL, .flags = EV_ADD},
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
static int wait_for_termination(void) {
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

int main(int argc, char *argv[]) {
    parse_options(argc, argv);

    setup_kq();
    setup_vmnet();

    // Acquire networks and start interfaces.
    for (int i = 0; i < opt.network_count; i++) {
        const char *name = opt.network_names[i];
        vmnet_network_ref network = acquire_network(name);
        start_interface(network, name);
        CFRelease(network);
    }

    // Wait for termination signal (interactive mode only).
    int wait_error = 0;
    if (!opt.quick) {
        wait_error = wait_for_termination();
    }

    // Stop all interfaces.
    stop_interfaces();

    if (wait_error) {
        fail("kevent", wait_error);
    }

    ok();
}
