#include "modlock/gameinterop/native_user_messages.h"

#include <cstddef>
#include <cstring>
#include <memory>
#include <vector>

#include "google/protobuf/io/coded_stream.h"
#include "google/protobuf/io/zero_copy_stream_impl_lite.h"
#include "modlock/gameinterop/mapped_module_image.h"
#include "proto/modlock/chat.pb.h"
#include "proto/modlock/hud.pb.h"

namespace modlock::gameinterop {
namespace {

#if defined(_WIN32)

// BitReader matches the installed networksystem's bf_read. Unserialize checks
// data bits at +12 and cursor at +16 before reading the length-prefixed proto.
// The native serializer owns protobuf allocation, parsing, and deallocation.
struct BitReader {
  const uint8_t* data;
  int32_t bytes;
  int32_t bits;
  int32_t cursor = 0;
  const char* debug_name = "modlock user message";
  bool overflow = false;
  bool assert_overflow = false;
};
static_assert(offsetof(BitReader, cursor) == 16);
static_assert(offsetof(BitReader, debug_name) == 24);
static_assert(sizeof(BitReader) == 40);

#endif

}  // namespace

std::expected<NativeUserMessages, std::string> NativeUserMessages::TryCreate() {
  const auto messages = ResolveEngineInterface(L"networksystem.dll", "NetworkMessagesVersion001");
  if (!messages) return std::unexpected(messages.error());
  const auto events = ResolveEngineInterface(L"engine2.dll", "GameEventSystemServerV001");
  if (!events) return std::unexpected(events.error());
  return NativeUserMessages(*messages, *events);
}

std::expected<void, std::string> NativeUserMessages::Chat(int32_t slot,
                                                          std::string_view text) const {
  engine::ChatMessage message;
  message.set_player_slot(-1);
  message.set_text(text);
  message.set_all_chat(true);
  return Send(slot, 314, message);
}

std::expected<void, std::string> NativeUserMessages::CenterText(int32_t slot,
                                                                std::string_view text) const {
  engine::HudTextMessage message;
  message.set_dest(4);
  message.add_param(text);
  return Send(slot, 124, message);
}

std::expected<void, std::string> NativeUserMessages::Announce(int32_t slot, std::string_view title,
                                                              std::string_view description) const {
  engine::HudAnnouncement message;
  message.set_title_locstring(title);
  message.set_description_locstring(description);
  return Send(slot, 363, message);
}

std::expected<void, std::string> NativeUserMessages::Send(
    int32_t slot, int32_t id, const google::protobuf::MessageLite& message) const {
  if (slot < 0 || slot >= 64) return std::unexpected("native user recipient slot is invalid");
  const auto size = message.ByteSizeLong();
  if (size == 0 || size > 4096) return std::unexpected("native user message size is invalid");
#if defined(_WIN32)
  const auto table = *static_cast<void***>(messages_);
  // The installed engine compares all 32 bits of the message ID. A 16-bit
  // declaration leaves the upper argument bits unspecified on Windows x64.
  using Find = void* (*)(void*, int32_t);
  auto* serializer = reinterpret_cast<Find>(table[31])(messages_, id);
  if (!serializer) return std::unexpected("native user message serializer unavailable");
  const auto methods = *static_cast<void***>(serializer);
  using Allocate = void* (*)(void*);
  auto* native_message = reinterpret_cast<Allocate>(methods[6])(serializer);
  if (!native_message) return std::unexpected("native user message allocation failed");
  const auto release = [&](void* value) {
    using Deallocate = void (*)(void*, void*, void*);
    reinterpret_cast<Deallocate>(table[9])(messages_, serializer, value);
  };
  const std::unique_ptr<void, decltype(release)> owned_message(native_message, release);

  std::string encoded;
  {
    google::protobuf::io::StringOutputStream output(&encoded);
    google::protobuf::io::CodedOutputStream coded(&output);
    coded.WriteVarint32(static_cast<uint32_t>(size));
    if (!message.SerializeToCodedStream(&coded) || coded.HadError())
      return std::unexpected("native user message serialization failed");
  }
  // The bit reader can fetch the final word; padding is addressable but is not
  // part of the declared stream length.
  std::vector<uint8_t> bytes(encoded.size() + 4);
  std::memcpy(bytes.data(), encoded.data(), encoded.size());
  BitReader reader{bytes.data(), static_cast<int32_t>(encoded.size()),
                   static_cast<int32_t>(encoded.size() * 8)};
  using Unserialize = bool (*)(void*, BitReader&, void*);
  if (!reinterpret_cast<Unserialize>(methods[8])(serializer, reader, native_message) ||
      reader.overflow || reader.cursor != reader.bits)
    return std::unexpected("native user message decoding failed");

  const uint64_t recipients = uint64_t{1} << slot;
  const auto event_methods = *static_cast<void***>(events_);
  using Post = void (*)(void*, int32_t, bool, int32_t, const uint64_t*, void*, const void*,
                        unsigned long, int32_t);
  // BUF_RELIABLE=1. PostEventAbstract copies the message before returning.
  // The client count is the mask's bit extent, including unselected lower slots.
  reinterpret_cast<Post>(event_methods[16])(events_, -1, false, slot + 1, &recipients, serializer,
                                            native_message, 0, 1);
  return {};
#else
  (void)id;
  return std::unexpected("native user messages require the Windows host");
#endif
}

}  // namespace modlock::gameinterop
