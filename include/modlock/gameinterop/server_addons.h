#pragma once

#include <expected>
#include <memory>
#include <string>

#include "modlock/export.h"

namespace modlock::gameinterop {

// ServerAddonsHook advertises content addons to connecting clients. For each
// CNetworkGameServerBase::ReplyConnection it offers addons, a comma-separated
// list of addon names, in place of the engine's own value and restores that
// value afterward. A client mounts each name from its addonroot while
// connecting. The list is fixed at install; destruction removes the detour.
class MODLOCK_API ServerAddonsHook {
 public:
  static std::expected<ServerAddonsHook, std::string> Install(std::string addons);
  ServerAddonsHook(ServerAddonsHook&&) noexcept;
  ServerAddonsHook& operator=(ServerAddonsHook&&) noexcept;
  ~ServerAddonsHook();
  ServerAddonsHook(const ServerAddonsHook&) = delete;
  ServerAddonsHook& operator=(const ServerAddonsHook&) = delete;

 private:
  struct Impl;
  explicit ServerAddonsHook(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
};

}  // namespace modlock::gameinterop
