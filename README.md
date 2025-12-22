# vmnet-broker

The vmnet-broker manages shared vmnet networks for Apple Virtualization
framework. With the broker you can run multiple virtual machines using the same
network without helpers like vmnet-helpers that require sudo when starting a VM.

## Compatibility

macOS Tahoe 26 or later is required.

## Advantages over vmnet-helper

These are initial assumptions that we need to validate:
- Likely to be more reliable in the long term, since this is the same method
  vmnet is used by [Apple container](https://github.com/apple/container).
- Likely better performance: no need to copy packets between the vm and vmnet -
  this may be done by vmnet, or maybe eliminated using kernel configuration.
- No root required: no need to run *vmnet-helper* as root since creating
  `vmnet_network_ref` does not require special entitlement
- Much simpler and smaller

## How it works

When the first client connects to the service and request a network, the broker
creates a new vmnet network based on the broker configuration, and return the
network to the client. The client use the network to create a virtual machine
using the new
[VZVmnetNetworkDeviceAttachment](https://developer.apple.com/documentation/virtualization/vzvmnetnetworkdeviceattachment).
introduced in macOS 26.

When the next client connects, the existing vmnet network is returned to the
client, so both clients are using the same vmnet network.

When the last client disconnects from the service, the vmnet network is
released, and the kernel resources are destroyed.

## Using with vfkit

To start a virtual machine using vfkit use the `vmnet` virtio-net device:

```
vfkit --device virtio-net,vment,network=default ...
```

The virtual machine will use the "shared" defined in the broker configuration.
If you start multiple instances they will use the same network and can
communicate.

## Configuration

The broker uses a configuration file (`/Library/Application Support/com.github.nirs.vmnet-broker/networks.json`)
defining the networks. You can add more networks as needed.

```
{
  "default": {
    "mode": "shared"
  }
}
```

## Writing a client

How a client can request a network from the broker.

```
XXX example C client code.
```

## Creating a user and group for vmnet-broker

The following should be part of the install script or package:

```bash
set -e

name="_vmnetbroker"

last_id=$(dscl . -list /Users UniqueID | awk '$2 > 200 && $2 < 400 {print $2}' | sort -n | tail -1)
id=$((last_id+1))

dscl . -create /Groups/$name
dscl . -create /Groups/$name PrimaryGroupID $id

dscl . -create /Users/$name
dscl . -create /Users/$name UniqueID $id
dscl . -create /Users/$name PrimaryGroupID $id
dscl . -create /Users/$name UserShell /usr/bin/false
dscl . -create /Users/$name RealName "com.github.nirs.vmnet-broker"
dscl . -create /Users/$name NFSHomeDirectory /var/empty
```
