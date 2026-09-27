#include "modlock/gameinterop/console_variables.h"

#include <cstdint>
#include <cstring>

#include "modlock/gameinterop/mapped_module_image.h"

namespace modlock::gameinterop {
namespace {

// CCvar's CUtlLinkedList<ConVarData*> uses CUtlLeanVector storage.
// Installed tier0.dll GetConVarData RVA 0x6be30 and Next RVA 0x6c240
// confirm capacity bits +0x42, entries +0x48, stride 16, next +0xa;
// First RVA 0x6c100 reads head +0x50. Public iteration and FindConVar
// filter DEVELOPMENTONLY entries, so discovery follows the registry itself.
constexpr size_t kCapacityOffset = 0x42;
constexpr size_t kEntriesOffset = 0x48;
constexpr size_t kHeadOffset = 0x50;
constexpr uint16_t kInvalidIndex = 0xffff;
constexpr uint64_t kExposureFlags = (1ull << 1) | (1ull << 4) | (1ull << 32);

// ConVarData x64 layout from sourcesdk public/tier1/convar.h.
constexpr size_t kTypeOffset = 0x28;
constexpr size_t kFlagsOffset = 0x30;
constexpr size_t kValueOffset = 0x58;

}  // namespace

std::expected<ConsoleVariables, std::string> ConsoleVariables::Resolve() {
  const auto instance = ResolveEngineInterface(L"tier0.dll", "VEngineCvar007");
  if (!instance) return std::unexpected(instance.error());
  return Bind(*instance);
}

std::expected<ConsoleVariables, std::string> ConsoleVariables::Bind(void* instance) {
  if (!instance) return std::unexpected("console variables: interface is null");
  ConsoleVariables variables;
  variables.instance_ = instance;
  return variables;
}

std::expected<std::byte*, std::string> ConsoleVariables::Find(const char* name) const {
  if (!name || !*name) return std::unexpected("console variables: empty name");
  if (!instance_) return std::unexpected("console variables: interface is unavailable");
  const auto* registry = static_cast<const std::byte*>(instance_);
  uint16_t capacity, index;
  const std::byte* entries = nullptr;
  std::memcpy(&capacity, registry + kCapacityOffset, sizeof(capacity));
  capacity &= 0x7fff;
  std::memcpy(&entries, registry + kEntriesOffset, sizeof(entries));
  std::memcpy(&index, registry + kHeadOffset, sizeof(index));
  uint16_t previous = kInvalidIndex;
  for (size_t visited = 0; index != kInvalidIndex; ++visited) {
    if (!entries || index >= capacity || visited >= capacity)
      return std::unexpected("console variables: invalid registry bounds or cycle");
    const auto* entry = entries + size_t(index) * 16;
    uint16_t back, next;
    std::byte* data = nullptr;
    std::memcpy(&data, entry, sizeof(data));
    std::memcpy(&back, entry + 8, sizeof(back));
    std::memcpy(&next, entry + 10, sizeof(next));
    if (back != previous || !data)
      return std::unexpected("console variables: invalid registry link or data");
    const char* actual_name = nullptr;
    std::memcpy(&actual_name, data, sizeof(actual_name));
    if (!actual_name) return std::unexpected("console variables: missing registry name");
    if (std::strcmp(actual_name, name) == 0) return data;
    previous = index;
    index = next;
  }
  return std::unexpected(std::string("console variable not found: ") + name);
}

std::expected<void, std::string> ConsoleVariables::Expose(const char* name) const {
  auto data = Find(name);
  if (!data) return std::unexpected(data.error());
  uint64_t flags;
  std::memcpy(&flags, *data + kFlagsOffset, sizeof(flags));
  flags &= ~kExposureFlags;
  std::memcpy(*data + kFlagsOffset, &flags, sizeof(flags));
  return {};
}

std::expected<bool, std::string> ConsoleVariables::ReadBool(const char* name) const {
  auto data = Find(name);
  if (!data) return std::unexpected(data.error());
  int16_t type;
  std::memcpy(&type, *data + kTypeOffset, sizeof(type));
  if (type != 0) return std::unexpected(std::string("console variable is not Boolean: ") + name);
  return std::to_integer<unsigned char>((*data)[kValueOffset]) != 0;
}

std::expected<float, std::string> ConsoleVariables::ReadFloat(const char* name) const {
  auto data = Find(name);
  if (!data) return std::unexpected(data.error());
  int16_t type;
  std::memcpy(&type, *data + kTypeOffset, sizeof(type));
  // EConVarType_Float32.
  if (type != 7) return std::unexpected(std::string("console variable is not a float: ") + name);
  float value;
  std::memcpy(&value, *data + kValueOffset, sizeof(value));
  return value;
}

std::expected<void, std::string> ConsoleVariables::SetBool(const char* name, bool value) const {
  auto data = Find(name);
  if (!data) return std::unexpected(data.error());
  int16_t type;
  std::memcpy(&type, *data + kTypeOffset, sizeof(type));
  if (type != 0) return std::unexpected(std::string("console variable is not Boolean: ") + name);
  (*data)[kValueOffset] = std::byte{value};
  return {};
}

std::expected<void, std::string> ConsoleVariables::SetFloat(const char* name, float value) const {
  auto data = Find(name);
  if (!data) return std::unexpected(data.error());
  int16_t type;
  std::memcpy(&type, *data + kTypeOffset, sizeof(type));
  // EConVarType_Float32.
  if (type != 7) return std::unexpected(std::string("console variable is not a float: ") + name);
  std::memcpy(*data + kValueOffset, &value, sizeof(value));
  return {};
}

std::expected<void, std::string> ConsoleVariables::SetInt32(const char* name, int32_t value) const {
  auto data = Find(name);
  if (!data) return std::unexpected(data.error());
  int16_t type;
  std::memcpy(&type, *data + kTypeOffset, sizeof(type));
  // EConVarType_Int32.
  if (type != 3) return std::unexpected(std::string("console variable is not a 32-bit integer: ") + name);
  std::memcpy(*data + kValueOffset, &value, sizeof(value));
  return {};
}

std::expected<void, std::string> ConsoleVariables::AllowServerChanges(const char* name) const {
  auto data = Find(name);
  if (!data) return std::unexpected(data.error());
  uint64_t flags;
  std::memcpy(&flags, *data + kFlagsOffset, sizeof(flags));
  flags &= ~(kExposureFlags | (1ull << 14));
  std::memcpy(*data + kFlagsOffset, &flags, sizeof(flags));
  return {};
}

}  // namespace modlock::gameinterop
