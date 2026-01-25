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

## Privilege separation

The broker runs as the unprivileged `_vmnetbroker` system user rather than
root. This limits the potential impact of security vulnerabilities in the
broker code.
