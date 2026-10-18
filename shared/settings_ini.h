#pragma once
#include <algorithm>
#include <functional>
#include <sstream>
#include <string>
#include <type_traits>

// Settings structs described by an X-macro list of X(type, name, default, min, max), with bool or int fields,
// stored as a flat UTF-8 ini of "name=value" lines with integer values.
//
//   #define MY_SETTINGS(X) X(int, size, 10, 1, 100) X(bool, shadow, 1, 0, 1)
//   struct MySettings { SETTINGS_STRUCT_BODY(MY_SETTINGS) };
//   LoadSettingsIni(path, s); SaveSettingsIni(path, s); ClampSettings(s);

#define SETTINGS_FIELD_(t, n, d, lo, hi) t n = (t)(d);
#define SETTINGS_VISIT_(t, n, d, lo, hi) f(#n, n, (int)(lo), (int)(hi));

// The fields with their defaults, and ForEach(f) calling f(name, field, min, max) for each in list order.
#define SETTINGS_STRUCT_BODY(LIST)          \
  LIST(SETTINGS_FIELD_)                     \
  template <class F>                        \
  void ForEach(F&& f) {                     \
    LIST(SETTINGS_VISIT_)                   \
  }                                         \
  template <class F>                        \
  void ForEach(F&& f) const {               \
    LIST(SETTINGS_VISIT_)                   \
  }

// Calls onValue(key, value) for every "key=value" line, in file order. Tolerates a BOM, CRLF and spaces
// around the key and the value; skips lines without '=' and values that do not start with an integer.
// False when the file cannot be opened.
bool ReadIniInts(const std::wstring& path, const std::function<void(const std::string& key, int value)>& onValue);

// Brings every field into its [min, max].
template <class S>
void ClampSettings(S& s) {
  s.ForEach([](const char*, auto& v, int lo, int hi) {
    v = static_cast<std::remove_reference_t<decltype(v)>>(std::clamp((int)v, lo, hi));
  });
}

// Fields missing from the file keep their values; unknown keys are ignored. False when the file cannot be read.
template <class S>
bool LoadSettingsIni(const std::wstring& path, S& s) {
  bool ok = ReadIniInts(path, [&](const std::string& key, int value) {
    s.ForEach([&](const char* name, auto& v, int lo, int hi) {
      // Bools: any non-zero means on, so clamping before the cast keeps "-5" from becoming true.
      if (key == name) v = static_cast<std::remove_reference_t<decltype(v)>>(std::clamp(value, lo, hi));
    });
  });
  if (!ok) return false;
  ClampSettings(s);
  return true;
}

// Every field as "name=value\n" in list order, LF only.
template <class S>
std::string SettingsIniText(const S& s) {
  std::ostringstream o;
  s.ForEach([&](const char* name, const auto& v, int, int) { o << name << '=' << (int)v << '\n'; });
  return o.str();
}

// Writes through `path`.tmp and a rename, so a reader never sees a partial file.
bool WriteIniText(const std::wstring& path, const std::string& text);

template <class S>
bool SaveSettingsIni(const std::wstring& path, const S& s) {
  return WriteIniText(path, SettingsIniText(s));
}
