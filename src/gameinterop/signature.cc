#include "modlock/gameinterop/signature.h"

#include <cctype>
#include <optional>

namespace modlock::gameinterop {
namespace {

// NibbleValue decodes one hex digit, or nullopt for anything else.
std::optional<int> NibbleValue(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return std::nullopt;
}

}  // namespace

std::expected<Signature, std::string> ParseSignature(std::string id, std::string_view pattern) {
  Signature sig{.id = std::move(id)};
  for (size_t i = 0; i < pattern.size();) {
    unsigned char c = static_cast<unsigned char>(pattern[i]);
    if (std::isspace(c)) {
      i++;
      continue;
    }
    if (pattern[i] == '?') {
      // One wildcard byte per token; the signature database writes it as
      // '?' or '??'. Consume the whole run of question marks.
      while (i < pattern.size() && pattern[i] == '?') {
        i++;
      }
      sig.bytes.push_back(0);
      sig.wildcard.push_back(true);
      continue;
    }
    auto high = NibbleValue(pattern[i]);
    auto low = i + 1 < pattern.size() ? NibbleValue(pattern[i + 1]) : std::nullopt;
    if (!high || !low) {
      return std::unexpected("signature '" + sig.id + "': expected hex byte at offset " +
                             std::to_string(i));
    }
    sig.bytes.push_back(static_cast<uint8_t>(*high * 16 + *low));
    sig.wildcard.push_back(false);
    i += 2;
  }
  if (sig.bytes.empty()) {
    return std::unexpected("signature '" + sig.id + "': empty pattern");
  }
  return sig;
}

std::vector<size_t> SignatureScan(std::span<const uint8_t> data, const Signature& sig) {
  std::vector<size_t> hits;
  if (sig.bytes.empty() || sig.bytes.size() > data.size()) {
    return hits;
  }
  const size_t last_start = data.size() - sig.bytes.size();
  for (size_t start = 0; start <= last_start; start++) {
    bool matched = true;
    for (size_t k = 0; k < sig.bytes.size(); k++) {
      if (!sig.wildcard[k] && data[start + k] != sig.bytes[k]) {
        matched = false;
        break;
      }
    }
    if (matched) {
      hits.push_back(start);
    }
  }
  return hits;
}

}  // namespace modlock::gameinterop
