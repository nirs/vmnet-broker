// SPDX-FileCopyrightText: The vmnet-broker authors
// SPDX-License-Identifier: Apache-2.0

import Foundation
import Logging

// XPC is a C API that predates Swift concurrency. The types (xpc_connection_t, xpc_object_t) don't
// conform to Sendable, but they are safe to use because:
// 1. All XPC handlers run on DispatchQueue.main (serialized access)
// 2. The C version uses the same approach - queue serialization instead of locks
// 3. Making these types Sendable would require wrapping every XPC function with locks, which is
//    unnecessary since we're already serialized
//
// @preconcurrency tells Swift to relax strict concurrency checking for this legacy C API.
// See: https://github.com/apple/swift-evolution/blob/main/proposals/0337-support-incremental-migration-to-concurrency-checking.md
@preconcurrency import XPC

// XPC connection flag (implementation detail, not exported in public header)
private let xpcConnectionMachServiceListener: UInt64 = 1 << 0

// MARK: - XPC Event Types

enum XPCEvent {
    // Connection events
    case connection(xpc_connection_t)
    case connectionInvalid
    case connectionInterrupted

    // Request events
    case requestDictionary(xpc_object_t)
    case requestError(description: String?)

    // Unexpected
    case unexpectedEvent

    init(_ event: xpc_object_t) {
        let type = xpc_get_type(event)
        switch type {
        case XPC_TYPE_ERROR:
            if xpc_equal(event, XPC_ERROR_CONNECTION_INVALID) {
                // Client connection is dead
                self = .connectionInvalid
            } else if xpc_equal(event, XPC_ERROR_CONNECTION_INTERRUPTED) {
                // Temporary interruption
                self = .connectionInterrupted
            } else {
                // Other XPC errors
                let desc = xpc_dictionary_get_string(event, XPC_ERROR_KEY_DESCRIPTION)
                self = .requestError(description: desc.map { String(cString: $0) })
            }
        case XPC_TYPE_CONNECTION:
            self = .connection(event)
        case XPC_TYPE_DICTIONARY:
            self = .requestDictionary(event)
        default:
            self = .unexpectedEvent
        }
    }
}

// MARK: - XPC Listener Delegate

/// Delegate for peer events.
@MainActor
protocol PeerDelegate: AnyObject {
    /// Called when a peer sends a request.
    func peerReceivedRequest(_ request: xpc_object_t, from peer: any Peer)

    /// Called when a peer disconnects.
    func peerDidDisconnect(_ peer: any Peer)
}

/// Delegate for XPC listener events.
@MainActor
protocol XPCListenerDelegate: PeerDelegate {
    /// Called when a new peer connection is established.
    func didAcceptPeer(_ peer: any Peer)
}

// MARK: - XPC Listener

struct XPCListener {
    private let connection: xpc_connection_t
    private weak var delegate: XPCListenerDelegate?

    init(serviceName: String, logger: Logger, delegate: XPCListenerDelegate?) {
        self.delegate = delegate

        connection = xpc_connection_create_mach_service(
            serviceName,
            DispatchQueue.main,
            xpcConnectionMachServiceListener
        )

        setupEventHandler(logger: logger)
        xpc_connection_resume(connection)
    }

    private func setupEventHandler(logger: Logger) {
        xpc_connection_set_event_handler(connection) { event in
            MainActor.assumeIsolated {
                let xpcEvent = XPCEvent(event)
                switch xpcEvent {
                case .connection(let peerConnection):
                    xpc_connection_set_target_queue(peerConnection, DispatchQueue.main)
                    // Create peer and notify delegate
                    // Delegate should never be nil in normal operation (broker outlives listener)
                    guard let delegate = delegate else {
                        logger.error("XPCListener delegate is nil - this should never happen")
                        exit(EXIT_FAILURE)
                    }
                    let peer = XPCPeer(
                        connection: peerConnection,
                        logger: logger,
                        delegate: delegate
                    )
                    delegate.didAcceptPeer(peer)

                case .requestError(let description):
                    // Listener errors are fatal
                    if let desc = description {
                        logger.error("Listener error: \(desc)")
                    } else {
                        logger.error("Listener error")
                    }
                    exit(EXIT_FAILURE)

                default:
                    // Unexpected but non-fatal events
                    let desc = xpc_copy_description(event)
                    logger.warning("Unexpected event type in listener: \(String(cString: desc))")
                    free(desc)
                }
            }
        }
    }
}

// MARK: - XPC Peer

// MARK: - Peer Protocol

/// Protocol for peer connections, allowing testability with mock implementations.
@MainActor
protocol Peer {
    var connection: xpc_connection_t { get }
    var pid: pid_t { get }

    /// Sends a reply message to the peer.
    func sendReply(_ reply: xpc_object_t)
}

// MARK: - XPCPeer

@MainActor
struct XPCPeer: Peer {
    let connection: xpc_connection_t
    let pid: pid_t
    private let logger: Logger
    private weak var delegate: PeerDelegate?

    init(
        connection: xpc_connection_t,
        logger: Logger,
        delegate: PeerDelegate?
    ) {
        self.connection = connection
        self.pid = xpc_connection_get_pid(connection)
        self.logger = logger
        self.delegate = delegate

        logger.info("Peer \(pid) connected")
        initEventHandler()
        xpc_connection_resume(connection)
    }

    /// Sets up the event handler for this peer connection.
    /// Called only during initialization.
    private func initEventHandler() {
        xpc_connection_set_event_handler(self.connection) { event in
            MainActor.assumeIsolated {
                let xpcEvent = XPCEvent(event)
                switch xpcEvent {
                case .connectionInvalid:
                    // Client connection is dead - notify broker
                    self.logger.info("Peer \(self.pid) disconnected")
                    guard let delegate = self.delegate else {
                        self.logger.error("XPCPeer delegate is nil - this should never happen")
                        exit(EXIT_FAILURE)
                    }
                    delegate.peerDidDisconnect(self)

                case .requestDictionary(let request):
                    guard let delegate = self.delegate else {
                        self.logger.error("XPCPeer delegate is nil - this should never happen")
                        exit(EXIT_FAILURE)
                    }
                    delegate.peerReceivedRequest(request, from: self)

                case .connectionInterrupted:
                    // Temporary interruption - log but don't disconnect
                    // Warning level since this shouldn't happen in normal operation
                    self.logger.warning("Peer \(self.pid) temporary interruption")

                case .requestError(let description):
                    // Other errors - log as warning
                    if let desc = description {
                        self.logger.warning("Peer \(self.pid) unexpected error: \(desc)")
                    } else {
                        self.logger.warning("Peer \(self.pid) unexpected error")
                    }

                default:
                    // Unexpected but non-fatal events
                    let desc = xpc_copy_description(event)
                    self.logger.warning("Unexpected event type from peer \(self.pid): \(String(cString: desc))")
                    free(desc)
                }
            }
        }
    }

    func sendReply(_ reply: xpc_object_t) {
        xpc_connection_send_message(connection, reply)
    }
}
