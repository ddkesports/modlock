//go:build wasip1

package mod

import (
	"runtime"
	"unsafe"

	"github.com/paralin/modlock/proto/modlock/wasm"
)

// hostCall hands the host one encoded HostRequest and returns the length of
// the encoded HostResponse, which hostRead then copies out.
//
//go:wasmimport modlock host_call
func hostCall(data unsafe.Pointer, size uint32) uint32

// hostRead copies the host's pending message, an Event or a HostResponse, into
// data, which holds exactly the length the host announced.
//
//go:wasmimport modlock host_read
func hostRead(data unsafe.Pointer, size uint32)

// modlockEvent receives one encoded Event of size bytes, dispatches it, and
// returns the encoded EventResult's address in the high 32 bits and its
// length in the low 32 bits.
//
//go:wasmexport modlock_event
func modlockEvent(size uint32) uint64 {
	// Copy the pending event out of the host and decode it.
	data := make([]byte, size)
	hostRead(unsafe.Pointer(unsafe.SliceData(data)), size)
	event := &wasm.Event{}
	if err := event.UnmarshalVT(data); err != nil {
		Log("modlock: cannot decode event: ", err)
		return 0
	}

	// Dispatch the event and keep its encoded answer for the host.
	result, err := dispatch(event).MarshalVT()
	if err != nil || len(result) == 0 {
		return 0
	}
	registered.result = result
	address := uint64(uintptr(unsafe.Pointer(unsafe.SliceData(result))))
	return address<<32 | uint64(len(result))
}

// hostExchange sends an encoded request to the host and returns its encoded
// response.
func hostExchange(request []byte) ([]byte, error) {
	// Hand the request to the host, which answers with the response length.
	size := hostCall(unsafe.Pointer(unsafe.SliceData(request)), uint32(len(request)))
	runtime.KeepAlive(request)

	// Copy the response out of the host.
	response := make([]byte, size)
	if size != 0 {
		hostRead(unsafe.Pointer(unsafe.SliceData(response)), size)
	}
	return response, nil
}
