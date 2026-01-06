// SPDX-FileCopyrightText: The vmnet-broker authors
// SPDX-License-Identifier: Apache-2.0

module github.com/nirs/vmnet-broker/go

go 1.24.0

require (
	github.com/Code-Hex/vz/v3 v3.7.1
	github.com/pkg/term v1.1.0
	golang.org/x/sys v0.39.0
)

require (
	github.com/Code-Hex/go-infinity-channel v1.0.0 // indirect
	golang.org/x/mod v0.22.0 // indirect
)

// https://github.com/Code-Hex/vz/pull/205
replace github.com/Code-Hex/vz/v3 => ../../norio-nomura/vz
