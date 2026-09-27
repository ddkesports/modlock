#include "modlock/gameinterop/game_symbols.h"

#include <algorithm>
#include <cstring>

#include "modlock/gameinterop/signature.h"

namespace modlock::gameinterop {
namespace {

// UniqueOffset returns the offset of the pattern's single match in image.
std::expected<size_t, std::string> UniqueOffset(const ModuleImage& image,
                                                const GameSignature& signature) {
  const std::string id(signature.id);
  auto parsed = ParseSignature(id, signature.pattern);
  if (!parsed) return std::unexpected(parsed.error());
  if (parsed->bytes.empty()) return std::unexpected("signature '" + id + "' has no pattern");
  const auto hits = SignatureScan(image.image_bytes(), *parsed);
  if (hits.empty()) return std::unexpected("signature '" + id + "' not found in the module image");
  if (hits.size() > 1) {
    return std::unexpected("signature '" + id + "' matched " + std::to_string(hits.size()) +
                           " times in the module image");
  }
  return hits.front();
}

}  // namespace

std::string_view GameModuleFile(GameModule module) {
  switch (module) {
    case GameModule::kServer:
      return "server.dll";
    case GameModule::kClient:
      return "client.dll";
    case GameModule::kEngine:
      return "engine2.dll";
  }
  return {};
}

const GameSignature* FindGameSignature(std::string_view id) {
  const auto signatures = GameSignatures();
  const auto found = std::ranges::find(signatures, id, &GameSignature::id);
  return found == signatures.end() ? nullptr : &*found;
}

std::expected<void*, std::string> ResolveSignature(const ModuleImage& image,
                                                   const GameSignature& signature) {
  const auto offset = UniqueOffset(image, signature);
  if (!offset) return std::unexpected(offset.error());
  if (signature.target == SignatureTarget::kMatch)
    return reinterpret_cast<void*>(image.base() + *offset);

  // Validate the complete instruction before adding the delta, so even a
  // wrapping delta cannot escape the image.
  const auto bytes = image.image_bytes();
  const size_t length = signature.target == SignatureTarget::kCall ? 5 : 7;
  const size_t remaining = bytes.size() - *offset;
  if (signature.delta > remaining || remaining - signature.delta < length) {
    return std::unexpected("signature '" + std::string(signature.id) +
                           "' instruction truncated by the image boundary");
  }
  const size_t site = *offset + signature.delta;
  if (signature.target == SignatureTarget::kCall && bytes[site] != 0xE8) {
    return std::unexpected("signature '" + std::string(signature.id) +
                           "' does not reach a relative call");
  }
  std::int32_t rel32 = 0;
  std::memcpy(&rel32, bytes.data() + site + length - sizeof(rel32), sizeof(rel32));
  const std::uintptr_t target = image.base() + static_cast<std::uintptr_t>(site) + length +
                                static_cast<std::uintptr_t>(static_cast<std::int64_t>(rel32));
  return reinterpret_cast<void*>(target);
}

std::expected<void*, std::string> ResolveSignature(const ModuleImage& image, std::string_view id) {
  const auto* signature = FindGameSignature(id);
  if (signature == nullptr)
    return std::unexpected("signature '" + std::string(id) + "' is not recorded");
  return ResolveSignature(image, *signature);
}

}  // namespace modlock::gameinterop
