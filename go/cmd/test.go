package main

import (
	"encoding/json"
	"fmt"
	"log"
	"net"
	"os"
	"os/signal"
	"path/filepath"
	"syscall"
	"time"

	"github.com/Code-Hex/vz/v3"
	"github.com/Code-Hex/vz/v3/pkg/vmnet"
	"github.com/pkg/term/termios"
	"golang.org/x/sys/unix"

	"github.com/nirs/vmnet-broker/go/vmnet_broker"
)

const mIb = 1024 * 1024

func main() {
	if err := run(); err != nil {
		log.Fatal(err)
	}
}

func run() error {
	if len(os.Args) != 2 {
		return fmt.Errorf("Usage: %s vm-name", os.Args[0])
	}
	vmConfig, err := loadVMConfig(os.Args[1])
	if err != nil {
		return err
	}

	bootLoader, err := createBootLoader(vmConfig)
	if err != nil {
		return fmt.Errorf("failed to create bootloader: %w", err)
	}

	config, err := vz.NewVirtualMachineConfiguration(
		bootLoader,
		vmConfig.Cpus,
		vmConfig.Memory*mIb,
	)
	if err != nil {
		return fmt.Errorf("failed to create virtual machine configuration: %w", err)
	}

	serialConfigs, err := createSerialPortConfigurations()
	if err != nil {
		return err
	}
	config.SetSerialPortsVirtualMachineConfiguration(serialConfigs)

	storageConfigs, err := createStorageDeviceConfigurations(vmConfig)
	if err != nil {
		return err
	}
	config.SetStorageDevicesVirtualMachineConfiguration(storageConfigs)

	networkConfigs, err := createNetworkDeviceConfigurations(vmConfig)
	if err != nil {
		return err
	}
	config.SetNetworkDevicesVirtualMachineConfiguration(networkConfigs)

	if validated, err := config.Validate(); !validated || err != nil {
		return fmt.Errorf("failed to validate config: %w", err)
	}

	vm, err := vz.NewVirtualMachine(config)
	if err != nil {
		return fmt.Errorf("failed to create virtual machine: %w", err)
	}

	if err := swithTerminalToRawMode(); err != nil {
		return fmt.Errorf("failed to swith terminal to raw mode: %w", err)
	}
	defer restoreTerminalMode()

	signalCh := setupShutdownSignals()

	if err := vm.Start(); err != nil {
		return fmt.Errorf("failed to start virtual machine: %w", err)
	}

	waitForTermination(vm, signalCh)

	return nil
}

func setupShutdownSignals() <-chan os.Signal {
	signalCh := make(chan os.Signal, 1)
	signal.Notify(signalCh, syscall.SIGTERM, syscall.SIGINT)
	return signalCh
}

func waitForTermination(vm *vz.VirtualMachine, signalCh <-chan os.Signal) {
	for {
		select {
		case sig := <-signalCh:
			log.Printf("recieved signal %v", sig)
			shutdownGracefully(vm)
			return
		case newState := <-vm.StateChangedNotify():
			if newState == vz.VirtualMachineStateStopped {
				log.Println("The guest stopped")
				return
			}
		}
	}
}

func shutdownGracefully(vm *vz.VirtualMachine) {
	log.Printf("Stopping guest gracefuly")
	if result, err := vm.RequestStop(); !result || err != nil {
		reason := "The guest cannot stop gracefully"
		if err != nil {
			reason = fmt.Sprintf("Failed to stop guest gracefully: %v", err)
		}
		hardStop(vm, reason)
		return
	}
	log.Printf("Waiting until guest is stopped")

	// If waiting for "stopped" event times out, fallback to hard stop.
	timeout := time.After(10 * time.Second)
	for {
		select {
		case newState := <-vm.StateChangedNotify():
			if newState == vz.VirtualMachineStateStopped {
				log.Println("The guest stopped")
				return
			}
		case <-timeout:
			hardStop(vm, "Timeout stopping guest gracefully")
			return
		}
	}
}

func hardStop(vm *vz.VirtualMachine, reason string) {
	log.Printf("%s: stopping guest", reason)
	if err := vm.Stop(); err != nil && vm.State() != vz.VirtualMachineStateStopped {
		log.Printf("Failed to stop: %v", err)
		return
	}
	log.Print("The guest was stopped")
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

func createNetworkDeviceConfigurations(cfg *VMConfig) ([]*vz.VirtioNetworkDeviceConfiguration, error) {
	macAddress, err := createMacAddress(cfg.Mac)
	if err != nil {
		return nil, err
	}

	serialization, err := vmnet_broker.StartSession("default")
	if err != nil {
		return nil, fmt.Errorf("failed to get network serializaion from broker: %w", err)
	}

	network, err := vmnet.NewNetworkWithSerialization(serialization.Raw())
	if err != nil {
		return nil, fmt.Errorf("failed to create network from serialization: %w", err)
	}

	attachment, err := vz.NewVmnetNetworkDeviceAttachment(network.Raw())
	if err != nil {
		return nil, fmt.Errorf("failed to create vmnet network device attachment: %w", err)
	}

	config, err := vz.NewVirtioNetworkDeviceConfiguration(attachment)
	if err != nil {
		return nil, fmt.Errorf("failed to create virtio network device configuration: %w", err)
	}

	config.SetMACAddress(macAddress)

	return []*vz.VirtioNetworkDeviceConfiguration{config}, nil
}

func createMacAddress(mac string) (*vz.MACAddress, error) {
	hwAddr, err := net.ParseMAC(mac)
	if err != nil {
		return nil, fmt.Errorf("failed to parse MAC address: %w", err)
	}
	macAddress, err := vz.NewMACAddress(hwAddr)
	if err != nil {
		return nil, fmt.Errorf("failed to create vz.MACAddress: %w", err)
	}
	return macAddress, nil
}

func createStorageDeviceConfigurations(cfg *VMConfig) ([]vz.StorageDeviceConfiguration, error) {
	diskAttachment, err := vz.NewDiskImageStorageDeviceAttachment(
		cfg.Disks[0].Path,
		cfg.Disks[0].Readonly,
	)
	if err != nil {
		return nil, fmt.Errorf("failed to create root disk attachment: %w", err)
	}

	diskConfig, err := vz.NewVirtioBlockDeviceConfiguration(diskAttachment)
	if err != nil {
		return nil, fmt.Errorf("failed to create root disk config: %w", err)
	}

	cidataAttachment, err := vz.NewDiskImageStorageDeviceAttachment(
		cfg.Disks[1].Path,
		cfg.Disks[1].Readonly,
	)
	if err != nil {
		return nil, fmt.Errorf("failed to create iso attachment: %w", err)
	}

	cidataConfig, err := vz.NewVirtioBlockDeviceConfiguration(cidataAttachment)
	if err != nil {
		return nil, fmt.Errorf("failed to create iso disk config: %w", err)
	}

	return []vz.StorageDeviceConfiguration{diskConfig, cidataConfig}, nil
}

func createSerialPortConfigurations() ([]*vz.VirtioConsoleDeviceSerialPortConfiguration, error) {
	attachment, err := vz.NewFileHandleSerialPortAttachment(os.Stdin, os.Stdout)
	if err != nil {
		return nil, fmt.Errorf("failed to create serial port attachment: %w", err)
	}

	config, err := vz.NewVirtioConsoleDeviceSerialPortConfiguration(attachment)
	if err != nil {
		return nil, fmt.Errorf("failed to create console device serial port configuration: %w", err)
	}

	return []*vz.VirtioConsoleDeviceSerialPortConfiguration{config}, nil
}

func createBootLoader(cfg *VMConfig) (vz.BootLoader, error) {
	return vz.NewLinuxBootLoader(
		cfg.Bootloader.Kernel,
		vz.WithInitrd(cfg.Bootloader.Initrd),
		vz.WithCommandLine("console=hvc0 root=LABEL=cloudimg-rootfs"),
	)
}

type VMConfig struct {
	Cpus       uint
	Memory     uint64
	Mac        string
	Bootloader BootloaderConfig
	Disks      []DiskConfig
}

type BootloaderConfig struct {
	Kernel string
	Initrd string
}

type DiskConfig struct {
	Path     string
	Readonly bool
}

func loadVMConfig(vmName string) (*VMConfig, error) {
	path := filepath.Join(".vms", vmName, "config.json")
	data, err := os.ReadFile(path)
	if err != nil {
		return nil, fmt.Errorf("failed to read config from '%s': %w", path, err)
	}
	config := &VMConfig{}
	if err := json.Unmarshal(data, &config); err != nil {
		return nil, fmt.Errorf("failed to parse config from '%s': %w", path, err)
	}
	return config, nil
}
