# TODO

## Testing multiple network scenarios

- [x] Acquire "shared" network
- [x] Acquire "host" network
- [x] Acquire non-existing network
- [x] Extend test.c to receive multiple networks
- [x] Split acquire and clean up operations so we can acquire multiple networks at the same time.
  - acquire network
  - start interface (validated the acquired network)
  - stop interface (clean up before exit)
- [x] Acquire both "shared" and "host"
- [x] Acquire same network multiple times
- [x] Multiple peers sharing same network

## Testing network configuration files

- [ ] Acquire MAX_PEER_NETWORKS networks
- [ ] Fail to acquire (MAX_PEER_NETWORKS + 1) networks

## Network idle shutdown

- [x] Add idle timeout for individual networks
- [x] Remove network after timeout when no peers are using it
- [x] Cancel timeout when peer acquires the network

## Run-at-load semantics

- [ ] Add run-at-load mode where daemon never shuts down
- [ ] In this mode, networks are never deleted (persist across peer disconnects)
- [ ] Useful for pre-warming networks or persistent network configurations

## Debugging

- [ ] Add command to dump daemon state (networks, peers, counters)
- [ ] Useful for troubleshooting without restarting the daemon

## README improvements

- [x] Fix typos and errors (XPS→XPC, vment→vmnet, drriver→driver, invalid subnet mask)
- [x] Create docs/ directory structure
- [x] Move configuration section to docs/configuration.md
- [x] Move vfkit/minikube integration to docs/integrations.md
- [x] Move hacking/development details to docs/development.md
- [x] Rewrite opening sections to match the technical tone of the rest
- [x] Streamline README structure with links to docs/

## Install/uninstall testing

- [ ] Add bats tests for install script (files, user, group, service)
- [ ] Add bats tests for uninstall script (cleanup verification)
- [ ] Test try_stop_service fails when peers are connected
- [ ] Note: requires root, suitable for CI only

## Smarter service shutdown

- [x] Handle `launchctl stop` signal for immediate graceful shutdown
- [x] Update uninstall script to stop service before bootout
- [ ] Increase idle timeout (2+ minutes) now that install/uninstall are smarter

## Performance

- [ ] Add performance benchmarks (iperf3 on macOS 26: vmnet-broker vs vmnet-helper vs socket_vmnet)
