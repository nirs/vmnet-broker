# TODO

## Testing multiple network scenarios

- [x] Acquire "shared" network
- [x] Acquire "host" network
- [x] Acquire non-existing network
- [ ] Extend test.c to receive multiple networks
- [ ] Split acquire and clean up operations so we can acquire multiple networks at the same time.
  - acquire network
  - start interface (validated the acquired network)
  - stop interface (clean up before exit)
- [ ] Acquire both "shared" and "host"
- [ ] Acquire same network multiple times
- [ ] Multiple peers sharing same network

## Testing network configuration files

- [ ] Acquire MAX_PEER_NETWORKS networks
- [ ] Fail to acquire (MAX_PEER_NETWORKS + 1) networks

## Network idle shutdown

- [ ] Add idle timeout for individual networks
- [ ] Delete network after timeout when no peers are using it
- [ ] Cancel timeout when peer acquires the network

## Run-at-load semantics

- [ ] Add run-at-load mode where daemon never shuts down
- [ ] In this mode, networks are never deleted (persist across peer disconnects)
- [ ] Useful for pre-warming networks or persistent network configurations

## Debugging

- [ ] Add command to dump daemon state (networks, peers, counters)
- [ ] Useful for troubleshooting without restarting the daemon
