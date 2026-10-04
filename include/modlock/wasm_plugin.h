#pragma once

#include <expected>
#include <filesystem>
#include <memory>
#include <string>

#include "modlock/export.h"
#include "modlock/plugin.h"
#include "modlock/plugin_library.h"

namespace modlock {

// LoadWasmPlugin compiles a WebAssembly mod, runs its initialization in a
// sandbox, and returns a plugin that delivers server frames and player
// commands to it. The mod reaches the game only through the host requests in
// proto/modlock/wasm.proto. A trap or an exhausted time or memory budget stops
// that mod with a log line; the server keeps running. The plugin is named for
// the file's stem.
[[nodiscard]] MODLOCK_API std::expected<std::unique_ptr<Plugin>, std::string> LoadWasmPlugin(
    const std::filesystem::path& path, const PluginContext& context);

}  // namespace modlock
