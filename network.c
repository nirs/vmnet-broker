#include "network.h"
#include <netinet/in.h>
#include <sys/socket.h>

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
