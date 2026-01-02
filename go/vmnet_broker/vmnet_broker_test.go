package vmnet_broker_test

import (
	"testing"

	"github.com/nirs/vmnet-broker/go/vmnet_broker"
)

func TestAcquireNetwork(t *testing.T) {
	// Note: These tests requires installation of the vmnet-broker launchd daemon.

	t.Run("ValidNetwork", func(t *testing.T) {
		s, err := vmnet_broker.AcquireNetwork("default")
		if err != nil {
			t.Fatalf("Expected success for 'default' network, got error: %v", err)
		}
		if s == nil {
			t.Fatal("Expected valid serialization, got nil")
		}
		if s.Raw() == nil {
			t.Fatal("Expected valid serialization pointer, got nil")
		}
	})
}
