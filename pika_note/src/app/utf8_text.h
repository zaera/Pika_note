#pragma once
#include <stdint.h>
#include <stddef.h>

// Invalid bytes consume one byte and render as '?'; never walk past NUL.
inline uint32_t pikaNextCodepoint(const char*& text) {
  const uint8_t first = static_cast<uint8_t>(*text++);
  if (first < 0x80) return first;
  int count = first >= 0xC2 && first <= 0xDF ? 1 :
              first >= 0xE0 && first <= 0xEF ? 2 :
              first >= 0xF0 && first <= 0xF4 ? 3 : 0;
  if (!count) return '?';
  const char* tail = text;
  uint32_t code = first & ((1U << (6 - count)) - 1);
  for (int i = 0; i < count; ++i) {
    uint8_t byte = static_cast<uint8_t>(tail[i]);
    if (!byte || (byte & 0xC0) != 0x80) return '?';
    code = (code << 6) | (byte & 0x3F);
  }
  if ((count == 1 && code < 0x80) || (count == 2 && code < 0x800) ||
      (count == 3 && code < 0x10000) || (code >= 0xD800 && code <= 0xDFFF) || code > 0x10FFFF) return '?';
  text += count;
  return code;
}

inline size_t pikaUtf8Prefix(const char* text, size_t bytes) {
  size_t length = 0;
  while (length < bytes && text[length]) ++length;
  if (length == bytes && text[length])
    while (length > 0 && (static_cast<uint8_t>(text[length]) & 0xC0) == 0x80) --length;
  return length;
}

inline size_t pikaUtf8Previous(const char* text, size_t length) {
  if (!length) return 0;
  --length;
  while (length && (static_cast<uint8_t>(text[length]) & 0xC0) == 0x80) --length;
  return length;
}

// Language for deterministic speech formatting, independent of UI settings.
// Model prompts detect the exact language; this fast path covers Russian/English.
inline bool pikaMostlyCyrillic(const char* text) {
  unsigned cyrillic = 0, latin = 0;
  while (*text) {
    const uint32_t code = pikaNextCodepoint(text);
    if (code >= 0x400 && code <= 0x4FF) ++cyrillic;
    else if ((code >= 'A' && code <= 'Z') || (code >= 'a' && code <= 'z')) ++latin;
  }
  return cyrillic > 0 && cyrillic * 2 >= latin;
}
