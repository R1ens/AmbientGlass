// shared/settings_ini.h with a settings list of its own: the X-macro load/save is not tied to VA_SETTINGS.
#include <windows.h>

#include <string>

#include "common.h"
#include "settings_ini.h"
#include "test_framework.h"

namespace {

#define TEST_SETTINGS(X)        \
  X(int,  size,    10,  1, 100) \
  X(bool, shadow,  1,   0, 1)   \
  X(int,  offset, -5, -20, 20)

struct TestSettings {
  SETTINGS_STRUCT_BODY(TEST_SETTINGS)
};

std::wstring TempPath(const wchar_t* tag) {
  wchar_t buf[MAX_PATH]{};
  DWORD n = GetTempPathW(MAX_PATH, buf);
  return std::wstring(buf, n) + L"vlcambient_test_ini_" + tag + L"_" + std::to_wstring(GetCurrentProcessId()) +
         L".ini";
}

}  // namespace

TEST(settings_ini_other_list_defaults_and_order) {
  TestSettings s;
  CHECK_EQ(s.size, 10);
  CHECK_EQ(s.shadow, true);
  CHECK_EQ(s.offset, -5);
  CHECK_EQ(SettingsIniText(s), std::string("size=10\nshadow=1\noffset=-5\n"));
}

TEST(settings_ini_other_list_clamp) {
  TestSettings s;
  s.size = 500;
  s.offset = -99;
  ClampSettings(s);
  CHECK_EQ(s.size, 100);
  CHECK_EQ(s.offset, -20);
}

TEST(settings_ini_other_list_round_trip) {
  const std::wstring path = TempPath(L"rt");
  TestSettings a;
  a.size = 77;
  a.shadow = false;
  a.offset = 3;
  CHECK(SaveSettingsIni(path, a));
  TestSettings b;
  CHECK(LoadSettingsIni(path, b));
  CHECK_EQ(b.size, 77);
  CHECK_EQ(b.shadow, false);
  CHECK_EQ(b.offset, 3);
  DeleteFileW(path.c_str());
}

TEST(settings_ini_other_list_load_clamps_and_skips) {
  const std::wstring path = TempPath(L"odd");
  CHECK(WriteAll(path, "\xEF\xBB\xBF size = 1000 \r\nshadow=-5\nunknown=3\noffset=abc\nnoequals\n"));
  TestSettings s;
  CHECK(LoadSettingsIni(path, s));
  CHECK_EQ(s.size, 100);       // clamped to max
  CHECK_EQ(s.shadow, false);   // -5 clamps to 0 before the cast to bool
  CHECK_EQ(s.offset, -5);      // not a number: the default stays
  DeleteFileW(path.c_str());
  TestSettings t;
  CHECK(!LoadSettingsIni(path, t));  // missing file
}
