#include "modlock/gameinterop/profile_manifest.h"

#include <array>
#include <bit>
#include <cstdint>
#include <string>

namespace modlock::gameinterop {
namespace {

// Sha256Hex computes the lowercase SHA-256 digest of data. The SDK builds with
// exceptions disabled and no crypto dependency, so the digest is computed here.
std::string Sha256Hex(const uint8_t* data, std::size_t size) {
  static constexpr uint32_t kRoundConstants[64] = {
      0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
      0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
      0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f,
      0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
      0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
      0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
      0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116,
      0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
      0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
      0xc67178f2};
  uint32_t state[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                       0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  const std::size_t bit_len = static_cast<std::size_t>(size) * 8;
  std::size_t offset = 0;
  auto transform = [&](const uint8_t* block) {
    uint32_t w[64];
    for (std::size_t i = 0; i < 16; ++i) {
      w[i] = (static_cast<uint32_t>(block[i * 4]) << 24) |
             (static_cast<uint32_t>(block[i * 4 + 1]) << 16) |
             (static_cast<uint32_t>(block[i * 4 + 2]) << 8) |
             static_cast<uint32_t>(block[i * 4 + 3]);
    }
    for (std::size_t i = 16; i < 64; ++i) {
      const uint32_t s0 = std::rotr(w[i - 15], 7) ^ std::rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
      const uint32_t s1 = std::rotr(w[i - 2], 17) ^ std::rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
    for (std::size_t i = 0; i < 64; ++i) {
      const uint32_t s1 = std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25);
      const uint32_t ch = (e & f) ^ (~e & g);
      const uint32_t t1 = h + s1 + ch + kRoundConstants[i] + w[i];
      const uint32_t s0 = std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22);
      const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
      const uint32_t t2 = s0 + maj;
      h = g;
      g = f;
      f = e;
      e = d + t1;
      d = c;
      c = b;
      b = a;
      a = t1 + t2;
    }
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
  };
  uint8_t block[64];
  while (offset + 64 <= size) {
    transform(data + offset);
    offset += 64;
  }
  std::size_t rest = size - offset;
  for (std::size_t i = 0; i < rest; ++i) block[i] = data[offset + i];
  block[rest++] = 0x80;
  if (rest > 56) {
    while (rest < 64) block[rest++] = 0;
    transform(block);
    rest = 0;
  }
  while (rest < 56) block[rest++] = 0;
  for (std::size_t i = 0; i < 8; ++i) block[56 + i] = static_cast<uint8_t>(bit_len >> (56 - i * 8));
  transform(block);
  std::string out;
  out.reserve(64);
  char hex[] = "0123456789abcdef";
  for (std::size_t i = 0; i < 8; ++i) {
    for (std::size_t shift = 28; shift < 32; shift -= 4) {
      out.push_back(hex[(state[i] >> shift) & 0xf]);
    }
  }
  return out;
}

}  // namespace

std::expected<const ProfileModule*, std::string> ProfileManifestFind(std::string_view module_path) {
  for (const auto& module : kProfileModules) {
    if (module.path == module_path) return &module;
  }
  return std::unexpected("module '" + std::string(module_path) +
                         std::string("' has no profile manifest entry; hooks may only install on "
                                     "reviewed modules"));
}

std::expected<bool, std::string> ClassifyModuleImage(const ModuleImage& image,
                                                     std::string_view module_path) {
  const auto entry = ProfileManifestFind(module_path);
  if (!entry) return std::unexpected(entry.error());
  const auto digest = Sha256Hex(reinterpret_cast<const uint8_t*>(image.image_bytes().data()),
                                image.image_bytes().size());
  for (std::size_t i = 0; i < (*entry)->accepted_count; ++i) {
    if ((*entry)->accepted[i] == digest) return true;
  }
  return std::unexpected("module '" + std::string(module_path) + "' build " + digest +
                         " is not a reviewed build (reviewed " +
                         std::string((*entry)->reviewed_utc) +
                         "); update the manifest with scripts/generate_profile_manifest.py");
}

std::vector<RecordedProbe> RecordedProbes() {
  return {
      {"ability.create-and-register", "server.dll"},
      {"ability.lookup-vdata-by-hash", "server.dll"},
      {"ability.refresh-charges", "server.dll"},
      {"ability.remove-item", "server.dll"},
      {"ability.set-upgrade-bits", "server.dll"},
      {"ability.swap-item-slots", "server.dll"},
      {"ability.think", "server.dll"},
      {"bot.create", "server.dll"},
      {"combat.broadcast", "server.dll"},
      {"combat.fire-modifier-event", "server.dll"},
      {"controller.create-hero-pawn", "server.dll"},
      {"controller.spawn-observer", "server.dll"},
      {"damage.construct", "server.dll"},
      {"damage.destroy", "server.dll"},
      {"entity-keyvalues.allocate", "server.dll"},
      {"entity-keyvalues.construct", "server.dll"},
      {"entity-keyvalues.create", "server.dll"},
      {"entity-keyvalues.set-bool", "server.dll"},
      {"entity-keyvalues.set-color", "server.dll"},
      {"entity-keyvalues.set-float", "server.dll"},
      {"entity-keyvalues.set-int", "server.dll"},
      {"entity-keyvalues.set-key-value", "server.dll"},
      {"entity-keyvalues.set-string", "server.dll"},
      {"entity-keyvalues.set-vector", "server.dll"},
      {"entity-system.create-entity-by-name", "server.dll"},
      {"entity-system.execute-queued-creation", "server.dll"},
      {"entity-system.queue-spawn-entity", "server.dll"},
      {"entity.emit-sound", "server.dll"},
      {"entity.take-damage", "server.dll"},
      {"game-rules.add-resource", "server.dll"},
      {"game-rules.current", "server.dll"},
      {"game-rules.manifest-builder", "server.dll"},
      {"game-rules.precache-call", "server.dll"},
      {"game-rules.precache-global", "server.dll"},
      {"game-rules.refresh-pause", "server.dll"},
      {"game-rules.start-koth", "server.dll"},
      {"game-rules.toggle-server-pause", "server.dll"},
      {"hero-definition-manager.get-hero-by-id", "server.dll"},
      {"hero-definition-manager.get-manager-anchor", "server.dll"},
      {"hero-definition-manager.hero-name-to-id", "server.dll"},
      {"pawn.add-item", "server.dll"},
      {"pawn.initialize-hero", "server.dll"},
      {"pawn.modify-currency", "server.dll"},
      {"pawn.reset-hero", "server.dll"},
      {"pawn.respawn", "server.dll"},
      {"pawn.select-hero-internal", "server.dll"},
      {"pawn.teleport-client-camera", "server.dll"},
      {"physics.trace-shape", "server.dll"},
      {"player-controller.m-steam-id", "server.dll"},
      {"preparation.damage-gate", "server.dll"},
      {"preparation.frozen-input", "server.dll"},
      {"preparation.set-move-type", "server.dll"},
      {"player-controller.set-pawn", "server.dll"},
      {"util.remove", "server.dll"},
      {"world.lookup-vdata-by-hash", "server.dll"},
  };
}

}  // namespace modlock::gameinterop
