#pragma once

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>

#include "modlock/export.h"

namespace modlock::gameinterop {

// ModuleImage is one game module mapped into this process. Implementations
// supply the load base and the mapped image bytes; signature scans run over
// the mapped bytes and hits convert to absolute addresses through the base.
class MODLOCK_API ModuleImage {
 public:
  virtual ~ModuleImage() = default;

  // base returns the runtime address of byte zero. The module outlives the view.
  virtual std::uintptr_t base() const = 0;

  // image_bytes returns the borrowed mapped bytes available to the scanner.
  virtual std::span<const uint8_t> image_bytes() const = 0;
};

// ResolveScannedSymbol scans image for the recorded pattern and returns
// base + hit for its unique match. Zero or multiple matches are errors naming
// the probe id; an empty pattern is an error because such symbols resolve by
// another documented means (vtable slot, schema write) and must not be
// scanned.
[[nodiscard]] MODLOCK_API std::expected<void*, std::string> ResolveScannedSymbol(
    const ModuleImage& image, std::string_view id, std::string_view pattern);

// DecodeRelativeCall resolves a recorded anchor whose target function sits
// behind an E8 relative call at call_delta bytes from the matched pattern:
// target = base + hit + call_delta + 5 + rel32. Used where the database
// records a caller as the anchor (hero-definition-manager.get-manager-anchor).
[[nodiscard]] MODLOCK_API std::expected<void*, std::string> DecodeRelativeCall(
    const ModuleImage& image, std::string_view id, std::string_view pattern, size_t call_delta);

// DecodeRelativeLea resolves a recorded anchor whose data address sits behind
// a seven-byte REX.W LEA at lea_delta bytes from the matched pattern:
// target = base + hit + lea_delta + 7 + rel32, with rel32 at lea_delta + 3.
// Used where the database records a referencing instruction as the anchor
// (game-rules.precache-global).
[[nodiscard]] MODLOCK_API std::expected<void*, std::string> DecodeRelativeLea(
    const ModuleImage& image, std::string_view id, std::string_view pattern, size_t lea_delta);

}  // namespace modlock::gameinterop
