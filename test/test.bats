#!/usr/bin/env bats
# SPDX-FileCopyrightText: The vmnet-broker authors
# SPDX-License-Identifier: Apache-2.0

# Require 1.5.0 for --separate-stderr flag (so $output contains only stdout)
bats_require_minimum_version 1.5.0

@test "acquire shared network" {
    ./test-c --quick shared
}

@test "acquire host network" {
    ./test-c --quick host
}

@test "non-existing network returns NOT_FOUND" {
    run --separate-stderr ./test-c --quick no-such-network
    [ "$status" -eq 1 ]
    [ "$output" = "fail acquire_network 5" ]
}

@test "acquire both shared and host networks" {
    run --separate-stderr ./test-c --quick shared host
    [ "$status" -eq 0 ]
    [ "$output" = "ok" ]
}

@test "acquire same network multiple times" {
    run --separate-stderr ./test-c --quick shared shared shared
    [ "$status" -eq 0 ]
    [ "$output" = "ok" ]
}

@test "multiple peers sharing same network" {
    # Run 3 clients concurrently, all using shared network
    run --separate-stderr bash -c '
        tmpdir="$1"
        for i in 1 2 3; do
            ./test-c --quick shared > "$tmpdir/peer$i.out" 2>"$tmpdir/peer$i.err" &
        done

        wait

        failed=0
        for i in 1 2 3; do
            if ! grep -q "^ok$" "$tmpdir/peer$i.out"; then
                failed=1
            fi
            echo "peer$i:"
            echo "  stdout: |"
            sed "s/^/    /" "$tmpdir/peer$i.out"
            echo "  stderr: |"
            sed "s/^/    /" "$tmpdir/peer$i.err"
        done
        [ $failed -eq 0 ]
    ' _ "$BATS_TEST_TMPDIR"

    [ "$status" -eq 0 ]
}
