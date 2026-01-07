# SPDX-FileCopyrightText: The vmnet-broker authors
# SPDX-License-Identifier: Apache-2.0

CC = clang
CFLAGS = -Wall -Wextra -O2 -Iinclude
LDFLAGS = -framework CoreFoundation -framework vmnet

headers = $(wildcard include/*.h)
broker_sources = $(wildcard broker/*.c) lib/common.c
test_sources = test/test.c client/client.c lib/common.c

.PHONY: all test install uninstall clean test-swift test-go

all: vmnet-broker test-c test-swift test-go

test: test-swift test-go
	cd go && go test -v ./vmnet_broker
	cd swift && swift test

vmnet-broker: $(broker_sources) $(headers)
	$(CC) $(CFLAGS) $(LDFLAGS) $(broker_sources) -o $@
	codesign -f -v --entitlements entitlements.plist -s - $@

test-c: $(test_sources) $(headers)
	$(CC) $(CFLAGS) $(LDFLAGS) $(test_sources) -o $@
	codesign -f -v --entitlements entitlements.plist -s - $@

test-swift:
	cd swift && swift build
	ln -fs $(shell cd swift && swift build --show-bin-path)/test $@
	codesign -f -v --entitlements entitlements.plist -s - $@

test-go:
	cd go && go build -o ../$@ cmd/test.go
	codesign -f -v --entitlements entitlements.plist -s - $@

install:
	sudo ./install.sh

uninstall:
	sudo ./uninstall.sh

clean:
	rm -f vmnet-broker test-c test-swift test-go
	cd swift && swift package clean
	cd go && go clean
