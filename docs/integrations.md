<!--
SPDX-FileCopyrightText: The vmnet-broker authors
SPDX-License-Identifier: Apache-2.0
-->

# Integrations

> [!NOTE]
> These integrations are not implemented yet.

## Using with vfkit

To start a virtual machine using vfkit use the `vmnet` virtio-net device:

```
vfkit --device virtio-net,vmnet,network=shared ...
```

The virtual machine will use the "shared" builtin network. If you start multiple
instances they will use the same network and can communicate.

## Using with Minikube

To start a cluster with the vfkit driver using the vmnet "shared" network:

```
minikube start --driver vfkit --network vmnet-shared ...
```

If running on older macOS version or vmnet-broker is not installed, minikube
will fallback to using vmnet-helper.
