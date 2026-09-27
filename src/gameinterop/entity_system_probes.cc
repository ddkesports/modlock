#include "modlock/gameinterop/entity_system_probes.h"

namespace modlock::gameinterop {

std::vector<EntitySystemProbe> EntitySystemProbes() {
  return {
      {.id = "entity-system.create-entity-by-name",
       .library = "server.dll",
       .pattern = "48 83 EC ?? 48 8B 0D ?? ?? ?? ?? 41 8B C0",
       .shape = "CEntityInstance* __thiscall(void* /* ignored this */, "
                "const char* class_name, int force_edict_index); the body reads "
                "the global entity system internally - pass nullptr this"},
      {.id = "entity-system.queue-spawn-entity",
       .library = "server.dll",
       .pattern = "40 56 57 41 56 48 83 EC ?? F7 42",
       .shape = "void __thiscall(CGameEntitySystem*, CEntityIdentity*, "
                "CEntityKeyValues*)"},
      {.id = "entity-system.execute-queued-creation",
       .library = "server.dll",
       .pattern = "48 89 5C 24 ?? 57 48 81 EC ?? ?? ?? ?? FF 81 ?? ?? ?? ?? 48 8D 44 24",
       .shape = "void __thiscall(CGameEntitySystem*)"},
  };
}

}  // namespace modlock::gameinterop
