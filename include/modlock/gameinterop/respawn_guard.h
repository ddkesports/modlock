#pragma once

#include <expected>
#include <functional>
#include <memory>
#include <string>

#include "modlock/export.h"
#include "modlock/gameinterop/game_symbols.h"

namespace modlock::gameinterop {

// RespawnGuard consults the session's eligibility before a native respawn.
// Install before play and retain until the engine stops. The callback runs on
// the engine thread and must outlive the guard; only one guard may be installed.
class MODLOCK_API RespawnGuard {
 public:
  using Blocked = std::function<bool(uint32_t)>;
  static std::expected<RespawnGuard, std::string> Install(const ModuleImage& server,
                                                          Blocked blocked);
  RespawnGuard(RespawnGuard&&) noexcept;
  RespawnGuard& operator=(RespawnGuard&&) noexcept;
  ~RespawnGuard();

 private:
  struct Impl;
  explicit RespawnGuard(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
};

}  // namespace modlock::gameinterop
