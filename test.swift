import Foundation
import Virtualization
import XPC
import vmnet
import vmnet_broker

// MARK: - Main

guard CommandLine.arguments.count == 2 else {
    print("Usage: \(CommandLine.arguments[0]) <config.json>")
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
    print("Failed to validate the virtual machine configuration. \(error)")
    exit(EXIT_FAILURE)
}

let virtualMachine = VZVirtualMachine(configuration: configuration)

let delegate = Delegate()
virtualMachine.delegate = delegate

virtualMachine.start { (result) in
    if case .failure(let error) = result {
        print("Failed to start the virtual machine. \(error)")
        exit(EXIT_FAILURE)
    }
}

RunLoop.main.run(until: Date.distantFuture)

// MARK: - Virtual Machine Delegate

class Delegate: NSObject, VZVirtualMachineDelegate {
    func guestDidStop(_ virtualMachine: VZVirtualMachine) {
        print("The guest shut down - exiting")
        exit(EXIT_SUCCESS)
    }
}

// MARK: - Helper Functions

func createBootLoader(_ cfg: BootloaderConfig) -> VZBootLoader {
    let kernelURL = URL(fileURLWithPath: cfg.kernel, isDirectory: false)
    let initrdURL = URL(fileURLWithPath: cfg.initrd, isDirectory: false)

    let bootLoader = VZLinuxBootLoader(kernelURL: kernelURL)
    bootLoader.initialRamdiskURL = initrdURL
    bootLoader.commandLine = "console=hvc0 root=LABEL=cloudimg-rootfs"

    return bootLoader
}

func createConsoleConfiguration() -> VZSerialPortConfiguration {
    let consoleConfiguration = VZVirtioConsoleDeviceSerialPortConfiguration()

    let inputFileHandle = FileHandle.standardInput
    let outputFileHandle = FileHandle.standardOutput

    // Put stdin into raw mode, disabling local echo, input canonicalization,
    // and CR-NL mapping.
    var attributes = termios()
    tcgetattr(inputFileHandle.fileDescriptor, &attributes)
    attributes.c_iflag &= ~tcflag_t(ICRNL)
    attributes.c_lflag &= ~tcflag_t(ICANON | ECHO)
    tcsetattr(inputFileHandle.fileDescriptor, TCSANOW, &attributes)

    let stdioAttachment = VZFileHandleSerialPortAttachment(
        fileHandleForReading: inputFileHandle,
        fileHandleForWriting: outputFileHandle)

    consoleConfiguration.attachment = stdioAttachment

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

    var error = vmnet_broker_error()
    guard let serialization = vmnet_broker_start_session("default", &error) else {
        let msg = withUnsafePointer(to: &error.message.0) { String(cString: $0) }
        fatalError("Failed to start vmnet-broker session: \(msg) (code: \(error.code))")
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
