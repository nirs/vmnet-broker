CC = clang
CFLAGS = -Wall -O2
LDFLAGS = -framework CoreFoundation -framework vmnet

user_name := _vmnetbroker
install_dir := /Library/Application Support/vmnet-broker
launchd_dir := /Library/LaunchDaemons
log_dir := /Library/Logs/vmnet-broker
service_name := com.github.nirs.vmnet-broker

.PHONY: all test clean test-swift test-go

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
	sudo mkdir -p "$(install_dir)"
	sudo install vmnet-broker "$(install_dir)"
	sudo mkdir -p "$(log_dir)"
	sudo chown root:wheel "$(install_dir)"
	sudo chown root:wheel "$(install_dir)/vmnet-broker"
	sudo chown $(user_name):$(user_name) "$(log_dir)"
	sudo chmod 0755 "$(log_dir)"

bootstrap:
	sudo cp $(service_name).plist "$(launchd_dir)"
	sudo chown root:wheel "$(launchd_dir)/$(service_name).plist"
	sudo chmod 0644 "$(launchd_dir)/$(service_name).plist"
	sudo launchctl bootstrap system "$(launchd_dir)/$(service_name).plist"

bootout:
	sudo launchctl bootout system "$(launchd_dir)/$(service_name).plist"
	sudo rm -f "$(launchd_dir)/$(service_name).plist"

print:
	sudo launchctl print system/$(service_name)

clean:
	rm -f vmnet-broker test-c test-swift test-go
	cd swift && swift package clean
	cd go && go clean
