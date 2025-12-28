package main

import (
	"log"

	"github.com/Code-Hex/vz/v3/pkg/vmnet"
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
}
