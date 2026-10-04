package mod

import (
	"errors"
	"fmt"

	"github.com/paralin/modlock/proto/modlock/wasm"
)

// Log writes one line to the server log under the mod's name. Arguments are
// formatted as by fmt.Sprint.
func Log(args ...any) {
	message := fmt.Sprint(args...)
	_ = call(&wasm.HostRequest{Body: &wasm.HostRequest_Log{Log: &wasm.LogRequest{Message: message}}})
}

// ServerCommand runs one line at the server console, as if typed there.
func ServerCommand(command string) error {
	return call(&wasm.HostRequest{Body: &wasm.HostRequest_ServerCommand{ServerCommand: &wasm.ServerCommandRequest{Command: command}}})
}

// call sends one request to the host and returns the host's error, if any.
func call(request *wasm.HostRequest) error {
	// Encode the request for the host boundary.
	data, err := request.MarshalVT()
	if err != nil {
		return err
	}

	// Exchange the request for the host's response.
	data, err = hostExchange(data)
	if err != nil {
		return err
	}

	// Decode the response and report its error.
	response := &wasm.HostResponse{}
	if err := response.UnmarshalVT(data); err != nil {
		return err
	}
	if message := response.GetError(); message != "" {
		return errors.New(message)
	}
	return nil
}
