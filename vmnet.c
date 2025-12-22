// Crappy code written by Gemini for reference.

#include <xpc/xpc.h>
#include <vmnet/vmnet.h>
#include <CoreFoundation/CoreFoundation.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

// --- Data Structures ---
typedef struct network_node {
    char *name;
    vmnet_network_ref ref;
    xpc_object_t serialization;
    int ref_count;
    struct network_node *next;
} network_node_t;

static network_node_t *active_networks = NULL;

// Helper 1: Handles the stream, parsing, and cleanup
CFPropertyListRef copy_vmnets_plist(const char *path) {
    // XXX Use CFPropertyListCreateWithData to support plist, binary plist and json.
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(kCFAllocatorDefault, (const UInt8 *)path, strlen(path), false);
    if (!url) return NULL;

    CFReadStreamRef stream = CFReadStreamCreateWithFile(kCFAllocatorDefault, url);
    CFRelease(url);
    if (!stream) return NULL;

    if (!CFReadStreamOpen(stream)) {
        CFRelease(stream);
        return NULL;
    }

    CFPropertyListFormat format;
    CFErrorRef error = NULL;
    CFPropertyListRef plist = CFPropertyListCreateWithStream(kCFAllocatorDefault, stream, 0, kCFPropertyListImmutable, &format, &error);

    CFReadStreamClose(stream);
    CFRelease(stream);

    if (error) CFRelease(error);
    return plist; // Caller is responsible for CFRelease
}

// Helper 2: Extracts specific network config and converts to XPC
// XXX return an error for the client (code, description)
xpc_object_t get_config_for_name(const char *name) {
    const char *path = "/Users/nir/Library/Application Support/vmnet-broker/networks.json";
    CFPropertyListRef plist = copy_vmnets_plist(path);
    if (!plist) return NULL;

    xpc_object_t xpc_cfg = NULL;

    if (CFGetTypeID(plist) == CFDictionaryGetTypeID()) {
        CFStringRef key = CFStringCreateWithCString(kCFAllocatorDefault, name, kCFStringEncodingUTF8);
        CFDictionaryRef net_dict = CFDictionaryGetValue((CFDictionaryRef)plist, key);

        // Verify we found a dictionary for this network name
        if (net_dict && CFGetTypeID(net_dict) == CFDictionaryGetTypeID()) {
            xpc_cfg = xpc_dictionary_create(NULL, NULL, 0);

            // XXX Check required type
            CFNumberRef mode = CFDictionaryGetValue(net_dict, CFSTR("mode"));
            if (mode) {
                int64_t mode_val;
                CFNumberGetValue(mode, kCFNumberSInt64Type, &mode_val);
                xpc_dictionary_set_uint64(xpc_cfg, vmnet_operation_mode_key, (uint64_t)mode_val);
            }
            // XXX Add other keys (interface_id, start_address, end_address, subnet, etc.)
            // XXX If config is invalid, log an error and skip it.
        }
        CFRelease(key);
    }

    CFRelease(plist);
    return xpc_cfg;
}

// --- Network Lifecycle ---
network_node_t* acquire_network(const char *name) {
    network_node_t *curr = active_networks;
    while (curr) {
        if (strcmp(curr->name, name) == 0) {
            curr->ref_count++;
            return curr;
        }
        curr = curr->next;
    }

    xpc_object_t cfg = get_config_for_name(name);
    if (!cfg) return NULL;

    vmnet_return_t status;
    vmnet_network_ref net = vmnet_network_create(cfg, &status);
    xpc_release(cfg);

    if (status != VMNET_SUCCESS || net == NULL) {
        return NULL;
    }

    xpc_object_t serialization = vmnet_network_copy_serialization(net, &status);
    if (status != VMNET_SUCCESS || serialization == NULL) {
        return NULL;
    }

    network_node_t *node = malloc(sizeof(network_node_t));
    node->name = strdup(name);
    node->ref = net;
    node->serialization = serialization;
    node->ref_count = 1;
    node->next = active_networks;
    active_networks = node;

    return node;
}

void release_network(const char *name) {
    network_node_t **curr = &active_networks;
    while (*curr) {
        network_node_t *entry = *curr;
        if (strcmp(entry->name, name) == 0) {
            entry->ref_count--;
            if (entry->ref_count == 0) {
                // The last VM using this specific network has exited.
                // We don't need to call stop_interface (Framework handles it),
                // but we release the C-ref to let the kernel clean up.
                xpc_release(entry->serialization);
                // Note: vmnet_network_ref is managed by the Serialization object's lifecycle
                *curr = entry->next;
                free(entry->name);
                free(entry);
                printf("Network '%s' released and destroyed.\n", name);
            }
            return;
        }
        curr = &entry->next;
    }
}
