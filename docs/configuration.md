<!--
SPDX-FileCopyrightText: The vmnet-broker authors
SPDX-License-Identifier: Apache-2.0
-->

# Configuration

The broker provides the default `shared` and `host` networks using
dynamic subnet and default mask (`255.255.255.0`). The vmnet framework
assigns the next available network. This is the most reliable way,
avoiding conflicts with other programs creating networks.

To create an additional specific network, create a configuration file
for each network at `/etc/vmnet-broker.d/*.plist`. The file name
(without the `.plist` extension) is used as the network name. Both XML
and binary plist formats are supported.

The configuration directory and files are owned by root:wheel. The
broker runs as the unprivileged `_vmnetbroker` user and only needs
read access to the configuration files. This ensures that
compromising the broker process does not allow modifying the system
network configuration.

```console
% sudo cp my-network.plist /etc/vmnet-broker.d/
```

## Subnet allocation

### Dynamic allocation

When `subnet` and `mask` are not specified, the vmnet framework
allocates the next available subnet (a /24 under 192.168/16). This is
the default for the builtin `shared` and `host` networks.

Benefits:

- Always succeeds -- no conflicts with other programs creating
  networks.
- No need to coordinate subnet assignments across programs.

Drawbacks:

- The VM's IP address may change between restarts if the network was
  removed and another program allocated the same subnet in the
  meantime.
- Harder to set up firewall rules or other configuration that depends
  on knowing the subnet in advance.

### Static allocation

When `subnet` and `mask` are specified, the vmnet framework uses the
requested subnet. This is useful when you need a predictable IP range.

Benefits:

- The VM always gets an address from the same subnet.
- Easier to configure firewall rules, DNS, and other services that
  depend on known IP addresses.
- Required for `port_forwarding_rules` and `dhcp_reservations` since
  these reference specific IP addresses.

Drawbacks:

- May fail to create the network if another program already allocated
  the same subnet.

## Configuration options

Each configuration file is a property list dictionary. The options
correspond to the `vmnet_network_configuration_ref` API in the vmnet
framework.

### `mode`

The operating mode for the vmnet network.

- Type: string
- Required: yes
- Values: `"shared"`, `"host"`
- vmnet: `vmnet_network_configuration_create()`

In **shared** mode, the VM can reach the Internet through a network
address translator (NAT), as well as communicate with other VMs and the
host. By default, the vmnet interface is able to communicate with other
shared mode interfaces. If a subnet range is specified, the vmnet
interface can communicate with other shared mode interfaces on the same
subnet.

In **host** mode, the VM can communicate with other VMs and the host,
but is unable to communicate with the outside network.

### `description`

A human-readable description of the network. This is not passed to the
vmnet framework.

- Type: string
- Required: no
- Default: none

### `subnet`

The IPv4 subnet address for the network. The first, second, and last
addresses of the range are reserved. The second address is reserved for
the host; the first and last are not assignable to any node.

Must be specified together with `mask`.

- Type: string (IPv4 address)
- Required: no
- Default: a /24 under 192.168/16, allocated by the vmnet framework
- vmnet: `vmnet_network_configuration_set_ipv4_subnet()`

Example: `"192.168.42.0"`

### `mask`

The IPv4 subnet mask for the network.

Must be specified together with `subnet`.

- Type: string (IPv4 subnet mask)
- Required: no
- Default: `"255.255.255.0"`
- vmnet: `vmnet_network_configuration_set_ipv4_subnet()`

Example: `"255.255.255.0"`

### `ipv6_prefix`

The IPv6 prefix for the network. The prefix must be a ULA, i.e. start
with `fd00::/8`.

- Type: string (IPv6 address)
- Required: no
- Default: random ULA prefix, allocated by the vmnet framework
- vmnet: `vmnet_network_configuration_set_ipv6_prefix()`

Example: `"fd12:3456:789a::"`

### `ipv6_prefix_length`

The IPv6 prefix length.

Must be specified together with `ipv6_prefix`.

- Type: integer
- Required: no
- Default: 64
- vmnet: `vmnet_network_configuration_set_ipv6_prefix()`

### `external_interface`

The external network interface to use for outbound traffic. Only
applicable to networks in shared mode.

- Type: string (interface name)
- Required: no
- Default: the default interface per the system routing table
- vmnet: `vmnet_network_configuration_set_external_interface()`

Example: `"en0"`

### `nat44`

Enable or disable IPv4 Network Address Translation (NAT44). When
enabled, VMs on the network can reach external IPv4 destinations through
the host.

- Type: boolean
- Required: no
- Default: `true`
- vmnet: `vmnet_network_configuration_disable_nat44()`

### `nat66`

Enable or disable IPv6 Network Address Translation (NAT66). When
enabled, VMs on the network can reach external IPv6 destinations through
the host.

- Type: boolean
- Required: no
- Default: `true`
- vmnet: `vmnet_network_configuration_disable_nat66()`

### `dhcp`

Enable or disable the DHCP server on the network. When enabled, VMs on
the network can obtain IPv4 addresses automatically.

- Type: boolean
- Required: no
- Default: `true`
- vmnet: `vmnet_network_configuration_disable_dhcp()`

### `dns_proxy`

Enable or disable the DNS proxy on the network. When enabled, the
network provides DNS resolution to VMs.

- Type: boolean
- Required: no
- Default: `true`
- vmnet: `vmnet_network_configuration_disable_dns_proxy()`

### `router_advertisement`

Enable or disable IPv6 router advertisement on the network. When
enabled, VMs on the network can use SLAAC for IPv6 address
configuration.

- Type: boolean
- Required: no
- Default: `true`
- vmnet: `vmnet_network_configuration_disable_router_advertisement()`

### `mtu`

The maximum transmission unit (MTU) for the network.

- Type: integer
- Required: no
- Default: `1500`
- vmnet: `vmnet_network_configuration_set_mtu()`

### `port_forwarding_rules`

A list of port forwarding rules for exposing VM services to the host's
network. Only applicable to networks in shared mode.

In shared mode, the host acts as a NAT router between the vmnet network
(e.g. 192.168.42.0/24) and the host's own network. VMs behind the NAT
are not directly reachable from the host's network. Port forwarding
makes a VM service accessible via the host's network address.

For example, if the host is at 10.0.0.5 and you forward external port
8080 to 192.168.42.2:80, machines on the host's network can reach the
VM's web server at 10.0.0.5:8080.

<!--
TODO: verify port forwarding behavior with actual testing. Our
assumption is that this works like NAT port forwarding, exposing a
VM service on the host's network address. This API was likely added
for Apple containers, to emulate how containers work on Linux (e.g.
docker run -p 8080:80).

TODO: verify which interfaces forwarded ports are exposed on. It is
likely only the external_interface, but the vmnet.h documentation
does not specify this.

TODO: since rules reference a specific IP address and are set up
before the network starts, port forwarding likely requires
dhcp_reservations or static addressing. Document this after testing.
-->

- Type: array of objects
- Required: no
- Default: none
- vmnet: `vmnet_network_configuration_add_port_forwarding_rule()`

Each rule object has the following properties (all required):

| Property | Type | Description |
|---|---|---|
| `protocol` | string | `"tcp"` or `"udp"` |
| `external_port` | integer | The port on the host's network address to redirect from |
| `internal_address` | string | IPv4 or IPv6 address of the VM on the vmnet network |
| `internal_port` | integer | The port on the VM to redirect the forwarded traffic to |

The address family (IPv4 or IPv6) is inferred from the
`internal_address` format.

Example:

```xml
<key>port_forwarding_rules</key>
<array>
  <dict>
    <key>protocol</key>
    <string>tcp</string>
    <key>external_port</key>
    <integer>8080</integer>
    <key>internal_address</key>
    <string>192.168.42.2</string>
    <key>internal_port</key>
    <integer>80</integer>
  </dict>
</array>
```

### `dhcp_reservations`

A list of DHCP address reservations. Each reservation assigns a fixed
DHCP address to a client with a specific MAC address.

DHCP reservations cannot be modified while a network is active.

- Type: array of objects
- Required: no
- Default: none
- vmnet: `vmnet_network_configuration_add_dhcp_reservation()`

Each reservation object has the following properties (all required):

| Property | Type | Description |
|---|---|---|
| `mac_address` | string | The MAC address of the client |
| `ip_address` | string | The IPv4 address to reserve |

Example:

```xml
<key>dhcp_reservations</key>
<array>
  <dict>
    <key>mac_address</key>
    <string>aa:bb:cc:dd:ee:ff</string>
    <key>ip_address</key>
    <string>192.168.42.10</string>
  </dict>
</array>
```

## Network lifetime

A configured network is created on demand when the first VM requests
it, and removed after a timeout when no VMs are using it. The
configuration file is read from disk each time the network is created.

Creating a network reserves resources in the vmnet framework (subnet,
DHCP server, NAT, etc.). The network stays active as long as at least
one VM is connected. When the last VM disconnects, the broker waits
before removing the network, allowing VMs to reconnect without
creating a new network.

With dynamic subnets, the VM may get a different subnet after the
network is removed if another program allocated the same subnet in
the meantime. With static subnets, the network always uses the
configured subnet, but may fail to create if the subnet is already in
use by another program.

## Modifying a network

An active network cannot be modified. To change a network's
configuration:

1. Edit the network configuration file.
2. Stop all VMs using the network. When the last VM disconnects, the
   broker removes the network automatically.
3. Start the VMs again. The broker loads the updated configuration from
   disk when re-cereating the network.

## Example

A shared network with a static subnet at 192.168.42.0/24, with DHCP
and DNS proxy disabled. VMs on this network must use static IP
addresses.

`/etc/vmnet-broker.d/my-network.plist`:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN"
  "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>description</key>
  <string>My testing network</string>
  <key>mode</key>
  <string>shared</string>
  <key>subnet</key>
  <string>192.168.42.0</string>
  <key>mask</key>
  <string>255.255.255.0</string>
  <!-- VMs use static IP addresses. -->
  <key>dhcp</key>
  <false/>
  <key>dns_proxy</key>
  <false/>
</dict>
</plist>
```
