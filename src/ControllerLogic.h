#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

// These helpers have no Arduino dependencies and can be exercised on the host.
inline bool parseUnsigned(const char* text, int maximum, int& result) {
  if (!text || !*text) return false;
  int value = 0;
  for (const char* p = text; *p; ++p) {
    if (*p < '0' || *p > '9') return false;
    int digit = *p - '0';
    if (value > maximum / 10 || (value == maximum / 10 && digit > maximum % 10)) return false;
    value = value * 10 + digit;
  }
  result = value;
  return true;
}
inline bool scanTimedOut(uint32_t now, uint32_t started, uint32_t timeout) {
  return uint32_t(now - started) >= timeout;
}
enum class ScanDecision { Wait, Received, Timeout };
inline ScanDecision scanDecision(bool received, uint32_t now, uint32_t started) {
  if (received && uint32_t(now - started) >= 500) return ScanDecision::Received;
  if (scanTimedOut(now, started, 1500)) return ScanDecision::Timeout;
  return ScanDecision::Wait;
}
template<class Text> void appendJsonString(Text& result, const char* value) {
  result += '"';
  for (const unsigned char* p = reinterpret_cast<const unsigned char*>(value); *p; ++p) {
    if (*p == '"' || *p == '\\') { result += '\\'; result += char(*p); }
    else if (*p < 0x20) {
      char escaped[7];
      snprintf(escaped, sizeof(escaped), "\\u%04x", *p);
      result += escaped;
    } else result += char(*p);
  }
  result += '"';
}
