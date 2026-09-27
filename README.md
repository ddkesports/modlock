# Modlock

**Modlock** is a native C++ modding framework for [Deadlock]. It runs the game
server or client inside its own host process, gives plugins typed access to the
engine, and loads plugins through a versioned C ABI. It is written in C++23 and
has no managed runtime.

[Deadlock]: https://store.steampowered.com/app/1422450/Deadlock/

Modlock finds engine functions, installs the native hooks, and calls plugins on
the engine thread. Plugins contain the mod itself: gameplay rules, content, and
match results.

> **Early development.** APIs change without notice. There are no prebuilt
> binaries, so build from source.

## Features

- **Plugin host.** `modlock-host` launches a listen server or a client, loads
  plugins, and runs them through a fixed `Load`, `Start`, `Tick`, `Stop`
  lifecycle.
- **Engine events.** Subscribe to frames, chat, console commands, combat and
  damage, connections, respawns, and world start and end through
  `modlock::EngineHost`.
- **Entities and rendering.** Create and remove world text, particles, and
  effects; observe and control player pawns; spawn bots; trace rays.
- **Engine interop.** Signature scanning, relative call decoding, virtual table
  slot hooks, schema offsets, and `CEntityKeyValues` construction. Each
  signature sits next to the code that uses it.
- **Session content.** Precache heroes and resources into the session manifest
  and advertise content addons to connecting clients.
- **Protocols.** Protobuf messages for HUD text, announcements, chat, stamina,
  and camera paths, generated for C++, Go, and TypeScript.
- **Portable tests.** Parsing, dispatch, and fixture tests run on macOS and
  Linux without the game.

The game runtime is Windows x64, including Windows builds under Proton.

## Building

Requirements: Go (the version in `go.mod`), CMake 3.24 or newer, and a C++23
compiler.

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

- **Windows:** run `scripts/win-build.ps1` from a Visual Studio developer shell.
- **macOS or Linux, targeting Windows:** `scripts/proton-build.sh` cross-builds
  the Windows SDK with Zig.

To build from a source archive without `.git`, pass its commit with
`-DMODLOCK_REVISION=<40-character SHA>`.

## Writing a plugin

The installed SDK exports `modlock::sdk` through CMake:

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
| `--plugin PATH` | Load a plugin library; repeat for several. |
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
