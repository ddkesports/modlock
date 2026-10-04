package mod

import "github.com/paralin/modlock/proto/modlock/wasm"

// Player addresses one connected player by their server slot.
type Player struct {
	// Slot identifies the player on the server.
	Slot int32
}

// Chat sends a server chat line to the player.
func (p Player) Chat(text string) error {
	return call(&wasm.HostRequest{Body: &wasm.HostRequest_Chat{Chat: &wasm.ChatRequest{Slot: p.Slot, Text: text}}})
}

// CenterText shows text in the middle of the player's screen until it is
// replaced. Empty text clears it.
func (p Player) CenterText(text string) error {
	return call(&wasm.HostRequest{Body: &wasm.HostRequest_CenterText{CenterText: &wasm.CenterTextRequest{Slot: p.Slot, Text: text}}})
}
