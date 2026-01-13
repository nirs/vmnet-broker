<!--
SPDX-FileCopyrightText: The vmnet-broker authors
SPDX-License-Identifier: Apache-2.0
-->

# vmnet-broker

A shared XPC service that manages vmnet networks for apps using the
Apple Virtualization framework.

## Why

macOS 26 added native vmnet support to the Virtualization framework. vmnet
networks can reach up to 80 Gbit/s, about 6x faster than file-handle based
networks.

The catch: vmnet networks are bound to the process that creates them. When
the process exits, the network is destroyed, even if VMs are still using it.
You can't create a network in your VM launcher because it would disappear
when the launcher exits.

The solution is to build an XPC service to hold networks alive. But if every
project does this independently:
- Duplicated code for XPC service, lifecycle management, launchd integration
- VMs from different tools can't communicate (separate networks)
- Multiple XPC services running on user machines
- Manual start/stop commands (e.g. `container system start/stop`)

## How it works

vmnet-broker is a single shared XPC service for all apps. Instead of each
project building its own network service, they all connect to the broker.

The broker provides "shared" and "host" networks out of the box. Apps call
`acquireNetwork()` with a network name. The broker creates the network or
returns an existing one if other VMs are already using it. Networks are
reference-counted and cleaned up when the last client disconnects.

![vmnet-broker architecture](media/architecture.svg)

For developers:
- Acquire a network with a single function call
- Client libraries for C, Go, and Swift
- Networks are created and removed automatically

For users:
- One broker for all tools instead of separate XPC services per project
- VMs from lima, podman, vfkit, minikube can share the same network

## Using the broker in your application

To access the broker you can use the C, Swift or Go client libraries:

### C

```c
vmnet_broker_return_t broker_status;
xpc_object_t serialization = vmnet_broker_acquire_network("default", &broker_status);
if (serialization == NULL) {
    ERRORF("failed to start broker session: (%d) %s",
           broker_status, vmnet_broker_strerror(broker_status));
    exit(EXIT_FAILURE);
}
```

- [client](include/vmnet-broker.h)
- [example](test/test.c)

### Swift

```swift
let serialization: xpc_object_t
do {
    serialization = try VmnetBroker.acquireNetwork(named: "default")
} catch {
    logger.error("Failed to get network from broker: \(error)")
    exit(EXIT_FAILURE)
}
```

- [client](swift/Sources/VmnetBroker/client.swift)
- [example](swift/Sources/test/main.swift)

### Go

```go
serialization, err := vmnet_broker.AcquireNetwork("default")
if err != nil {
    return nil, fmt.Errorf("failed to acquire network: %w", err)
}
```

- [client](go/vmnet_broker/vmnet_broker.go)
- [example](go/cmd/test.go)

## Compatibility

macOS Tahoe 26 or later is required.

## Status

This project is work in progress. You can play with it and contribute to
the project.

## Documentation

- [Configuration](docs/configuration.md)
- [Integrations](docs/integrations.md)
- [Protocol](docs/protocol.md)
- [Development](docs/development.md)

## License

This project is licensed under the [Apache License 2.0](LICENSE).
