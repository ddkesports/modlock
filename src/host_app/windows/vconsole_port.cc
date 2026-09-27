#include "host_app/windows/vconsole_port.h"

#include <winsock2.h>
#include <ws2tcpip.h>

// Winsock declarations must precede windows.h.
#include <windows.h>

#include <cstring>
#include <iostream>

namespace modlock::host_app {
namespace {

// kBindOrdinal is bind's export ordinal in ws2_32.dll; vconcomm imports it by
// ordinal rather than by name.
constexpr WORD kBindOrdinal = 2;

using BindFn = int(WSAAPI*)(SOCKET, const sockaddr*, int);

// original_bind is ws2_32's bind, read from the patched import slot. It is
// written once before the engine starts and read by the listener thread.
BindFn original_bind = nullptr;

// SharedBind binds as requested and, when the address is taken, retries the
// same address on port 0 so the system picks a free port. vconcomm sets
// SO_REUSEADDR, which lets two processes bind the port and fails only the later
// listen, where the port can no longer change; clearing it makes bind report the
// conflict.
int WSAAPI SharedBind(SOCKET socket, const sockaddr* address, int length) {
  const BOOL reuse = FALSE;
  setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse),
             sizeof(reuse));
  const int result = original_bind(socket, address, length);
  if (result == 0 || WSAGetLastError() != WSAEADDRINUSE) return result;

  sockaddr_storage retry{};
  if (length <= 0 || static_cast<size_t>(length) > sizeof(retry)) return result;
  std::memcpy(&retry, address, length);
  if (retry.ss_family == AF_INET) {
    reinterpret_cast<sockaddr_in&>(retry).sin_port = 0;
  } else if (retry.ss_family == AF_INET6) {
    reinterpret_cast<sockaddr_in6&>(retry).sin6_port = 0;
  } else {
    return result;
  }
  const int retried = original_bind(socket, reinterpret_cast<const sockaddr*>(&retry), length);
  if (retried == 0) {
    std::cerr << "[modlock] VConsole port in use; listening on an ephemeral port\n";
  }
  return retried;
}

// BindImportSlot returns the import address table entry through which module
// calls ws2_32's bind, or nullptr when module does not import it.
void** BindImportSlot(HMODULE module) {
  const auto base = reinterpret_cast<const uint8_t*>(module);
  const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
  const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
  const auto& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
  if (!directory.VirtualAddress) return nullptr;

  auto descriptor =
      reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base + directory.VirtualAddress);
  for (; descriptor->Name; ++descriptor) {
    if (_stricmp(reinterpret_cast<const char*>(base + descriptor->Name), "ws2_32.dll") != 0) {
      continue;
    }
    auto names = reinterpret_cast<const IMAGE_THUNK_DATA*>(base + descriptor->OriginalFirstThunk);
    auto slots =
        reinterpret_cast<IMAGE_THUNK_DATA*>(const_cast<uint8_t*>(base) + descriptor->FirstThunk);
    for (; names->u1.AddressOfData; ++names, ++slots) {
      if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) {
        if (IMAGE_ORDINAL(names->u1.Ordinal) == kBindOrdinal) {
          return reinterpret_cast<void**>(&slots->u1.Function);
        }
        continue;
      }
      auto import = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
      if (std::strcmp(reinterpret_cast<const char*>(import->Name), "bind") == 0) {
        return reinterpret_cast<void**>(&slots->u1.Function);
      }
    }
  }
  return nullptr;
}

}  // namespace

std::expected<void, std::string> ShareVConsolePort(const std::filesystem::path& vconcomm_dll) {
  // The engine loads this same module later and keeps it for the process
  // lifetime, so this load reference is never released.
  HMODULE module = LoadLibraryExW(vconcomm_dll.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
  if (!module) {
    return std::unexpected("VConsole port sharing: loading " + vconcomm_dll.string() +
                           " failed with error code " + std::to_string(GetLastError()));
  }
  void** slot = BindImportSlot(module);
  if (!slot) return std::unexpected("VConsole port sharing: vconcomm.dll does not import bind");

  DWORD protection = 0;
  if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &protection)) {
    return std::unexpected("VConsole port sharing: VirtualProtect failed with error code " +
                           std::to_string(GetLastError()));
  }
  original_bind = reinterpret_cast<BindFn>(*slot);
  *slot = reinterpret_cast<void*>(&SharedBind);
  VirtualProtect(slot, sizeof(*slot), protection, &protection);
  return {};
}

}  // namespace modlock::host_app
