#!/bin/bash
# SPDX-FileCopyrightText: The vmnet-broker authors
# SPDX-License-Identifier: Apache-2.0

# Create reproducible release tarball
#
# The tarball mirrors the final filesystem layout for easy installation.
#
# Usage: ./scripts/dist.sh

set -eu -o pipefail

# Source common.sh for shared variables
# shellcheck source=scripts/common.sh
source "$(dirname "$0")/common.sh"

version="${1:-$(git describe --tags --always --dirty 2>/dev/null || echo "dev")}"
echo "Version: $version"

root_dir="build/root"

rm -rf "$root_dir"
mkdir -p "$root_dir"

# Create filesystem layout (directories only, no log_dir - created by postinstall)
mkdir -p "$root_dir$install_dir"
mkdir -p "$root_dir$launchd_dir"

cp vmnet-broker "$root_dir$install_dir/"
cp uninstall.sh "$root_dir$install_dir/"
cp LICENSE "$root_dir$install_dir/"
cp com.github.nirs.vmnet-broker.plist "$root_dir$launchd_dir/"

# Set proper permissions
chmod 755 "$root_dir$install_dir/vmnet-broker"
chmod 755 "$root_dir$install_dir/uninstall.sh"
chmod 644 "$root_dir$install_dir/LICENSE"
chmod 644 "$root_dir$launchd_dir/com.github.nirs.vmnet-broker.plist"

# Use git commit time for reproducibility
commit_time=$(git log -1 --pretty=%ct)
commit_time_iso=$(date -u -r "$commit_time" +%Y-%m-%dT%H:%M:%SZ)
find "$root_dir" -exec touch -d "$commit_time_iso" {} \;

echo "Created target root directory"
tree -pD "$root_dir"

echo "Creating build/vmnet-broker.tar.gz"

# Create reproducible archive with files only (sorted, uid/gid=0)
# Including only files ensures tar cannot modify existing system directories.
(cd "$root_dir" && find -s . -type f) | \
    tar --create \
        --verbose \
        --file build/vmnet-broker.tar \
        --uid 0 \
        --gid 0 \
        --no-recursion \
        --directory "$root_dir" \
        --files-from /dev/stdin

# Compress without embedding filename/timestamp
rm -f build/vmnet-broker.tar.gz
gzip -9 --no-name build/vmnet-broker.tar

echo "Created build/vmnet-broker.tar.gz"
