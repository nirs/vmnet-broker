#!/usr/bin/env bats
# SPDX-FileCopyrightText: The vmnet-broker authors
# SPDX-License-Identifier: Apache-2.0

# Require 1.5.0 for --separate-stderr flag (so $output contains only stdout)
bats_require_minimum_version 1.5.0

# Check peer results and print YAML-formatted output.
# Usage: check_peers <tmpdir> <peer_count>
check_peers() {
    local tmpdir=$1
    local count=$2
    local failed=0

    for i in $(seq 1 "$count"); do
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
}

@test "acquire shared network" {
    run --separate-stderr  ./test-c --quick shared
    [ "$status" -eq 0 ]
    [ "$output" = "ok" ]
}

@test "acquire host network" {
    run --separate-stderr  ./test-c --quick host
    [ "$status" -eq 0 ]
    [ "$output" = "ok" ]
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

@test "acquire same network 9 times (exceeds MAX_PEER_NETWORKS)" {
    run --separate-stderr ./test-c --quick shared shared shared shared shared shared shared shared shared
    [ "$status" -eq 0 ]
    [ "$output" = "ok" ]
}

@test "serial peers reuse network (exercise timer cancellation)" {
    # When peer disconnects, the broker starts idle timers. The next peer cancels
    # the timers when it acquires the network.
    for i in 1 2 3; do
        ./test-c --quick shared > "$BATS_TEST_TMPDIR/peer$i.out" 2>"$BATS_TEST_TMPDIR/peer$i.err"
    done
    run --separate-stderr check_peers "$BATS_TEST_TMPDIR" 3
    [ "$status" -eq 0 ]
}

@test "multiple peers with different networks" {
    ./test-c --quick shared > "$BATS_TEST_TMPDIR/peer1.out" 2>"$BATS_TEST_TMPDIR/peer1.err" &
    ./test-c --quick host > "$BATS_TEST_TMPDIR/peer2.out" 2>"$BATS_TEST_TMPDIR/peer2.err" &
    wait
    run --separate-stderr check_peers "$BATS_TEST_TMPDIR" 2
    [ "$status" -eq 0 ]
}

@test "multiple peers sharing same network" {
    for i in 1 2 3; do
        ./test-c --quick shared > "$BATS_TEST_TMPDIR/peer$i.out" 2>"$BATS_TEST_TMPDIR/peer$i.err" &
    done
    wait
    run --separate-stderr check_peers "$BATS_TEST_TMPDIR" 3
    [ "$status" -eq 0 ]
}
