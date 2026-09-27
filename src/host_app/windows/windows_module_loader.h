#pragma once

#include "modlock/host_app/module_loader.h"

namespace modlock::host_app {

// WindowsModuleLoader maps libraries with LoadLibraryW.
class WindowsModuleLoader final : public ModuleLoader {
 public:
  [[nodiscard]] std::expected<std::unique_ptr<LoadedModule>, std::string> Load(
      const std::filesystem::path& path) override;
};

}  // namespace modlock::host_app
