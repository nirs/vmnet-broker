#ifndef VMNET_NETWORK_H
#define VMNET_NETWORK_H

#include <arpa/inet.h>
#include <vmnet/vmnet.h>

struct network_info {
    char subnet[INET_ADDRSTRLEN];
    char mask[INET_ADDRSTRLEN];
    char ipv6_prefix[INET6_ADDRSTRLEN];
    uint8_t prefix_len;
};

void network_info(vmnet_network_ref network, struct network_info *info);

#endif
