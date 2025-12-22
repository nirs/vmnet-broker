CC = clang
CFLAGS = -Wall -O2
LDFLAGS = -framework CoreFoundation -framework vmnet

install_dir := /Library/Application Support/vmnet-broker
launchd_dir := /Library/LaunchDaemons
log_dir := /Library/Logs/vmnet-broker
service_name := com.github.nirs.vmnet-broker
user := _vmnetbroker

all: vmnet-broker vmnet-client

vmnet-broker: broker.c
	$(CC) $(CFLAGS) $(LDFLAGS) $< -o $@
	codesign -s - --force $@

vmnet-client: client.c
	$(CC) $(CFLAGS) $(LDFLAGS) $< -o $@
	codesign -s - --force $@

install:
	sudo mkdir -p "$(install_dir)"
	sudo install vmnet-broker "$(install_dir)"
	sudo mkdir -p "$(log_dir)"
	sudo chown root:wheel "$(install_dir)"
	sudo chown root:wheel "$(install_dir)/vmnet-broker"
	sudo chown $(user):$(user) "$(log_dir)"
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
	rm -f vmnet-broker vmnet-client
