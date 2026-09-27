#pragma once

#include <cstdint>
#include <deque>
#include <expected>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include "modlock/export.h"
#include "modlock/gameinterop/connection_tracker.h"
#include "modlock/gameinterop/game_symbols.h"

namespace modlock::gameinterop {

// HeroReadinessToken is an owned identity snapshot emitted after the engine
// finishes InitializeHeroOnPawn. It contains no entity pointer.
struct HeroReadinessToken {
  uint32_t pawn_handle = 0;
  uint64_t steam_id = 0;
  uint32_t connection_generation = 0;
};

// HeroReadiness crosses the synchronous hero-initialization callback into the
// next engine frame. Publish copies only a serial-corrected handle and the
// engine-reported connection identity; Consume accepts each exact token once.
class MODLOCK_API HeroReadiness {
 public:
  [[nodiscard]] bool Publish(void* pawn, ConnectionTracker::SlotState slot_state);
  [[nodiscard]] bool Consume(uint32_t pawn_handle, uint64_t steam_id,
                             uint32_t connection_generation);

 private:
  std::mutex mutex_;
  std::deque<HeroReadinessToken> pending_;
};

// HeroInitializationHook interposes the current build's
// CCitadelPlayerPawn::InitializeHeroOnPawn. The thunk calls the engine original
// first, then publishes readiness. The callback and owner both run on the
// engine server thread; after RunEngine returns that thread is quiescent,
// so destruction restores the original bytes before releasing callback state.
class MODLOCK_API HeroInitializationHook {
 public:
  using Handler = std::function<void(void*, ConnectionTracker::SlotState)>;
  static std::expected<HeroInitializationHook, std::string> Install(const ModuleImage& server,
                                                                    Handler handler);

  HeroInitializationHook(HeroInitializationHook&&) noexcept;
  HeroInitializationHook& operator=(HeroInitializationHook&&) noexcept;
  ~HeroInitializationHook();

  HeroInitializationHook(const HeroInitializationHook&) = delete;
  HeroInitializationHook& operator=(const HeroInitializationHook&) = delete;

 private:
  struct Impl;
  explicit HeroInitializationHook(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
};

}  // namespace modlock::gameinterop
