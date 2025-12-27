#ifndef VMNET_BROKER_H
#define VMNET_BROKER_H

#include <vmnet/vmnet.h>

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

// Error keys
#define ERROR_MESSAGE "message"
#define ERROR_CODE "code"

// Error codes

// Failed to send XPC message to broker.
#define ERROR_XPC_REQUEST 1

// Broker return invalid reply.
#define ERROR_INVALID_REPLY 2

// Broker rejected the request becasue the user is not allowed to get the network.
#define ERROR_NOT_ALLOWED 3

// Broker rejected the request becasue the request was invalid.
#define ERROR_INVALID_REQUEST 4

// Broker did not find the requested network in the broker configuration.
#define ERROR_NOT_FOUND 5

// Broker failed to create the requested network.
#define ERROR_CREATE_NETWORK 6

// Broker error.
#define ERROR_MESSAGE_SIZE 160

struct vmnet_broker_error {
    char message[ERROR_MESSAGE_SIZE];
    int code;
};

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
 * @param error
 * Optional output parameter, filled if the function return NULL.
 *
 * @result
 * Retained network serialization, or NULL on error, in which case error is
 * filled with information about the error. Use `CFRelease()` to release the
 * serialization.
 *
 */
xpc_object_t vmnet_broker_start_session(const char *network_name, struct vmnet_broker_error *error);

#endif // VMNET_BROKER_H
