// The file's format line under the artist.
#include <string>

#include "../../aimp/core/ui_text.h"
#include "../test_framework.h"

using namespace glass;

TEST(format_line_lists_what_is_known) {
  TrackInfo t;
  CHECK(FormatLine(t).empty());
  t.format = L"FLAC", t.sampleRate = 44100, t.bits = 16, t.bitrate = 867;
  CHECK(FormatLine(t) == L"FLAC | 44.1 kHz | 16 bit | 867 kbps");
  t.sampleRate = 48000, t.bits = 24;
  CHECK(FormatLine(t) == L"FLAC | 48 kHz | 24 bit | 867 kbps");
  t.format = L"MP3", t.sampleRate = 22050, t.bits = 0, t.bitrate = 320;  // lossy: no bit depth
  CHECK(FormatLine(t) == L"MP3 | 22.05 kHz | 320 kbps");
}

TEST(format_name_from_extension_or_codec) {
  CHECK(FormatName(L"D:\\Music\\a.flac", L"") == L"FLAC");
  CHECK(FormatName(L"D:\\Music\\b.Mp3", L"MPEG Layer 3") == L"MP3");
  CHECK(FormatName(L"D:\\Music\\c.m4a", L"ALAC") == L"ALAC");  // a container: the codec says more
  CHECK(FormatName(L"D:\\Music\\c.m4a", L"") == L"M4A");
  CHECK(FormatName(L"http://radio.example/stream", L"AAC") == L"AAC");
  CHECK(FormatName(L"D:\\Music.v2\\track", L"") == L"");  // a dot in a folder name is no extension
}
