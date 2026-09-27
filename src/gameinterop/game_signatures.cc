// This table records every byte pattern modlock scans for in the game
// binaries. After a game update, run modlock-sigcheck against the new
// binaries and repair the failing entries here; callers resolve by id and
// need no change while the native shapes hold.

#include <array>

#include "modlock/gameinterop/game_symbols.h"

namespace modlock::gameinterop {
namespace {

using enum SignatureTarget;

constexpr uint8_t kEngine = static_cast<uint8_t>(GameModule::kEngine);

// Anchors shared by several entries. Each resolves one function and the
// calls or globals it references.

// CCitadelPlayerPawn::RemoveItem, which finds and detaches the ability slot.
constexpr std::string_view kRemoveItem =
    "48 89 5C 24 ?? 55 56 57 48 83 EC 60 41 0F B6 E8 48 8B F2 48 8B F9";

// CBaseEntity's inner Teleport, which calls the absolute-state setters.
constexpr std::string_view kTeleportInner =
    "4C 89 4C 24 20 4C 89 44 24 18 48 89 54 24 10 48 89 4C 24 08 53 55 56 57 "
    "41 56 41 57 48 83 EC 78 48 8B 1A 33 F6";

// The citadel_force_koth callback: push rdi; sub rsp,30; mov rdi,[rip+disp32].
constexpr std::string_view kForceKoth = "57 48 83 EC 30 48 8B 3D ?? ?? ?? ?? 48 85 FF";

// The citadel_toggle_server_pause callback.
constexpr std::string_view kTogglePause =
    "40 53 48 83 EC 20 48 8B 1D ?? ?? ?? ?? 48 85 DB 74 ?? 80 BB ?? ?? ?? ?? 00";

// CCitadelGameRules::BuildGameSessionManifest.
constexpr std::string_view kManifestBuilder =
    "48 89 54 24 ?? 48 89 4C 24 ?? 55 53 56 57 41 54 41 55 41 56 41 57 "
    "48 8D AC 24 ?? ?? ?? ?? 48 81 EC ?? ?? ?? ?? ?? ?? ?? 4C 8B FA";

// The entries sort by id.
constexpr std::array kSignatures = {
    GameSignature{
        .id = "ability.create-and-register",
        .pattern = "48 89 5C 24 ?? 44 89 4C 24 ?? 55 56 57 41 56 41 57 48 83 EC",
        .shape = "void* CreateAbility(CCitadelAbilityComponent*, definition, uint16 slot, "
                 "int32 flags, ...)",
    },
    GameSignature{
        .id = "ability.detach-slot",
        .pattern = kRemoveItem,
        .target = kCall,
        .delta = 0x8d,
        .shape = "detaches the found ability slot inside RemoveItem",
    },
    GameSignature{
        .id = "ability.find-slot",
        .pattern = kRemoveItem,
        .target = kCall,
        .delta = 0x7d,
        .shape = "finds the ability slot inside RemoveItem",
    },
    GameSignature{
        .id = "ability.refresh-charges",
        .pattern = "FF 90 ?? ?? ?? ?? 84 C0 74 17 48 8B 1F 48 8B CF FF 93 ?? ?? ?? ?? "
                   "8B D0 48 8B CF FF 93 ?? ?? ?? ?? 45 33 D2 FF C5 49 83 C6 04",
        .shape = "HeroRefresh loop calling HasCharges (+2) and GetMaxCharges (+18) virtual "
                 "slots; callers read the slot displacements",
    },
    GameSignature{
        .id = "ability.remove-item",
        .pattern = kRemoveItem,
        .shape = "void RemoveItem(CCitadelAbilityComponent*, CCitadelBaseAbility*, uint8)",
    },
    GameSignature{
        .id = "ability.set-upgrade-bits",
        .pattern = "48 8B C4 89 50 ?? 55 57 48 8D 68",
        .shape = "void SetUpgradeBits(CCitadelBaseAbility*, uint32 bits)",
    },
    GameSignature{
        .id = "ability.swap-item-slots",
        .pattern = "66 41 3B D0 0F 84 ?? ?? ?? ?? 66 44 89 44 24 18 66 89 54 24 10 55 56 57",
        .shape = "void SwapItemSlots(CCitadelAbilityComponent*, uint16, uint16)",
    },
    GameSignature{
        .id = "ability.think",
        .pattern = "40 55 53 41 54 41 55 41 57 48 8D AC 24",
        .shape = "CCitadelBaseAbility::AbilityThink",
    },
    GameSignature{
        .id = "bot.create",
        .pattern = "40 53 55 41 54 41 55 48 83 EC 38 4C 8B E1 49 8B E9 48 8B 0D ?? ?? ?? ?? "
                   "41 8B D8 44 8B EA",
        .shape = "CreateCitadelBot(const char* name, int team, int hero, const Vector* position)",
    },
    GameSignature{
        .id = "combat.broadcast",
        .pattern = "48 89 54 24 10 89 4C 24 08 55 53 56 57 41 54 41 55 41 56 41 57 48 8D 6C "
                   "24 E1",
        .shape = "native combat event broadcast for damage, healing and shield events",
    },
    GameSignature{
        .id = "combat.fire-modifier-event",
        .pattern = "40 55 56 41 55 48 8D 6C 24 ?? 48 81 EC ?? ?? ?? ?? 33 F6",
        .shape = "FireModifierEvent",
    },
    GameSignature{
        .id = "controller.create-hero-pawn",
        .pattern = "48 8B C4 48 89 48 ?? 55 57 48 8D 68 ?? 48 81 EC ?? ?? ?? ?? 48 89 58 ?? "
                   "48 8B F9 48 8B 0D",
        .shape = "CCitadelPlayerPawn* CreateHeroPawn(CCitadelPlayerController*, int team)",
    },
    GameSignature{
        .id = "controller.set-pawn",
        .pattern = "44 88 4C 24 ?? 53 57",
        .shape = "CBasePlayerController::SetPawn",
    },
    GameSignature{
        .id = "controller.spawn-observer",
        .pattern = "48 8B C4 55 53 48 8D 68 ?? 48 81 EC ?? ?? ?? ?? 48 89 78 ?? 4C 89 60",
        .shape = "void* SpawnObserverPawn(CCitadelPlayerController*)",
    },
    GameSignature{
        .id = "damage.construct",
        .pattern = "40 53 48 83 EC 50 F3 0F 10 84 24 80 00 00 00 48 8D 05",
        .shape = "CTakeDamageInfo constructor",
    },
    GameSignature{
        .id = "damage.destroy",
        .pattern = "48 89 5C 24 08 57 48 83 EC 20 48 8D 05 ?? ?? ?? ?? 48 8B D9 48 89 01 "
                   "48 81 C1 FC 00 00 00 E8 ?? ?? ?? ?? 8B 83 F4 00 00 00",
        .shape = "CTakeDamageInfo destructor",
    },
    GameSignature{
        .id = "entity-instance.accept-input",
        .pattern = "48 89 5C 24 ?? 48 89 74 24 ?? 57 48 83 EC ?? 49 8B F0 48 8B D9",
        .shape = "bool AcceptInput(CEntityInstance*, const char* input, CEntityInstance* "
                 "activator, CEntityInstance* caller, variant_t* value, int output_id, void*)",
    },
    GameSignature{
        .id = "entity-keyvalues.allocate",
        // The allocator wrapper repeats; the neighboring realloc wrapper makes
        // the match unique.
        .pattern = "48 8B 05 ?? ?? ?? ?? 48 8B D1 48 8B 08 48 8B 01 48 FF 60 08 "
                   "CC CC CC CC CC CC CC CC CC CC CC CC "
                   "48 8B 05 ?? ?? ?? ?? 4C 8B C9 4C 8B C2 49 8B D1",
        .shape = "void* (size_t); engine MemAlloc_Alloc wrapper",
    },
    GameSignature{
        .id = "entity-keyvalues.construct",
        .pattern = "48 89 5C 24 08 57 48 83 EC 20 33 FF 48 8B D9 48 89 79 28 48 89 79 30 "
                   "44 88 41 25 48 85 D2 74 ??",
        .shape = "CEntityKeyValues* (storage, CKV3Arena*, EntityKVAllocatorType_t)",
    },
    GameSignature{
        .id = "entity-keyvalues.set-key-value",
        .pattern = "40 53 55 56 48 83 EC 30 66 83 79 22 00 41 0F B6 E8 48 8B F2 48 8B D9 7E ??",
        .shape = "KeyValues3* (CEntityKeyValues*, const CKV3MemberName*, bool)",
    },
    GameSignature{
        .id = "entity-system.create-entity-by-name",
        .pattern = "48 83 EC ?? 48 8B 0D ?? ?? ?? ?? 41 8B C0",
        .shape = "CEntityInstance* (void* ignored, const char* class_name, int edict); reads "
                 "the global entity system itself",
    },
    GameSignature{
        .id = "entity-system.execute-queued-creation",
        .pattern = "48 89 5C 24 ?? 57 48 81 EC ?? ?? ?? ?? FF 81 ?? ?? ?? ?? 48 8D 44 24",
        .shape = "void (CGameEntitySystem*)",
    },
    GameSignature{
        .id = "entity-system.queue-spawn-entity",
        .pattern = "40 56 57 41 56 48 83 EC ?? F7 42",
        .shape = "void (CGameEntitySystem*, CEntityIdentity*, CEntityKeyValues*)",
    },
    GameSignature{
        .id = "entity.emit-sound",
        .pattern = "48 89 5C 24 ?? 48 89 74 24 ?? 48 89 7C 24 ?? 55 48 8B EC 48 81 EC ?? ?? ?? "
                   "?? 33 C0",
        .shape = "CBaseEntity::EmitSoundParams",
    },
    GameSignature{
        .id = "entity.remove",
        .pattern = "48 85 C9 74 ? 48 8B D1 48 8B 0D",
        .shape = "void UTIL_Remove(CEntityInstance*); queues removal at the next entity frame",
    },
    GameSignature{
        .id = "entity.set-abs-angles",
        .pattern = kTeleportInner,
        .target = kCall,
        .delta = 0x427,
        .shape = "void SetAbsAngles(CBaseEntity*, const QAngle*)",
    },
    GameSignature{
        .id = "entity.set-abs-origin",
        .pattern = kTeleportInner,
        .target = kCall,
        .delta = 0x472,
        .shape = "void SetAbsOrigin(CBaseEntity*, const Vector*)",
    },
    GameSignature{
        .id = "entity.set-abs-velocity",
        .pattern = kTeleportInner,
        .target = kCall,
        .delta = 0x452,
        .shape = "void SetAbsVelocity(CBaseEntity*, const Vector*)",
    },
    GameSignature{
        .id = "entity.set-move-type",
        .pattern = "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 48 89 7C 24 20 41 56 48 83 "
                   "EC 20 41 0F B6 F0 0F B6 EA 48 8B F9 38 91 F3 02 00 00",
        .shape = "void SetMoveType(CBaseEntity*, uint8 move_type, uint8 move_collide)",
    },
    GameSignature{
        .id = "entity.take-damage",
        .pattern = "40 55 41 54 41 55 41 56 41 57 48 81 EC ?? ?? ?? ?? 48 8D 6C 24 ?? 48 89 9D "
                   "?? ?? ?? ?? 45 33 ED",
        .shape = "CBaseEntity::TakeDamageOld(CTakeDamageInfo*)",
    },
    GameSignature{
        .id = "game-rules.add-resource",
        .pattern = "48 89 5C 24 ?? 57 48 83 EC ?? 48 8B FA 48 8B D9 48 85 C9 74 ?? ?? ?? ?? 74",
        .shape = "adds one resource path to the session manifest",
    },
    GameSignature{
        .id = "game-rules.current",
        .pattern = kForceKoth,
        .target = kRipRelative,
        .delta = 5,
        .shape = "CCitadelGameRules** global loaded by the force-KOTH callback",
    },
    GameSignature{
        .id = "game-rules.manifest-builder",
        .pattern = kManifestBuilder,
        .shape = "CCitadelGameRules::BuildGameSessionManifest",
    },
    GameSignature{
        .id = "game-rules.precache-call",
        .pattern = kManifestBuilder,
        .target = kCall,
        .delta = 0x394,
        .shape = "hero precache helper called by the manifest builder",
    },
    GameSignature{
        .id = "game-rules.precache-global",
        .pattern = kManifestBuilder,
        .target = kRipRelative,
        .delta = 0x389,
        .shape = "hero precache context loaded by the manifest builder",
    },
    GameSignature{
        .id = "game-rules.refresh-pause",
        .pattern = kTogglePause,
        .target = kCall,
        .delta = 0x51,
        .shape = "void (CCitadelGameRules*); pause-modifier reconciliation",
    },
    GameSignature{
        .id = "game-rules.start-koth",
        .pattern = kForceKoth,
        .target = kCall,
        .delta = 0xb0,
        .shape = "void (CCitadelGameRules*); KOTH warning and spawn routine",
    },
    GameSignature{
        .id = "game-rules.toggle-server-pause",
        .pattern = kTogglePause,
        .shape = "citadel_toggle_server_pause callback",
    },
    GameSignature{
        .id = "hero-definition-manager.get-hero-by-id",
        .pattern = "48 8B C1 85 D2 74 04 3B 11 72 03 33 C0 C3 48 8B 40 08 48 63 CA 48 8B 04 C8 C3",
        .shape = "CHeroDefinition* GetHeroById(CHeroDefinitionManager*, unsigned hero_id)",
    },
    GameSignature{
        .id = "hero-definition-manager.get-manager",
        .pattern = "40 53 56 41 55 48 83 EC 30 8B DA 48 8B F1 E8",
        .target = kCall,
        .delta = 0xE,
        .shape = "CHeroDefinitionManager* GetManager(); TLS-guarded getter",
    },
    GameSignature{
        .id = "hero-definition-manager.hero-name-to-id",
        .pattern = "48 89 5C 24 ?? 57 48 83 EC ?? 49 8B C8",
        .shape = "int* HeroNameToId(CHeroDefinitionManager*, int* out_id, const char* name)",
    },
    GameSignature{
        .id = "modifier.add",
        .pattern = "44 89 44 24 ?? 48 89 54 24 ?? 53 55 56 57 41 55",
        .shape = "CModifierProperty::AddModifier",
    },
    GameSignature{
        .id = "movement.process",
        .modules = GameModule::kServer | GameModule::kClient,
        .pattern = "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 70 48 8D 05 ?? ?? ?? ?? "
                   "48 C7 44 24 28 F8 03 00 00",
        .shape = "ProcessMovement(movement services, CMoveData*)",
    },
    GameSignature{
        .id = "pawn.add-item",
        .pattern = "48 89 5C 24 ?? 48 89 6C 24 ?? 48 89 74 24 ?? 57 48 83 EC 30 48 8B D9 41 8B "
                   "F1 B9 04 00 00 00 41",
        .shape = "CCitadelBaseAbility* AddItem(CCitadelPlayerPawn*, const char* name, int32 bits, "
                 "int32 context)",
    },
    GameSignature{
        .id = "pawn.initialize-hero",
        .pattern = "4C 8B DC 41 55 41 57 48 83 EC 58 0F B6 81 ?? ?? ?? ?? 44 0F B6 FA 83 C0 FE "
                   "4C 8B E9",
        .shape = "CCitadelPlayerPawn::InitializeHeroOnPawn",
    },
    GameSignature{
        .id = "pawn.modify-currency",
        .pattern = "48 89 5C 24 ?? 55 41 54 41 55 41 56 41 57 48 8D AC 24",
        .shape = "CCitadelPlayerPawn::ModifyCurrency",
    },
    GameSignature{
        .id = "pawn.reset-hero",
        .pattern = "48 89 74 24 ?? 57 48 83 EC 20 0F B6 81 ?? ?? ?? ?? 0F B6 F2",
        .shape = "int64 ResetHero(CCitadelPlayerPawn*, bool reset_abilities)",
    },
    GameSignature{
        .id = "pawn.respawn",
        .pattern = "48 89 5C 24 08 57 48 83 EC 20 48 8B D9 0F B6 FA 48 8B 89 A8 0B 00 00",
        .shape = "void Respawn(CCitadelPlayerPawn*, bool force)",
    },
    GameSignature{
        .id = "pawn.select-hero-internal",
        .pattern = "40 55 41 54 41 55 41 56 48 8D 6C 24 88 48 81 EC 78 01 00 00 4C 8B E1 4C 8B EA",
        .shape = "void SelectHeroInternal(CCitadelPlayerPawn*, CHeroDefinition*)",
    },
    GameSignature{
        .id = "pawn.teleport-client-camera",
        .pattern = "48 89 5C 24 08 48 89 74 24 18 48 89 7C 24 20 55 41 56 41 57 48 8D 6C 24 B9 "
                   "48 81 EC D0 00 00 00 48 8B 02 49 8B F9 4D 8B D0",
        .shape = "teleports a pawn and its client camera together",
    },
    GameSignature{
        .id = "physics.trace-shape",
        .pattern = "48 89 5C 24 ?? 48 89 4C 24 ?? 55 56 41 54",
        .shape = "TraceShape(physics, Ray_t*, Vector* start, Vector* end, CTraceFilter*, "
                 "CGameTrace*)",
    },
    GameSignature{
        .id = "preparation.damage-gate",
        .pattern = "44 38 AE E0 02 00 00 0F 84 ?? ?? ?? ?? 48 8B 4F 78 0F 57 FF",
        .shape = "TakeDamageOld's m_bTakesDamage test",
    },
    GameSignature{
        .id = "preparation.frozen-input",
        .pattern = "F6 80 80 03 00 00 20 75 ?? 48 8D 54 24 ?? 48 8B CB E8",
        .shape = "movement setup's FL_FROZEN test before it clears input",
    },
    GameSignature{
        .id = "projectile.contact",
        .pattern = "40 55 56 57 41 56 41 57 48 8D 6C 24 C0 48 81 EC 40 01 00 00 80 B9 21 08 "
                   "00 00 00",
        .shape = "projectile contact handler",
    },
    GameSignature{
        .id = "projectile.impact",
        .pattern = "48 89 5C 24 10 48 89 6C 24 18 56 57 41 57 48 83 EC 50 44 8B 91 F8 07 00 00",
        .shape = "projectile impact handler",
    },
    GameSignature{
        .id = "server.reply-connection",
        .modules = kEngine,
        .pattern = "48 8B C4 55 41 55 41 56",
        .shape = "CNetworkGameServerBase::ReplyConnection",
    },
    GameSignature{
        .id = "vdata.lookup-by-hash",
        .modules = GameModule::kServer | GameModule::kClient,
        .pattern = "40 53 48 83 EC ?? 89 54 24 ?? 8B D9",
        .shape = "void* LookupVData(int kind, uint32 hash); EDX is the full 32-bit hash",
    },
};

}  // namespace

std::span<const GameSignature> GameSignatures() { return kSignatures; }

}  // namespace modlock::gameinterop
