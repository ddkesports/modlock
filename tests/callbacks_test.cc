#include "callbacks.h"

#include <gtest/gtest.h>

namespace modlock {
namespace {

TEST(Callbacks, ClaimsInRegistrationOrderAndReleasesCaptures) {
  Callbacks<bool()> callbacks;
  std::vector<int> calls;
  auto lifetime = std::make_shared<int>(1);
  auto first = callbacks.Add([&, lifetime] {
    calls.push_back(*lifetime);
    return false;
  });
  auto second = callbacks.Add([&] {
    calls.push_back(2);
    return true;
  });
  auto third = callbacks.Add([&] {
    calls.push_back(3);
    return false;
  });
  std::weak_ptr<int> observed = lifetime;
  lifetime.reset();
  EXPECT_TRUE(callbacks.Dispatch());
  EXPECT_EQ(calls, (std::vector<int>{1, 2}));
  first.Reset();
  EXPECT_TRUE(observed.expired());
  second.Reset();
  calls.clear();
  EXPECT_FALSE(callbacks.Dispatch());
  EXPECT_EQ(calls, (std::vector<int>{3}));
}

TEST(Callbacks, DispatchAllowsRemovalAndDefersNewRegistrations) {
  Callbacks<void()> callbacks;
  std::vector<int> calls;
  Subscription second;
  Subscription added;
  auto first = callbacks.Add([&] {
    calls.push_back(1);
    second.Reset();
    added = callbacks.Add([&] { calls.push_back(3); });
  });
  second = callbacks.Add([&] { calls.push_back(2); });
  auto empty = callbacks.Add({});
  callbacks.Dispatch();
  EXPECT_EQ(calls, (std::vector<int>{1}));
  first.Reset();
  callbacks.Dispatch();
  EXPECT_EQ(calls, (std::vector<int>{1, 3}));
}

}  // namespace
}  // namespace modlock
