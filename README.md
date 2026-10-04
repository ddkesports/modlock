# Modlock

**Modlock** is the best tool for hand-writing the logic of [Deadlock] custom
game modes. You write a mod in a high-level language, Modlock compiles it to
WebAssembly, and the game server runs it in a secure sandbox. Every mod speaks
to the game through one common protobuf schema, so each language sees the same
events and calls. Mods deploy to [hyperline.gg], the default backend and
marketplace for custom games.

[Deadlock]: https://store.steampowered.com/app/1422450/Deadlock/
[hyperline.gg]: https://hyperline.gg

```go
package main

import "github.com/paralin/modlock/mod"

func init() {
	mod.Command("hello", func(p mod.Player, args string) {
		p.Chat("Hello from Go!")
	})
}

func main() {}
```

> **Early development.** APIs change without notice. There are no prebuilt
> binaries, so build from source. Go mods work today; TypeScript, JavaScript,
> Lua and Python, the `modlock` command line and `modlock publish` are next.

## Why WebAssembly

- **Safe to share.** A mod runs in a [Wasmtime] sandbox inside the server. It
  has no files, network or environment, only the calls the schema offers. A
  crash, an endless loop or a runaway allocation stops that mod with a log line
  and leaves the match running.
- **Any language.** Anything that compiles to WebAssembly can be a mod. Each
  language gets a small library over the generated protobuf types.
- **Built once.** A mod is a portable `.wasm` file. It does not depend on the
  compiler or source revision of the server that runs it.
- **Fast.** Wasmtime compiles mods to machine code with Cranelift, and the
  server calls them in the game frame, so a mod can decide whether to claim a
  command or change a frame as it happens.

[Wasmtime]: https://wasmtime.dev

## Writing a mod in Go

A Go mod registers its handlers in `init`. [`examples/hello-go`](examples/hello-go)
answers `/hello` in chat and logs the first server frame. Build it
with Go 1.24 or newer:

```sh
GOOS=wasip1 GOARCH=wasm go build -buildmode=c-shared -o hello.wasm ./examples/hello-go
modlock-host --check-plugin --plugin hello.wasm
modlock-host --game-dir <Deadlock installation> --plugin hello.wasm
```

The [`mod`](mod) package offers:

| Call | Effect |
| --- | --- |
| `mod.Command(name, handler)` | Run `handler` when a player types `/name` in chat. |
| `mod.OnFrame(handler)` | Run `handler` once per server frame. |
| `mod.OnStart(handler)` | Run `handler` when the server starts the mod, with the arguments after `--`. |
| `mod.Log(...)` | Write a line to the server log under the mod's name. |
| `mod.ServerCommand(line)` | Run a line at the server console. |
| `player.Chat(text)` | Send server chat to one player. |
| `player.CenterText(text)` | Show text in the middle of one player's screen. |

## How mods reach the game

A mod is a WASI preview 1 reactor module. It imports two functions and exports
one:

| Name | Direction | Meaning |
| --- | --- | --- |
| `modlock.host_call(ptr, len) -> len` | mod to host | Hand the host an encoded `HostRequest`; returns the length of the encoded `HostResponse`. |
| `modlock.host_read(ptr, len)` | mod to host | Copy the host's pending message, an `Event` or a `HostResponse`, into mod memory. |
| `modlock_event(len) -> i64` | host to mod | Deliver an `Event` of `len` bytes, which the mod copies with `host_read`. Returns the address and length of the encoded `EventResult`, packed as `address << 32 \| length`, or zero. |

The messages are in [`proto/modlock/wasm.proto`](proto/modlock/wasm.proto). A
new capability is a new case in a `oneof`; the functions never change. Each
event runs within a time budget, and each mod has a memory limit.

## The framework

The WebAssembly host is built on Modlock's C++ framework, which also serves
[native plugins](#native-plugins):

- **Plugin host.** `modlock-host` launches a listen server or a client, loads
  mods and plugins, and runs them through a fixed `Load`, `Start`, `Tick`,
  `Stop` lifecycle.
- **Engine events.** Subscribe to frames, chat, console commands, combat and
  damage, connections, respawns, and world start and end through
  `modlock::EngineHost`.
- **Entities and rendering.** Create and remove world text, particles, and
  effects; observe and control player pawns; spawn bots; trace rays.
- **Engine interop.** Signature scanning, relative call decoding, virtual table
  slot hooks, schema offsets, and `CEntityKeyValues` construction. Every
  byte signature lives in one table, and `modlock-sigcheck` checks the table
  against the game binaries after an update.
- **Session content.** Precache heroes and resources into the session manifest
  and advertise content addons to connecting clients.
- **Protocols.** Protobuf messages for mods, HUD text, announcements, chat,
  stamina, and camera paths, generated for C++, Go, and TypeScript.
- **Portable tests.** Parsing, dispatch, sandbox, and fixture tests run on macOS
  and Linux without the game.

The game runtime is Windows x64, including Windows builds under Proton.

## Building

Requirements: Go (the version in `go.mod`), CMake 3.24 or newer, and a C++23
compiler. The tests build the Go example mod, so they also need Go.

```sh
git submodule update --init --recursive
go mod download github.com/aperturerobotics/protobuf github.com/aperturerobotics/abseil-cpp

jobs=$(( $(getconf _NPROCESSORS_ONLN) / 2 ))
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel "$jobs"
ctest --test-dir build --output-on-failure
cmake --install build --prefix "$PWD/build/sdk"
```

Protobuf and abseil take a long time to build. Use half the cores, as shown, so
the build leaves the machine usable.

CMake downloads the [Wasmtime C API](https://docs.wasmtime.dev/c-api/) release
for the target platform. Pass `-DMODLOCK_WASMTIME_DIR=<extracted release>` to
build offline. Install puts the Wasmtime library next to `modlock-host`.

- **Windows:** run `scripts/win-build.ps1` from a Visual Studio developer shell.
- **macOS or Linux, targeting Windows:** `scripts/proton-build.sh` cross-builds
  the Windows SDK with Zig.

To build from a source archive without `.git`, pass its commit with
`-DMODLOCK_REVISION=<40-character SHA>`.

## Native plugins

Native C++ plugins extend the host itself: new engine hooks, new host calls for
mods, and framework features. They have full access to the game process and
none of the sandbox's protection. The installed SDK exports `modlock::sdk` through CMake:

```cmake
find_package(Modlock CONFIG REQUIRED)
add_library(my_plugin MODULE my_plugin.cc)
target_link_libraries(my_plugin PRIVATE modlock::sdk)
target_compile_features(my_plugin PRIVATE cxx_std_23)
```

A plugin implements `modlock::Plugin` and exports three functions. `Load`,
`Tick`, and `Stop` are optional overrides:

```cpp
#include "modlock/engine_host.h"
#include "modlock/plugin_library.h"

class MyPlugin final : public modlock::Plugin {
 public:
  explicit MyPlugin(const modlock::PluginContext& context) : engine_(context.engine) {}
  uint32_t InterfaceVersion() const override { return modlock::PluginInterfaceVersion; }
  const char* Name() const override { return "my-plugin"; }
  bool Start() override {
    auto command = engine_->OnCommand([](int32_t, std::string_view text) {
      return text == "hello";  // true claims the command
    });
    if (!command) return false;
    command_ = std::move(*command);
    return true;
  }
  void Stop() override { command_.Reset(); }

 private:
  modlock::EngineHost* engine_;
  modlock::Subscription command_;
};

MODLOCK_PLUGIN_EXPORT modlock::PluginManifest ModlockPluginManifest_v1() {
  return {modlock::PluginInterfaceVersion, MODLOCK_SDK_ABI};
}
MODLOCK_PLUGIN_EXPORT modlock::Plugin* ModlockPluginCreate_v1(const modlock::PluginContext* c) {
  return c ? new MyPlugin(*c) : nullptr;
}
MODLOCK_PLUGIN_EXPORT void ModlockPluginDestroy_v1(modlock::Plugin* p) { delete p; }
```

[`examples/hello`](examples/hello) is a complete plugin that creates a world
text entity, runs a console command, removes the entity before world shutdown,
and exits. Build and run it against the installed SDK:

```sh
cmake -S examples/hello -B build-hello -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$PWD/build/sdk"
cmake --build build-hello --parallel "$jobs"

modlock-host --check-plugin --plugin <hello library>
modlock-host --game-dir <Deadlock installation> --plugin <hello library>
```

| Option | Effect |
| --- | --- |
| `--plugin PATH` | Load a WebAssembly mod (`.wasm`) or a plugin library; repeat for several. |
| `--check-plugin` | Load, start, and stop the plugins without opening game modules. |
| `--game-dir DIR` | Run a listen server from the Deadlock installation at `DIR` (or `DEADLOCK_DIR`). |
| `--map NAME` | Start on `NAME` (default `dl_midtown`). |
| `--hostport PORT` | Serve on UDP `PORT` (default 27067). |
| `--connect ADDRESS` | Run a client that joins `ADDRESS`, with the plugin loaded in the client. |
| `--engine-args ARGS` | Append engine command-line arguments in either role. |
| `-- ARGS` | Pass the remaining arguments to the plugin. |

Clients run with `-insecure` and cannot join VAC-secured servers. See
`modlock-host --help` for every option.

## Plugin contract

**ABI.** Before it creates a plugin, the loader checks that the plugin was built
with the same interface version, SDK source, public headers, compiler, standard
library, build mode, and runtime configuration as the host. A plugin loads only
into the host it was built with, so build and ship the host, SDK, and plugins
together.

**Engine access.** The host loads the engine modules and installs the native
hooks. Plugins call the engine through `PluginContext::engine` and keep the
`Subscription`s it returns. Resetting a subscription removes the callback and
frees what it captured.

**Dispatch.** Callbacks run on the engine thread in the order they were
registered. For a command, damage, or respawn decision, the first callback that
returns true handles it, and later callbacks are not called.

**Lifecycle.** `Load`, `Start`, and `Stop` each run once, in order, with `Tick`
once per frame between `Start` and `Stop`. If startup fails, every plugin that
began starting stops in reverse order, including the one that failed. Remove
live entities in `OnWorldEnding`, before the engine tears down the world, and
drop views of the previous world in `OnWorld`. `Stop` runs after the engine
returns. Each plugin is destroyed inside its own library before that library
unloads. Plugins cannot be reloaded while the host runs.

[Engine access](docs/engine-access.md) covers module lookup, signature
resolution, platform boundaries, and hook lifetime.

## Protocols

Messages live in [`proto/modlock`](proto/modlock) and keep the `modlock` wire
package. `bun run gen` regenerates the Go, TypeScript, and C++ code with the
pinned protobuf toolchain.

## Acknowledgments

Thank you to the Deadlock modding community, whose shared research made this
project possible, and especially to [Deadworks], whose signatures, engine
calling conventions, and startup sequence Modlock learned from. See
[ATTRIBUTION.md](ATTRIBUTION.md) for details.

[Deadworks]: https://github.com/Deadworks-net/deadworks

## License

MIT. See [LICENSE](LICENSE).
