#include "modlock/gameinterop/game_rules.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>

#include "modlock/gameinterop/entity_abi.h"

#if defined(_WIN32)
#include <windows.h>

#include <safetyhook.hpp>
#endif

namespace modlock::gameinterop {
namespace {

// kManifestBuilderPattern identifies CCitadelGameRules::BuildGameSessionManifest.
constexpr char kManifestBuilderPattern[] =
    "48 89 54 24 ?? 48 89 4C 24 ?? 55 53 56 57 41 54 41 55 41 56 41 57 "
    "48 8D AC 24 ?? ?? ?? ?? 48 81 EC ?? ?? ?? ?? ?? ?? ?? 4C 8B FA";

// Offsets into the manifest builder where it references its own precache
// helpers.
constexpr size_t kPrecacheGlobalLea = 0x389;
constexpr size_t kPrecacheCall = 0x394;
constexpr char kAddResourcePattern[] =
    "48 89 5C 24 ?? 57 48 83 EC ?? 48 8B FA 48 8B D9 48 85 C9 74 ?? ?? ?? ?? 74";

constexpr char kForceKothPattern[] = "57 48 83 EC 30 48 8B 3D ?? ?? ?? ?? 48 85 FF";

#if defined(_WIN32)

using HeroPrecacheFn = void(__fastcall*)(void* global_set, const char* hero_name,
                                         void* resource_ctx);

// Thunk state: one hook may exist at a time. The names and decoded targets
// are set once at install; the manifest context is per call.
std::vector<std::string> g_hero_names;
std::vector<std::string> g_resource_names;
void* g_precache_global = nullptr;
HeroPrecacheFn g_precache_fn = nullptr;
GameRulesHooks::AddResourceFn g_add_resource = nullptr;
safetyhook::InlineHook g_hook;

// Resource context the game's precache routine expects during a manifest
// build: {128-bit zero, manifest pointer}.
struct ResourceContext {
  std::int64_t zero_a;
  std::int64_t zero_b;
  void* manifest;
};

void PrecacheIntoCurrentManifest(void** original_args) {
  if (g_hero_names.empty()) return;
  if (g_precache_fn == nullptr || g_precache_global == nullptr) {
    std::cout << "[modlock] game rules: precache pair unresolved; manifest "
                 "carries no heroes\n";
    return;
  }
  ResourceContext ctx{0, 0, original_args != nullptr ? *original_args : nullptr};
  int count = 0;
  for (const auto& name : g_hero_names) {
    // An unknown name fails inside the game's own lookup without touching
    // the manifest; the log line keeps the outcome visible for the session.
    g_precache_fn(g_precache_global, name.c_str(), &ctx);
    ++count;
  }
  std::cout << "[modlock] game rules: precached " << count
            << " hero(es) into the session manifest\n";
}

__int64 __fastcall ManifestBuildThunk(void* thisptr, void** original_args) {
  const __int64 result = g_hook.call<__int64>(thisptr, original_args);
  PrecacheIntoCurrentManifest(original_args);
  void* manifest = original_args ? *original_args : nullptr;
  GameRulesHooks::RegisterResources(manifest, g_add_resource, g_resource_names);
  return result;
}

#endif

}  // namespace

struct GameRulesHooks::Impl {
  ~Impl() {
#if defined(_WIN32)
    g_hook.reset();
    g_hero_names.clear();
    g_resource_names.clear();
    g_precache_global = nullptr;
    g_precache_fn = nullptr;
    g_add_resource = nullptr;
#endif
  }
};

GameRulesHooks::GameRulesHooks(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
GameRulesHooks::GameRulesHooks(GameRulesHooks&&) noexcept = default;
GameRulesHooks& GameRulesHooks::operator=(GameRulesHooks&&) noexcept = default;
GameRulesHooks::~GameRulesHooks() = default;

bool GameRulesHooks::RegisterResources(void* manifest, AddResourceFn add_resource,
                                       std::span<const std::string> resources) {
  if (!manifest || !add_resource) return false;
  for (const auto& resource : resources) add_resource(resource.c_str(), manifest);
  return true;
}

void GameRulesHooks::AddPrecache(std::vector<std::string> heroes,
                                 std::vector<std::string> resources) {
#if defined(_WIN32)
  for (auto& hero : heroes) {
    if (std::find(g_hero_names.begin(), g_hero_names.end(), hero) == g_hero_names.end())
      g_hero_names.push_back(std::move(hero));
  }
  for (auto& resource : resources) {
    if (std::find(g_resource_names.begin(), g_resource_names.end(), resource) ==
        g_resource_names.end())
      g_resource_names.push_back(std::move(resource));
  }
#endif
}

std::expected<void* const*, std::string> ResolveCurrentGameRules(const ModuleImage& server) {
  // Installed force-KOTH callback: push rdi; sub rsp,30; mov rdi,[rip+disp32].
  // The same global is loaded by the native dev-test callback before its call.
  auto anchor = ResolveScannedSymbol(server, "game-rules.current", kForceKothPattern);
  if (!anchor) return std::unexpected(anchor.error());
  const auto offset = reinterpret_cast<uintptr_t>(*anchor) - server.base();
  int32_t displacement = 0;
  std::memcpy(&displacement, server.image_bytes().data() + offset + 8, sizeof(displacement));
  const int64_t target = static_cast<int64_t>(offset) + 12 + displacement;
  if (target < 0 || server.image_bytes().size() < sizeof(void*) ||
      static_cast<uint64_t>(target) > server.image_bytes().size() - sizeof(void*))
    return std::unexpected("game-rules current pointer leaves server module");
  return reinterpret_cast<void* const*>(server.base() + static_cast<uintptr_t>(target));
}

std::expected<GamePause, std::string> GamePause::Resolve(const ModuleImage& server,
                                                         void* schema_system) {
  auto current = ResolveCurrentGameRules(server);
  if (!current) return std::unexpected(current.error());
  // citadel_toggle_server_pause reads the current rules, toggles the replicated
  // server-pause field and runs the ordinary pause-modifier reconciliation.
  constexpr char pattern[] =
      "40 53 48 83 EC 20 48 8B 1D ?? ?? ?? ?? 48 85 DB 74 ?? 80 BB 90 29 00 00 00";
  auto toggle = ResolveScannedSymbol(server, "game-rules.toggle-server-pause", pattern);
  if (!toggle) return std::unexpected(toggle.error());
  auto refresh = DecodeRelativeCall(server, "game-rules.refresh-pause", pattern, 0x51);
  if (!refresh) return std::unexpected(refresh.error());
  const auto refresh_address = reinterpret_cast<uintptr_t>(*refresh);
  if (refresh_address < server.base() ||
      refresh_address - server.base() >= server.image_bytes().size())
    return std::unexpected("native pause refresh leaves server module");
  auto field = SchemaFieldOf(schema_system, "server.dll", "CCitadelGameRules", "m_bServerPaused");
  if (!field) return std::unexpected(field.error());
  if (field->offset != 0x2990 || field->size < sizeof(uint8_t))
    return std::unexpected("native server-pause field changed");
  GamePause pause;
  pause.current_ = *current;
  pause.toggle_ = reinterpret_cast<void (*)()>(*toggle);
  pause.refresh_ = reinterpret_cast<void (*)(void*)>(*refresh);
  pause.paused_offset_ = field->offset;
  return pause;
}

std::expected<void, std::string> GamePause::Set(bool paused) const {
  void* rules = nullptr;
  if (current_) std::memcpy(&rules, current_, sizeof(rules));
  if (!rules || !toggle_ || !refresh_) return std::unexpected("game-pause rules unavailable");
  auto* value = static_cast<const uint8_t*>(rules) + paused_offset_;
  if (*value > 1) return std::unexpected("invalid native server-pause state");
  if ((*value != 0) != paused)
    toggle_();
  else if (paused)
    refresh_(rules);
  void* after = nullptr;
  std::memcpy(&after, current_, sizeof(after));
  if (after != rules || (*value != 0) != paused)
    return std::unexpected("native game-pause readback differs");
  return {};
}

std::expected<KothRules, std::string> KothRules::Resolve(const ModuleImage& server,
                                                         void* schema_system) {
  auto current = ResolveCurrentGameRules(server);
  if (!current) return std::unexpected(current.error());
  KothRules reader;
  reader.current_ = *current;
  reader.module_begin_ = server.base();
  reader.module_size_ = server.image_bytes().size();
  // Native force-spawn callback RVA 966121 sets next location and calls the
  // ordinary warning/spawn routine at +b0 (E8, sole RCX argument is rules).
  auto start = DecodeRelativeCall(server, "game-rules.start-koth", kForceKothPattern, 0xb0);
  if (!start) return std::unexpected(start.error());
  const auto start_address = reinterpret_cast<uintptr_t>(*start);
  if (start_address < server.base() || start_address - server.base() >= reader.module_size_)
    return std::unexpected("native KOTH start leaves server module");
  reader.start_ = reinterpret_cast<void (*)(void*)>(*start);
  auto position =
      SchemaFieldOf(schema_system, "server.dll", "CCitadelGameRules", "m_vNextKothLocation");
  if (!position) return std::unexpected(position.error());
  if (position->size < sizeof(float) * 3) return std::unexpected("KOTH location storage too short");
  reader.next_position_offset_ = position->offset;
  constexpr const char* names[] = {"m_nKothScoringTeam",      "m_timeKothScoring",
                                   "m_timeKothCashInStarted", "m_timeKothGiveUp",
                                   "m_timeNextKothSpawn",     "m_timeNextKothSpawnWindowTime"};
  for (size_t i = 0; i < reader.offsets_.size(); ++i) {
    auto field = SchemaFieldOf(schema_system, "server.dll", "CCitadelGameRules", names[i]);
    if (!field) return std::unexpected(field.error());
    if (field->size < sizeof(uint32_t))
      return std::unexpected(std::string("KOTH field storage too short: ") + names[i]);
    reader.offsets_[i] = field->offset;
  }
  return reader;
}

std::expected<void*, std::string> KothRules::Current() const {
  if (!current_) return std::unexpected("native game rules reader is unresolved");
  void* rules = nullptr;
  std::memcpy(&rules, current_, sizeof(rules));
  if (!rules) return std::unexpected("native game rules are not active");
  uintptr_t table = 0;
  std::memcpy(&table, rules, sizeof(table));
  if (table < module_begin_ || table - module_begin_ >= module_size_)
    return std::unexpected("native game rules vtable is outside server module");
  return rules;
}

std::expected<void, std::string> KothRules::StartAt(const std::array<float, 3>& position) {
  for (float value : position)
    if (!std::isfinite(value)) return std::unexpected("KOTH location is nonfinite");
  auto rules = Current();
  if (!rules) return std::unexpected(rules.error());
  if (!start_) return std::unexpected("native KOTH start is unresolved");
  const auto state = Read();
  if (!state) return std::unexpected(state.error());
  if (state->cash_in_started > 0 ||
      (started_after_scoring_ && state->scoring_time <= *started_after_scoring_))
    return std::unexpected("previous native KOTH objective has not ended");
  // The native force-spawn callback writes this vector directly before the
  // same call; notification and objective creation remain native-owned.
  std::memcpy(static_cast<unsigned char*>(*rules) + next_position_offset_, position.data(),
              sizeof(float) * position.size());
  start_(*rules);
  started_after_scoring_ = state->scoring_time;
  return {};
}

std::expected<void, std::string> KothRules::Reset() {
  auto rules = Current();
  if (!rules) return std::unexpected(rules.error());
  auto* bytes = static_cast<unsigned char*>(*rules);
  const uint32_t zero = 0;
  for (const auto offset : offsets_) std::memcpy(bytes + offset, &zero, sizeof(zero));
  started_after_scoring_.reset();
  return {};
}

std::expected<KothState, std::string> KothRules::Read() const {
  auto rules = Current();
  if (!rules) return std::unexpected(rules.error());
  const auto* bytes = static_cast<const unsigned char*>(*rules);
  KothState state{};
  std::memcpy(&state.scoring_team, bytes + offsets_[0], sizeof(state.scoring_team));
  float* fields[] = {&state.scoring_time, &state.cash_in_started, &state.give_up_time,
                     &state.next_spawn, &state.spawn_window};
  for (size_t i = 0; i < std::size(fields); ++i) {
    std::memcpy(fields[i], bytes + offsets_[i + 1], sizeof(float));
    if (!std::isfinite(*fields[i])) return std::unexpected("native KOTH time is nonfinite");
  }
  return state;
}

std::expected<GameRulesHooks, std::string> GameRulesHooks::Install(
    const ModuleImage& server, std::vector<std::string> precache_heroes,
    std::vector<std::string> resources) {
#if defined(_WIN32)
  if (g_hook) return std::unexpected("the session manifest hook is already installed");
  // Stage 1: locate the manifest builder. Zero or multiple matches fail loud:
  // an engine update that shifts the pattern must stop here, not mis-hook.
  auto builder =
      ResolveScannedSymbol(server, "game-rules.manifest-builder", kManifestBuilderPattern);
  if (!builder.has_value()) {
    return std::unexpected(builder.error());
  }
  const auto builder_addr = reinterpret_cast<std::uintptr_t>(*builder);

  // Decode the precache pair from the builder's instruction stream.
  const std::string_view pattern(kManifestBuilderPattern);
  auto global =
      DecodeRelativeLea(server, "game-rules.precache-global", pattern, kPrecacheGlobalLea);
  if (!global.has_value()) {
    return std::unexpected(global.error());
  }
  auto fn = DecodeRelativeCall(server, "game-rules.precache-call", pattern, kPrecacheCall);
  if (!fn.has_value()) {
    return std::unexpected(fn.error());
  }
  auto add_resource = ResolveScannedSymbol(server, "game-rules.add-resource", kAddResourcePattern);
  if (!add_resource.has_value()) {
    return std::unexpected(add_resource.error());
  }

  // Stage 3: install. The thunk runs the original build first so the
  // manifest exists, then rides the same thread to add our heroes.
  auto created = safetyhook::create_inline(*builder, reinterpret_cast<void*>(&ManifestBuildThunk));
  if (!created) {
    return std::unexpected("inline hook on the manifest builder failed");
  }
  g_hook = std::move(created);

  g_precache_global = *global;
  g_add_resource = reinterpret_cast<GameRulesHooks::AddResourceFn>(*add_resource);
  void* fn_addr = *fn;
  g_precache_fn = reinterpret_cast<HeroPrecacheFn>(fn_addr);
  GameRulesHooks owner(std::make_unique<Impl>());
  owner.AddPrecache(std::move(precache_heroes), std::move(resources));

  std::cout << "[modlock] game rules: manifest hook installed at +0x" << std::hex
            << builder_addr - server.base() << std::dec << "; " << g_hero_names.size()
            << " hero(es) registered\n";
  return owner;
#else
  (void)server;
  (void)precache_heroes;
  return std::unexpected("the game-rules hooks require the Windows host build");
#endif
}

}  // namespace modlock::gameinterop
