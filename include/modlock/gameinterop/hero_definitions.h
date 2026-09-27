#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

#include "modlock/export.h"
#include "modlock/gameinterop/game_symbols.h"

namespace modlock::gameinterop {

// Recorded server.dll signatures, shared with the ghost probe catalog.
struct HeroDefinitionProbe {
  std::string_view id;
  std::string_view library;
  std::string_view pattern;
  std::string_view shape;
};

[[nodiscard]] MODLOCK_API std::vector<HeroDefinitionProbe> HeroDefinitionProbes();

struct MODLOCK_API HeroDefinitionCalls {
  void* (*manager_getter)() = nullptr;
  // NativeHero.cpp reads the output id, not the returned pointer.
  int* (*hero_name_to_id)(void* manager, int* out_id, const char* name) = nullptr;
  void* (*get_hero_by_id)(void* manager, unsigned hero_id) = nullptr;
};

struct HeroDefinition {
  int id = -1;
  void* native_definition_pointer = nullptr;
};

// Engine-thread lookup without entity creation. Find reborrows the world-owned
// manager on each call; the returned definition must not outlive that world.
class MODLOCK_API HeroDefinitions {
 public:
  static std::expected<HeroDefinitions, std::string> Resolve(const ModuleImage& server);
  HeroDefinitions() = default;
  explicit HeroDefinitions(HeroDefinitionCalls calls);
  [[nodiscard]] std::expected<HeroDefinition, std::string> Find(std::string_view hero_name) const;
  // Find resolves a copied source hero identifier in the current world.
  [[nodiscard]] std::expected<HeroDefinition, std::string> Find(uint32_t hero_id) const;

 private:
  // FindInManager resolves an identifier through one borrowed world manager.
  [[nodiscard]] std::expected<HeroDefinition, std::string> FindInManager(void* manager,
                                                                         uint32_t hero_id) const;
  HeroDefinitionCalls calls_;
};

}  // namespace modlock::gameinterop
