// common.h helpers: Trim, UTF-8 conversions, RECT helpers.
#include <windows.h>

#include <string>

#include "common.h"
#include "test_framework.h"

TEST(common_trim) {
  CHECK_EQ(Trim(""), std::string(""));
  CHECK_EQ(Trim("   "), std::string(""));
  CHECK_EQ(Trim("\r\n\t "), std::string(""));
  CHECK_EQ(Trim("abc"), std::string("abc"));
  CHECK_EQ(Trim("  abc"), std::string("abc"));
  CHECK_EQ(Trim("abc  "), std::string("abc"));
  CHECK_EQ(Trim("\t abc \r\n"), std::string("abc"));
  CHECK_EQ(Trim(" a b c "), std::string("a b c"));  // inner whitespace is kept
  CHECK_EQ(Trim("\xC2\xA0x"), std::string("\xC2\xA0x"));  // only ASCII whitespace is trimmed
}

TEST(common_utf8_round_trip) {
  CHECK_EQ(ToUtf8(L""), std::string(""));
  CHECK(FromUtf8("").empty());
  CHECK_EQ(ToUtf8(L"abc"), std::string("abc"));
  CHECK(FromUtf8("abc") == L"abc");
  // Cyrillic "Привет" (U+041F U+0440 U+0438 U+0432 U+0435 U+0442)
  const std::wstring wide = L"Привет";
  const std::string utf8 = "\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82";
  CHECK_EQ(ToUtf8(wide), utf8);
  CHECK(FromUtf8(utf8) == wide);
  CHECK(FromUtf8(ToUtf8(wide)) == wide);
  // Outside the BMP: U+1F600 is a surrogate pair in UTF-16 and 4 bytes in UTF-8.
  const std::wstring emoji = L"\xD83D\xDE00";
  CHECK_EQ(ToUtf8(emoji), std::string("\xF0\x9F\x98\x80"));
  CHECK(FromUtf8("\xF0\x9F\x98\x80") == emoji);
  // Embedded NUL survives (sizes are passed explicitly, not strlen).
  const std::wstring withNul(L"a\0b", 3);
  CHECK_EQ(ToUtf8(withNul).size(), (size_t)3);
  CHECK(FromUtf8(std::string("a\0b", 3)) == withNul);
}

TEST(common_rect_helpers) {
  RECT r{10, 20, 110, 70};
  CHECK_EQ(RectW(r), 100);
  CHECK_EQ(RectH(r), 50);
  CHECK_EQ(RectW(RECT{}), 0);
  CHECK_EQ(RectW(RECT{5, 0, 2, 0}), -3);  // no clamping: callers check for positive sizes
  CHECK(SameRect(r, RECT{10, 20, 110, 70}));
  CHECK(!SameRect(r, RECT{11, 20, 110, 70}));
  CHECK(!SameRect(r, RECT{10, 21, 110, 70}));
  CHECK(!SameRect(r, RECT{10, 20, 111, 70}));
  CHECK(!SameRect(r, RECT{10, 20, 110, 71}));
  CHECK(SameRect(RECT{}, RECT{}));
}
