#include "modlock/gameinterop/game_symbols.h"

#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <limits>

namespace {

using modlock::gameinterop::DecodeRelativeCall;
using modlock::gameinterop::DecodeRelativeLea;
using modlock::gameinterop::ModuleImage;
using modlock::gameinterop::ResolveScannedSymbol;

class Image final : public ModuleImage {
 public:
  std::uintptr_t base() const override { return 0x1000; }
  std::span<const uint8_t> image_bytes() const override { return bytes; }

  std::array<uint8_t, 32> bytes{};
};

TEST(GameSymbols, AllDecodersRequireOneMatch) {
  Image image;
  image.bytes[4] = 0xcc;
  auto symbol = ResolveScannedSymbol(image, "target", "CC");
  ASSERT_TRUE(symbol) << symbol.error();
  EXPECT_EQ(*symbol, reinterpret_cast<void*>(0x1004));

  image.bytes[16] = 0xcc;
  for (const auto pattern : {"CC", "FF", "", "ZZ"}) {
    EXPECT_FALSE(ResolveScannedSymbol(image, "target", pattern));
    EXPECT_FALSE(DecodeRelativeCall(image, "call", pattern, 0));
    EXPECT_FALSE(DecodeRelativeLea(image, "lea", pattern, 0));
  }
}

TEST(GameSymbols, RelativeInstructionsPreserveSignedDisplacements) {
  for (const int32_t displacement : {-16, 16}) {
    Image image;
    image.bytes[4] = 0xcc;
    image.bytes[5] = 0xe8;
    std::memcpy(image.bytes.data() + 6, &displacement, sizeof(displacement));
    auto call = DecodeRelativeCall(image, "call", "CC E8", 1);
    ASSERT_TRUE(call) << call.error();
    EXPECT_EQ(*call, reinterpret_cast<void*>(0x100a + displacement));

    image.bytes[5] = 0x48;
    image.bytes[6] = 0x8d;
    image.bytes[7] = 0x05;
    std::memcpy(image.bytes.data() + 8, &displacement, sizeof(displacement));
    auto lea = DecodeRelativeLea(image, "lea", "CC 48 8D 05", 1);
    ASSERT_TRUE(lea) << lea.error();
    EXPECT_EQ(*lea, reinterpret_cast<void*>(0x100c + displacement));
  }
}

TEST(GameSymbols, RelativeInstructionsRejectTruncationAndWrappingOffsets) {
  Image image;
  image.bytes[4] = 0xcc;
  for (const size_t delta : {size_t{28}, std::numeric_limits<size_t>::max()}) {
    EXPECT_FALSE(DecodeRelativeCall(image, "call", "CC", delta));
    EXPECT_FALSE(DecodeRelativeLea(image, "lea", "CC", delta));
  }
  image.bytes[30] = 0xe8;
  EXPECT_FALSE(DecodeRelativeCall(image, "call", "E8", 0));
  EXPECT_FALSE(DecodeRelativeLea(image, "lea", "E8", 0));
  EXPECT_FALSE(DecodeRelativeCall(image, "not-a-call", "CC", 0));
}

}  // namespace
