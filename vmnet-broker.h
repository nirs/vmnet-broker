#ifndef VMNET_BROKER_H
#define VMNET_BROKER_H

#include <assert.h>
#include <vmnet/vmnet.h>
#include <xpc/xpc.h>

// The broker Mach service name.
#define MACH_SERVICE_NAME "com.github.nirs.vmnet-broker"

// Request keys.
#define REQUEST_COMMAND "command"

// Request commands.
#define COMMAND_GET "get"

// Reply keys
#define REPLY_NETWORK "network"
#define REPLY_ERROR "error"

// Error keys
#define ERROR_MESSAGE "message"
#define ERROR_CODE "code"

// Error codes
#define ERROR_INVALID_REQUEST 1
#define ERROR_CREATE_NETWORK 2

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))

static inline void xpc_clear(xpc_object_t *p) {
    assert(p);
    if (*p) {
        xpc_release(*p);
        *p = NULL;
    }
}

static inline const char *vmnet_strerror(vmnet_return_t v)
{
    switch (v) {
    case VMNET_SUCCESS:
        return "VMNET_SUCCESS";
    case VMNET_FAILURE:
        return "VMNET_FAILURE";
    case VMNET_MEM_FAILURE:
        return "VMNET_MEM_FAILURE";
    case VMNET_INVALID_ARGUMENT:
        return "VMNET_INVALID_ARGUMENT";
    case VMNET_SETUP_INCOMPLETE:
        return "VMNET_SETUP_INCOMPLETE";
    case VMNET_INVALID_ACCESS:
        return "VMNET_INVALID_ACCESS";
    case VMNET_PACKET_TOO_BIG:
        return "VMNET_PACKET_TOO_BIG";
    case VMNET_BUFFER_EXHAUSTED:
        return "VMNET_BUFFER_EXHAUSTED";
    case VMNET_TOO_MANY_PACKETS:
        return "VMNET_TOO_MANY_PACKETS";
    default:
        return "(unknown status)";
    }
}

#endif // VMNET_BROKER_H
