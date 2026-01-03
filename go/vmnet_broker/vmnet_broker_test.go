package vmnet_broker_test

import (
	"errors"
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

func TestError(t *testing.T) {

	t.Run("known status", func(t *testing.T) {
		// Assume the constants are correct.
		notFoundStatus := uint32(vmnet_broker.ErrNotFound)

		notFound := vmnet_broker.Error(notFoundStatus)

		// Compare to known error.
		if notFound != vmnet_broker.ErrNotFound {
			t.Errorf("Expected %+v, got %+v", vmnet_broker.ErrNotFound, notFound)
		}
		if notFound == vmnet_broker.ErrInternalError {
			t.Errorf("Not found is equal to internal error")
		}

		// Match with errors.Is()
		if !errors.Is(notFound, vmnet_broker.ErrNotFound) {
			t.Errorf("Expected %+v, got %+v", vmnet_broker.ErrNotFound, notFound)
		}
		if errors.Is(notFound, vmnet_broker.ErrInternalError) {
			t.Errorf("Not found is internal error")
		}

		// Error message matches the C library output.
		cLibraryMessage := "Network name not found"
		actualMessage := notFound.Error()
		if actualMessage != cLibraryMessage {
			t.Errorf("Expected message '%s', got '%s", cLibraryMessage, actualMessage)
		}
	})

	t.Run("unknown status", func(t *testing.T) {
		unknownStatus := uint32(999)
		unknownError := vmnet_broker.Error(unknownStatus)

		// Preserve the unknown status value.
		if uint32(unknownError) != unknownStatus {
			t.Errorf("Expected %+v, got %+v", unknownStatus, uint32(unknownError))
		}

		// Not mapped to internal error
		if unknownError == vmnet_broker.ErrInternalError {
			t.Errorf("Unknown error matches known error")
		}
		if errors.Is(unknownError, vmnet_broker.ErrInternalError) {
			t.Errorf("Unknown error is known error")
		}

		// Error message matches the C library output.
		cLibraryMessage := "(unknown status)"
		actualMessage := unknownError.Error()
		if actualMessage != cLibraryMessage {
			t.Errorf("Expected message '%s', got '%s", cLibraryMessage, actualMessage)
		}
	})
}
