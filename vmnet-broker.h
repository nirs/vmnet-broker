#ifndef VMNET_BROKER_H
#define VMNET_BROKER_H

// The broker Mach service name.
#define MACH_SERVICE_NAME "com.github.nirs.vmnet-broker"

// Request keys.
#define REQUEST_COMMAND "command"

// Request commands.
#define COMMAND_GET "get"

// Reply keys
#define REPLY_VALUE "value"
#define REPLY_ERROR "error"

// Error keys
#define ERROR_MESSAGE "message"
#define ERROR_CODE "code"

// Error codes
#define ERROR_INVALID_REQUEST 1

#endif // VMNET_BROKER_H
