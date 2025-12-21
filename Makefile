CC = clang
CFLAGS = -Wall -O2
LDFLAGS = -framework CoreFoundation -framework vmnet

service_name := com.github.nirs.vmnet-broker
user_id := $(shell id -u)

all: vmnet-broker vmnet-client

vmnet-broker: broker.c
	$(CC) $(CFLAGS) $(LDFLAGS) $< -o $@

vmnet-client: client.c
	$(CC) $(CFLAGS) $(LDFLAGS) $< -o $@

install:
	mkdir -p /opt/vmnet-broker/bin
	install vmnet-broker /opt/vmnet-broker/bin/

bootstrap:
	mkdir -p ~/Library/LaunchAgents/
	cp $(service_name).plist ~/Library/LaunchAgents/
	launchctl bootstrap gui/$(user_id) ~/Library/LaunchAgents/$(service_name).plist

bootout:
	launchctl bootout gui/$(user_id) ~/Library/LaunchAgents/$(service_name).plist

print:
	launchctl print gui/$(user_id)/$(service_name)

clean:
	rm -f vmnet-broker vmnet-client
