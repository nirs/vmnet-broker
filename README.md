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

## Installing

Run `make install`:

```console
% make install
sudo ./install.sh
▫️  Created system group _vmnetbroker
▫️  Created system user _vmnetbroker
▫️  Installed broker in /Library/Application Support/vmnet-broker
▫️  Created log directory /Library/Logs/vmnet-broker
▫️  Installed service /Library/LaunchDaemons/com.github.nirs.vmnet-broker.plist
▫️  Bootstrapped service com.github.nirs.vmnet-broker
✅ Install completed
```

## Uninstalling

Run `make uninstall`:

```console
% make uninstall
sudo ./uninstall.sh
▫️  Booted out service com.github.nirs.vmnet-broker
▫️  Deleted /Library/Application Support/vmnet-broker
▫️  Deleted /Library/Logs/vmnet-broker
▫️  Deleted system group _vmnetbroker
▫️  Deleted system user _vmnetbroker
✅ Uninstall completed
```

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

## Creating a user and group for vmnet-broker

The most secure way to run vmnet-broker is using an unprivileged user. Run the
following commands to create a system user for vment-broker.

```bash
set -e

VMNET_BROKER_NAME="_vmnetbroker"

last_id=$(dscl . -list /Users UniqueID | awk '$2 > 200 && $2 < 400 {print $2}' | sort -n | tail -1)
VMNET_BROKER_ID=$((last_id+1))

dscl . -create /Groups/$VMNET_BROKER_NAME
dscl . -create /Groups/$VMNET_BROKER_NAME PrimaryGroupID $VMNET_BROKER_ID

dscl . -create /Users/$VMNET_BROKER_NAME
dscl . -create /Users/$VMNET_BROKER_NAME UniqueID $VMNET_BROKER_ID
dscl . -create /Users/$VMNET_BROKER_NAME PrimaryGroupID $VMNET_BROKER_ID
dscl . -create /Users/$VMNET_BROKER_NAME UserShell /usr/bin/false
dscl . -create /Users/$VMNET_BROKER_NAME RealName "Vmnet Broker Daemon"
dscl . -create /Users/$VMNET_BROKER_NAME NFSHomeDirectory /var/empty
```

## Using the broker in your application

To access the broker you can use the libvmentbroker static library:

```C
struct vmnet_broker_error error;

xpc_object_t serialization = vmnet_broker_start_session("default", &error);

if (serialization == NULL) {
    fprintf(stderr, "failed to start broker session: (%d) %s\n", error.code, error.message);
    exit(EXIT_FAILURE);
}
```

The library can be baked directly into Go (CGo), Swift (Bridging Header),
Objective-C, or Rust (FFI) projects

See `test.c` for complete example.
