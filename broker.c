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

// --- XPC Event Handler ---
static void event_handler(xpc_connection_t peer) {
    xpc_connection_set_event_handler(peer, ^(xpc_object_t event) {
        xpc_type_t type = xpc_get_type(event);
        if (type == XPC_TYPE_DICTIONARY) {
            const char *net_name = xpc_dictionary_get_string(event, "network_name");
            xpc_object_t reply = xpc_dictionary_create_reply(event);

            network_node_t *node = acquire_network(net_name);
            if (node) {
                xpc_dictionary_set_value(reply, "handle", node->serialization);
                // Tag the connection so we know what to clean up on disconnect
                xpc_connection_set_context(peer, strdup(net_name));
            } else {
                xpc_dictionary_set_string(reply, "error", "Network not found or creation failed");
            }
            xpc_connection_send_message(peer, reply);
            xpc_release(reply);
        } else if (event == XPC_ERROR_CONNECTION_INVALID) {
            char *net_name = xpc_connection_get_context(peer);
            if (net_name) {
                release_network(net_name);
                free(net_name);
            }
        }
    });
    xpc_connection_resume(peer);
}

int main() {
    xpc_connection_t listener = xpc_connection_create_mach_service("com.github.nirs.vmnet-broker", NULL, XPC_CONNECTION_MACH_SERVICE_LISTENER);
    xpc_connection_set_event_handler(listener, ^(xpc_object_t peer) { event_handler(peer); });
    xpc_connection_resume(listener);
    dispatch_main();
}
