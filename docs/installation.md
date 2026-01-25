<!--
SPDX-FileCopyrightText: The vmnet-broker authors
SPDX-License-Identifier: Apache-2.0
-->

# Installation details

## What the install script does

The install script requires root to:

- Stop the existing broker (if upgrading)
- Install the broker to `/Library/Application Support/vmnet-broker`
- Create the `_vmnetbroker` system user and group
- Install the launchd service to `/Library/LaunchDaemons`
- Create the log directory at `/Library/Logs/vmnet-broker`

> [!NOTE]
> The install will fail if VMs are currently using the broker, to prevent
> breaking running virtual machines.

### Example output

```console
$ curl -fsSL https://github.com/nirs/vmnet-broker/releases/latest/download/install.sh | sudo bash
▫️  Downloading vmnet-broker latest
▫️  Created system group _vmnetbroker
▫️  Created system user _vmnetbroker
▫️  Installing files
▫️  Bootstrapped service com.github.nirs.vmnet-broker
✅ Install completed
```

## Installing a specific version

To install a specific version, set the `VMNET_BROKER_VERSION` environment
variable:

```console
curl -fsSL https://github.com/nirs/vmnet-broker/releases/download/v0.3.0/install.sh \
  | sudo VMNET_BROKER_VERSION=v0.3.0 bash
```

## What the uninstall script does

The uninstall script:

- Tries to stop the broker gracefully (fails if VMs are connected)
- Removes the launchd service
- Deletes the broker files from `/Library/Application Support/vmnet-broker`
- Deletes the log directory `/Library/Logs/vmnet-broker`
- Removes the `_vmnetbroker` system user and group

> [!NOTE]
> The uninstall will fail if VMs are currently using the broker, to prevent
> breaking running virtual machines.

### Example output

```console
$ sudo /Library/Application\ Support/vmnet-broker/uninstall.sh
▫️  Booted out service com.github.nirs.vmnet-broker
▫️  Deleted /Library/Application Support/vmnet-broker
▫️  Deleted /Library/Logs/vmnet-broker
▫️  Deleted system group _vmnetbroker
▫️  Deleted system user _vmnetbroker
✅ Uninstall completed
```

## Installation logs

To view install/uninstall logs:

```console
$ log show --info --predicate 'sender == "logger" AND eventMessage CONTAINS "vmnet-broker"' --last 5m
Filtering the log data using "sender == "logger" AND composedMessage CONTAINS "vmnet-broker""
Skipping debug messages, pass --debug to include.
Timestamp                       Thread     Type        Activity             PID    TTL  
2026-01-25 23:03:22.041859+0200 0x150d23a  Info        0x0                  6103   0    logger: vmnet-broker: Downloading vmnet-broker latest
2026-01-25 23:03:22.726155+0200 0x150d27b  Info        0x0                  6125   0    logger: vmnet-broker: Created system group _vmnetbroker
2026-01-25 23:03:22.861156+0200 0x150d2c3  Info        0x0                  6143   0    logger: vmnet-broker: Created system user _vmnetbroker
2026-01-25 23:03:22.866972+0200 0x150d2c6  Info        0x0                  6144   0    logger: vmnet-broker: Installing files
2026-01-25 23:03:22.937301+0200 0x150d2e9  Info        0x0                  6160   0    logger: vmnet-broker: Bootstrapped service com.github.nirs.vmnet-broker
2026-01-25 23:03:22.939421+0200 0x150d2eb  Default     0x0                  6161   0    logger: vmnet-broker: Install completed
```

## Privilege separation

The broker runs as the unprivileged `_vmnetbroker` system user rather than
root. This limits the potential impact of security vulnerabilities in the
broker code.
