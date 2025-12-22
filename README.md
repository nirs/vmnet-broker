# vmnet-broker

The vmnet-broker manages shared vmnet networks for Apple Virtualization
framework. With the broker you can run multiple virtual machines using the same
network without helpers like vmnet-helpers that require root when starting a VM.

## Compatibility

macOS Tahoe 26 or later is required.

## Advantages over vmnet-helper

These are initial assumptions that we need to validate:

- **Better performance**: the broker is only in the control path. The data path
  is implemented by vment. vmnet-helper implement the data path in user space.
- **No root required**: the client does not need to run as root or the special
  "com.apple.networking" entitlement. vmnet-helper must run as root to start
  the vmnet interface.
- **More reliable**: Using public APIs make the broker more reliable in the long
  term. vment-helper depends on private APIs to get good performance.
- **Easier to maintain**: the broker in not in the data path so the code is much
  simpler. vment-helper implements the data path.

## How it works

The broker is running as a launchd daemon to be able to create networks. It must
installed as root to ensure the program cannot be modified by unprivileged user.

When the first client connects to the broker to request a network, the broker
creates a new vmnet network based on the broker configuration, and return the
network to the client. The client use the network to create a virtual machine
using the new
[VZVmnetNetworkDeviceAttachment](https://developer.apple.com/documentation/virtualization/vzvmnetnetworkdeviceattachment)
introduced in macOS 26, that does not require root or the special
"com.apple.networking" entitlement.

When the next client connects, the existing vmnet network is returned to the
client, so both clients are using the same vmnet network.

When the last client disconnects from the service, the vmnet network is
released, and the kernel resources are destroyed.

## Using with vfkit

To start a virtual machine using vfkit use the `vmnet` virtio-net device:

```
vfkit --device virtio-net,vment,network=default ...
```

The virtual machine will use the "default" network defined in the broker
configuration.  If you start multiple instances they will use the same network
and can communicate.

## Configuration

The broker uses a configuration file (`/etc/vmnet-broker/networks.json`)
defining the networks. You can add more networks as needed.

```
{
  "default": {
    "mode": "shared"
  }
}
```

## Writing a client

See `client.c` to learn how to access the broker using xpc and request a
network.
