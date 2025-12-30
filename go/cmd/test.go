package main

import (
	"fmt"
	"log"
	"net"
	"os"
	"os/signal"
	"syscall"
	"time"

	"github.com/Code-Hex/vz/v3"
	"github.com/Code-Hex/vz/v3/pkg/vmnet"
	"github.com/pkg/term/termios"
	"golang.org/x/sys/unix"

	"github.com/nirs/vmnet-broker/go/vmnet_broker"
)

func main() {
	// Bootloader

	bootLoader, err := vz.NewLinuxBootLoader(
		"vm/vm1/ubuntu-25.04-server-cloudimg-arm64-vmlinuz-generic",
		vz.WithCommandLine("console=hvc0 root=LABEL=cloudimg-rootfs"),
		vz.WithInitrd("vm/vm1/ubuntu-25.04-server-cloudimg-arm64-initrd-generic"),
	)
	if err != nil {
		log.Fatalf("failed to create bootloader: %s", err)
	}

	// Network devices configuration

	serialization, err := vmnet_broker.StartSession("default")
	if err != nil {
		log.Fatalf("Faled to get network serializaion from broker: %v", err)
	}

	network, err := vmnet.NewNetworkWithSerialization(serialization.Raw())
	if err != nil {
		log.Fatalf("failed to create network from serialization: %v", err)
	}

	ipv4, err := network.IPv4Subnet()
	if err != nil {
		log.Fatalf("failed to get IPv4 subnet: %v", err)
	}

	log.Printf("✅ Using nework %s", ipv4)

	attachment, err := vz.NewVmnetNetworkDeviceAttachment(network.Raw())
	if err != nil {
		log.Fatalf("failed to create vmnet network device attachment: %s", err)
	}

	networkConfig, err := vz.NewVirtioNetworkDeviceConfiguration(attachment)
	if err != nil {
		log.Fatalf("failed to create virtio network device configuration: %s", err)
	}

	hwAddr, err := net.ParseMAC("82:e9:dd:3d:68:1f")
	if err != nil {
		log.Fatalf("failed to parse MAC address: %v", err)
	}

	macAddress, err := vz.NewMACAddress(hwAddr)
	if err != nil {
		log.Fatalf("failed to create vz.MACAddress: %v", err)
	}

	networkConfig.SetMACAddress(macAddress)

	config, err := vz.NewVirtualMachineConfiguration(
		bootLoader,
		1,
		1*1024*1024*1024,
	)
	if err != nil {
		log.Fatalf("failed to create virtual machine configuration: %s", err)
	}

	config.SetNetworkDevicesVirtualMachineConfiguration(
		[]*vz.VirtioNetworkDeviceConfiguration{networkConfig})

	// Storage devices configuration

	rootDiskAttachment, err := vz.NewDiskImageStorageDeviceAttachment(
		"vm/vm1/ubuntu-25.04-server-cloudimg-arm64.img",
		false,
	)
	if err != nil {
		log.Fatalf("failed to create root disk attachment: %v", err)
	}

	isoAttachment, err := vz.NewDiskImageStorageDeviceAttachment(
		"vm/vm1/cidata.iso",
		true,
	)
	if err != nil {
		log.Fatalf("failed to create iso attachment: %v", err)
	}

	rootDiskConfig, err := vz.NewVirtioBlockDeviceConfiguration(rootDiskAttachment)
	if err != nil {
		log.Fatalf("failed to create root disk config: %s", err)
	}

	isoDiskConfig, err := vz.NewVirtioBlockDeviceConfiguration(isoAttachment)
	if err != nil {
		log.Fatalf("failed to create iso disk config: %s", err)
	}

	config.SetStorageDevicesVirtualMachineConfiguration([]vz.StorageDeviceConfiguration{
		rootDiskConfig, isoDiskConfig,
	})

	// Serial ports configuration

	serialPortAttachment, err := vz.NewFileHandleSerialPortAttachment(os.Stdin, os.Stdout)
	if err != nil {
		log.Fatalf("failed to create serial port attachment: %s", err)
	}

	consoleConfig, err := vz.NewVirtioConsoleDeviceSerialPortConfiguration(serialPortAttachment)
	if err != nil {
		log.Fatalf("failed to create console device serial port configuartion: %s", err)
	}

	config.SetSerialPortsVirtualMachineConfiguration([]*vz.VirtioConsoleDeviceSerialPortConfiguration{
		consoleConfig,
	})

	validated, err := config.Validate()
	if !validated || err != nil {
		log.Fatal("failed to validate config", err)
	}

	if err := swithTerminalToRawMode(); err != nil {
		log.Fatalf("failed to swith terminal to raw mode: %s", err)
	}
	defer restoreTerminalMode()

	vm, err := vz.NewVirtualMachine(config)
	if err != nil {
		log.Fatalf("Virtual machine creation failed: %s", err)
	}

	signalCh := setupShutdownSignals()

	if err := vm.Start(); err != nil {
		log.Fatalf("failed to start virtual machine: %s", err)
	}

	log.Printf("✅ Started virtual machine")

	waitForTermination(vm, signalCh)
}

func setupShutdownSignals() <-chan os.Signal {
	signalCh := make(chan os.Signal, 1)
	signal.Notify(signalCh, syscall.SIGTERM, syscall.SIGINT)
	return signalCh
}

func waitForTermination(vm *vz.VirtualMachine, signalCh <-chan os.Signal) int {
	for {
		select {
		case sig := <-signalCh:
			log.Printf("recieved signal %v", sig)
			return shutdownGracefully(vm)
		case newState := <-vm.StateChangedNotify():
			if newState == vz.VirtualMachineStateRunning {
				log.Println("✅ The guest is running")
			}
			if newState == vz.VirtualMachineStateStopped {
				log.Println("The guest stopped")
				return 0
			}
		}
	}
}

func shutdownGracefully(vm *vz.VirtualMachine) int {
	log.Printf("Stopping guest gracefuly")
	if result, err := vm.RequestStop(); !result || err != nil {
		if err != nil {
			return hardStop(vm, fmt.Sprintf("Failed to stop guest gracefully: %v", err))
		}
		return hardStop(vm, "The guest cannot stop gracefully")
	}
	log.Printf("Waiting until guest is stopped")

	// If waiting for "stopped" event times out, fallback to hard stop.
	timeout := time.After(10 * time.Second)
	for {
		select {
		case newState := <-vm.StateChangedNotify():
			if newState == vz.VirtualMachineStateStopped {
				log.Println("The guest stopped")
				return 0
			}
		case <-timeout:
			return hardStop(vm, "Timeout stopping guest gracefully")
		}
	}
}

func hardStop(vm *vz.VirtualMachine, reason string) int {
	log.Printf("%s: stopping guest", reason)
	if err := vm.Stop(); err != nil && vm.State() != vz.VirtualMachineStateStopped {
		log.Printf("Failed to stop: %v", err)
		return 1
	}
	log.Print("The guest was stopped")
	return 0
}

var originalTerminalAttr unix.Termios

func swithTerminalToRawMode() error {
	fd := os.Stdin.Fd()
	if err := termios.Tcgetattr(fd, &originalTerminalAttr); err != nil {
		return err
	}

	// Configure stdin for a transparent serial console:
	// - unix.ICANON: Disable line-buffering so the guest receives every
	//   keystroke immediately without waiting for a newline.
	// - unix.ECHO: Disable local echo; the guest OS displays characters,
	//   preventing duplicates and keeping passwords hidden.
	// - unix.ICRNL: Disable CR-to-NL mapping so the guest receives raw carriage
	//   returns (\r) for proper line ending control.

	rawAttr := originalTerminalAttr
	rawAttr.Iflag &^= unix.ICRNL
	rawAttr.Lflag &^= unix.ICANON | unix.ECHO

	// reflects the changed settings
	return termios.Tcsetattr(fd, termios.TCSANOW, &rawAttr)
}

func restoreTerminalMode() {
	// TCSAFLUSH ensures that unread guest output doesn't leak into the host
	// shell prompt.
	if err := termios.Tcsetattr(os.Stdin.Fd(), termios.TCSAFLUSH, &originalTerminalAttr); err != nil {
		log.Printf("Failed to restore terminal to normal mode: %s", err)
	}
}
