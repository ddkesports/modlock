//go:build !wasip1

package mod

import "errors"

// errNotInHost reports a host call made outside modlock-host.
var errNotInHost = errors.New("mods run inside modlock-host; build with GOOS=wasip1 GOARCH=wasm")

// hostExchange fails outside the WebAssembly host, which is the only place a
// mod's calls can reach the game.
func hostExchange([]byte) ([]byte, error) {
	return nil, errNotInHost
}
