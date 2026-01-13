<!--
SPDX-FileCopyrightText: The vmnet-broker authors
SPDX-License-Identifier: Apache-2.0
-->

# Development

## Prerequisites

The following tools are required:

- **Xcode Command Line Tools** - provides `clang`, `codesign`, `swift`, `git`,
  and `make` (`xcode-select --install`)
- **Go 1.24.0+** - for building the Go client and test runner
- **bats 1.5.0+** - Bash testing framework
- **clang-format** - for code formatting
- **shellcheck** - for shell script linting

If you use Homebrew, you can install all the tools with:

```console
xcode-select --install
brew install go bats-core clang-format shellcheck
```

## Dependencies

The Go test runner depends on this upstream PR:
https://github.com/Code-Hex/vz/pull/205

To build the Go test runner clone this repo:

```console
git clone https://github.com/norio-nomura/vz norio-nomura/vz
cd norio-nomura/vz
git checkout feat-add-vmnet-network-device-attachment
make
```

## Build and install

```console
make
make install
```

## Uninstall

```console
make uninstall
```

## Running the tests

To run Go and Swift tests run:

```console
make test
```

For more control run `go test` from the `go/` directory and `swift test`
from the `swift/` directory.

## Running a test VM

To create test VMs run:

```console
./create-vm vm1
./create-vm vm2
```

To run the VMs use the Go or Swift test runners:

```console
./test-go vm1
./test-swift vm2
```

> [!NOTE]
> Login with user: ubuntu password: pass
