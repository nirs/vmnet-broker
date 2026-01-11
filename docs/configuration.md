<!--
SPDX-FileCopyrightText: The vmnet-broker authors
SPDX-License-Identifier: Apache-2.0
-->

# Configuration

The broker provides the default `shared` and `host` networks using
dynamic subnet and default mask (`255.255.255.0`). The vmnet framework
assigns the next available network. This is the most reliable way,
avoiding conflicts with other programs creating networks.

To create an additional specific network, create a configuration file for
each network at `/etc/vmnet-broker.d/*.json`.

## Using static subnet

> [!NOTE]
> This feature is not implemented yet.

```console
% cat /etc/vmnet-broker.d/my-testing-network.json
{
  "description": "My testing network",
  "mode": "shared",
  "subnet": "192.168.42.1"
  "mask": "255.255.255.0"
}
```

This example creates the network "192.168.42.1/24". This is useful when you want
to know the IP addresses, but may fail to create if vmnet allocated the network
to another program not using vmnet-broker.

> [!TIP]
> To avoid conflicts, all programs should use vmnet-broker.

---
See https://github.com/nirs/vmnet-broker/issues/2 for more info.
