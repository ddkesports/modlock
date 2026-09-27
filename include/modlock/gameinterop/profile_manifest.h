#pragma once

#include <array>
#include <cstddef>
#include <expected>
#include <string_view>
#include <vector>

#include "modlock/export.h"
#include "modlock/gameinterop/game_symbols.h"

namespace modlock::gameinterop {

// ProfileModule pins one hooked game module's reviewed identity.
//
// accepted holds the SHA-256 digests of module builds whose probe results the
// signature database was reviewed against. A module image whose digest is not
// listed is a different build: its scan results are unreviewed and hooks must
// not install. reviewed_utc records the last review date of the accepted
// builds.
struct ProfileModule {
  // Path is the module path relative to the game directory.
  std::string_view path;
  // Role is the module's responsibility in the hook set.
  std::string_view role;
  // ReviewedUtc is the last review date covering every accepted digest.
  std::string_view reviewed_utc;
  // Accepted lists the reviewed SHA-256 digests, lowercase hex.
  std::array<std::string_view, 4> accepted;
  // AcceptedCount is the number of meaningful entries in accepted.
  std::size_t accepted_count;
};

// The manifest is the single source of truth for which game builds the
// signature database was reviewed against. scripts/generate_profile_manifest.py
// refreshes it from an installed game directory; every probe definition must
// target a module pinned here.
inline constexpr std::array kProfileModules{
    ProfileModule{
        .path = "game/citadel/bin/win64/server.dll",
        .role = "server",
        .reviewed_utc = "2026-09-18",
        .accepted =
            std::array<std::string_view, 4>{
                "e654794754d05f43b542f969682a1cab372617b4ed521800037461408f03a430"},
        .accepted_count = 1,
    },
    ProfileModule{
        .path = "game/bin/win64/engine2.dll",
        .role = "engine",
        .reviewed_utc = "2026-09-11",
        .accepted =
            std::array<std::string_view, 4>{
                "887201acec33837fdb18d73c04f8e0894971d26eebafe992a28a12fada118afb",
                "301d042c7443090241d7b83244747bf8a32916f61df60aea5d8a1799f432ef8d"},
        .accepted_count = 2,
    },
    ProfileModule{
        .path = "game/bin/win64/tier0.dll",
        .role = "tier0",
        .reviewed_utc = "2026-09-09",
        .accepted =
            std::array<std::string_view, 4>{
                "b4300eb0abfe73e1e877516ab6b8bdd1a1bdb4ffc47c7515a349d0623b852f69",
                "b3192eac3cb8c54ac3f9c7aaf7c725ddfcc2dc46d99ba13d16177b6ebf736ebc"},
        .accepted_count = 2,
    },
};

// ProfileManifestFind returns the manifest entry for a module path relative to
// the game directory, or an error naming the unreviewed module.
[[nodiscard]] MODLOCK_API std::expected<const ProfileModule*, std::string> ProfileManifestFind(
    std::string_view module_path);

// ClassifyModuleImage hashes the mapped image and classifies the module build:
// supported when the digest is pinned, unsupported with a named reason when the
// digest is not in the entry's accepted list, and an error when the module has
// no manifest entry.
[[nodiscard]] MODLOCK_API std::expected<bool, std::string> ClassifyModuleImage(
    const ModuleImage& image, std::string_view module_path);

// RecordedProbe names one byte-pattern probe in the signature database.
struct RecordedProbe {
  // Id is the stable caller-chosen identity of the target function.
  std::string_view id;
  // Library is the module carrying the code.
  std::string_view library;
};

// RecordedProbes is the single source of truth for the byte-pattern probe
// inventory: every id passed to ResolveScannedSymbol, DecodeRelativeCall, or
// DecodeRelativeLea, and every probe definition in the probe accessors, appears
// exactly once here. A new probe definition must add its entry and confirm its
// library is pinned in kProfileModules.
[[nodiscard]] MODLOCK_API std::vector<RecordedProbe> RecordedProbes();

}  // namespace modlock::gameinterop
