#include "modlock/gameinterop/mapped_module_image.h"

#include <gtest/gtest.h>

#include <string>

namespace {

using modlock::gameinterop::MappedModuleImage;
using modlock::gameinterop::ResolveEngineInterface;

TEST(MappedModuleImage, MissingModuleReturnsAnError) {
  const auto image = MappedModuleImage::ForModule(L"modlock-test-absent-module.dll");
  ASSERT_FALSE(image);
  EXPECT_FALSE(image.error().empty());
}

TEST(MappedModuleImage, MissingInterfaceErrorNamesTheRequestedRegistration) {
  const auto instance =
      ResolveEngineInterface(L"modlock-test-absent-module.dll", "ModlockAbsentInterface001");
  ASSERT_FALSE(instance);
  EXPECT_NE(instance.error().find("ModlockAbsentInterface001"), std::string::npos);
}

}  // namespace
