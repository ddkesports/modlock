#pragma once

#include <expected>
#include <filesystem>
#include <string>
#include <vector>

#include "modlock/export.h"
#include "modlock/host_app/game_paths.h"

namespace modlock::host_app {

// LoadedModule owns one loaded game module for the process lifetime.
class MODLOCK_API LoadedModule {
 public:
  virtual ~LoadedModule() = default;
};

// ModuleLoader is the seam between the host app and the OS library loader.
// The Windows build binds LoadLibraryW; tests bind a recording fake so the
// load order and diagnostics are pinned without any real game binaries.
class MODLOCK_API ModuleLoader {
 public:
  virtual ~ModuleLoader() = default;

  // Load maps the library into the process. An error names the path and the
  // OS reason.
  [[nodiscard]] virtual std::expected<std::unique_ptr<LoadedModule>, std::string> Load(
      const std::filesystem::path& path) = 0;
};

// PlannedLoad is one step of the fixed module sequence.
struct PlannedLoad {
  std::string name;
  std::filesystem::path path;
};

// PlanModuleLoads returns the required load order: engine2 before the game
// server modules, which live under the citadel tree.
[[nodiscard]] MODLOCK_API std::vector<PlannedLoad> PlanModuleLoads(const GamePaths& paths);

}  // namespace modlock::host_app
