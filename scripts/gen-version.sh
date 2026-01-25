#!/bin/bash
# SPDX-FileCopyrightText: The vmnet-broker authors
# SPDX-License-Identifier: Apache-2.0

# Generate version.h only if content changed (avoids unnecessary rebuilds)

set -eu

output="include/version.h"

git_version=$(git describe --tags --always --dirty 2>/dev/null || echo "unknown")
git_commit=$(git rev-parse HEAD 2>/dev/null || echo "unknown")

content="#ifndef VERSION_H
#define VERSION_H

#define GIT_VERSION \"$git_version\"
#define GIT_COMMIT  \"$git_commit\"

#endif
"

# Only update if content changed (use hash to avoid trailing newline issues)
new_hash=$(printf '%s' "$content" | shasum -a 256 | cut -d' ' -f1)
if [ -f "$output" ]; then
    old_hash=$(shasum -a 256 < "$output" | cut -d' ' -f1)
    if [ "$new_hash" = "$old_hash" ]; then
        exit 0
    fi
fi

echo "Generating $output"
printf '%s' "$content" > "$output"
