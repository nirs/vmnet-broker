CC = clang
SWIFTC = swiftc
CFLAGS = -Wall -O2
LDFLAGS = -framework CoreFoundation -framework vmnet

install_dir := /Library/Application Support/vmnet-broker
launchd_dir := /Library/LaunchDaemons
log_dir := /Library/Logs/vmnet-broker
service_name := com.github.nirs.vmnet-broker

.PHONY: all clean compile_commands.json

all: vmnet-broker test-c test-swift

vmnet-broker: broker.c common.c common.h vmnet-broker.h
	$(CC) $(CFLAGS) $(LDFLAGS) broker.c common.c -o $@
	codesign -f -v --entitlements entitlements.plist -s - $@

test-c: test.c common.c common.h libvmnetbroker.a
	$(CC) $(CFLAGS) $(LDFLAGS) -L. -lvmnetbroker test.c common.c -o $@
	codesign -f -v --entitlements entitlements.plist -s - $@

test-swift: test.swift
	$(SWIFTC) $^ -target arm64-apple-macosx26.0 -I. -L. -lvmnetbroker -framework vmnet -o $@
	codesign -f -v --entitlements entitlements.plist -s - $@

libvmnetbroker.a: client.c vmnet-broker.h
	$(CC) $(CFLAGS) -c client.c
	$(AR) rcs $@ client.o

compile_commands.json:
	$(MAKE) clean
	# brew install bear
	bear -- $(MAKE) all

install:
	sudo mkdir -p "$(install_dir)"
	sudo install vmnet-broker "$(install_dir)"
	sudo mkdir -p "$(log_dir)"
	sudo chown root:wheel "$(install_dir)"
	sudo chown root:wheel "$(install_dir)/vmnet-broker"
	sudo chown root:wheel "$(log_dir)"
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
	rm -f vmnet-broker test-c test-swift compile_commands.json *.o *.a
