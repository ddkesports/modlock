#include "modlock/gameinterop/game_symbols.h"

#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <limits>
#include <string>

#include "modlock/gameinterop/signature.h"

namespace {

using modlock::gameinterop::FindGameSignature;
using modlock::gameinterop::GameModule;
using modlock::gameinterop::GameSignature;
using modlock::gameinterop::GameSignatures;
using modlock::gameinterop::ModuleImage;
using modlock::gameinterop::ResolveSignature;
using modlock::gameinterop::SignatureTarget;

class Image final : public ModuleImage {
 public:
  std::uintptr_t base() const override { return 0x1000; }
  std::span<const uint8_t> image_bytes() const override { return bytes; }

  std::array<uint8_t, 32> bytes{};
};

GameSignature Match(std::string_view pattern) { return {.id = "target", .pattern = pattern}; }

GameSignature Relative(std::string_view pattern, SignatureTarget target, size_t delta) {
  return {.id = "relative", .pattern = pattern, .target = target, .delta = delta};
}

TEST(GameSignatures, TableIsSortedUniqueAndParses) {
  const auto signatures = GameSignatures();
  ASSERT_FALSE(signatures.empty());
  for (size_t i = 0; i < signatures.size(); ++i) {
    const auto& signature = signatures[i];
    if (i > 0) EXPECT_LT(signatures[i - 1].id, signature.id) << "sort and dedupe the table";
    EXPECT_NE(signature.modules, 0) << signature.id;
    EXPECT_FALSE(signature.shape.empty()) << signature.id;
    const auto parsed =
        modlock::gameinterop::ParseSignature(std::string(signature.id), signature.pattern);
    ASSERT_TRUE(parsed) << parsed.error();
    EXPECT_FALSE(parsed->bytes.empty()) << signature.id;
    if (signature.target == SignatureTarget::kMatch) EXPECT_EQ(signature.delta, 0) << signature.id;
    EXPECT_EQ(FindGameSignature(signature.id), &signature);
  }
  EXPECT_EQ(FindGameSignature("not.recorded"), nullptr);
}

TEST(GameSignatures, ModulesNameTheirFiles) {
  EXPECT_EQ(modlock::gameinterop::GameModuleFile(GameModule::kServer), "server.dll");
  EXPECT_EQ(modlock::gameinterop::GameModuleFile(GameModule::kClient), "client.dll");
  EXPECT_EQ(modlock::gameinterop::GameModuleFile(GameModule::kEngine), "engine2.dll");
  const GameSignature shared{.modules = GameModule::kServer | GameModule::kClient};
  EXPECT_TRUE(shared.In(GameModule::kClient));
  EXPECT_FALSE(shared.In(GameModule::kEngine));
}

TEST(GameSymbols, EveryTargetRequiresOneMatch) {
  Image image;
  image.bytes[4] = 0xcc;
  auto symbol = ResolveSignature(image, Match("CC"));
  ASSERT_TRUE(symbol) << symbol.error();
  EXPECT_EQ(*symbol, reinterpret_cast<void*>(0x1004));

  image.bytes[16] = 0xcc;
  for (const auto pattern : {"CC", "FF", "", "ZZ"}) {
    EXPECT_FALSE(ResolveSignature(image, Match(pattern)));
    EXPECT_FALSE(ResolveSignature(image, Relative(pattern, SignatureTarget::kCall, 0)));
    EXPECT_FALSE(ResolveSignature(image, Relative(pattern, SignatureTarget::kRipRelative, 0)));
  }
  const auto unknown = ResolveSignature(image, "not.recorded");
  ASSERT_FALSE(unknown);
  EXPECT_NE(unknown.error().find("not.recorded"), std::string::npos);
}

TEST(GameSymbols, RelativeInstructionsPreserveSignedDisplacements) {
  for (const int32_t displacement : {-16, 16}) {
    Image image;
    image.bytes[4] = 0xcc;
    image.bytes[5] = 0xe8;
    std::memcpy(image.bytes.data() + 6, &displacement, sizeof(displacement));
    auto call = ResolveSignature(image, Relative("CC E8", SignatureTarget::kCall, 1));
    ASSERT_TRUE(call) << call.error();
    EXPECT_EQ(*call, reinterpret_cast<void*>(0x100a + displacement));

    image.bytes[5] = 0x48;
    image.bytes[6] = 0x8d;
    image.bytes[7] = 0x05;
    std::memcpy(image.bytes.data() + 8, &displacement, sizeof(displacement));
    auto lea = ResolveSignature(image, Relative("CC 48 8D 05", SignatureTarget::kRipRelative, 1));
    ASSERT_TRUE(lea) << lea.error();
    EXPECT_EQ(*lea, reinterpret_cast<void*>(0x100c + displacement));
  }
}

TEST(GameSymbols, RelativeInstructionsRejectTruncationAndWrappingOffsets) {
  Image image;
  image.bytes[4] = 0xcc;
  for (const size_t delta : {size_t{28}, std::numeric_limits<size_t>::max()}) {
    EXPECT_FALSE(ResolveSignature(image, Relative("CC", SignatureTarget::kCall, delta)));
    EXPECT_FALSE(ResolveSignature(image, Relative("CC", SignatureTarget::kRipRelative, delta)));
  }
  image.bytes[30] = 0xe8;
  EXPECT_FALSE(ResolveSignature(image, Relative("E8", SignatureTarget::kCall, 0)));
  EXPECT_FALSE(ResolveSignature(image, Relative("E8", SignatureTarget::kRipRelative, 0)));
  EXPECT_FALSE(ResolveSignature(image, Relative("CC", SignatureTarget::kCall, 0)));
}

}  // namespace
