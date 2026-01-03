import Foundation
import vmnet_broker

/// A namespace for interacting with the vmnet-broker service.
public enum VmnetBroker {

    /// An error returned by vmnet-broker. This is implemented as a struct rather
    /// than an enum to maintain compatibility with future broker versions that may
    /// introduce new error codes not known at compile time.
    public struct Error: Swift.Error, LocalizedError, Equatable {

        // Known error constants mapped from the C library.
        public static let xpcFailure = Error(VMNET_BROKER_XPC_FAILURE)
        public static let invalidReply = Error(VMNET_BROKER_INVALID_REPLY)
        public static let notAllowed = Error(VMNET_BROKER_NOT_ALLOWED)
        public static let invalidRequest = Error(VMNET_BROKER_INVALID_REQUEST)
        public static let notFound = Error(VMNET_BROKER_NOT_FOUND)
        public static let createFailure = Error(VMNET_BROKER_CREATE_FAILURE)
        public static let internalError = Error(VMNET_BROKER_INTERNAL_ERROR)

        /// The raw status code returned by the vmnet-broker C API.
        public let status: vmnet_broker_return_t

        /// Initializes a new error from a raw C status code.
        public init(_ status: vmnet_broker_return_t) {
            self.status = status
        }

        /// A localized message describing the error, retrieved from the C library.
        public var errorDescription: String? {
            return String(cString: vmnet_broker_strerror(status))
        }

        /// Enables pattern matching in catch blocks, allowing syntax like:
        /// `catch VmnetBroker.Error.notFound`
        public static func ~= (lhs: Error, rhs: Swift.Error) -> Bool {
            guard let rhs = rhs as? Error else {
                return false
            }
            return lhs == rhs
        }
    }

    /// AcquireNetwork Acquires a shared lock on a configured network, instantiating
    /// it if necessary.
    ///
    /// The specified network name must exist in the broker's configuration. This
    /// function retrieves a reference to the network if it already exists, or
    /// instantiates it if needed.
    ///
    /// The shared lock ensures the network remains active as long as the calling
    /// process is using it. The lock is automatically released when the process
    /// terminates.
    ///
    /// - Parameter named: The unique name of the network to acquire.
    /// - Returns: An `xpc_object_t` containing the network serialization.
    /// - Throws: `VmnetBroker.Error` if the operation fails.
    public static func acquireNetwork(named: String) throws -> xpc_object_t {
        var status: vmnet_broker_return_t = VMNET_BROKER_SUCCESS
        guard let serialization = vmnet_broker_acquire_network(named, &status) else {
            throw Error(status)
        }
        return serialization
    }
}
