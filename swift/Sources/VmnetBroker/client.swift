import Foundation
import vmnet_broker_binary

public enum VmnetBrokerError: Int32, Error, LocalizedError {
    case xpcFailure = 1
    case invalidReply
    case notAllowed
    case invalidRequest
    case notFound
    case createFailure
    case internalError

    public var errorDescription: String? {
        switch self {
        case .xpcFailure:
            return "Failed to send XPC message to broker"
        case .invalidReply:
            return "Broker returned invalid reply"
        case .notAllowed:
            return "You are not allowed to create a network"
        case .invalidRequest:
            return "Invalid broker request"
        case .notFound:
            return "Network name not found"
        case .createFailure:
            return "Failed to create network"
        case .internalError:
            return "Internal or unknown error"
        }
    }
}

public enum VmnetBroker {
    public static func createNetwork(named: String) throws -> xpc_object_t {
        var status: Int32 = 0
        guard let serialization = vmnet_broker_start_session(named, &status) else {
            throw VmnetBrokerError(rawValue: status) ?? .internalError
        }
        return serialization
    }
}
