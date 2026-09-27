#include "modlock/gameinterop/keyvalues.h"

#include <cctype>
#include <cstring>
#include <limits>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace modlock::gameinterop {
namespace {

// kStringTokenSeed is STRINGTOKEN_MURMURHASH_SEED from
// tier0/utlstringtoken.h.
constexpr std::uint32_t kStringTokenSeed = 0x31415926u;

// MurmurHash2 is the conventional 32-bit mix from tier1/generichash.cpp,
// transcribed byte-for-byte so the built names hash exactly as the game's.
std::uint32_t MurmurHash2(const unsigned char* data, size_t length, std::uint32_t seed) {
  constexpr std::uint32_t m = 0x5bd1e995u;
  constexpr int r = 24;
  std::uint32_t h = seed ^ static_cast<std::uint32_t>(length);
  size_t position = 0;
  while (length - position >= 4) {
    std::uint32_t k;
    std::memcpy(&k, data + position, sizeof(k));
    k *= m;
    k ^= k >> r;
    k *= m;
    h *= m;
    h ^= k;
    position += 4;
  }
  const size_t remaining = length - position;
  if (remaining == 3) {
    h ^= static_cast<std::uint32_t>(data[position + 2]) << 16;
  }
  if (remaining >= 2) {
    h ^= static_cast<std::uint32_t>(data[position + 1]) << 8;
  }
  if (remaining >= 1) {
    h ^= data[position];
    h *= m;
  }
  h ^= h >> 13;
  h *= m;
  h ^= h >> 15;
  return h;
}

// SlotFor maps one probe id to its KeyValuesCalls slot.
void** SlotFor(KeyValuesCalls& calls, std::string_view id) {
  if (id == "entity-keyvalues.allocate") {
    return reinterpret_cast<void**>(&calls.allocate);
  }
  if (id == "entity-keyvalues.construct") {
    return reinterpret_cast<void**>(&calls.construct_key_values);
  }
  if (id == "entity-keyvalues.set-key-value") {
    return reinterpret_cast<void**>(&calls.set_key_value);
  }
  if (id == "entity-keyvalues.create") {
    return reinterpret_cast<void**>(&calls.create_key_values);
  }
  if (id == "entity-keyvalues.set-string") {
    return reinterpret_cast<void**>(&calls.set_string);
  }
  if (id == "entity-keyvalues.set-bool") {
    return reinterpret_cast<void**>(&calls.set_bool);
  }
  if (id == "entity-keyvalues.set-int") {
    return reinterpret_cast<void**>(&calls.set_int);
  }
  if (id == "entity-keyvalues.set-float") {
    return reinterpret_cast<void**>(&calls.set_float);
  }
  if (id == "entity-keyvalues.set-color") {
    return reinterpret_cast<void**>(&calls.set_color);
  }
  if (id == "entity-keyvalues.set-vector") {
    return reinterpret_cast<void**>(&calls.set_vector);
  }
  return nullptr;
}

struct RawKeyValues3 {
  std::uint64_t metadata;
  std::uint64_t data;
};
static_assert(sizeof(RawKeyValues3) == 16);

constexpr std::uint64_t kTypeMask = 0xFFull << 2;
constexpr std::uint64_t kSubtypeMask = 0xFFull << 10;
constexpr std::uint64_t kFreeArrayMemoryMask = 1ull << 1;
constexpr std::uint64_t kArrayCountMask = 0x1Full << 42;
constexpr std::uint8_t kTypeNull = 1;
constexpr std::uint8_t kTypeBool = 2;
constexpr std::uint8_t kTypeInt = 3;
constexpr std::uint8_t kTypeDouble = 5;
constexpr std::uint8_t kTypeStringExternal = 0x26;
constexpr std::uint8_t kTypeArrayFloat32 = 0x48;
constexpr std::uint8_t kTypeArrayUint8Short = 0x88;
constexpr std::uint8_t kSubtypeBool8 = 13;
constexpr std::uint8_t kSubtypeInt32 = 20;
constexpr std::uint8_t kSubtypeFloat32 = 24;
constexpr std::uint8_t kSubtypeString = 26;
constexpr std::uint8_t kSubtypeColor32 = 28;
constexpr size_t kEntityKeyValuesSize = 56;

std::uint8_t RawType(const RawKeyValues3& value) {
  return static_cast<std::uint8_t>((value.metadata >> 2) & 0xFFu);
}

void SetFreshMetadata(RawKeyValues3& value, std::uint8_t type, std::uint8_t subtype,
                      std::uint8_t array_count = 0) {
  value.metadata &= ~(kTypeMask | kSubtypeMask | kFreeArrayMemoryMask | kArrayCountMask);
  value.metadata |= static_cast<std::uint64_t>(type) << 2;
  value.metadata |= static_cast<std::uint64_t>(subtype) << 10;
  value.metadata |= static_cast<std::uint64_t>(array_count) << 42;
  value.data = 0;
}

std::expected<void, std::string> SetFreshRawValue(RawKeyValues3& target,
                                                  const EntityKeyValue& pair) {
  if (RawType(target) != kTypeNull) {
    return std::unexpected("keyvalues member '" + std::string(pair.key) +
                           "' was not freshly allocated");
  }
  return std::visit(
      [&](const auto& value) -> std::expected<void, std::string> {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, bool>) {
          SetFreshMetadata(target, kTypeBool, kSubtypeBool8);
          target.data = value ? 1 : 0;
        } else if constexpr (std::is_same_v<T, int>) {
          SetFreshMetadata(target, kTypeInt, kSubtypeInt32);
          target.data = static_cast<std::uint64_t>(static_cast<std::int64_t>(value));
        } else if constexpr (std::is_same_v<T, float>) {
          SetFreshMetadata(target, kTypeDouble, kSubtypeFloat32);
          const double converted = value;
          std::memcpy(&target.data, &converted, sizeof(converted));
        } else if constexpr (std::is_same_v<T, std::string_view>) {
          if (value.data() == nullptr) {
            return std::unexpected("keyvalues string for '" + std::string(pair.key) +
                                   "' has no storage");
          }
          SetFreshMetadata(target, kTypeStringExternal, kSubtypeString);
          target.data = reinterpret_cast<std::uintptr_t>(value.data());
        } else if constexpr (std::is_same_v<T, KeyValueColor>) {
          const std::uint8_t count = value.alpha == 255 ? 3 : 4;
          SetFreshMetadata(target, kTypeArrayUint8Short, kSubtypeColor32, count);
          target.data = static_cast<std::uint64_t>(value.red) |
                        (static_cast<std::uint64_t>(value.green) << 8) |
                        (static_cast<std::uint64_t>(value.blue) << 16) |
                        (static_cast<std::uint64_t>(value.alpha) << 24);
        } else if constexpr (std::is_same_v<T, KeyValueVector>) {
          // AllocArray<float32>(..., KV3_ARRAY_ALLOC_EXTERN) stores a borrowed
          // contiguous triple. The caller retains pairs through queued creation.
          static_assert(sizeof(KeyValueVector) == 3 * sizeof(float));
          static_assert(offsetof(KeyValueVector, y) == sizeof(float));
          static_assert(offsetof(KeyValueVector, z) == 2 * sizeof(float));
          SetFreshMetadata(target, kTypeArrayFloat32, kSubtypeFloat32, 3);
          target.data = reinterpret_cast<std::uintptr_t>(&value.x);
        }
        return {};
      },
      pair.value);
}

}  // namespace

std::vector<KeyValuesProbe> KeyValuesProbes() {
  // The allocator wrapper repeats. Its neighboring realloc instructions make
  // the match unique; RIP-relative addresses vary between game builds.
  return {
      {.id = "entity-keyvalues.allocate",
       .library = "server.dll",
       .pattern = "48 8B 05 ?? ?? ?? ?? 48 8B D1 48 8B 08 48 8B 01 48 FF 60 08 "
                  "CC CC CC CC CC CC CC CC CC CC CC CC "
                  "48 8B 05 ?? ?? ?? ?? 4C 8B C9 4C 8B C2 49 8B D1",
       .shape = "void* __cdecl(size_t); engine MemAlloc_Alloc wrapper"},
      {.id = "entity-keyvalues.construct",
       .library = "server.dll",
       .pattern = "48 89 5C 24 08 57 48 83 EC 20 33 FF 48 8B D9 48 89 79 28 48 89 79 30 "
                  "44 88 41 25 48 85 D2 74 ??",
       .shape = "CEntityKeyValues* __thiscall(storage, CKV3Arena*, EntityKVAllocatorType_t)"},
      {.id = "entity-keyvalues.set-key-value",
       .library = "server.dll",
       .pattern = "40 53 55 56 48 83 EC 30 66 83 79 22 00 41 0F B6 E8 48 8B F2 48 8B D9 "
                  "7E ??",
       .shape = "KeyValues3* __thiscall(CEntityKeyValues*, const CKV3MemberName*, bool)"},
  };
}

MemberName MakeMemberName(std::string_view key) {
  // HashStringWithBuffer lowercases into a buffer before hashing
  // (tier0/utlstringtoken.h), so the token is case-insensitive.
  std::string lowered(key);
  for (char& c : lowered) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return MemberName{
      .hash = MurmurHash2(reinterpret_cast<const unsigned char*>(lowered.data()), lowered.size(),
                          kStringTokenSeed),
      .symbol = kMemberNameInvalidSymbol,
      .string = key.data(),
  };
}

std::expected<KeyValuesCalls, std::string> ResolveKeyValuesCalls(
    const ModuleImage& image, std::span<const KeyValuesProbe> probes) {
  KeyValuesCalls calls;
  for (const auto& probe : probes) {
    void** slot = SlotFor(calls, probe.id);
    if (slot == nullptr) {
      return std::unexpected("keyvalues probe '" + std::string(probe.id) +
                             "' has no call slot; extend SlotFor with it");
    }
    if (auto address = ResolveScannedSymbol(image, probe.id, probe.pattern)) {
      *slot = *address;
    } else {
      return std::unexpected(address.error());
    }
  }
  return calls;
}

std::expected<KeyValuesCalls, std::string> ResolveKeyValuesCalls(const ModuleImage& image) {
  const auto probes = KeyValuesProbes();
  return ResolveKeyValuesCalls(image, probes);
}

void* AllocateEmptyEntityKeyValues() {
#if defined(_WIN32)
  HMODULE tier0 = GetModuleHandleW(L"tier0.dll");
  if (tier0 == nullptr) {
    return nullptr;
  }
  auto mem_alloc = reinterpret_cast<void* (*)(size_t)>(GetProcAddress(tier0, "MemAlloc_AllocFunc"));
  auto mem_free = reinterpret_cast<void (*)(void*)>(GetProcAddress(tier0, "MemAlloc_FreeFunc"));
  if (mem_alloc == nullptr || mem_free == nullptr) {
    return nullptr;
  }
  void* storage = mem_alloc(kEntityKeyValuesSize);
  if (storage != nullptr) {
    std::memset(storage, 0, kEntityKeyValuesSize);
  }
  return storage;
#else
  return nullptr;
#endif
}

void FreeEmptyEntityKeyValues(void* key_values) {
  if (key_values == nullptr) {
    return;
  }
#if defined(_WIN32)
  HMODULE tier0 = GetModuleHandleW(L"tier0.dll");
  if (tier0 == nullptr) {
    return;
  }
  auto mem_free = reinterpret_cast<void (*)(void*)>(GetProcAddress(tier0, "MemAlloc_FreeFunc"));
  if (mem_free != nullptr) {
    mem_free(key_values);
  }
#else
  (void)key_values;
#endif
}

std::expected<void*, std::string> BuildEntityKeyValues(const KeyValuesCalls& calls,
                                                       std::span<const EntityKeyValue> pairs) {
  const bool raw_surface = calls.allocate != nullptr && calls.construct_key_values != nullptr &&
                           calls.set_key_value != nullptr;
  void* ekv = nullptr;
  if (calls.create_key_values != nullptr) {
    ekv = calls.create_key_values();
  } else if (raw_surface) {
    void* storage = calls.allocate(kEntityKeyValuesSize);
    if (storage == nullptr) {
      return std::unexpected("CEntityKeyValues allocation returned null");
    }
    ekv = calls.construct_key_values(storage, nullptr, 0);
  } else {
    return std::unexpected(
        "keyvalues construction is unresolved; require the complete native "
        "allocate/construct/set-key-value surface");
  }
  if (ekv == nullptr) {
    return std::unexpected("CEntityKeyValues construction returned null");
  }

  for (const auto& pair : pairs) {
    const MemberName name = MakeMemberName(pair.key);
    if (raw_surface && calls.create_key_values == nullptr) {
      void* member = calls.set_key_value(ekv, &name, 0);
      if (member == nullptr) {
        return std::unexpected("CEntityKeyValues could not create member '" +
                               std::string(pair.key) + "'");
      }
      if (auto applied = SetFreshRawValue(*static_cast<RawKeyValues3*>(member), pair);
          !applied.has_value()) {
        return std::unexpected(applied.error());
      }
      continue;
    }

    auto apply = [&](const auto& value) -> std::expected<void, std::string> {
      using T = std::decay_t<decltype(value)>;
      if constexpr (std::is_same_v<T, bool>) {
        if (calls.set_bool == nullptr) {
          return std::unexpected("keyvalues setter for '" + std::string(pair.key) +
                                 "' is unresolved: 'entity-keyvalues.set-bool'");
        }
        calls.set_bool(ekv, &name, value ? 1 : 0);
      } else if constexpr (std::is_same_v<T, int>) {
        if (calls.set_int == nullptr) {
          return std::unexpected("keyvalues setter for '" + std::string(pair.key) +
                                 "' is unresolved: 'entity-keyvalues.set-int'");
        }
        calls.set_int(ekv, &name, value);
      } else if constexpr (std::is_same_v<T, float>) {
        if (calls.set_float == nullptr) {
          return std::unexpected("keyvalues setter for '" + std::string(pair.key) +
                                 "' is unresolved: 'entity-keyvalues.set-float'");
        }
        calls.set_float(ekv, &name, value);
      } else if constexpr (std::is_same_v<T, std::string_view>) {
        if (calls.set_string == nullptr) {
          return std::unexpected("keyvalues setter for '" + std::string(pair.key) +
                                 "' is unresolved: 'entity-keyvalues.set-string'");
        }
        const std::string owned(value);
        calls.set_string(ekv, &name, owned.c_str());
      } else if constexpr (std::is_same_v<T, KeyValueColor>) {
        if (calls.set_color == nullptr) {
          return std::unexpected("keyvalues setter for '" + std::string(pair.key) +
                                 "' is unresolved: 'entity-keyvalues.set-color'");
        }
        const std::uint32_t packed = static_cast<std::uint32_t>(value.red) |
                                     (static_cast<std::uint32_t>(value.green) << 8) |
                                     (static_cast<std::uint32_t>(value.blue) << 16) |
                                     (static_cast<std::uint32_t>(value.alpha) << 24);
        calls.set_color(ekv, &name, packed);
      } else if constexpr (std::is_same_v<T, KeyValueVector>) {
        if (calls.set_vector == nullptr) {
          return std::unexpected("keyvalues setter for '" + std::string(pair.key) +
                                 "' is unresolved: 'entity-keyvalues.set-vector'");
        }
        const float xyz[3] = {value.x, value.y, value.z};
        calls.set_vector(ekv, &name, xyz);
      }
      return {};
    };
    if (auto applied = std::visit(apply, pair.value); !applied.has_value()) {
      return std::unexpected(applied.error());
    }
  }
  return ekv;
}

}  // namespace modlock::gameinterop
