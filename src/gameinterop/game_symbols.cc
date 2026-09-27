#include "modlock/gameinterop/game_symbols.h"

#include <cstring>

#include "modlock/gameinterop/signature.h"

namespace modlock::gameinterop {
namespace {

// UniqueOffset resolves one symbol or instruction anchor inside the mapped
// image. Every address decoder requires the same unambiguous match.
std::expected<size_t, std::string> UniqueOffset(const ModuleImage& image, std::string_view id,
                                                std::string_view pattern) {
  if (pattern.empty()) {
    return std::unexpected("symbol '" + std::string(id) +
                           "' has no recorded pattern; it resolves by documented non-scan means");
  }
  auto parsed = ParseSignature(std::string(id), pattern);
  if (!parsed) {
    return std::unexpected(parsed.error());
  }
  const auto hits = SignatureScan(image.image_bytes(), *parsed);
  if (hits.empty()) {
    return std::unexpected("symbol '" + std::string(id) + "' not found in the module image");
  }
  if (hits.size() > 1) {
    return std::unexpected("symbol '" + std::string(id) + "' matched " +
                           std::to_string(hits.size()) + " times in the module image");
  }
  return hits.front();
}

}  // namespace

std::expected<void*, std::string> ResolveScannedSymbol(const ModuleImage& image,
                                                       std::string_view id,
                                                       std::string_view pattern) {
  const auto offset = UniqueOffset(image, id, pattern);
  if (!offset) return std::unexpected(offset.error());
  return reinterpret_cast<void*>(image.base() + *offset);
}

std::expected<void*, std::string> DecodeRelativeCall(const ModuleImage& image, std::string_view id,
                                                     std::string_view pattern, size_t call_delta) {
  const auto offset = UniqueOffset(image, id, pattern);
  if (!offset) return std::unexpected(offset.error());

  // Validate the complete instruction before adding the caller's delta.
  const size_t remaining = image.image_bytes().size() - *offset;
  if (call_delta > remaining || remaining - call_delta < 5) {
    return std::unexpected("anchor '" + std::string(id) + "' call truncated by the image boundary");
  }
  const size_t site = *offset + call_delta;
  if (image.image_bytes()[site] != 0xE8)
    return std::unexpected("anchor '" + std::string(id) + "' is not a relative call");
  std::int32_t rel32 = 0;
  std::memcpy(&rel32, image.image_bytes().data() + site + 1, sizeof(rel32));
  const std::uintptr_t target = image.base() + static_cast<std::uintptr_t>(site) + 5 +
                                static_cast<std::uintptr_t>(static_cast<std::int64_t>(rel32));
  return reinterpret_cast<void*>(target);
}

std::expected<void*, std::string> DecodeRelativeLea(const ModuleImage& image, std::string_view id,
                                                    std::string_view pattern, size_t lea_delta) {
  const auto offset = UniqueOffset(image, id, pattern);
  if (!offset) return std::unexpected(offset.error());

  // Validate before addition so even a wrapping delta cannot escape the image.
  const size_t remaining = image.image_bytes().size() - *offset;
  if (lea_delta > remaining || remaining - lea_delta < 7) {
    return std::unexpected("anchor '" + std::string(id) + "' lea truncated by the image boundary");
  }
  const size_t site = *offset + lea_delta;
  const size_t rel32_at = site + 3;
  std::int32_t rel32 = 0;
  std::memcpy(&rel32, image.image_bytes().data() + rel32_at, sizeof(rel32));
  const std::uintptr_t target = image.base() + static_cast<std::uintptr_t>(site) + 7 +
                                static_cast<std::uintptr_t>(static_cast<std::int64_t>(rel32));
  return reinterpret_cast<void*>(target);
}

}  // namespace modlock::gameinterop
