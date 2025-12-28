package vmnet_broker

/*
#cgo LDFLAGS: -L${SRCDIR}/../.. -lvmnet-broker
#include "vmnet-broker.h"
#include <stdlib.h>
#include <xpc/xpc.h>
*/
import "C"
import (
	"errors"
	"runtime"
	"unsafe"
)

// Serialization wraps the C xpc_object_t for Go-side lifecycle management.
type Serialization struct {
	ptr C.xpc_object_t
}

// StartSession initiates a session with the vmnet-broker.  It returns a
// Serialization on success, or an error on failure.
func StartSession(networkName string) (*Serialization, error) {
	cName := C.CString(networkName)
	defer C.free(unsafe.Pointer(cName))

	var status C.vmnet_broker_return_t
	obj := C.vmnet_broker_start_session(cName, &status)
	if obj == nil {
		errMsg := C.GoString(C.vmnet_broker_strerror(status))
		return nil, errors.New(errMsg)
	}

	serialization := &Serialization{ptr: obj}

	// The serialization is retained by the broker. We must release it when
	// done.
	runtime.SetFinalizer(serialization, func(s *Serialization) {
		C.xpc_release(s.ptr)
	})

	return serialization, nil
}

// Raw returns the underlying xpc_object_t as [unsafe.Pointer].
// This pointer is managed by a Go finalizer and remains valid
// as long as the Serialization object is reachable.
func (s *Serialization) Raw() unsafe.Pointer {
	return unsafe.Pointer(s.ptr)
}
