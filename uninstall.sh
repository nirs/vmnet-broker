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

trap 'fatal "Uninstall failed"' ERR

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

if [[ -d "$install_dir" ]]; then
    debug "Deleting $install_dir"
    run rm -rf "$install_dir"
    info "Deleted $install_dir"
fi

if [[ -d "$log_dir" ]]; then
    debug "Deleting $log_dir"
    run rm -rf "$log_dir"
    info "Deleted $log_dir"
fi

if group_exists; then
    debug "Deleting system group $group_name"
    run dscl . -delete /Groups/$group_name
    info "Deleted system group $group_name"
fi

if user_exists; then
    debug "Deleting system user $user_name"
    run dscl . -delete /Users/$user_name
    info "Deleted system user $user_name"
fi

notice "Uninstall completed"
