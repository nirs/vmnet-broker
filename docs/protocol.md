# Broker Protocol

This document describes the XPC protocol used for communication between
clients and the vmnet-broker. For a complete implementation example, see
[client/client.c](../client/client.c).

## Overview

The broker uses macOS XPC via a Mach service for IPC. Clients connect to the
broker, send requests, and receive replies. The connection is kept open for
the lifetime of the client process—the broker uses the connection to track
which networks are in use.

## Connection

Clients connect to the Mach service:

```
com.github.nirs.vmnet-broker
```

> [!IMPORTANT]
> The connection must remain open while using acquired networks.
> When the connection closes (e.g., client process terminates), the broker
> automatically releases any networks held by that client.

## Request Format

Request is an XPC dictionary with the following keys:

| Key | Type | Description |
|-----|------|-------------|
| `command` | string | The command to execute (required) |
| `network_name` | string | Name of the network (required for `acquire`) |

### Commands

#### `acquire`

Acquires a shared reference to a network, creating it if necessary. A client
can acquire multiple networks by sending multiple `acquire` requests.

**Builtin network names:**
- `shared` - NAT network with internet access via the host
- `host` - Host-only network (no internet access)

## Reply Format

Reply is an XPC dictionary with one of the following:

### Success Reply

| Key | Type | Description |
|-----|------|-------------|
| `network` | xpc_object | Opaque network serialization |

The `network` value is an opaque XPC object that can be passed to
`vmnet_interface_set_network()` to attach a VM interface to the shared
network.

### Error Reply

| Key | Type | Description |
|-----|------|-------------|
| `error` | int64 | Error code |

> [!NOTE]
> The broker only sends `error` on failure. A successful reply
> contains `network` but no `error` key.

## Error Codes

The broker returns these error codes in the `error` field:

| Code | Name | Description |
|------|------|-------------|
| 3 | `NOT_ALLOWED` | User is not allowed to access the network |
| 4 | `INVALID_REQUEST` | Request was malformed or missing required fields |
| 5 | `NOT_FOUND` | Network name not found in broker configuration |
| 6 | `CREATE_FAILURE` | Failed to create the network (vmnet error) |
| 7 | `INTERNAL_ERROR` | Internal or unknown error |

## Connection Lifecycle

1. **Connect**: Client creates connection to Mach service
2. **Acquire**: Client sends `acquire` request(s) to get network references
3. **Use**: Client uses the network serialization with vmnet APIs
4. **Disconnect**: When client closes connection or terminates, the broker
   updates network reference counts. Unused networks are removed after a delay.
