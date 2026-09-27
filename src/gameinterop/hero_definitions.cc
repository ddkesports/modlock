#include "modlock/gameinterop/hero_definitions.h"

#include <limits>

#include "modlock/gameinterop/signature.h"

namespace modlock::gameinterop {
namespace {

// ProbePattern finds one recorded HeroDefinitionProbes() entry by id.
std::string_view ProbePattern(std::string_view id) {
  for (const auto& probe : HeroDefinitionProbes()) {
    if (probe.id == id) {
      return probe.pattern;
    }
  }
  return {};
}

}  // namespace

std::vector<HeroDefinitionProbe> HeroDefinitionProbes() {
  return {
      {.id = "hero-definition-manager.get-manager-anchor",
       .library = "server.dll",
       .pattern = "40 53 56 41 55 48 83 EC 30 8B DA 48 8B F1 E8",
       .shape = "anchor for the TLS-guarded manager getter; the E8 call at +0xE resolves "
                "CHeroDefinitionManager* GetManager()"},
      {.id = "hero-definition-manager.hero-name-to-id",
       .library = "server.dll",
       .pattern = "48 89 5C 24 ?? 57 48 83 EC ?? 49 8B C8",
       .shape = "int* HeroNameToId(CHeroDefinitionManager* this, int* outId, const char* name)"},
      {.id = "hero-definition-manager.get-hero-by-id",
       .library = "server.dll",
       .pattern = "48 8B C1 85 D2 74 04 3B 11 72 03 33 C0 C3 48 8B 40 08 48 63 CA 48 8B 04 C8 C3",
       .shape = "CHeroDefinition* GetHeroById(CHeroDefinitionManager* this, unsigned int heroId)"},
  };
}

std::expected<HeroDefinitions, std::string> HeroDefinitions::Resolve(const ModuleImage& server) {
  HeroDefinitionCalls calls;
  // The anchor's E8 call sits 0xE bytes into the match per the recorded
  // shape; its target is the TLS-guarded CHeroDefinitionManager getter.
  // DecodeRelativeCall re-walks the pattern, which already proved unique.
  constexpr size_t kManagerAnchorCallDelta = 0xE;
  constexpr std::string_view kAnchorId = "hero-definition-manager.get-manager-anchor";
  if (auto target =
          DecodeRelativeCall(server, kAnchorId, ProbePattern(kAnchorId), kManagerAnchorCallDelta)) {
    calls.manager_getter = reinterpret_cast<void* (*)()>(reinterpret_cast<std::uintptr_t>(*target));
  } else {
    return std::unexpected(target.error());
  }
  constexpr std::string_view kNameToId = "hero-definition-manager.hero-name-to-id";
  constexpr std::string_view kGetById = "hero-definition-manager.get-hero-by-id";
  if (auto address = ResolveScannedSymbol(server, kNameToId, ProbePattern(kNameToId))) {
    calls.hero_name_to_id = reinterpret_cast<int* (*)(void*, int*, const char*)>(
        reinterpret_cast<std::uintptr_t>(*address));
  } else {
    return std::unexpected(address.error());
  }
  if (auto address = ResolveScannedSymbol(server, kGetById, ProbePattern(kGetById))) {
    calls.get_hero_by_id =
        reinterpret_cast<void* (*)(void*, unsigned)>(reinterpret_cast<std::uintptr_t>(*address));
  } else {
    return std::unexpected(address.error());
  }
  return HeroDefinitions(calls);
}

HeroDefinitions::HeroDefinitions(HeroDefinitionCalls calls) : calls_(calls) {}

std::expected<HeroDefinition, std::string> HeroDefinitions::Find(std::string_view hero_name) const {
  // Fail closed before any engine call: a caller-built owner (tests,
  // alternate constructors) may carry unresolved slots.
  if (calls_.manager_getter == nullptr) {
    return std::unexpected("hero definition calls: manager getter is unresolved");
  }
  if (calls_.hero_name_to_id == nullptr) {
    return std::unexpected("hero definition calls: hero-name-to-id is unresolved");
  }
  if (calls_.get_hero_by_id == nullptr) {
    return std::unexpected("hero definition calls: get-hero-by-id is unresolved");
  }
  // The manager is re-resolved on every call: it is TLS-guarded and dies
  // with the world that created it.
  void* manager = calls_.manager_getter();
  if (manager == nullptr) {
    return std::unexpected("hero definition manager is not live yet");
  }
  // HeroNameToId returns a pointer; success is judged
  // from an initialized out_id alone. Never dereference or log the returned
  // pointer.
  int hero_id = -1;
  std::string name(hero_name);
  calls_.hero_name_to_id(manager, &hero_id, name.c_str());
  if (hero_id < 0) {
    return std::unexpected("unknown hero name '" + name + "'");
  }
  return FindInManager(manager, static_cast<uint32_t>(hero_id));
}

std::expected<HeroDefinition, std::string> HeroDefinitions::Find(uint32_t hero_id) const {
  if (hero_id > std::numeric_limits<int>::max()) {
    return std::unexpected("hero id is outside the native identifier range");
  }
  if (calls_.manager_getter == nullptr) {
    return std::unexpected("hero definition calls: manager getter is unresolved");
  }
  if (calls_.get_hero_by_id == nullptr) {
    return std::unexpected("hero definition calls: get-hero-by-id is unresolved");
  }
  void* manager = calls_.manager_getter();
  if (manager == nullptr) {
    return std::unexpected("hero definition manager is not live yet");
  }
  return FindInManager(manager, hero_id);
}

std::expected<HeroDefinition, std::string> HeroDefinitions::FindInManager(void* manager,
                                                                          uint32_t hero_id) const {
  void* hero_def = calls_.get_hero_by_id(manager, hero_id);
  if (hero_def == nullptr) {
    return std::unexpected("hero id " + std::to_string(hero_id) + " resolved to no definition");
  }
  return HeroDefinition{.id = static_cast<int>(hero_id), .native_definition_pointer = hero_def};
}

}  // namespace modlock::gameinterop
