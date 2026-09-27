#include "modlock/gameinterop/player_identity.h"

#include <cstring>

#include "modlock/gameinterop/signature.h"

namespace modlock::gameinterop {
namespace {

// DecodeDisp32 reads the little-endian 32-bit displacement that follows the
// prefix_len matched accessor bytes at hit inside window.
std::expected<int32_t, std::string> DecodeDisp32(std::span<const uint8_t> window, size_t hit,
                                                 size_t prefix_len) {
  if (hit + prefix_len + sizeof(int32_t) > window.size()) {
    return std::unexpected("m_steamID accessor truncated by the window boundary");
  }
  int32_t disp = 0;
  std::memcpy(&disp, window.data() + hit + prefix_len, sizeof(disp));
  return disp;
}

}  // namespace

std::expected<size_t, std::string> ResolveSteamIdOffset(std::span<const uint8_t> window) {
  auto parsed = ParseSignature("player-controller.m-steam-id", kSteamIdAccessorPattern);
  if (!parsed) {
    return std::unexpected(parsed.error());
  }
  const size_t prefix_len = parsed->bytes.size();
  const auto hits = SignatureScan(window, *parsed);
  if (hits.empty()) {
    return std::unexpected("m_steamID accessor not found in the recorded window");
  }
  if (hits.size() > 1) {
    return std::unexpected("m_steamID accessor matched " + std::to_string(hits.size()) +
                           " times in the recorded window");
  }
  auto disp = DecodeDisp32(window, hits.front(), prefix_len);
  if (!disp) {
    return std::unexpected(disp.error());
  }
  if (*disp <= 0) {
    return std::unexpected("m_steamID displacement is not a positive field offset");
  }
  return static_cast<size_t>(*disp);
}

std::expected<uint64_t, std::string> ReadSteamId(std::span<const uint8_t> controller,
                                                 size_t field_offset) {
  if (controller.size() < field_offset + sizeof(uint64_t)) {
    return std::unexpected("controller blob of " + std::to_string(controller.size()) +
                           " bytes cannot hold m_steamID at offset " +
                           std::to_string(field_offset));
  }
  uint64_t steam_id = 0;
  std::memcpy(&steam_id, controller.data() + field_offset, sizeof(steam_id));
  return steam_id;
}

std::expected<std::vector<ControllerIdentity>, std::string> ReadControllerIdentities(
    std::span<const uint8_t> window, std::span<const SlotWindow> slots) {
  auto field_offset = ResolveSteamIdOffset(window);
  if (!field_offset) {
    return std::unexpected(field_offset.error());
  }
  std::vector<ControllerIdentity> identities;
  identities.reserve(slots.size());
  for (const auto& slot : slots) {
    auto steam_id = ReadSteamId(slot.bytes, *field_offset);
    if (!steam_id) {
      return std::unexpected("slot " + std::to_string(slot.slot) + ": " + steam_id.error());
    }
    identities.push_back(ControllerIdentity{.slot = slot.slot, .steam_id = *steam_id});
  }
  return identities;
}

}  // namespace modlock::gameinterop
