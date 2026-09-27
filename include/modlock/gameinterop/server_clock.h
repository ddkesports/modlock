#pragma once

#include <cstdint>
#include <expected>
#include <string>

#include "modlock/export.h"

namespace modlock::gameinterop {

// ServerClockSample is the engine-owned server tick and game time observed
// after one GameFrame. It carries no wall-clock or locally advanced value.
struct ServerClockSample {
  uint64_t tick = 0;
  double time_seconds = 0;
};

// ServerClock reads the current world clock from INetworkGameServer. The
// service is process-owned; the network server pointer is resolved for each
// observation and never retained across a world boundary.
class MODLOCK_API ServerClock {
 public:
  static std::expected<ServerClock, std::string> Resolve();

  // ServerClock wraps the process-owned NetworkServerService_001 interface.
  // Tests provide a synthetic vtable through the same constructor.
  explicit ServerClock(void* network_server_service)
      : network_server_service_(network_server_service) {}

  [[nodiscard]] std::expected<ServerClockSample, std::string> Observe() const;

 private:
  void* network_server_service_ = nullptr;
};

}  // namespace modlock::gameinterop
