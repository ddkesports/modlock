#pragma once

#include <string_view>
#include <vector>

#include "modlock/export.h"

namespace modlock::gameinterop {

// EntitySystemProbe names one CEntitySystem creation-chain function the live
// session must resolve before queued entity creation can run.
//
// Patterns follow the gameinterop signature-database syntax ("??" marks a
// wildcard byte). A live session resolves them against current binaries through
// ParseSignature + SignatureScan.
//
// An empty pattern means the function resolves by another documented means
// instead of byte scanning.
struct EntitySystemProbe {
  // Id is the stable caller-chosen identity of the target function.
  std::string_view id;
  // Library is the expected module carrying the code.
  std::string_view library;
  // Pattern is the recorded byte signature, or empty when resolved otherwise.
  std::string_view pattern;
  // Shape is the calling convention and argument contract.
  std::string_view shape;
};

// EntitySystemProbes is the single source of truth for the entity-system
// creation surfaces shared by every consumer (ghost backend, world-text
// factory). Each byte pattern is recorded exactly once, here.
[[nodiscard]] MODLOCK_API std::vector<EntitySystemProbe> EntitySystemProbes();

}  // namespace modlock::gameinterop
