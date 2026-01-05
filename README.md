# vmnet-broker

The vmnet-broker manages shared vmnet networks for applications using
the Apple Virtualization framework. With the broker you can run multiple
virtual machines using the same network without helpers like
vmnet-helper that require root when starting a VM.

## Compatibility

macOS Tahoe 26 or later is required.

## Advantages over vmnet-helper

- **Better performance**: Testing shows 3-6 time higher bandwidth with
  much lower cpu usage compared to vmnet-helper.
- **No root required**: Since macOS 26, root is not required to use
  vmnet. The only requirement is the `com.apple.security.virtualization`
  entitlement. vmnet-helper on macOS < 26 required root.
- **More reliable**: Using public APIs make the broker more reliable in the long
  term. vmnet-helper depends on private APIs to get good performance.
- **Easier to maintain**: the broker in not in critical the data path.

## How it works

The broker is running as a launchd daemon. It must installed as root to
ensure the program cannot be modified by unprivileged user, but it runs
as an unprivileged user.

When the first client connects to the broker to request a network, the broker
creates a new vmnet network based on the broker configuration, and returns the
network to the client. The client use the network to create a virtual machine
using the new
[VZVmnetNetworkDeviceAttachment](https://developer.apple.com/documentation/virtualization/vzvmnetnetworkdeviceattachment)
introduced in macOS 26, that does not require root or the special
"com.apple.networking" entitlement.

When the next client connects, the existing vmnet network is returned to the
client, so both clients are using the same vmnet network.

When the last client disconnects from the broker, the vmnet network is
released, and the kernel resources are destroyed.

## Status

This project is work in progress. You can play with it and contribute to
the project.

## Hacking

### Dependencies

The Go test runner depends on this upstream PR:
https://github.com/Code-Hex/vz/pull/205

To build the Go test runner clone this repo:

```console
git clone https://github.com/norio-nomura/vz norio-nomura/vz
cd norio-nomura/vz
git checkout feat-add-vmnet-network-device-attachment
make
```

### Build and install

```console
make
make install
```

### Uninstall

```console
make uninstall
```

### Running the tests

To run Go and Swift tests run:

```console
make test
```

For more control run `go test` from the `go/` directory and `swift test`
from the `swift/` directory.

### Running a test VM

To create test VMs run:

```console
./create-vm vm1
./create-vm vm2
```

To run the VMs use the Go or Swift test runners:

```console
./test-go vm1
./test-swift vm2
```

> [!NOTE]
> Login with user: ubuntu password: pass

## Using the broker in your application

To access the broker you can use the C, Swift or Go client libraries:

### C

```c
vmnet_broker_return_t broker_status;
xpc_object_t serialization = vmnet_broker_acquire_network("default", &broker_status);
if (serialization == NULL) {
    ERRORF("failed to start broker session: (%d) %s", broker_status, vmnet_broker_strerror(broker_status));
    exit(EXIT_FAILURE);
}
```

- [client](vmnet-broker.h)
- [example](test.c)

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

## Future work

The following are not implemented yet.

### Configuration

The broker provides the default `shared` and `host` networks using
dynamic subnet and default mask (`255.255.255.0`). The vmnet framework
assigns the next available network. This is the most reliable way,
avoiding conflicts with other programs creating networks.

To create additional specific network, create a configuration file for
each network at `/etc/vmnet-broker.d/*.json`.

#### Using dynamic subnet with static mask

```console
% cat /etc/vmnet-broker.d/shared-tiny.json
{
  "description": "My tiny testing network",
  "mode": "shared",
  "mask": "255.255.255.28"
}
```

This example creates a network using dynamic subnet assigned by vmnet,
and mask of `255.255.255.28`, providing 16 addresses. The first address
is used by the host, and last address is the broadcast address so you
can use only 14 addresses.

#### Specific subnet

```console
% cat /etc/vmnet-broker.d/share-static.json
{
  "description": "My meaningful network",
  "mode": "shared",
  "subnet": "192.168.42.1"
}
```

This example creates the network "192.168.42.1/24". This is useful when
you want to know the IP addresses, but may fail to create if vmnet
allocated the network to another program not using vmnet-broker.

> [!TIP]
> To avoid conflicts, all programs should use vmnet-broker.

---
See https://github.com/nirs/vmnet-broker/issues/2 for more info.

### Using with vfkit

To start a virtual machine using vfkit use the `vmnet` virtio-net device:

```
vfkit --device virtio-net,vment,network=default ...
```

The virtual machine will use the "default" network defined in the broker
configuration.  If you start multiple instances they will use the same network
and can communicate.

