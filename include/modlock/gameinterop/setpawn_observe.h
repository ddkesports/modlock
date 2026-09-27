#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <string>

#include "modlock/export.h"
#include "modlock/gameinterop/game_symbols.h"

namespace modlock::gameinterop {

// SetPawnObservation is a one-shot observational packet: a read-only
// attach that interposes CBasePlayerController::SetPawn and logs every call
// to durable stderr. It mutates nothing - the thunk runs the engine original
// first (so the engine's own pawn binding proceeds untouched), then records
// one bounded line per call:
//
//   [modlock] setpawn observe: controller=0x... pawn=0x... flags=R,C,A,P caller=server.dll+0x...
//
// The four flags (retainOldPawnTeam, copyMovementState, allowTeamMismatch,
// preserveMovementState) are logged verbatim; the truth table is classified
// afterward from the capture. The caller return address is resolved to an
// RVA inside server.dll where possible, so caller RVAs can be matched against
// a static disassembly of server.dll.
//
// Rate safety: the hook is bounded per process. A line budget (default 256)
// caps total output; after the budget is exhausted a single typed overflow
// marker is emitted and further calls are logged no further. Every line is
// flushed immediately, so evidence survives a crash or kill.
//
// The hook is default-off: Install is only called when MODLOCK_SETPAWN_OBSERVE
// is explicitly enabled (host_app main.cc).
//
// Any install failure (module not mapped, signature unresolved, hook refused)
// is fail-closed: the host runs on with observation disabled and a named
// diagnostic on stderr.
class MODLOCK_API SetPawnObservation {
 public:
  // Install resolves the recorded controller.set-pawn signature and hooks it. Failure
  // names the rejected shape (module image, signature, or hook).
  static std::expected<SetPawnObservation, std::string> Install(const ModuleImage& server);

  SetPawnObservation(SetPawnObservation&& other) noexcept;
  SetPawnObservation& operator=(SetPawnObservation&& other) noexcept;
  ~SetPawnObservation();

  SetPawnObservation(const SetPawnObservation&) = delete;
  SetPawnObservation& operator=(const SetPawnObservation&) = delete;

  // LineBudget is the maximum number of SetPawn lines this hook will emit.
  static constexpr size_t kDefaultLineBudget = 256;

 private:
  struct Impl;
  explicit SetPawnObservation(std::unique_ptr<Impl> impl) noexcept;
  std::unique_ptr<Impl> impl_;
};

}  // namespace modlock::gameinterop
