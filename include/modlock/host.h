#pragma once

#include <memory>
#include <string>
#include <vector>

#include "modlock/plugin.h"

namespace modlock::host {

// LoadedPlugin reports one registered plugin's identity and tick count.
struct LoadedPlugin {
  std::string name;
  uint32_t interface_version = 0;
  uint64_t ticks = 0;
};

// PluginHost owns the plugin set: it validates interface versions, drives the
// Load -> Start -> Tick -> Stop sequence exactly once per plugin, and refuses
// work outside that sequence. It is the single writer of plugin state (P1).
class MODLOCK_API PluginHost {
 public:
  PluginHost() = default;
  ~PluginHost();

  PluginHost(const PluginHost&) = delete;
  PluginHost& operator=(const PluginHost&) = delete;

  // Register takes ownership of a plugin. A version mismatch or duplicate
  // name returns an error and leaves the host unchanged. Success returns nullptr.
  [[nodiscard]] std::unique_ptr<std::string> Register(std::unique_ptr<Plugin> plugin);

  // StartAll loads and starts every registered plugin once. A failure stops
  // all attempted plugins, including the partially loaded or started instance.
  [[nodiscard]] bool StartAll(std::string& error);

  // TickAll advances one frame across all started plugins, in registration
  // order, without position data.
  void TickAll();

  // TickAll advances one frame and supplies the same clock and pawn positions
  // to every started plugin in registration order.
  void TickAll(const TickContext& context);

  // StopAll stops every started plugin in reverse registration order. Safe
  // to call repeatedly; TickAll after StopAll is a no-op.
  void StopAll();

  // Plugins returns the current roster with tick counts.
  [[nodiscard]] std::vector<LoadedPlugin> Plugins() const;

  // ExitCode preserves an engine failure or the first nonzero plugin result.
  int ExitCode(int engine_exit) const;

 private:
  enum class State { kRegistered, kLoading, kStarted, kStopped };

  struct Entry {
    std::unique_ptr<Plugin> plugin;
    State state = State::kRegistered;
    uint64_t ticks = 0;
  };

  std::vector<Entry> entries_;
  bool accepting_ = true;
  bool stopped_ = false;
};

}  // namespace modlock::host
