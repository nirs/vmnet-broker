#!/bin/bash

set -eu -o pipefail

service_name="com.github.nirs.vmnet-broker"
user_name="_vmnetbroker"
group_name="_vmnetbroker"
install_dir="/Library/Application Support/vmnet-broker"
launchd_dir="/Library/LaunchDaemons"
log_dir="/Library/Logs/vmnet-broker"

log() {
    local level="$1"
    local msg="$2"
    logger -p user.$level "vmnet-broker: $msg"
}

debug() {
    log debug "$*"
}

info() {
    log info "$*"
    echo "▫️  $*"
}

notice() {
    log notice "$*"
    echo "✅ $*"
}

error() {
    log error "$*"
    echo "❌ Error: $*"
}

run() {
    log debug "Running \"$*\""
    "$@"
}

fatal() {
    # Disable trap and errexit to avoid endless loop if command fail here.
    trap - ERR
    set +e

    log error "$*"
    echo "❌ Error: $*"

    exit 1
}

service_installed() {
    debug "Checking if the service exists"
    run launchctl list "$service_name" >/dev/null 2>&1
}

service_pid() {
    debug "Checking if the service is running"
    run bash -c "launchctl list | awk \"/$service_name/\"' {print \$1}'"
}

# Generate a unique ID in the 200-400 range for a system user.
find_unique_id() {
    debug "Looking up available unique id"
    local last_id=$(run bash -c "dscl . -list /Users UniqueID | awk '\$2 > 200 && \$2 < 400 {print \$2}' | sort -n | tail -1")
    echo $((last_id + 1))
}

group_exists() {
    debug "Checking if group exists"
    run dscl . -read /Groups/$group_name >/dev/null 2>&1
}

user_exists() {
    debug "Checking if user exists"
    run id -u $user_name >/dev/null 2>&1
}

if [[ $EUID -ne 0 ]]; then
    fatal "This script must be run as root"
fi

trap 'fatal "Install failed"' ERR

if service_installed; then
    pid=$(service_pid)
    if [[ "$pid" != "-" ]]; then
        fatal "Service $service_name is currently running (service_pid: $pid)"
    fi

    debug "Booting out service $service_name"
    run launchctl bootout "system/$service_name" || true
    run rm -f "$launchd_dir/$service_name.plist"
    info "Booted out service $service_name"
fi

unique_id=$(find_unique_id)

if ! group_exists; then
    debug "Creating system group $group_name id $unique_id"
    run dscl . -create /Groups/$group_name
    run dscl . -create /Groups/$group_name PrimaryGroupID $unique_id
    info "Created system group $group_name"
fi

if ! user_exists; then
    debug "Creating system user $user_name id $unique_id"
    run dscl . -create /Users/$user_name
    run dscl . -create /Users/$user_name UniqueID $unique_id
    run dscl . -create /Users/$user_name PrimaryGroupID $unique_id
    run dscl . -create /Users/$user_name UserShell /usr/bin/false
    run dscl . -create /Users/$user_name RealName "Vmnet Broker Daemon"
    run dscl . -create /Users/$user_name NFSHomeDirectory /var/empty
    info "Created system user $user_name"
fi

debug "Installing broker in $install_dir"
run mkdir -p "$install_dir"
run install vmnet-broker "$install_dir"
run chown root:wheel "$install_dir"
run chown root:wheel "$install_dir/vmnet-broker"
info "Installed broker in $install_dir"

debug "Creating log directory $log_dir"
run mkdir -p "$log_dir"
run chown $user_name:$user_name "$log_dir"
run chmod 0755 "$log_dir"
info "Created log directory $log_dir"

debug "Installing service $launchd_dir/$service_name.plist"
run cp $service_name.plist "$launchd_dir"
run chown root:wheel "$launchd_dir/$service_name.plist"
run chmod 0644 "$launchd_dir/$service_name.plist"
info "Installed service $launchd_dir/$service_name.plist"

debug "Bootstrapping service $service_name"
run launchctl bootstrap system "$launchd_dir/$service_name.plist"
info "Bootstrapped service $service_name"

notice "Install completed"
