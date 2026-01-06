// SPDX-FileCopyrightText: The vmnet-broker authors
// SPDX-License-Identifier: Apache-2.0

import Foundation
import Logging
@preconcurrency import XPC
import vmnet_broker

private let logger = Logger(label: "vmnet-broker")

// MARK: - Broker

@MainActor
class Broker: XPCListenerDelegate {
    private var peers: [pid_t: any Peer] = [:]

    // MARK: - XPCListenerDelegate

    /// Called when a new peer connects.
    /// Adds the peer to the tracking dictionary.
    func didAcceptPeer(_ peer: any Peer) {
        peers[peer.pid] = peer
    }

    // MARK: - PeerDelegate

    /// Called when a peer disconnects.
    /// Removes the peer from the tracking dictionary.
    func peerDidDisconnect(_ peer: any Peer) {
        peers.removeValue(forKey: peer.pid)
    }

    /// Called when a peer sends a request.
    /// Processes the request and sends a reply back to the peer.
    func peerReceivedRequest(_ request: xpc_object_t, from peer: any Peer) {
        guard let reply = xpc_dictionary_create_reply(request) else {
            logger.warning("Failed to create reply")
            return
        }

        // Return constant reply: 42
        xpc_dictionary_set_int64(reply, REPLY_ERROR, 42)
        peer.sendReply(reply)
    }
}

// MARK: - Entry Point

logger.info("Starting broker pid=\(getpid())")

private let broker = Broker()

private let listener = XPCListener(serviceName: MACH_SERVICE_NAME, logger: logger, delegate: broker)

logger.info("XPC listener started")

dispatchMain()
