#pragma once

#include <expected>
#include <functional>
#include <memory>
#include <string>

#include "modlock/export.h"
#include "modlock/gameinterop/game_symbols.h"
#include "modlock/gameinterop/native_trace.h"

namespace modlock::gameinterop {

// ProjectileImpactHook wraps the native projectile impact operation. Its handler
// runs synchronously on the engine thread and invokes the supplied native call
// exactly once; that borrowed call must not escape the handler. Native flight
// owns the projectile's lifetime. The handler receives the original collision
// facts before native effects change the world. Destruction removes both detours.
class MODLOCK_API ProjectileImpactHook {
 public:
  using NativeImpact = std::function<void()>;
  using Handler = std::function<void(void*, const TraceResult&, const NativeImpact&)>;

  static std::expected<ProjectileImpactHook, std::string> Install(const ModuleImage& server,
                                                                  Handler handler);
  ProjectileImpactHook(ProjectileImpactHook&&) noexcept;
  ProjectileImpactHook& operator=(ProjectileImpactHook&&) noexcept;
  ~ProjectileImpactHook();
  ProjectileImpactHook(const ProjectileImpactHook&) = delete;
  ProjectileImpactHook& operator=(const ProjectileImpactHook&) = delete;

 private:
  struct Impl;
  explicit ProjectileImpactHook(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
};

}  // namespace modlock::gameinterop
