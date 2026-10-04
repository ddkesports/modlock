// Package mod writes Modlock mods in Go. A mod registers its handlers in an
// init function; modlock-host compiles the module to a sandbox inside the game
// server and calls the handlers on the server's frame thread, one at a time.
//
//	package main
//
//	import "github.com/paralin/modlock/mod"
//
//	func init() {
//		mod.Command("hello", func(p mod.Player, args string) {
//			p.Chat("Hello from Go!")
//		})
//	}
//
//	func main() {}
//
// Build it with:
//
//	GOOS=wasip1 GOARCH=wasm go build -buildmode=c-shared -o hello.wasm
package mod

import (
	"strings"

	"github.com/paralin/modlock/proto/modlock/wasm"
)

// Frame identifies one server frame.
type Frame struct {
	// Tick is the server tick, or zero when the clock is unavailable.
	Tick uint64
	// Time is the game clock at this frame, in seconds.
	Time float64
}

// handlers holds what the mod registered. A module is exactly one mod, and the
// host enters it through the modlock_event export, which has no receiver, so
// the module keeps one handlers value that init registers into and every event
// reads.
type handlers struct {
	// commands maps each registered command name to its handler.
	commands map[string]func(Player, string)
	// frames run in registration order on every server frame.
	frames []func(Frame)
	// starts run in registration order when the server starts the mod.
	starts []func(args []string)
	// result holds the last encoded EventResult until the host has copied it.
	result []byte
}

// registered is the mod's single set of handlers.
var registered = &handlers{commands: map[string]func(Player, string){}}

// Command calls handler when a player types the console command name. The
// handler receives the text after the name, trimmed of surrounding spaces.
// Registering a name again replaces its handler.
func Command(name string, handler func(p Player, args string)) {
	registered.commands[name] = handler
}

// OnFrame calls handler once per server frame.
func OnFrame(handler func(f Frame)) {
	registered.frames = append(registered.frames, handler)
}

// OnStart calls handler once when the server starts the mod, with the
// arguments that follow -- on the modlock-host command line.
func OnStart(handler func(args []string)) {
	registered.starts = append(registered.starts, handler)
}

// dispatch delivers one event to the registered handlers and returns the
// answer the host expects for it.
func dispatch(event *wasm.Event) *wasm.EventResult {
	switch body := event.GetBody().(type) {
	case *wasm.Event_Start:
		return start(body.Start)
	case *wasm.Event_Frame:
		frame := Frame{Tick: body.Frame.GetTick(), Time: body.Frame.GetTimeSeconds()}
		for _, handler := range registered.frames {
			handler(frame)
		}
	case *wasm.Event_Command:
		return command(body.Command)
	}
	return &wasm.EventResult{}
}

// start runs the start handlers and asks for frames when any were registered.
func start(event *wasm.StartEvent) *wasm.EventResult {
	for _, handler := range registered.starts {
		handler(event.GetArgs())
	}
	frames := len(registered.frames) != 0
	return &wasm.EventResult{Body: &wasm.EventResult_Start{Start: &wasm.StartResult{Frames: frames}}}
}

// command runs the handler registered for the command's name and claims the
// command when one exists.
func command(event *wasm.CommandEvent) *wasm.EventResult {
	// Split the command line into its name and arguments.
	line := strings.TrimSpace(event.GetLine())
	name, args, _ := strings.Cut(line, " ")
	handler, ok := registered.commands[name]
	if !ok {
		return &wasm.EventResult{}
	}

	// Run the handler and claim the command.
	handler(Player{Slot: event.GetSlot()}, strings.TrimSpace(args))
	return &wasm.EventResult{Body: &wasm.EventResult_Command{Command: &wasm.CommandResult{Claimed: true}}}
}
