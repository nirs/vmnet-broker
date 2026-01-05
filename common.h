// SPDX-FileCopyrightText: The vmnet-broker authors
// SPDX-License-Identifier: Apache-2.0

#ifndef COMMON_H
#define COMMON_H

#include <vmnet/vmnet.h>
#include <xpc/xpc.h>

struct network_info {
    char subnet[INET_ADDRSTRLEN];
    char mask[INET_ADDRSTRLEN];
    char ipv6_prefix[INET6_ADDRSTRLEN];
    uint8_t prefix_len;
};

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))

void network_info(vmnet_network_ref network, struct network_info *info);
const char *vmnet_strerror(vmnet_return_t status);

#endif // COMMON_H
