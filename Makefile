CC = clang
CFLAGS = -Wall -O2
LDFLAGS = -framework CoreFoundation -framework vmnet

install_dir := /opt/vmnet-broker/bin
launchd_dir := /Library/LaunchDaemons
service_name := com.github.nirs.vmnet-broker
user_id := $(shell id -u)

all: vmnet-broker vmnet-client

vmnet-broker: broker.c
	$(CC) $(CFLAGS) $(LDFLAGS) $< -o $@
	codesign -s - --force $@

vmnet-client: client.c
	$(CC) $(CFLAGS) $(LDFLAGS) $< -o $@
	codesign -s - --force $@

install:
	sudo mkdir -p $(install_dir)
	sudo install vmnet-broker $(install_dir)

bootstrap:
	sudo cp $(service_name).plist $(launchd_dir)
	sudo launchctl bootstrap system $(launchd_dir)/$(service_name).plist

bootout:
	sudo launchctl bootout system $(launchd_dir)/$(service_name).plist
	sudo rm -f $(launchd_dir)/$(service_name).plist

print:
	sudo launchctl print system/$(service_name)

clean:
	rm -f vmnet-broker vmnet-client
