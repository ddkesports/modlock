#include <array>
#include <cstdint>
#include <cstring>

#include "gtest/gtest.h"
#include "modlock/gameinterop/hero_initialization.h"

namespace {

using modlock::gameinterop::ConnectionTracker;
using modlock::gameinterop::HeroReadiness;

uint32_t HandleOf(uint32_t index, uint32_t serial) {
  return (index & 0x7FFF) | ((serial & 0x1FFFF) << 15);
}

struct EntityFixture {
  std::array<unsigned char, 0x80> entity{};
  std::array<unsigned char, 0x70> identity{};

  explicit EntityFixture(uint32_t handle) {
    void* identity_pointer = identity.data();
    std::memcpy(entity.data() + 0x10, &identity_pointer, sizeof(identity_pointer));
    void* entity_pointer = entity.data();
    std::memcpy(identity.data(), &entity_pointer, sizeof(entity_pointer));
    std::memcpy(identity.data() + 0x10, &handle, sizeof(handle));
  }
};

TEST(HeroReadiness, ConsumesOnlyExactOwnedIdentityOnce) {
  HeroReadiness readiness;
  const uint32_t handle = HandleOf(2597, 17747);
  EntityFixture pawn(handle);
  ASSERT_TRUE(readiness.Publish(
      pawn.entity.data(), ConnectionTracker::SlotState{
                              .occupied = true, .xuid = 76561197964264161ULL, .generation = 9}));

  EXPECT_FALSE(readiness.Consume(handle, 76561197964264161ULL, 8));
  EXPECT_FALSE(readiness.Consume(HandleOf(2597, 17748), 76561197964264161ULL, 9));
  EXPECT_FALSE(readiness.Consume(handle, 76561197964264162ULL, 9));
  EXPECT_TRUE(readiness.Consume(handle, 76561197964264161ULL, 9));
  EXPECT_FALSE(readiness.Consume(handle, 76561197964264161ULL, 9));
}

TEST(HeroReadiness, RejectsEmptyBotAndDetachedPawnSignals) {
  HeroReadiness readiness;
  const uint32_t handle = HandleOf(4, 2);
  EntityFixture pawn(handle);

  EXPECT_FALSE(readiness.Publish(pawn.entity.data(), ConnectionTracker::SlotState{}));
  EXPECT_FALSE(readiness.Consume(handle, 1, 1));
  EXPECT_FALSE(readiness.Publish(
      pawn.entity.data(),
      ConnectionTracker::SlotState{.occupied = true, .xuid = 0, .generation = 1}));
  EXPECT_FALSE(readiness.Consume(handle, 0, 1));

  void* null_identity = nullptr;
  std::memcpy(pawn.entity.data() + 0x10, &null_identity, sizeof(null_identity));
  (void)readiness.Publish(pawn.entity.data(), ConnectionTracker::SlotState{
                                                  .occupied = true, .xuid = 5, .generation = 1});
  EXPECT_FALSE(readiness.Consume(handle, 5, 1));
}

TEST(HeroReadiness, OtherPawnSignalsDoNotOverwriteTheHumanToken) {
  HeroReadiness readiness;
  const uint32_t human_handle = HandleOf(7, 3);
  const uint32_t other_handle = HandleOf(8, 4);
  EntityFixture human(human_handle);
  EntityFixture other(other_handle);
  const ConnectionTracker::SlotState state{.occupied = true, .xuid = 44, .generation = 5};
  (void)readiness.Publish(human.entity.data(), state);
  (void)readiness.Publish(other.entity.data(), state);

  EXPECT_TRUE(readiness.Consume(human_handle, 44, 5));
}

TEST(HeroReadiness, MarkedForDeletionPawnDoesNotPublish) {
  HeroReadiness readiness;
  const uint32_t handle = HandleOf(7, 3);
  EntityFixture pawn(handle);
  const uint32_t flags = 0x200;
  std::memcpy(pawn.identity.data() + 0x30, &flags, sizeof(flags));
  (void)readiness.Publish(pawn.entity.data(), ConnectionTracker::SlotState{
                                                  .occupied = true, .xuid = 44, .generation = 5});
  EXPECT_FALSE(readiness.Consume(handle, 44, 5));
}

TEST(HeroReadiness, NewInitializationSupersedesAStaleGeneration) {
  HeroReadiness readiness;
  const uint32_t handle = HandleOf(7, 3);
  EntityFixture pawn(handle);
  (void)readiness.Publish(pawn.entity.data(), ConnectionTracker::SlotState{
                                                  .occupied = true, .xuid = 44, .generation = 3});
  (void)readiness.Publish(pawn.entity.data(), ConnectionTracker::SlotState{
                                                  .occupied = true, .xuid = 44, .generation = 4});

  EXPECT_FALSE(readiness.Consume(handle, 44, 3));
  EXPECT_TRUE(readiness.Consume(handle, 44, 4));
}

}  // namespace
