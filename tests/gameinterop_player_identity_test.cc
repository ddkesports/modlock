// Contract tests for the typed player-controller identity accessor: field-
// offset resolution, little-endian SteamID reads, and per-slot identity
// collection, all against synthetic recorded windows. Live offsets are
// unknown until a live capture (see the TODO at the bottom); no process
// access anywhere.
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "modlock/gameinterop/player_identity.h"
#include "modlock/gameinterop/signature.h"

namespace {

using modlock::gameinterop::ControllerIdentity;
using modlock::gameinterop::kSteamIdAccessorPattern;
using modlock::gameinterop::ReadControllerIdentities;
using modlock::gameinterop::ReadSteamId;
using modlock::gameinterop::ResolveSteamIdOffset;
using modlock::gameinterop::SlotWindow;

constexpr uint64_t kKnownSteamId = 76561199000000042ull;

// A window of filler bytes around one concrete instance of the accessor
// pattern carrying disp32 displacement.
std::vector<uint8_t> AccessorWindow(int32_t disp, size_t lead_fill = 16, size_t tail_fill = 16) {
  const auto parsed = modlock::gameinterop::ParseSignature("test", kSteamIdAccessorPattern);
  std::vector<uint8_t> window(lead_fill, 0xCC);
  const auto& prefix = parsed->bytes;
  window.insert(window.end(), prefix.begin(), prefix.end());
  for (size_t i = 0; i < sizeof(disp); ++i) {
    window.push_back(static_cast<uint8_t>((static_cast<uint32_t>(disp) >> (8 * i)) & 0xFF));
  }
  window.insert(window.end(), tail_fill, 0xCC);
  return window;
}

TEST(PlayerIdentityTest, ResolvesFieldOffsetFromSyntheticWindow) {
  const auto window = AccessorWindow(0x9D8);
  const auto offset = ResolveSteamIdOffset(window);
  ASSERT_TRUE(offset.has_value()) << offset.error();
  EXPECT_EQ(*offset, 0x9D8u);
}

TEST(PlayerIdentityTest, OffsetResolutionRejectsMissingAmbiguousAndTruncatedHits) {
  // Missing: filler only.
  EXPECT_FALSE(ResolveSteamIdOffset(std::vector<uint8_t>(64, 0xCC)).has_value());

  // Ambiguous: the same accessor twice.
  auto doubled = AccessorWindow(0x10);
  const auto single = AccessorWindow(0x20);
  doubled.insert(doubled.end(), single.begin(), single.end());
  EXPECT_FALSE(ResolveSteamIdOffset(doubled).has_value());

  // Truncated: the displacement runs past the window boundary.
  const auto parsed = modlock::gameinterop::ParseSignature("test", kSteamIdAccessorPattern);
  std::vector<uint8_t> truncated(parsed->bytes.begin(), parsed->bytes.end());
  truncated.push_back(0x08);
  truncated.push_back(0x09);
  EXPECT_FALSE(ResolveSteamIdOffset(truncated).has_value());
}

TEST(PlayerIdentityTest, ReadsLittleEndianSteamIdAtFieldOffset) {
  constexpr size_t kFieldOffset = 0x9D8;
  std::vector<uint8_t> controller(kFieldOffset + 8, 0x00);
  for (size_t i = 0; i < sizeof(kKnownSteamId); ++i) {
    controller[kFieldOffset + i] = static_cast<uint8_t>((kKnownSteamId >> (8 * i)) & 0xFF);
  }
  const auto steam_id = ReadSteamId(controller, kFieldOffset);
  ASSERT_TRUE(steam_id.has_value()) << steam_id.error();
  EXPECT_EQ(*steam_id, kKnownSteamId);

  // The blob must actually hold the field.
  EXPECT_FALSE(ReadSteamId(std::span(controller).first(kFieldOffset), kFieldOffset).has_value());

  // All-zero fields are valid reads naming an unauthenticated slot.
  auto anonymous = ReadSteamId(std::vector<uint8_t>(kFieldOffset + 8, 0x00), kFieldOffset);
  ASSERT_TRUE(anonymous.has_value());
  EXPECT_EQ(*anonymous, 0u);
}

TEST(PlayerIdentityTest, ReadsEverySlotAgainstOneResolvedOffset) {
  const auto window = AccessorWindow(0x48);

  std::vector<uint8_t> attributed(0x50, 0x00);
  for (size_t i = 0; i < sizeof(kKnownSteamId); ++i) {
    attributed[0x48 + i] = static_cast<uint8_t>((kKnownSteamId >> (8 * i)) & 0xFF);
  }

  const std::vector slots = {SlotWindow{.slot = 1, .bytes = attributed},
                             SlotWindow{.slot = 2, .bytes = std::vector<uint8_t>(0x50, 0x00)}};
  const auto identities = ReadControllerIdentities(window, slots);
  ASSERT_TRUE(identities.has_value()) << identities.error();
  ASSERT_EQ(identities->size(), 2u);
  EXPECT_EQ((*identities)[0], (ControllerIdentity{.slot = 1, .steam_id = kKnownSteamId}));
  EXPECT_EQ((*identities)[1], (ControllerIdentity{.slot = 2, .steam_id = 0}));

  // A blob too small for the resolved offset names its slot in the error.
  const std::vector undersized = {SlotWindow{.slot = 3, .bytes = std::vector<uint8_t>(4, 0x00)}};
  const auto failed = ReadControllerIdentities(window, undersized);
  ASSERT_FALSE(failed.has_value());
  EXPECT_NE(failed.error().find("slot 3"), std::string::npos) << failed.error();
}

// TODO: live offsets are unknown until a live capture records real bytes
// from the current server.dll build. The capture then adds to
// tests/gameinterop_fixtures.h and pins them here with the same assertions as
// the synthetic tests above:
//   - kSteamIdAccessorWindow: a 256-byte code window centered on the real
//     accessor hit (offset 128), captured like kUtilRemoveWindow, plus the
//     confirmed database pattern that locates it;
//   - kSteamIdFieldOffset: the true byte offset of m_steamID within
//     CBasePlayerController on that build;
//   - kControllerWindow: recorded controller-entity bytes holding a real
//     SteamID64 at kSteamIdFieldOffset.

}  // namespace
