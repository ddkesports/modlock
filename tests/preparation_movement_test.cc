#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>

#include "modlock/gameinterop/pawn_observer.h"

namespace {

using modlock::gameinterop::ModuleImage;
using modlock::gameinterop::ResolvePreparationMovement;

// Instruction windows recorded from server.dll on September 18, 2026.
constexpr std::array<uint8_t, 18> kInput{0xf6, 0x80, 0x80, 0x03, 0x00, 0x00, 0x20, 0x75, 0x50,
                                         0x48, 0x8d, 0x54, 0x24, 0x78, 0x48, 0x8b, 0xcb, 0xe8};
constexpr std::array<uint8_t, 20> kDamage{0x44, 0x38, 0xae, 0xe0, 0x02, 0x00, 0x00,
                                          0x0f, 0x84, 0x78, 0x05, 0x00, 0x00, 0x48,
                                          0x8b, 0x4f, 0x78, 0x0f, 0x57, 0xff};
constexpr std::array<uint8_t, 42> kMovement{
    0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24,
    0x18, 0x48, 0x89, 0x7c, 0x24, 0x20, 0x41, 0x56, 0x48, 0x83, 0xec, 0x20, 0x41, 0x0f,
    0xb6, 0xf0, 0x0f, 0xb6, 0xea, 0x48, 0x8b, 0xf9, 0x38, 0x91, 0xf3, 0x02, 0x00, 0x00};

class Image final : public ModuleImage {
 public:
  std::uintptr_t base() const override { return 0x180000000; }
  std::span<const uint8_t> image_bytes() const override { return bytes; }

  void Place(size_t offset) {
    std::ranges::copy(kInput, bytes.begin() + offset);
    std::ranges::copy(kDamage, bytes.begin() + offset + 64);
    std::ranges::copy(kMovement, bytes.begin() + offset + 128);
  }

  std::array<uint8_t, 1024> bytes{};
};

TEST(PreparationMovement, ResolvesRelocatedInstructionWindows) {
  for (const size_t offset : {32, 384}) {
    Image image;
    image.Place(offset);
    const auto movement = ResolvePreparationMovement(image);
    ASSERT_TRUE(movement) << movement.error();
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(*movement), image.base() + offset + 128);
  }
}

TEST(PreparationMovement, RejectsChangedOrAmbiguousContracts) {
  for (const size_t changed : {32, 96, 160}) {
    Image image;
    image.Place(32);
    image.bytes[changed] = 0;
    EXPECT_FALSE(ResolvePreparationMovement(image));
  }

  Image image;
  image.Place(32);
  image.Place(384);
  const auto movement = ResolvePreparationMovement(image);
  ASSERT_FALSE(movement);
  EXPECT_NE(movement.error().find("matched 2 times"), std::string::npos);
}

}  // namespace
