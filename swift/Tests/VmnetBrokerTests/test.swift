import Testing
import VmnetBroker
import vmnet_broker

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
