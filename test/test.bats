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
    ./test-c --quick shared > /tmp/peer1.out 2>/dev/null &
    pid1=$!
    ./test-c --quick shared > /tmp/peer2.out 2>/dev/null &
    pid2=$!
    ./test-c --quick shared > /tmp/peer3.out 2>/dev/null &
    pid3=$!

    # Wait for all and collect exit codes
    wait $pid1; status1=$?
    wait $pid2; status2=$?
    wait $pid3; status3=$?

    # All must succeed
    [ "$status1" -eq 0 ]
    [ "$status2" -eq 0 ]
    [ "$status3" -eq 0 ]

    # All must output "ok"
    [ "$(cat /tmp/peer1.out)" = "ok" ]
    [ "$(cat /tmp/peer2.out)" = "ok" ]
    [ "$(cat /tmp/peer3.out)" = "ok" ]
}
