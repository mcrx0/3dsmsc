#include <cstddef>
#include <cstdint>
#include <string>

#include "tag_readers.hpp"

namespace threedsmsc::tags {
namespace {

// The text encoding byte that starts an ID3v2 text frame (3, UTF-8, is the fall-through case).
constexpr unsigned char encoding_latin1 = 0;
constexpr unsigned char encoding_utf16_bom = 1;
constexpr unsigned char encoding_utf16_be = 2;

constexpr std::uint32_t high_surrogate_first = 0xD800;
constexpr std::uint32_t high_surrogate_last = 0xDBFF;
constexpr std::uint32_t low_surrogate_first = 0xDC00;
constexpr std::uint32_t low_surrogate_last = 0xDFFF;

void append_utf8(std::string& output, std::uint32_t value) {
  if (value <= 0x7F) {
    output.push_back(static_cast<char>(value));
  } else if (value <= 0x7FF) {
    output.push_back(static_cast<char>(0xC0 | (value >> 6)));
    output.push_back(static_cast<char>(0x80 | (value & 0x3F)));
  } else if (value >= 0x10000) {
    output.push_back(static_cast<char>(0xF0 | (value >> 18)));
    output.push_back(static_cast<char>(0x80 | ((value >> 12) & 0x3F)));
    output.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3F)));
    output.push_back(static_cast<char>(0x80 | (value & 0x3F)));
  } else {
    output.push_back(static_cast<char>(0xE0 | (value >> 12)));
    output.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3F)));
    output.push_back(static_cast<char>(0x80 | (value & 0x3F)));
  }
}

std::uint32_t read_utf16_unit(const unsigned char* bytes, bool big_endian) {
  return big_endian ? (static_cast<std::uint32_t>(bytes[0]) << 8) | bytes[1]
                    : (static_cast<std::uint32_t>(bytes[1]) << 8) | bytes[0];
}

// Reads UTF-16 up to a NUL unit, combining surrogate pairs into one character.
std::string decode_utf16(const unsigned char* text, std::size_t size, bool big_endian) {
  size -= size % 2;  // a trailing odd byte is not a whole unit
  std::string result;
  for (std::size_t offset = 0; offset < size; offset += 2) {
    std::uint32_t value = read_utf16_unit(text + offset, big_endian);
    if (value == 0)
      break;
    const bool starts_pair = value >= high_surrogate_first && value <= high_surrogate_last;
    if (starts_pair && offset + 4 <= size) {
      const std::uint32_t low = read_utf16_unit(text + offset + 2, big_endian);
      if (low >= low_surrogate_first && low <= low_surrogate_last) {
        value = 0x10000 + ((value - high_surrogate_first) << 10) + (low - low_surrogate_first);
        offset += 2;
      }
    }
    append_utf8(result, value);
  }
  return result;
}

std::string decode_latin1(const unsigned char* text, std::size_t length) {
  std::string result;
  result.reserve(length);
  for (std::size_t index = 0; index < length; ++index)
    append_utf8(result, text[index]);
  return result;
}

// Length of the text before the first NUL byte.
std::size_t terminated_length(const unsigned char* text, std::size_t size) {
  std::size_t end = 0;
  while (end < size && text[end] != 0)
    ++end;
  return end;
}

}

std::string decode_id3_text(const unsigned char* data, std::size_t size) {
  if (size == 0)
    return {};
  const unsigned char encoding = data[0];
  const unsigned char* text = data + 1;
  const std::size_t text_size = size - 1;
  if (encoding == encoding_utf16_bom) {
    if (text_size < 2)
      return {};
    const bool bom_be = text[0] == 0xFE && text[1] == 0xFF;
    const bool bom_le = text[0] == 0xFF && text[1] == 0xFE;
    if (!bom_be && !bom_le)
      return {};
    return decode_utf16(text + 2, text_size - 2, bom_be);
  }
  if (encoding == encoding_utf16_be)
    return decode_utf16(text, text_size, true);
  const std::size_t length = terminated_length(text, text_size);
  if (encoding == encoding_latin1)
    return decode_latin1(text, length);
  return std::string(reinterpret_cast<const char*>(text), length);  // UTF-8
}

std::uint16_t parse_number_prefix(const std::string& value) {
  std::uint32_t number = 0;
  bool found = false;
  for (const char digit : value) {
    if (digit < '0' || digit > '9')
      break;
    found = true;
    number = number * 10 + static_cast<std::uint32_t>(digit - '0');
    if (number > 9999)
      return 0;
  }
  return found ? static_cast<std::uint16_t>(number) : 0;
}

}
