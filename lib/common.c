// SPDX-FileCopyrightText: The vmnet-broker authors
// SPDX-License-Identifier: Apache-2.0

#include <arpa/inet.h>
#include "common.h"

void network_info(vmnet_network_ref network, struct network_info *info) {
    struct in_addr subnet;
    struct in_addr mask;
    vmnet_network_get_ipv4_subnet(network, &subnet, &mask);

    struct in6_addr ipv6_prefix;
    vmnet_network_get_ipv6_prefix(network, &ipv6_prefix, &info->prefix_len);

    inet_ntop(AF_INET, &subnet, info->subnet, INET_ADDRSTRLEN),
    inet_ntop(AF_INET, &mask, info->mask, INET_ADDRSTRLEN);
    inet_ntop(AF_INET6, &ipv6_prefix, info->ipv6_prefix, INET6_ADDRSTRLEN);
}

const char *vmnet_strerror(vmnet_return_t status)
{
    switch (status) {
    case VMNET_SUCCESS:
        return "VMNET_SUCCESS";
    case VMNET_FAILURE:
        return "VMNET_FAILURE";
    case VMNET_MEM_FAILURE:
        return "VMNET_MEM_FAILURE";
    case VMNET_INVALID_ARGUMENT:
        return "VMNET_INVALID_ARGUMENT";
    case VMNET_SETUP_INCOMPLETE:
        return "VMNET_SETUP_INCOMPLETE";
    case VMNET_INVALID_ACCESS:
        return "VMNET_INVALID_ACCESS";
    case VMNET_PACKET_TOO_BIG:
        return "VMNET_PACKET_TOO_BIG";
    case VMNET_BUFFER_EXHAUSTED:
        return "VMNET_BUFFER_EXHAUSTED";
    case VMNET_TOO_MANY_PACKETS:
        return "VMNET_TOO_MANY_PACKETS";
    default:
        return "(unknown status)";
    }
}
