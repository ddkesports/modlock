#include "modlock/gameinterop/player_selection.h"

namespace modlock::gameinterop {

std::vector<HeroDefinitionProbe> PlayerSelectionProbes() {
  return {
      {"controller.create-hero-pawn", "server.dll",
       "48 8B C4 48 89 48 ?? 55 57 48 8D 68 ?? 48 81 EC ?? ?? ?? ?? 48 89 58 ?? 48 8B F9 48 8B 0D",
       "CCitadelPlayerPawn* CreateHeroPawn(CCitadelPlayerController*, int team)"},
      {"pawn.select-hero-internal", "server.dll",
       "40 55 41 54 41 55 41 56 48 8D 6C 24 88 48 81 EC 78 01 00 00 4C 8B E1 4C 8B EA",
       "void SelectHeroInternal(CCitadelPlayerPawn*, CHeroDefinition*)"},
      {"controller.spawn-observer", "server.dll",
       "48 8B C4 55 53 48 8D 68 ?? 48 81 EC ?? ?? ?? ?? 48 89 78 ?? 4C 89 60",
       "void* SpawnObserverPawn(CCitadelPlayerController*)"},
  };
}

std::expected<PlayerSelectionCalls, std::string> PlayerSelectionCalls::Resolve(
    const ModuleImage& server) {
  PlayerSelectionCalls calls;
  auto probes = PlayerSelectionProbes();
  auto create = ResolveScannedSymbol(server, probes[0].id, probes[0].pattern);
  auto select = ResolveScannedSymbol(server, probes[1].id, probes[1].pattern);
  auto observer = ResolveScannedSymbol(server, probes[2].id, probes[2].pattern);
  if (!create) return std::unexpected(create.error());
  if (!select) return std::unexpected(select.error());
  if (!observer) return std::unexpected(observer.error());
  calls.create_pawn = reinterpret_cast<decltype(calls.create_pawn)>(*create);
  calls.select_hero = reinterpret_cast<decltype(calls.select_hero)>(*select);
  calls.spawn_observer = reinterpret_cast<decltype(calls.spawn_observer)>(*observer);
  return calls;
}

std::expected<ResetHeroPawn, std::string> ResolveResetHeroPawn(const ModuleImage& server) {
  auto target = ResolveScannedSymbol(server, "pawn.reset-hero",
                                     "48 89 74 24 ?? 57 48 83 EC 20 0F B6 81 ?? ?? ?? ?? 0F B6 F2");
  if (!target) return std::unexpected(target.error());
  return reinterpret_cast<ResetHeroPawn>(*target);
}

std::expected<RespawnPawn, std::string> ResolveRespawnPawn(const ModuleImage& server) {
  // CCitadelPlayerController's respawn command calls this on m_hHeroPawn with true.
  auto target =
      ResolveScannedSymbol(server, "pawn.respawn",
                           "48 89 5C 24 08 57 48 83 EC 20 48 8B D9 0F B6 FA 48 8B 89 A8 0B 00 00");
  if (!target) return std::unexpected(target.error());
  return reinterpret_cast<RespawnPawn>(*target);
}

}  // namespace modlock::gameinterop
