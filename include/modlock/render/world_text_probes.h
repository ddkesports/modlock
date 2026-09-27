#pragma once

#include <string_view>
#include <vector>

#include "modlock/export.h"

namespace modlock::render {

// WorldTextProbe names one game function the live session must resolve before
// a real factory can run. Patterns follow the gameinterop signature-database
// syntax ("??" marks a wildcard byte). A live session resolves current binaries
// through gameinterop::ParseSignature + SignatureScan. An empty pattern means
// the function resolves by another documented means instead of byte scanning.
struct WorldTextProbe {
  // Id is the stable caller-chosen identity of the target function.
  std::string_view id;
  // Library is the expected module carrying the code.
  std::string_view library;
  // Pattern is the recorded byte signature, or empty when resolved otherwise.
  std::string_view pattern;
  // Shape is the rough calling convention and argument contract.
  std::string_view shape;
};

// WorldTextProbes lists the four lifecycle targets of the world-text seam:
// create ("point_worldtext"), set-text ("SetMessage" input), set-origin
// (Teleport), and remove (UTIL_Remove). Spawning also needs the queued-creation
// pair because entity creation inside the frame loop goes through the queue.
[[nodiscard]] MODLOCK_API std::vector<WorldTextProbe> WorldTextProbes();

}  // namespace modlock::render
