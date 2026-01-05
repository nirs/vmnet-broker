CC = clang
CFLAGS = -Wall -O2
LDFLAGS = -framework CoreFoundation -framework vmnet

.PHONY: all test install uninstall clean test-swift test-go

all: vmnet-broker test-c test-swift test-go

test: test-swift test-go
	cd go && go test -v ./vmnet_broker
	cd swift && swift test

vmnet-broker: broker.c common.c common.h vmnet-broker.h log.h
	$(CC) $(CFLAGS) $(LDFLAGS) broker.c common.c -o $@
	codesign -f -v --entitlements entitlements.plist -s - $@

test-c: test.c client.c common.c client.c common.h vmnet-broker.h log.h
	$(CC) $(CFLAGS) $(LDFLAGS) test.c client.c common.c -o $@
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
