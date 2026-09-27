#pragma once

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "modlock/export.h"

namespace modlock::gameinterop {

// SteamIdAccessorPattern matches the server.dll code that reads the
// m_steamID schema field of CBasePlayerController.
// TODO: the live pattern is unconfirmed until a live capture records
// a real window from the current build; this placeholder pins the expected
// instruction shape (mov r64, [r64 + disp32]) so the captured fixture drops
// straight into the fixture-gated tests.
inline constexpr std::string_view kSteamIdAccessorPattern = "48 8B 83 ?? ?? ?? ??";

// ControllerIdentity is one attributed player-controller slot.
struct ControllerIdentity {
  int32_t slot = 0;
  uint64_t steam_id = 0;

  friend bool operator==(const ControllerIdentity&, const ControllerIdentity&) = default;
};

// SlotWindow pairs a player slot with the recorded bytes of its
// CBasePlayerController entity.
struct SlotWindow {
  int32_t slot = 0;
  std::span<const uint8_t> bytes;
};

// ResolveSteamIdOffset scans a recorded code window for
// SteamIdAccessorPattern and decodes the matched instruction's trailing
// 4-byte little-endian displacement: the byte offset of m_steamID within
// CBasePlayerController. Exactly one hit must match; zero hits, multiple
// hits, or a displacement truncated by the window boundary are errors.
[[nodiscard]] MODLOCK_API std::expected<size_t, std::string> ResolveSteamIdOffset(
    std::span<const uint8_t> window);

// ReadSteamId reads the little-endian uint64 SteamID64 stored at field_offset
// inside a recorded controller-entity blob. A blob shorter than
// field_offset + 8 is an error. An all-zero field is a valid read: the slot
// has no authenticated identity.
[[nodiscard]] MODLOCK_API std::expected<uint64_t, std::string> ReadSteamId(
    std::span<const uint8_t> controller, size_t field_offset);

// ReadControllerIdentities resolves the m_steamID field offset once from the
// recorded code window, then reads every recorded slot against it. The first
// failing slot names the slot in the error.
[[nodiscard]] MODLOCK_API std::expected<std::vector<ControllerIdentity>, std::string>
ReadControllerIdentities(std::span<const uint8_t> window, std::span<const SlotWindow> slots);

}  // namespace modlock::gameinterop
