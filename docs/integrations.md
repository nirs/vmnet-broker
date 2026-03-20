<!--
SPDX-FileCopyrightText: The vmnet-broker authors
SPDX-License-Identifier: Apache-2.0
-->

# Integrations

## Using with vmnet-helper

On macOS 26, vmnet-helper can join a vmnet-broker network using the
`--network` option. VMs using vmnet-helper share the same subnet as VMs
using native vmnet, but use file-handle based packet forwarding instead
of native vmnet performance.

This is useful for VMs that don't support native vmnet (e.g. QEMU,
libkrun) but need to communicate with VMs on the broker network.

```
vmnet-helper --network shared --fd 3
```

Or using vmnet-client:

```
vmnet-client --network shared -- vfkit --device virtio-net,fd=4 ...
```

For more info see the [vmnet-helper architecture].

[vmnet-helper architecture]: https://github.com/nirs/vmnet-helper/blob/main/docs/architecture.md#native-vmnet-on-macos-26

## Using with vfkit

> [!NOTE]
> This integration is not implemented yet.

To start a virtual machine using vfkit use the `vmnet` virtio-net device:

```
vfkit --device virtio-net,vmnet,network=shared ...
```

The virtual machine will use the "shared" builtin network. If you start multiple
instances they will use the same network and can communicate.

For more info see https://github.com/crc-org/vfkit/issues/439

## Using with Minikube

> [!NOTE]
> This integration is not implemented yet.

To start a cluster with the vfkit driver using the vmnet "shared" network:

```
minikube start --driver vfkit --network vmnet-shared ...
```

If running on older macOS version or vmnet-broker is not installed, minikube
will fallback to using vmnet-helper.
