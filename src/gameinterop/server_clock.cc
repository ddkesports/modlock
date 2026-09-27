#include "modlock/gameinterop/server_clock.h"

#include <cmath>

#include "modlock/gameinterop/mapped_module_image.h"

namespace modlock::gameinterop {
namespace {

inline constexpr size_t kGetNetworkServerSlot = 23;
inline constexpr size_t kGetServerTickSlot = 11;
// The installed CNetworkGameServer has IsBackgroundMap at 28. GetTime at
// 29 multiplies GetServerTick by the engine tick interval.
inline constexpr size_t kGetTimeSlot = 29;

}  // namespace

std::expected<ServerClock, std::string> ServerClock::Resolve() {
  const auto instance = ResolveEngineInterface(L"engine2.dll", "NetworkServerService_001");
  if (!instance) return std::unexpected(instance.error());
  return ServerClock(*instance);
}

std::expected<ServerClockSample, std::string> ServerClock::Observe() const {
  if (network_server_service_ == nullptr) {
    return std::unexpected("network server service is null");
  }
  void** service_table = *static_cast<void***>(network_server_service_);
  if (service_table == nullptr) {
    return std::unexpected("network server service vtable is null");
  }
  using GetNetworkServerFn = void* (*)(void*);
  if (service_table[kGetNetworkServerSlot] == nullptr) {
    return std::unexpected("network server service method is unavailable");
  }
  void* server = reinterpret_cast<GetNetworkServerFn>(service_table[kGetNetworkServerSlot])(
      network_server_service_);
  if (server == nullptr) {
    return std::unexpected("network game server is not ready");
  }
  void** server_table = *static_cast<void***>(server);
  if (server_table == nullptr) {
    return std::unexpected("network game server vtable is null");
  }
  using GetServerTickFn = int (*)(void*);
  using GetTimeFn = float (*)(void*);
  if (server_table[kGetServerTickSlot] == nullptr || server_table[kGetTimeSlot] == nullptr) {
    return std::unexpected("network game server clock methods are unavailable");
  }
  const int tick = reinterpret_cast<GetServerTickFn>(server_table[kGetServerTickSlot])(server);
  const float time = reinterpret_cast<GetTimeFn>(server_table[kGetTimeSlot])(server);
  if (tick < 0 || !std::isfinite(time)) {
    return std::unexpected("network game server returned an invalid clock");
  }
  return ServerClockSample{.tick = static_cast<uint64_t>(tick),
                           .time_seconds = static_cast<double>(time)};
}

}  // namespace modlock::gameinterop
