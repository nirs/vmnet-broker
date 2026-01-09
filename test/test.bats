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
