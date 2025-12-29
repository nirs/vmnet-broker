package main

import (
	"log"
	"net"
	"os"
	"os/signal"
	"syscall"

	"github.com/Code-Hex/vz/v3"
	"github.com/Code-Hex/vz/v3/pkg/vmnet"
	"github.com/pkg/term/termios"
	"golang.org/x/sys/unix"

	"github.com/nirs/vmnet-broker/go/vmnet_broker"
)

func main() {
	serialization, err := vmnet_broker.StartSession("default")
	if err != nil {
		log.Fatalf("Faled to get network serializaion from broker: %v", err)
	}
	log.Printf("✅ Receive serialization from broker %p", serialization.Raw())

	network, err := vmnet.NewNetworkWithSerialization(serialization.Raw())
	if err != nil {
		log.Fatalf("failed to create network from serialization: %v", err)
	}

	ipv4, err := network.IPv4Subnet()
	if err != nil {
		log.Fatalf("failed to get IPv4 subnet: %v", err)
	}
	log.Printf("✅ Create nework from serialization subnet %s", ipv4)

	bootLoader, err := vz.NewLinuxBootLoader(
		"vm/vm1/ubuntu-25.04-server-cloudimg-arm64-vmlinuz-generic",
		vz.WithCommandLine("console=hvc0 root=LABEL=cloudimg-rootfs"),
		vz.WithInitrd("vm/vm1/ubuntu-25.04-server-cloudimg-arm64-initrd-generic"),
	)
	if err != nil {
		log.Fatalf("failed to create bootloader: %s", err)
	}
	log.Println("bootLoader:", bootLoader)

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

	// 2. Convert net.HardwareAddr to *vz.MACAddress
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

	if err := setRawMode(os.Stdin); err != nil {
		log.Fatalf("failed to set stdin to raw mode: %s", err)
	}
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

	log.Printf("✅ Validated virtual machine configuration")

	vm, err := vz.NewVirtualMachine(config)
	if err != nil {
		log.Fatalf("Virtual machine creation failed: %s", err)
	}

	signalCh := make(chan os.Signal, 1)
	signal.Notify(signalCh, syscall.SIGTERM, syscall.SIGINT)

	if err := vm.Start(); err != nil {
		log.Fatalf("failed to start virtual machine: %s", err)
	}

	log.Printf("✅ Started virtual machine")

	errCh := make(chan error, 1)

	for {
		select {
		case <-signalCh:
			log.Println("recieved signal - shutting down gracefuly")
			result, err := vm.RequestStop()
			if err != nil {
				log.Printf("failed to stop gracefully: %v - hard stop", err)
				if err := vm.Stop(); err != nil {
					log.Printf("failed to stop: %v - existing", err)
					return
				}
			}
			log.Printf("Requested stop: %v", result)
		case newState := <-vm.StateChangedNotify():
			if newState == vz.VirtualMachineStateRunning {
				log.Println("The guest is running")
			}
			if newState == vz.VirtualMachineStateStopped {
				log.Println("The guest stopped - existing")
				return
			}
		case err := <-errCh:
			log.Printf("start failed: %v", err)
		}
	}
}

func setRawMode(f *os.File) error {
	var attr unix.Termios
	if err := termios.Tcgetattr(f.Fd(), &attr); err != nil {
		return err
	}

	// Put stdin into raw mode, disabling local echo, input canonicalization,
	// and CR-NL mapping.
	attr.Iflag &^= unix.ICRNL
	attr.Lflag &^= unix.ICANON | unix.ECHO

	// reflects the changed settings
	return termios.Tcsetattr(f.Fd(), termios.TCSANOW, &attr)
}
