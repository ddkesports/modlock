#include "modlock/render/world_text_probes.h"

#include "modlock/gameinterop/entity_system_probes.h"

namespace modlock::render {

std::vector<WorldTextProbe> WorldTextProbes() {
  std::vector<WorldTextProbe> probes;
  // The entity-system creation surfaces come from the shared gameinterop
  // table: the single source of truth for those recorded byte patterns.
  for (const auto& probe : modlock::gameinterop::EntitySystemProbes()) {
    probes.push_back(
        {.id = probe.id, .library = probe.library, .pattern = probe.pattern, .shape = probe.shape});
  }
  probes.push_back({.id = "entity-instance.accept-input",
                    .library = "server.dll",
                    .pattern = "48 89 5C 24 ?? 48 89 74 24 ?? 57 48 83 EC ?? 49 8B F0 48 8B D9",
                    .shape = "bool __thiscall(CEntityInstance*, const char* input_name, "
                             "CEntityInstance* activator, CEntityInstance* caller, "
                             "variant_t* value, int nSetOutputID, void*); set-text fires "
                             "input_name=\"SetMessage\" with the text as a string variant"});
  probes.push_back({.id = "base-entity.teleport",
                    .library = "server.dll",
                    .pattern = "",
                    .shape = "void(CBaseEntity*, const Vector* position, const QAngle* "
                             "angles, const Vector* velocity); resolved as vtable slot 163 "
                             "of CBaseEntity, not by byte scan"});
  probes.push_back({.id = "util.remove",
                    .library = "server.dll",
                    .pattern = "48 85 C9 74 ? 48 8B D1 48 8B 0D",
                    .shape = "void __cdecl(CEntityInstance*); queues removal at the next "
                             "entity-system frame"});
  return probes;
}

}  // namespace modlock::render
