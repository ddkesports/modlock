#include <array>

#include "gtest/gtest.h"
#include "modlock/gameinterop/server_clock.h"

namespace {

struct FakeObject {
  void** vtable = nullptr;
};

FakeObject g_server;
int g_tick = 0;
float g_time = 0;

void* GetNetworkServer(void*) { return &g_server; }
int GetServerTick(void*) { return g_tick; }
float GetTime(void*) { return g_time; }

TEST(ServerClock, ReadsTickAndGameTimeThroughTheEngineInterfaces) {
  std::array<void*, 30> server_table{};
  server_table[11] = reinterpret_cast<void*>(&GetServerTick);
  server_table[29] = reinterpret_cast<void*>(&GetTime);
  g_server.vtable = server_table.data();
  g_tick = 812;
  g_time = 12.6875F;

  std::array<void*, 24> service_table{};
  service_table[23] = reinterpret_cast<void*>(&GetNetworkServer);
  FakeObject service{service_table.data()};

  modlock::gameinterop::ServerClock clock(&service);
  auto sample = clock.Observe();
  ASSERT_TRUE(sample.has_value()) << sample.error();
  EXPECT_EQ(sample->tick, 812u);
  EXPECT_DOUBLE_EQ(sample->time_seconds, 12.6875);
}

TEST(ServerClock, RefusesMissingClockMethods) {
  std::array<void*, 30> server_table{};
  g_server.vtable = server_table.data();
  std::array<void*, 24> service_table{};
  service_table[23] = reinterpret_cast<void*>(&GetNetworkServer);
  FakeObject service{service_table.data()};
  modlock::gameinterop::ServerClock clock(&service);
  EXPECT_FALSE(clock.Observe().has_value());
}

TEST(ServerClock, RefusesMissingServiceMethod) {
  std::array<void*, 24> service_table{};
  FakeObject service{service_table.data()};
  modlock::gameinterop::ServerClock clock(&service);
  EXPECT_FALSE(clock.Observe().has_value());
}

TEST(ServerClock, RefusesMissingOrInvalidWorldClock) {
  modlock::gameinterop::ServerClock missing(nullptr);
  EXPECT_FALSE(missing.Observe().has_value());

  std::array<void*, 30> server_table{};
  server_table[11] = reinterpret_cast<void*>(&GetServerTick);
  server_table[29] = reinterpret_cast<void*>(&GetTime);
  g_server.vtable = server_table.data();
  std::array<void*, 24> service_table{};
  service_table[23] = reinterpret_cast<void*>(&GetNetworkServer);
  FakeObject service{service_table.data()};
  modlock::gameinterop::ServerClock clock(&service);

  g_tick = -1;
  g_time = 1;
  EXPECT_FALSE(clock.Observe().has_value());
}

}  // namespace
