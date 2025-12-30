import Foundation
import Logging
import Virtualization
import XPC
import vmnet
import vmnet_broker

// MARK: Global state

private let logger = Logger(label: "main")

// For restoring terminal to normal mode on exit. Must be global so we can
// access in atexit.
var originalTermios = termios()

// Prevent garbage collections of dispatch sources.
private var dispatchSignalSources: [DispatchSourceSignal] = []

// MARK: - Main

setupLogging()

guard CommandLine.arguments.count == 2 else {
    logger.info("Usage: \(CommandLine.arguments[0]) <config.json>")
    exit(EX_USAGE)
}

let vmConfig = loadVMConfig(CommandLine.arguments[1])

let configuration = VZVirtualMachineConfiguration()
configuration.cpuCount = 1
configuration.memorySize = 1 * 1024 * 1024 * 1024
configuration.serialPorts = [createConsoleConfiguration()]
configuration.bootLoader = createBootLoader(vmConfig.bootloader)
configuration.networkDevices = [createVmnetNetworkDeviceConfiguration(vmConfig.mac)]
configuration.storageDevices = [
    createStorageDevice(vmConfig.disks[0]),
    createStorageDevice(vmConfig.disks[1]),
]

do {
    try configuration.validate()
} catch {
    logger.error("Failed to validate the virtual machine configuration. \(error)")
    exit(EXIT_FAILURE)
}

let virtualMachine = VZVirtualMachine(configuration: configuration)

let delegate = Delegate()
virtualMachine.delegate = delegate

setupShutdownSignals(virtualMachine)

virtualMachine.start { (result) in
    if case .failure(let error) = result {
        logger.error("Failed to start the virtual machine. \(error)")
        exit(EXIT_FAILURE)
    }
}

RunLoop.main.run(until: Date.distantFuture)

// MARK: - Virtual Machine Delegate

class Delegate: NSObject, VZVirtualMachineDelegate {
    // Normal shutdown within the guest (e.g poweroff)
    func guestDidStop(_ virtualMachine: VZVirtualMachine) {
        logger.info("The guest stopped")
        exit(EXIT_SUCCESS)
    }
    // Not clear when this happens, but good to know if it did.
    func virtualMachine(_ virtualMachine: VZVirtualMachine, didStopWithError error: any Error) {
        logger.warning("The guest stopped with error: \(error)")
        exit(EXIT_FAILURE)
    }
}

// MARK: - Helper Functions

func setupLogging() {
    LoggingSystem.bootstrap { label in
        var handler = StreamLogHandler.standardError(label: label)
        handler.logLevel = .debug
        return handler
    }
}

@MainActor
func shutdownGracefully(_ vm: VZVirtualMachine) {
    logger.info("Stopping guest gracefully")
    do {
        try vm.requestStop()
    } catch {
        hardStop(vm, reason: "Failed to stop guest gracefully: \(error)")
        return
    }
    logger.debug("Waiting until guest is stopped")

    // If guestDidStop is not called, fallback to hard stop.
    DispatchQueue.main.asyncAfter(deadline: .now() + 10) {
        if vm.state != .stopped {
            hardStop(vm, reason: "Timeout stopping guest gracefully")
        }
    }
}

func hardStop(_ vm: VZVirtualMachine, reason: String) {
    logger.warning("\(reason): stopping guest")
    vm.stop { err in
        if let err = err, vm.state != .stopped {
            logger.warning("Failed to stop guest: \(err)")
            exit(EXIT_FAILURE)
        }
        logger.info("The guest was stopped")
        exit(EXIT_SUCCESS)
    }
}

@MainActor
func setupShutdownSignals(_ vm: VZVirtualMachine) {
    for sig in [SIGINT, SIGTERM] {
        signal(sig, SIG_IGN)
        let signalSource = DispatchSource.makeSignalSource(signal: sig, queue: .main)
        signalSource.setEventHandler {
            logger.info("Received signal \(sig) - exiting")
            shutdownGracefully(vm)
        }
        signalSource.resume()
        dispatchSignalSources.append(signalSource)
    }
}

func createBootLoader(_ cfg: BootloaderConfig) -> VZBootLoader {
    let kernelURL = URL(fileURLWithPath: cfg.kernel, isDirectory: false)
    let initrdURL = URL(fileURLWithPath: cfg.initrd, isDirectory: false)

    let bootLoader = VZLinuxBootLoader(kernelURL: kernelURL)
    bootLoader.initialRamdiskURL = initrdURL
    bootLoader.commandLine = "console=hvc0 root=LABEL=cloudimg-rootfs"

    return bootLoader
}

@MainActor
func createConsoleConfiguration() -> VZSerialPortConfiguration {
    let consoleConfiguration = VZVirtioConsoleDeviceSerialPortConfiguration()

    // Save terminal state and restore it at exit.
    tcgetattr(STDIN_FILENO, &originalTermios)
    atexit {
        // TCSAFLUSH ensures that unread guest output doesn't leak into the host
        // shell prompt.
        if tcsetattr(STDIN_FILENO, TCSAFLUSH, &originalTermios) != 0 {
            let reason = String(cString: strerror(errno))
            logger.warning("Failed to restore terminal to normal mode: \(reason)")
        }
    }

    // Configure stdin for a transparent serial console:
    // - ICANON: Disable line-buffering so the guest receives every keystroke
    //   immediately without waiting for a newline.
    // - ECHO: Disable local echo; the guest OS displays characters, preventing
    //   duplicates and keeping passwords hidden.
    // - ICRNL: Disable CR-to-NL mapping so the guest receives raw carriage
    //   returns (\r) for proper line ending control.

    var rawAttributes = originalTermios
    rawAttributes.c_iflag &= ~tcflag_t(ICRNL)
    rawAttributes.c_lflag &= ~tcflag_t(ICANON | ECHO)
    tcsetattr(STDIN_FILENO, TCSANOW, &rawAttributes)

    consoleConfiguration.attachment = VZFileHandleSerialPortAttachment(
        fileHandleForReading: FileHandle.standardInput,
        fileHandleForWriting: FileHandle.standardOutput)

    return consoleConfiguration
}

func createStorageDevice(_ cfg: DiskConfig) -> VZStorageDeviceConfiguration {
    let url = URL(fileURLWithPath: cfg.path, isDirectory: false)
    guard let attachment = try? VZDiskImageStorageDeviceAttachment(url: url, readOnly: cfg.readonly)
    else {
        fatalError("Failed to create disk image from \(cfg.path)")
    }
    return VZVirtioBlockDeviceConfiguration(attachment: attachment)
}

func createVmnetNetworkDeviceConfiguration(_ mac: String) -> VZVirtioNetworkDeviceConfiguration {
    guard let macAddress = VZMACAddress(string: mac) else {
        fatalError("Invalid MAC address: \(mac)")
    }

    var broker_status = VMNET_BROKER_SUCCESS
    guard let serialization = vmnet_broker_start_session("default", &broker_status) else {
        let msg = vmnet_broker_strerror(broker_status)
        fatalError("Failed to start vmnet-broker session: \(msg)")
    }
    // Swift ARC naturally releases CoreFoundation types.

    var status: vmnet_return_t = vmnet_return_t.VMNET_SUCCESS
    guard let network = vmnet_network_create_with_serialization(serialization, &status) else {
        fatalError("Failed to create network from serialization: \(status)")
    }
    // Unfortunately, Swift ARC doesn't release opaque pointer automatically.
    defer {
        Unmanaged<AnyObject>.fromOpaque(UnsafeRawPointer(network)).release()
    }

    let configuration = VZVirtioNetworkDeviceConfiguration()
    configuration.attachment = VZVmnetNetworkDeviceAttachment(network: network)
    configuration.macAddress = macAddress

    return configuration
}

// MARK: - Loading configuration

struct VMConfig: Codable {
    let mac: String
    let bootloader: BootloaderConfig
    let disks: [DiskConfig]
}

struct BootloaderConfig: Codable {
    let kernel: String
    let initrd: String
}

struct DiskConfig: Codable {
    let path: String
    let readonly: Bool
}

func loadVMConfig(_ path: String) -> VMConfig {
    guard let data = try? Data(contentsOf: URL(fileURLWithPath: path)) else {
        fatalError("Failed to read config file \(path)")
    }
    guard let config = try? JSONDecoder().decode(VMConfig.self, from: data) else {
        fatalError("Failed to decode JSON from \(path)")
    }
    return config
}
