#pragma once

#include <expected>
#include <filesystem>
#include <memory>
#include <string>

#include "modlock/host_app/module_loader.h"

namespace modlock::host_app {

// StubModuleLoader refuses every load with a clear message: real module
// mapping only happens in the Windows build.
class StubModuleLoader final : public ModuleLoader {
 public:
  [[nodiscard]] std::expected<std::unique_ptr<LoadedModule>, std::string> Load(
      const std::filesystem::path& path) override {
    return std::unexpected(
        "module loading requires the Windows host build (attempted: " + path.string() + ")");
  }
};

}  // namespace modlock::host_app
