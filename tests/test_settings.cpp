// Settings: X-macro defaults, Clamp, ini Save/Load round trips and tolerance of odd input.
#include <windows.h>

#include <fstream>
#include <sstream>
#include <string>
#include <type_traits>

#include "settings.h"
#include "test_framework.h"

namespace {

// A unique file under %TEMP%, deleted (with SaveSettings' .tmp sibling) when the test ends.
struct TempIni {
  std::wstring path;
  explicit TempIni(const wchar_t* tag) {
    wchar_t buf[MAX_PATH]{};
    DWORD n = GetTempPathW(MAX_PATH, buf);
    path.assign(buf, n);
    path += L"vlcambient_test_";
    path += tag;
    path += L"_" + std::to_wstring(GetCurrentProcessId()) + L".ini";
  }
  ~TempIni() {
    DeleteFileW(path.c_str());
    DeleteFileW((path + L".tmp").c_str());
  }
};

bool WriteRaw(const std::wstring& path, const std::string& bytes) {
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  f.write(bytes.data(), (std::streamsize)bytes.size());
  return f.good();
}

std::string ReadRaw(const std::wstring& path) {
  std::ifstream f(path, std::ios::binary);
  std::ostringstream o;
  o << f.rdbuf();
  return o.str();
}

// Every field moved away from its default: the min when the default is not the min, else the max.
Settings Modified() {
  Settings s;
#define VA_MOD(t, n, d, lo, hi) s.n = (t)((d) == (lo) ? (hi) : (lo));
  VA_SETTINGS(VA_MOD)
#undef VA_MOD
  return s;
}

}  // namespace

TEST(settings_defaults_match_x_macro) {
  Settings s;
#define VA_DEF(t, n, d, lo, hi) \
  CHECK_EQ((int)s.n, (int)(d)); \
  CHECK((int)(lo) <= (int)(d) && (int)(d) <= (int)(hi));
  VA_SETTINGS(VA_DEF)
#undef VA_DEF
  CHECK(s.enabled);
  CHECK_EQ(s.blur, 30);
  CHECK_EQ(s.spread, 17);
}

TEST(settings_modified_differs_in_every_field) {
  Settings d, m = Modified();
#define VA_DIFF(t, n, d_, lo, hi) CHECK((int)d.n != (int)m.n);
  VA_SETTINGS(VA_DIFF)
#undef VA_DIFF
  CHECK(!(d == m));
}

TEST(settings_round_trip_defaults) {
  TempIni f(L"rt_defaults");
  Settings defaults;
  CHECK(SaveSettings(f.path, defaults));
  Settings loaded = Modified();  // start far from the defaults so the load has to do the work
  CHECK(LoadSettings(f.path, loaded));
  CHECK(loaded == defaults);
  CHECK_EQ(GetFileAttributesW((f.path + L".tmp").c_str()), INVALID_FILE_ATTRIBUTES);  // no .tmp left behind
}

TEST(settings_round_trip_modified) {
  TempIni f(L"rt_modified");
  Settings m = Modified();
  CHECK(SaveSettings(f.path, m));
  Settings loaded;
  CHECK(LoadSettings(f.path, loaded));
  CHECK(loaded == m);
#define VA_EQ(t, n, d, lo, hi) CHECK_EQ((int)loaded.n, (int)m.n);
  VA_SETTINGS(VA_EQ)
#undef VA_EQ
}

TEST(settings_save_format_is_key_equals_int_per_line) {
  TempIni f(L"format");
  Settings s;
  s.blur = 42;
  s.enabled = false;
  CHECK(SaveSettings(f.path, s));
  std::string text = ReadRaw(f.path);
  CHECK(text.find("blur=42\n") != std::string::npos);
  CHECK(text.find("enabled=0\n") != std::string::npos);
  CHECK(text.find('\r') == std::string::npos);  // LF only, written in binary mode
  std::ostringstream expected;
#define VA_LINE(t, n, d, lo, hi) expected << #n << '=' << (int)s.n << '\n';
  VA_SETTINGS(VA_LINE)
#undef VA_LINE
  CHECK_EQ(text, expected.str());
}

TEST(settings_load_missing_file_returns_false_and_keeps_values) {
  TempIni f(L"missing");
  Settings s = Modified();
  Settings before = s;
  CHECK(!LoadSettings(f.path, s));
  CHECK(s == before);
}

TEST(settings_clamp_enforces_every_max) {
#define VA_MAX(t, n, d, lo, hi)    \
  {                                \
    Settings s;                    \
    s.n = (t)((hi) + 5);           \
    s.Clamp();                     \
    CHECK_EQ((int)s.n, (int)(hi)); \
  }
  VA_SETTINGS(VA_MAX)
#undef VA_MAX
}

TEST(settings_clamp_enforces_every_min) {
  // A bool cannot hold min-5: (bool)(-5) is already true before Clamp runs, so for bool fields Clamp
  // can only guarantee the value lies within [lo, hi]. Int fields must land exactly on the minimum.
#define VA_MIN(t, n, d, lo, hi)                                            \
  {                                                                        \
    Settings s;                                                            \
    s.n = (t)((lo) - 5);                                                   \
    s.Clamp();                                                             \
    CHECK((int)(lo) <= (int)s.n && (int)s.n <= (int)(hi));                 \
    if constexpr (!std::is_same_v<t, bool>) CHECK_EQ((int)s.n, (int)(lo)); \
  }
  VA_SETTINGS(VA_MIN)
#undef VA_MIN
}

TEST(settings_clamp_keeps_in_range_values) {
  Settings m = Modified();
  Settings c = m;
  c.Clamp();
  CHECK(c == m);
  Settings d;
  d.Clamp();
  CHECK(d == Settings{});
}

TEST(settings_load_clamps_out_of_range_values) {
  TempIni f(L"oor");
  std::ostringstream o;
#define VA_OVER(t, n, d, lo, hi) o << #n << '=' << ((hi) + 5) << '\n';
  VA_SETTINGS(VA_OVER)
#undef VA_OVER
  CHECK(WriteRaw(f.path, o.str()));
  Settings s;
  CHECK(LoadSettings(f.path, s));
#define VA_AT_MAX(t, n, d, lo, hi) CHECK_EQ((int)s.n, (int)(hi));
  VA_SETTINGS(VA_AT_MAX)
#undef VA_AT_MAX

  std::ostringstream u;
#define VA_UNDER(t, n, d, lo, hi) u << #n << '=' << ((lo) - 5) << '\n';
  VA_SETTINGS(VA_UNDER)
#undef VA_UNDER
  CHECK(WriteRaw(f.path, u.str()));
  Settings s2;
  CHECK(LoadSettings(f.path, s2));
#define VA_AT_MIN(t, n, d, lo, hi) CHECK_EQ((int)s2.n, (int)(lo));
  VA_SETTINGS(VA_AT_MIN)
#undef VA_AT_MIN
  // A negative number for a bool key clamps to 0 before the cast, so it does not read as true.
  CHECK_EQ((int)s2.enabled, 0);
}

TEST(settings_load_tolerates_crlf) {
  TempIni f(L"crlf");
  CHECK(WriteRaw(f.path, "blur=55\r\nedge=20\r\nenabled=0\r\n"));
  Settings s;
  CHECK(LoadSettings(f.path, s));
  CHECK_EQ(s.blur, 55);
  CHECK_EQ(s.edge, 20);
  CHECK(!s.enabled);
}

TEST(settings_load_without_trailing_newline) {
  TempIni f(L"nonl");
  CHECK(WriteRaw(f.path, "blur=55\nedge=20"));
  Settings s;
  CHECK(LoadSettings(f.path, s));
  CHECK_EQ(s.blur, 55);
  CHECK_EQ(s.edge, 20);
}

TEST(settings_load_with_utf8_bom) {
  TempIni f(L"bom");
  // The file loads and every key after the first line is read.
  CHECK(WriteRaw(f.path, "\xEF\xBB\xBF# written by notepad\nblur=55\nedge=20\n"));
  Settings s;
  CHECK(LoadSettings(f.path, s));
  CHECK_EQ(s.blur, 55);
  CHECK_EQ(s.edge, 20);

  // The BOM is stripped, so a key on the very first line is read too.
  CHECK(WriteRaw(f.path, "\xEF\xBB\xBF" "blur=55\nedge=20\n"));  // split: "\xBFb" would be one escape
  Settings s2;
  CHECK(LoadSettings(f.path, s2));
  CHECK_EQ(s2.blur, 55);
  CHECK_EQ(s2.edge, 20);
}

TEST(settings_load_spaces_around_equals) {
  TempIni f(L"spaces");
  // Keys and values are trimmed, so hand-edited files with spaces or tabs around '=' load.
  CHECK(WriteRaw(f.path,
                 "blur = 55\n"
                 "edge= 20\n"
                 "spread=33 \n"
                 " fadeStart=7\n"
                 "fadeCurve =9\n"
                 "\tedgeBlend=11\r\n"));
  Settings s;
  CHECK(LoadSettings(f.path, s));
  CHECK_EQ(s.blur, 55);
  CHECK_EQ(s.edge, 20);
  CHECK_EQ(s.spread, 33);
  CHECK_EQ(s.fadeStart, 7);
  CHECK_EQ(s.fadeCurve, 9);
  CHECK_EQ(s.edgeBlend, 11);
}

TEST(settings_load_ignores_unknown_keys_and_junk_lines) {
  TempIni f(L"unknown");
  CHECK(WriteRaw(f.path,
                 "; comment\n"
                 "[section]\n"
                 "\n"
                 "noequals\n"
                 "unknownKey=99\n"
                 "Blur=70\n"  // case matters: not "blur"
                 "blur=55\n"
                 "=12\n"
                 "edge=20\n"));
  Settings s;
  CHECK(LoadSettings(f.path, s));
  CHECK_EQ(s.blur, 55);
  CHECK_EQ(s.edge, 20);
  Settings expected;
  expected.blur = 55;
  expected.edge = 20;
  CHECK(s == expected);
}

TEST(settings_load_garbage_values_keep_previous_value) {
  TempIni f(L"garbage");
  CHECK(WriteRaw(f.path,
                 "blur=abc\n"               // non-numeric: ignored
                 "edge=\n"                  // empty: ignored
                 "spread=   \n"             // whitespace only: ignored
                 "fadeStart=99999999999\n"  // out of int range: stoi throws, ignored
                 "fadeCurve=-\n"            // sign only: ignored
                 "edgeBlend=12abc\n"        // current behaviour: stoi parses the prefix -> 12
                 "smoothingMs=0x10\n"       // current behaviour: base 10, parses "0" -> 0
                 "debanding=  7\n"));       // ok -> 7
  Settings s;
  s.blur = 55;
  s.edge = 21;
  s.spread = 33;
  s.fadeStart = -7;
  s.fadeCurve = 9;
  s.smoothingMs = 500;
  s.debanding = 50;
  CHECK(LoadSettings(f.path, s));
  CHECK_EQ(s.blur, 55);
  CHECK_EQ(s.edge, 21);
  CHECK_EQ(s.spread, 33);
  CHECK_EQ(s.fadeStart, -7);
  CHECK_EQ(s.fadeCurve, 9);
  CHECK_EQ(s.edgeBlend, 12);   // current behaviour
  CHECK_EQ(s.smoothingMs, 0);  // current behaviour
  CHECK_EQ(s.debanding, 7);
}

TEST(settings_load_partial_file_keeps_other_fields) {
  TempIni f(L"partial");
  CHECK(WriteRaw(f.path, "blur=55\n"));
  Settings s = Modified();
  Settings before = s;
  CHECK(LoadSettings(f.path, s));
  CHECK_EQ(s.blur, 55);
  before.blur = 55;
  CHECK(s == before);
}

TEST(settings_load_last_duplicate_key_wins) {
  TempIni f(L"dup");
  CHECK(WriteRaw(f.path, "blur=10\nblur=20\nblur=30\n"));
  Settings s;
  CHECK(LoadSettings(f.path, s));
  CHECK_EQ(s.blur, 30);
}

TEST(settings_load_bool_from_any_nonzero) {
  TempIni f(L"bools");
  CHECK(WriteRaw(f.path, "enabled=0\ndirTop=1\ndirRight=7\ndirBottom=0\ndirLeft=-5\n"));
  Settings s;
  CHECK(LoadSettings(f.path, s));
  CHECK(!s.enabled);
  CHECK(s.dirTop);
  CHECK(s.dirRight);  // clamped to the 0..1 range before the cast: 7 -> 1
  CHECK_EQ((int)s.dirRight, 1);
  CHECK(!s.dirBottom);
  CHECK(!s.dirLeft);  // -5 clamps to 0, not "non-zero means true"
}

TEST(settings_save_overwrites_existing_file) {
  TempIni f(L"overwrite");
  Settings a;
  a.blur = 11;
  CHECK(SaveSettings(f.path, a));
  Settings b;
  b.blur = 22;
  CHECK(SaveSettings(f.path, b));
  Settings loaded;
  CHECK(LoadSettings(f.path, loaded));
  CHECK_EQ(loaded.blur, 22);
}
