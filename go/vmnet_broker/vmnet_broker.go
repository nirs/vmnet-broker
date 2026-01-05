// SPDX-FileCopyrightText: The vmnet-broker authors
// SPDX-License-Identifier: Apache-2.0

//go:build darwin

// package vmnet_broker interacts with the vmnet-broker service.
package vmnet_broker

/*
#cgo CFLAGS: -I${SRCDIR}/../.. -Wall -O2
#include "vmnet-broker.h"
#include <stdlib.h>
*/
import "C"
import (
	"runtime"
	"unsafe"
)

// Error is an error returned by the vmnet-broker service.
type Error C.vmnet_broker_return_t

const (
	// Known error constants mapped from the C library.
	ErrXPCFailure     = Error(C.VMNET_BROKER_XPC_FAILURE)
	ErrInvalidReply   = Error(C.VMNET_BROKER_INVALID_REPLY)
	ErrNotAllowed     = Error(C.VMNET_BROKER_NOT_ALLOWED)
	ErrInvalidRequest = Error(C.VMNET_BROKER_INVALID_REQUEST)
	ErrNotFound       = Error(C.VMNET_BROKER_NOT_FOUND)
	ErrCreateFailure  = Error(C.VMNET_BROKER_CREATE_FAILURE)
	ErrInternalError  = Error(C.VMNET_BROKER_INTERNAL_ERROR)
)

// Error returns a message describing the error, retrieved from the C library.
func (e Error) Error() string {
	status := C.vmnet_broker_return_t(e)
	return C.GoString(C.vmnet_broker_strerror(status))
}

// Serialization wraps the C xpc_object_t for Go-side lifecycle management.
type Serialization struct {
	ptr C.xpc_object_t
}

// AcquireNetwork Acquires a shared lock on a configured network, instantiating
// it if necessary.
//
// The specified `networkName` must exist in the broker's configuration. This
// function retrieves a reference to the network if it already exists, or
// instantiates it if needed.
//
// The shared lock ensures the network remains active as long as the calling
// process is using it. The lock is automatically released when the process
// terminates.
func AcquireNetwork(networkName string) (*Serialization, error) {
	cName := C.CString(networkName)
	defer C.free(unsafe.Pointer(cName))

	var status C.vmnet_broker_return_t
	obj := C.vmnet_broker_acquire_network(cName, &status)
	if obj == nil {
		return nil, Error(status)
	}

	serialization := &Serialization{ptr: obj}

	// Release obj when it becomes unreachable.
	runtime.AddCleanup(serialization, func(obj C.xpc_object_t) {
		C.xpc_release(obj)
	}, obj)

	return serialization, nil
}

// Raw returns the underlying xpc_object_t as [unsafe.Pointer].
// This pointer is managed by a Go cleanup and remains valid
// as long as the Serialization object is reachable.
func (s *Serialization) Raw() unsafe.Pointer {
	return unsafe.Pointer(s.ptr)
}
