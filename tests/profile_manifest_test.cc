#include "modlock/gameinterop/profile_manifest.h"

#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <string>

namespace {

using modlock::gameinterop::ClassifyModuleImage;
using modlock::gameinterop::ProfileManifestFind;

class FakeImage final : public modlock::gameinterop::ModuleImage {
 public:
  std::uintptr_t base() const override { return 0x1000; }
  std::span<const uint8_t> image_bytes() const override { return bytes; }

  std::array<uint8_t, 7> bytes{};
};

TEST(ProfileManifest, FindsEveryPinnedModuleAndRejectsUnknownPaths) {
  for (const auto& module : modlock::gameinterop::kProfileModules) {
    auto found = ProfileManifestFind(module.path);
    ASSERT_TRUE(found) << found.error();
    EXPECT_EQ((*found)->role, module.role);
    EXPECT_GE((*found)->accepted_count, 1u);
    EXPECT_FALSE((*found)->reviewed_utc.empty());
    for (std::size_t i = 0; i < (*found)->accepted_count; ++i) {
      EXPECT_EQ((*found)->accepted[i].size(), 64u) << (*found)->accepted[i];
    }
  }
  auto missing = ProfileManifestFind("game/bin/win64/inputsystem.dll");
  ASSERT_FALSE(missing);
  EXPECT_NE(missing.error().find("no profile manifest entry"), std::string::npos);
}

TEST(ProfileManifest, ClassifyAcceptsNothingWithoutAManifestEntry) {
  FakeImage image;
  auto result = ClassifyModuleImage(image, "game/bin/win64/inputsystem.dll");
  ASSERT_FALSE(result);
  EXPECT_NE(result.error().find("no profile manifest entry"), std::string::npos);
}

TEST(ProfileManifest, ClassifyNamesTheUnreviewedDigestOfAnUnpinnedBuild) {
  FakeImage image;
  std::memcpy(image.bytes.data(), "modlock", image.bytes.size());
  auto result = ClassifyModuleImage(image, "game/citadel/bin/win64/server.dll");
  ASSERT_FALSE(result);
  // The digest of the fixed bytes pins the hash implementation itself.
  EXPECT_NE(result.error().find("49186718a7f7ed90c3580eda8436de4242fdddb6b59851eca4c6d1a404d0e995"),
            std::string::npos)
      << result.error();
  EXPECT_NE(result.error().find("update the manifest"), std::string::npos);
}

TEST(ProfileManifest, EveryRecordedProbeTargetsAPinnedModule) {
  const auto probes = modlock::gameinterop::RecordedProbes();
  ASSERT_FALSE(probes.empty());
  for (const auto& probe : probes) {
    bool pinned = false;
    for (const auto& module : modlock::gameinterop::kProfileModules) {
      // The manifest paths carry the game-directory prefix; the probe library
      // is the bare module file name.
      const auto module_file = module.path.substr(module.path.find_last_of('/') + 1);
      if (module_file == probe.library) pinned = true;
    }
    EXPECT_TRUE(pinned) << "probe '" << probe.id << "' targets unpinned module '" << probe.library
                        << "'";
  }
}

TEST(ProfileManifest, RecordedProbeIdsAreUnique) {
  const auto probes = modlock::gameinterop::RecordedProbes();
  for (std::size_t i = 0; i < probes.size(); ++i) {
    for (std::size_t j = i + 1; j < probes.size(); ++j) {
      EXPECT_NE(probes[i].id, probes[j].id) << "duplicate probe id: " << probes[i].id;
    }
  }
}

}  // namespace
