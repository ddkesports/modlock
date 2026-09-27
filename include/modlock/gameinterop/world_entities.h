#pragma once

#include <array>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "modlock/export.h"
#include "modlock/gameinterop/game_symbols.h"
#include "modlock/gameinterop/keyvalues.h"

namespace modlock::gameinterop {

class NativeDamage;

// WorldEntities reconciles NPC instances through the native entity system.
// Resolve and all operations run on the engine thread after world initialization.
class MODLOCK_API WorldEntities {
 public:
  struct Target {
    std::string designer_name;
    uint32_t subclass_id;
    int32_t team;
    std::array<float, 3> position;
    std::array<float, 3> facing;
    std::array<float, 3> velocity;
    int32_t health;
    int32_t max_health;
    std::optional<uint32_t> lane;
  };
  struct Sample {
    uint32_t handle;
    Target state;
  };
  static std::expected<WorldEntities, std::string> Resolve(const ModuleImage& server,
                                                           void* schema_system);
  std::expected<std::vector<Sample>, std::string> Read() const;
  // Restore retains matching structures, replaces moving NPCs, and removes
  // structures absent from targets. FinishRestore completes their placement
  // after one native simulation frame; a later Read verifies the world.
  std::expected<void, std::string> Restore(std::span<const Target> targets);
  // FinishRestore applies recorded state once initialization has run, using
  // the native identities retained by Restore. Call on the next engine frame.
  std::expected<void, std::string> FinishRestore();

  enum class Pickup { kUrn, kMovementBuff };
  // CreatePickup spawns one real native pickup and returns its serial-fenced
  // identity. Later ReadPickup distinguishes a live pickup from collection/removal.
  std::expected<uint32_t, std::string> CreatePickup(Pickup kind,
                                                    const std::array<float, 3>& position);
  std::expected<std::optional<Sample>, std::string> ReadPickup(uint32_t handle) const;
  // ClearAuthored kills interfering NPCs and removes native round objectives without
  // kill rewards. It preserves scenery and urn delivery triggers. Exact replay
  // restoration never calls this authored-world operation.
  std::expected<void, std::string> ClearAuthored(const NativeDamage& damage);
  // Remove deletes every live entity with this exact designer name through
  // UTIL_Remove, without damage, kill rewards or the console cheat gate. It
  // returns how many entities were queued for removal.
  std::expected<size_t, std::string> Remove(std::string_view designer_name);

 private:
  std::expected<Sample, std::string> ReadEntity(void* entity, std::string name) const;
  std::expected<void*, std::string> Create(const Target& target);
  std::expected<void, std::string> Apply(void* entity, const Target& target) const;
  void* schema_ = nullptr;
  KeyValuesCalls key_values_;
  void* (*create_)(void*, const char*, int) = nullptr;
  void (*queue_)(void*, void*, void*) = nullptr;
  void (*execute_)(void*) = nullptr;
  void (*remove_)(void*) = nullptr;
  void* (*definition_)(int32_t, uint32_t) = nullptr;
  std::array<size_t, 7> offsets_{};
  std::vector<Sample> pending_;
};

}  // namespace modlock::gameinterop
