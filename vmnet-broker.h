#ifndef VMNET_BROKER_H
#define VMNET_BROKER_H

#include <stdint.h>
#include <xpc/xpc.h>

// The broker Mach service name.
#define MACH_SERVICE_NAME "com.github.nirs.vmnet-broker"

// Request keys.
#define REQUEST_COMMAND "command"
#define REQUEST_NETWORK_NAME "network_name"

// Request commands.
#define COMMAND_GET "get"

// Reply keys
#define REPLY_NETWORK "network"
#define REPLY_ERROR "error"

// Status codes

// Session started.
#define VMNET_BROKER_SUCCESS 0

// Failed to send XPC message to broker.
#define VMNET_BROKER_XPC_FAILURE 1

// Broker returned invalid reply.
#define VMNET_BROKER_INVALID_REPLY 2

// Broker rejected the request because the user is not allowed to get the network.
#define VMNET_BROKER_NOT_ALLOWED 3

// Broker rejected the request because the request was invalid.
#define VMNET_BROKER_INVALID_REQUEST 4

// Broker did not find the requested network in the broker configuration.
#define VMNET_BROKER_NOT_FOUND 5

// Broker failed to create the requested network.
#define VMNET_BROKER_CREATE_FAILURE 6

// Internal or unknown error.
#define VMNET_BROKER_INTERNAL_ERROR 7

// Match vmnet framework style.
typedef int32_t vmnet_broker_return_t;

/*!
 * @function vmnet_broker_start_session
 *
 * @abstract
 * Start a session with the broker, returning serialization of the requested
 * network. Use `vmnet_create_network_with_serialization` to create a new
 * network.
 *
 * The session is kept open during the lifetime of the process, ensuring that
 * the broker keeps the network alive while clients are using the network.
 *
 * @param network_name
 * The network name.
 *
 * @param status
 * Optional output parameter, returns status.
 *
 * @result
 * Retained network serialization, NULL otherwise. `status` will contain the
 * error code. Use `xpc_release()` to release the serialization.
 */
xpc_object_t _Nullable vmnet_broker_start_session(
    const char * _Nonnull network_name,
    vmnet_broker_return_t * _Nullable status);

/*!
 * @function vmnet_broker_strerror
 *
 * @abstract
 * Return description of the status returned from `vmnet_broker_start_session`.
 *
 * @param status
 * Status returned by `vmnet_broker_start_session`
 *
 * @result
 * Description of the status.
 */
const char * _Nonnull vmnet_broker_strerror(vmnet_broker_return_t status);

#endif // VMNET_BROKER_H
