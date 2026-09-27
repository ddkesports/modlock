#pragma once

#include <expected>
#include <string>

#include "modlock/export.h"

namespace modlock::gameinterop {

class ModuleImage;

// NativeSound emits native sound events through the mapped server module.
// The server module must remain mapped throughout this adapter's lifetime.
class MODLOCK_API NativeSound {
 public:
  // TryCreate resolves the unique native sound entry point in server.dll.
  static std::expected<NativeSound, std::string> TryCreate(const ModuleImage& server);

  // Emit borrows a live entity on the server frame thread for this call only.
  // Pitch is a percentage; delay is in seconds. True means the native call
  // was dispatched, not that the client loaded or audibly played the sound.
  [[nodiscard]] bool Emit(void* entity, const char* sound_name, int pitch = 100,
                          float volume = 1.0f, float delay = 0.0f) const;

 private:
#if defined(_WIN32)
  using EmitFunction = void(__fastcall*)(void*, const char*, int, float, float);
#else
  using EmitFunction = void (*)(void*, const char*, int, float, float);
#endif

  explicit NativeSound(EmitFunction emit) : emit_(emit) {}

  EmitFunction emit_;
};

}  // namespace modlock::gameinterop
