// Keyvalues contracts cover unique symbol resolution, engine member-name
// hashing, and typed construction through the native ABI.
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "gameinterop_module_image_test.h"
#include "gtest/gtest.h"
#include "modlock/gameinterop/keyvalues.h"

namespace {

using modlock::gameinterop::BuildEntityKeyValues;
using modlock::gameinterop::EntityKeyValue;
using modlock::gameinterop::KeyValuesCalls;
using modlock::gameinterop::KeyValuesProbe;
using modlock::gameinterop::KeyValuesProbes;
using modlock::gameinterop::MakeMemberName;
using modlock::gameinterop::ResolveKeyValuesCalls;
namespace gti = modlock::gameinterop::testing;

// SyntheticProbeTable isolates resolver behavior from production signatures.
// The returned strings outlive the probes' string_views.
std::pair<std::vector<KeyValuesProbe>, std::vector<std::string>> SyntheticProbeTable() {
  std::vector<std::string> patterns;
  std::vector<KeyValuesProbe> probes;
  int seed = 0;
  for (const auto& probe : KeyValuesProbes()) {
    // A unique trailing byte keeps each probe's hit unique inside the
    // synthetic image.
    char suffix[8];
    std::snprintf(suffix, sizeof(suffix), "%02X", seed++);
    patterns.push_back("48 8B C4 55 41 54 ?? ?? ?? ?? ?? ?? ?? 48 89 58 " + std::string(suffix));
    probes.push_back({probe.id, probe.library, patterns.back(), probe.shape});
  }
  return {std::move(probes), std::move(patterns)};
}

gti::FakeModuleImage BuildImage(std::span<const KeyValuesProbe> probes) {
  gti::FakeModuleImage image;
  for (const auto& probe : probes) {
    EXPECT_FALSE(probe.pattern.empty()) << probe.id;
    image.Add(probe.id, probe.pattern);
  }
  image.Seal(0x50000000);
  return image;
}

TEST(KeyValuesTest, ProductionTableCarriesTheSourceMatchedNativeSurface) {
  const auto probes = KeyValuesProbes();
  std::vector<std::string_view> ids;
  for (const auto& probe : probes) {
    ids.push_back(probe.id);
    EXPECT_FALSE(probe.pattern.empty()) << probe.id;
    EXPECT_FALSE(probe.shape.empty()) << probe.id;
  }
  EXPECT_EQ(ids.size(), 3u);
}

TEST(KeyValuesTest, ProductionSurfaceResolvesFromASyntheticImage) {
  const auto probes = KeyValuesProbes();
  auto image = BuildImage(probes);
  auto resolved = ResolveKeyValuesCalls(image);
  ASSERT_TRUE(resolved.has_value()) << resolved.error();
  EXPECT_NE(resolved->allocate, nullptr);
  EXPECT_NE(resolved->construct_key_values, nullptr);
  EXPECT_NE(resolved->set_key_value, nullptr);
}

TEST(KeyValuesTest, ResolveFillsEverySlotFromASyntheticImage) {
  const auto [probes, owned_patterns] = SyntheticProbeTable();
  auto image = BuildImage(probes);
  auto resolved = ResolveKeyValuesCalls(image, probes);
  ASSERT_TRUE(resolved.has_value()) << resolved.error();
  EXPECT_NE(resolved->allocate, nullptr);
  EXPECT_NE(resolved->construct_key_values, nullptr);
  EXPECT_NE(resolved->set_key_value, nullptr);
}

TEST(KeyValuesTest, AllocationResolvesUpdatedBuildAmongSimilarWrappers) {
  gti::FakeModuleImage image;
  // Recorded server.dll build 25173285 bytes. The first wrapper is a
  // separate allocator entry; the second is followed by the realloc wrapper.
  image.Add("other-allocator", "48 8B 05 61 1F 86 00 48 8B D1 48 8B 08 48 8B 01 48 FF 60 08");
  image.Add("allocator",
            "48 8B 05 31 1C 86 00 48 8B D1 48 8B 08 48 8B 01 48 FF 60 08 "
            "CC CC CC CC CC CC CC CC CC CC CC CC "
            "48 8B 05 11 1C 86 00 4C 8B C9 4C 8B C2 49 8B D1");
  for (const auto& probe : KeyValuesProbes()) {
    if (probe.id != "entity-keyvalues.allocate") image.Add(probe.id, probe.pattern);
  }
  image.Seal(0x50000000);
  auto resolved = ResolveKeyValuesCalls(image);
  ASSERT_TRUE(resolved.has_value()) << resolved.error();
  EXPECT_EQ(reinterpret_cast<std::uintptr_t>(resolved->allocate),
            image.base() + image.OffsetOf("allocator"));
}

TEST(KeyValuesTest, AllocationRefusesDuplicateMatches) {
  const auto probes = KeyValuesProbes();
  auto image = BuildImage(probes);
  image.Add("duplicate-allocator", probes.front().pattern);
  image.Seal(0x50000000);
  auto resolved = ResolveKeyValuesCalls(image);
  ASSERT_FALSE(resolved.has_value());
  EXPECT_NE(resolved.error().find("entity-keyvalues.allocate' matched 2 times"), std::string::npos);
}

TEST(KeyValuesTest, ResolveNamesAProbeMissingFromTheImage) {
  auto [probes, owned_patterns] = SyntheticProbeTable();
  gti::FakeModuleImage image;
  for (const auto& probe : probes) {
    if (probe.id != "entity-keyvalues.set-key-value") {
      image.Add(probe.id, probe.pattern);
    }
  }
  image.Seal(0x50000000);
  auto resolved = ResolveKeyValuesCalls(image, probes);
  ASSERT_FALSE(resolved.has_value());
  EXPECT_NE(resolved.error().find("entity-keyvalues.set-key-value"), std::string::npos)
      << resolved.error();
}

TEST(KeyValuesTest, MemberNameTranscribesTheStringTokenHash) {
  // MurmurHash2 over the lowercased key with STRINGTOKEN_MURMURHASH_SEED.
  const auto name = MakeMemberName("Font_Name");
  EXPECT_EQ(name.hash, 0x7a0c84aau);
  EXPECT_EQ(name.symbol, 0xFFFFFFFFu);
  EXPECT_EQ(std::string_view(name.string), "Font_Name");
  // Case-insensitive: the hash folds case even though the pointer keeps the
  // caller's spelling.
  EXPECT_EQ(MakeMemberName("FONT_NAME").hash, name.hash);
}

struct RecordedSetterCall {
  std::string key;
  std::uint32_t hash;
};

void* g_created_key_values = reinterpret_cast<void*>(0x2000);
std::vector<std::string> g_setter_order;
RecordedSetterCall g_last_call;

void* RecordCreate() { return g_created_key_values; }

void RecordSetString(void* ekv, const modlock::gameinterop::MemberName* name, const char* value) {
  EXPECT_EQ(ekv, g_created_key_values);
  g_setter_order.push_back("set-string:" + std::string(value));
  g_last_call = {name->string, name->hash};
}

void RecordSetBool(void* ekv, const modlock::gameinterop::MemberName* name, unsigned char value) {
  EXPECT_EQ(ekv, g_created_key_values);
  g_setter_order.push_back("set-bool:" + std::to_string(value));
  g_last_call = {name->string, name->hash};
}

void RecordSetInt(void* ekv, const modlock::gameinterop::MemberName* name, int value) {
  EXPECT_EQ(ekv, g_created_key_values);
  g_setter_order.push_back("set-int:" + std::to_string(value));
  g_last_call = {name->string, name->hash};
}

void RecordSetColor(void* ekv, const modlock::gameinterop::MemberName* name, std::uint32_t packed) {
  EXPECT_EQ(ekv, g_created_key_values);
  g_setter_order.push_back("set-color:" + std::to_string(packed));
  g_last_call = {name->string, name->hash};
}

TEST(KeyValuesTest, BuildAppliesEveryTypedPairThroughItsSetter) {
  KeyValuesCalls calls;
  calls.create_key_values = &RecordCreate;
  calls.set_string = &RecordSetString;
  calls.set_bool = &RecordSetBool;
  calls.set_int = &RecordSetInt;
  calls.set_color = &RecordSetColor;
  const std::vector<EntityKeyValue> pairs = {
      {.key = "message_text", .value = std::string_view("hello")},
      {.key = "enabled", .value = true},
      {.key = "font_size", .value = 100},
      {.key = "color",
       .value = modlock::gameinterop::KeyValueColor{.red = 1, .green = 2, .blue = 3, .alpha = 4}},
  };
  auto built = BuildEntityKeyValues(calls, pairs);
  ASSERT_TRUE(built.has_value()) << built.error();
  EXPECT_EQ(*built, g_created_key_values);
  ASSERT_EQ(g_setter_order.size(), 4u);
  EXPECT_EQ(g_setter_order[0], "set-string:hello");
  EXPECT_EQ(g_setter_order[1], "set-bool:1");
  EXPECT_EQ(g_setter_order[2], "set-int:100");
  // Packed little-endian RGBA word: r | g << 8 | b << 16 | a << 24.
  EXPECT_EQ(g_setter_order[3], "set-color:" + std::to_string(0x04030201u));
  EXPECT_EQ(g_last_call.key, "color");
  EXPECT_EQ(g_last_call.hash, MakeMemberName("color").hash);
}

struct RawMember {
  std::uint64_t metadata = 1ull << 2;
  std::uint64_t data = 0;
};

std::array<std::byte, 56> g_raw_ekv;
std::array<RawMember, 8> g_raw_members;
size_t g_raw_member_count = 0;

void* RecordAllocate(size_t size) {
  EXPECT_EQ(size, g_raw_ekv.size());
  g_raw_ekv.fill(std::byte{});
  return g_raw_ekv.data();
}

void* RecordConstruct(void* storage, void* allocator, unsigned char allocator_type) {
  EXPECT_EQ(storage, g_raw_ekv.data());
  EXPECT_EQ(allocator, nullptr);
  EXPECT_EQ(allocator_type, 0);
  return storage;
}

void* RecordSetKeyValue(void* ekv, const modlock::gameinterop::MemberName*,
                        unsigned char as_attribute) {
  EXPECT_EQ(ekv, g_raw_ekv.data());
  EXPECT_EQ(as_attribute, 0);
  EXPECT_LT(g_raw_member_count, g_raw_members.size());
  g_raw_members[g_raw_member_count] = RawMember{};
  return &g_raw_members[g_raw_member_count++];
}

TEST(KeyValuesTest, NativeSurfaceEncodesFreshMembersExactly) {
  g_raw_member_count = 0;
  KeyValuesCalls calls;
  calls.allocate = &RecordAllocate;
  calls.construct_key_values = &RecordConstruct;
  calls.set_key_value = &RecordSetKeyValue;
  const std::string message = "CURRENT 1/18";
  const std::vector<EntityKeyValue> pairs = {
      {.key = "message_text", .value = std::string_view(message)},
      {.key = "enabled", .value = true},
      {.key = "fullbright", .value = 1},
      {.key = "font_size", .value = 100.0f},
      {.key = "color",
       .value = modlock::gameinterop::KeyValueColor{.red = 1, .green = 2, .blue = 3, .alpha = 255}},
  };
  auto built = BuildEntityKeyValues(calls, pairs);
  ASSERT_TRUE(built.has_value()) << built.error();
  EXPECT_EQ(*built, g_raw_ekv.data());
  ASSERT_EQ(g_raw_member_count, 5u);
  auto type = [](const RawMember& value) { return (value.metadata >> 2) & 0xFF; };
  auto subtype = [](const RawMember& value) { return (value.metadata >> 10) & 0xFF; };
  EXPECT_EQ(type(g_raw_members[0]), 0x26u);
  EXPECT_EQ(subtype(g_raw_members[0]), 26u);
  EXPECT_EQ(g_raw_members[0].data, reinterpret_cast<std::uintptr_t>(message.c_str()));
  EXPECT_EQ(type(g_raw_members[1]), 2u);
  EXPECT_EQ(g_raw_members[1].data, 1u);
  EXPECT_EQ(type(g_raw_members[2]), 3u);
  EXPECT_EQ(g_raw_members[2].data, 1u);
  EXPECT_EQ(type(g_raw_members[3]), 5u);
  double font_size = 0;
  std::memcpy(&font_size, &g_raw_members[3].data, sizeof(font_size));
  EXPECT_EQ(font_size, 100.0);
  EXPECT_EQ(type(g_raw_members[4]), 0x88u);
  EXPECT_EQ(subtype(g_raw_members[4]), 28u);
  EXPECT_EQ((g_raw_members[4].metadata >> 42) & 0x1F, 3u);
  EXPECT_EQ(g_raw_members[4].data, 0xFF030201u);
}

TEST(KeyValuesTest, NativeVectorBorrowsAllComponentsThroughQueuedCreation) {
  g_raw_member_count = 0;
  KeyValuesCalls calls;
  calls.allocate = &RecordAllocate;
  calls.construct_key_values = &RecordConstruct;
  calls.set_key_value = &RecordSetKeyValue;
  const EntityKeyValue pairs[] = {
      {.key = "scales", .value = modlock::gameinterop::KeyValueVector{0.22f, 0.3f, 0.4f}}};
  auto built = BuildEntityKeyValues(calls, pairs);
  ASSERT_TRUE(built.has_value()) << built.error();
  ASSERT_EQ(g_raw_member_count, 1u);
  const auto& member = g_raw_members[0];
  EXPECT_EQ((member.metadata >> 2) & 0xFF, 0x48u);
  EXPECT_EQ((member.metadata >> 10) & 0xFF, 24u);
  EXPECT_EQ((member.metadata >> 42) & 0x1F, 3u);
  EXPECT_EQ(member.metadata & (1ull << 1), 0u);
  const auto& vector = std::get<modlock::gameinterop::KeyValueVector>(pairs[0].value);
  EXPECT_EQ(member.data, reinterpret_cast<std::uintptr_t>(&vector.x));
  std::array<float, 3> consumed{};
  std::memcpy(consumed.data(), reinterpret_cast<const void*>(member.data), sizeof(consumed));
  EXPECT_EQ(consumed, (std::array<float, 3>{0.22f, 0.3f, 0.4f}));
}

TEST(KeyValuesTest, BuildNamesTheFirstMissingConstructor) {
  auto built = BuildEntityKeyValues(KeyValuesCalls{}, {});
  ASSERT_FALSE(built.has_value());
  EXPECT_NE(built.error().find("complete native"), std::string::npos) << built.error();
}

TEST(KeyValuesTest, BuildNamesTheMissingSetterForItsKind) {
  KeyValuesCalls calls;
  calls.create_key_values = &RecordCreate;
  const std::vector<EntityKeyValue> pairs = {
      {.key = "origin", .value = modlock::gameinterop::KeyValueVector{1, 2, 3}}};
  auto built = BuildEntityKeyValues(calls, pairs);
  ASSERT_FALSE(built.has_value());
  EXPECT_NE(built.error().find("entity-keyvalues.set-vector"), std::string::npos) << built.error();
}

}  // namespace
