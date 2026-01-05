// SPDX-FileCopyrightText: The vmnet-broker authors
// SPDX-License-Identifier: Apache-2.0

import Testing
import VmnetBroker
import vmnet
import vmnet_broker

/// Test install vmnet-broker (require installing the vmnet-broker launchd daemon).
@Suite("VmnetBroker validation")
struct VmnetBrokerTests {

    /// Test acquiring a valid network
    @Test
    func validNetwork() throws {
        let serialization = try VmnetBroker.acquireNetwork(named: "default")

        var status: vmnet_return_t = .VMNET_SUCCESS
        let network = vmnet_network_create_with_serialization(serialization, &status)

        #expect(
            network != nil && status == .VMNET_SUCCESS,
            "Failed to create network with status: \(status)")

        if let network = network {
            Unmanaged<AnyObject>.fromOpaque(UnsafeRawPointer(network)).release()
        }
    }
}

@Suite("VmnetBroker.Error validation")
struct VmnetBrokerErrorTests {

    /// Validates creating from existing C library constants.
    @Test
    func creation() {
        let error = VmnetBroker.Error(VMNET_BROKER_NOT_FOUND)
        #expect(error.status == VMNET_BROKER_NOT_FOUND)
        #expect(error == .notFound)
    }

    /// Validates pattern matching in do-catch blocks.
    @Test
    func patternMatching() {
        let error = VmnetBroker.Error.notFound
        var caughtCorrectly = false

        do {
            throw error
        } catch VmnetBroker.Error.notFound {
            caughtCorrectly = true
        } catch {
            // Unexpected error caught
        }

        #expect(caughtCorrectly)
    }

    /// Validates that error exposes the C library error message.
    @Test
    func localizedDescription() {
        let error = VmnetBroker.Error.notFound
        let expectedCMessage = String(cString: vmnet_broker_strerror(error.status))
        #expect(error.localizedDescription == expectedCMessage)
    }

    /// Validates handling of unknown status
    @Test
    func unknownStatus() {
        let unknownStatus = vmnet_broker_return_t(999)

        let error = VmnetBroker.Error(unknownStatus)
        #expect(error.status == unknownStatus)

        let expectedCMessage = String(cString: vmnet_broker_strerror(error.status))
        #expect(error.localizedDescription == expectedCMessage)
    }
}
