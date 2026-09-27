#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

#include "modlock/export.h"

namespace google::protobuf {
class MessageLite;
}

namespace modlock::gameinterop {

// NativeUserMessages sends generated messages through engine-owned serializers.
// Calls run on the game frame thread while the engine modules remain loaded.
class MODLOCK_API NativeUserMessages {
 public:
  // TryCreate borrows the loaded network-message and game-event interfaces.
  static std::expected<NativeUserMessages, std::string> TryCreate();

  // Chat sends server-authored global chat to the addressed player slot.
  std::expected<void, std::string> Chat(int32_t slot, std::string_view text) const;

  // CenterText replaces the native center text. Refresh while visible and send
  // empty text to clear it; it does not enqueue an announcement toast.
  std::expected<void, std::string> CenterText(int32_t slot, std::string_view text) const;

  // Announce sends a stock game announcement to the addressed player slot.
  std::expected<void, std::string> Announce(int32_t slot, std::string_view title,
                                            std::string_view description) const;

 private:
  NativeUserMessages(void* messages, void* events) : messages_(messages), events_(events) {}
  std::expected<void, std::string> Send(int32_t slot, int32_t id,
                                        const google::protobuf::MessageLite& message) const;

  // Engine interfaces outlive the host's frame callbacks.
  void* messages_;
  void* events_;
};

}  // namespace modlock::gameinterop
