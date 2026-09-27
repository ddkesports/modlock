#pragma once

#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include "modlock/export.h"

namespace modlock::gameinterop {

// ClientCommandHook forwards authenticated client commands on the engine thread.
// Returning true consumes a command; all other commands retain the engine
// handler. Destruction restores the handler once the engine thread has
// quiesced.
class MODLOCK_API ClientCommandHook {
 public:
  using Handler = std::function<bool(int32_t, std::string_view)>;
  static std::expected<ClientCommandHook, std::string> Install(Handler handler);
  ClientCommandHook(ClientCommandHook&&) noexcept;
  ClientCommandHook& operator=(ClientCommandHook&&) noexcept;
  ~ClientCommandHook();

 private:
  struct Impl;
  explicit ClientCommandHook(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
};

}  // namespace modlock::gameinterop
