#pragma once

#include <array>
#include <expected>
#include <string>

#include "modlock/export.h"
#include "modlock/gameinterop/game_symbols.h"

namespace modlock::gameinterop {

// MatchClock restores native match time without changing the simulation clock.
// Calls run on the engine thread and borrow the current world's game rules.
class MODLOCK_API MatchClock {
 public:
  static std::expected<MatchClock, std::string> Resolve(const ModuleImage& server,
                                                        void* schema_system);
  std::expected<float, std::string> Read(float current_time, float interval) const;
  std::expected<void, std::string> Restore(float seconds, float current_time, float interval);

 private:
  std::expected<void*, std::string> Current() const;
  void* const* current_ = nullptr;
  uintptr_t module_begin_ = 0;
  size_t module_size_ = 0;
  std::array<size_t, 4> offsets_{};
};

}  // namespace modlock::gameinterop
