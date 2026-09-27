#include "modlock/gameinterop/mapped_module_image.h"

namespace modlock::gameinterop {

std::expected<MappedModuleImage, std::string> MappedModuleImage::ForModule(std::wstring_view) {
  return std::unexpected("mapped engine modules require the Windows host");
}

std::expected<void*, std::string> ResolveEngineInterface(std::wstring_view,
                                                         std::string_view interface_name) {
  return std::unexpected("interface '" + std::string(interface_name) +
                         "': mapped engine modules require the Windows host");
}

}  // namespace modlock::gameinterop
