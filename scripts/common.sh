# Common configuration and functions for install/uninstall scripts
# This file is embedded during build - do not use directly

service_name="com.github.nirs.vmnet-broker"
user_name="_vmnetbroker"
group_name="_vmnetbroker"
install_dir="/Library/Application Support/vmnet-broker"
launchd_dir="/Library/LaunchDaemons"
log_dir="/Library/Logs/vmnet-broker"
stop_timeout=5

log() {
    local level="$1"
    local msg="$2"
    logger -p "user.$level" "vmnet-broker: $msg"
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

# Generate a unique ID in the 200-400 range for a system user.
find_unique_id() {
    debug "Looking up available unique id"
    local last_id
    last_id=$(run bash -c "dscl . -list /Users UniqueID | awk '\$2 > 200 && \$2 < 400 {print \$2}' | sort -n | tail -1")
    echo $((last_id + 1))
}

# Stop the broker if it is idle. Fails if the broker has active connections.
# Uses kill directly instead of launchctl stop to avoid triggering launchd's
# ExitTimeOut which would SIGKILL the broker after 60 seconds.
try_stop_service() {
    local pid
    pid=$(service_pid)

    if [[ "$pid" == "-" ]]; then
        debug "Service is not running"
        return 0
    fi

    debug "Stopping service (pid: $pid)"
    run kill -TERM "$pid"

    # Wait for graceful shutdown
    local i
    for i in $(seq $stop_timeout); do
        debug "Waiting for service to stop (attempt $i/$stop_timeout)"
        run sleep 1
        if ! run kill -0 "$pid" 2>/dev/null; then
            info "Stopped service $service_name (pid: $pid)"
            return 0
        fi
    done

    debug "Timeout stopping service"
    return 1
}
