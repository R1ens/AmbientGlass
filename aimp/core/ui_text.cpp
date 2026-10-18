#include "ui_text.h"

#include <cmath>
#include <cstdio>
#include <cwchar>
#include <cwctype>

#include "search.h"

namespace glass {

std::wstring FormatTime(double seconds) {
  if (!(seconds > 0)) seconds = 0;
  long long s = (long long)std::floor(seconds + 1e-6);
  long long h = s / 3600, mnt = (s / 60) % 60, sec = s % 60;
  wchar_t buf[32];
  if (h > 0)
    std::swprintf(buf, 32, L"%lld:%02lld:%02lld", h, mnt, sec);
  else
    std::swprintf(buf, 32, L"%lld:%02lld", mnt, sec);
  return buf;
}

std::wstring TrackCount(int n) {
  int a = n % 100, b = n % 10;
  const wchar_t* word = (a >= 11 && a <= 14) ? L"треков" : b == 1 ? L"трек" : (b >= 2 && b <= 4) ? L"трека" : L"треков";
  return std::to_wstring(n) + L" " + word;
}

std::wstring ListSummary(const Model& m) {
  double total = 0;
  for (int i : m.visible) total += m.rows[i].duration;
  std::wstring s = L"• " + TrackCount((int)m.visible.size());
  if (total > 0) s += L" • " + FormatTime(total);
  return s;
}

std::wstring ArtistLine(const TrackInfo& t, const wchar_t* sep) {
  if (t.artist.empty()) return t.album;
  if (t.album.empty()) return t.artist;
  return t.artist + sep + t.album;
}

void ApplyQuery(Model& m) {
  m.visible.clear();
  std::vector<std::wstring> tokens = QueryTokens(m.query);
  for (int i = 0; i < (int)m.rows.size(); i++)
    if (tokens.empty() || MatchesAll(m.rows[i].search, tokens)) m.visible.push_back(i);
}

std::wstring FormatName(const std::wstring& path, const std::wstring& codec) {
  const size_t slash = path.find_last_of(L"\\/"), dot = path.find_last_of(L'.');
  std::wstring ext;
  if (dot != std::wstring::npos && (slash == std::wstring::npos || dot > slash) && path.size() - dot - 1 <= 5)
    for (wchar_t c : path.substr(dot + 1)) ext += (wchar_t)towupper(c);
  const bool container = ext == L"M4A" || ext == L"MP4" || ext == L"MKA" || ext == L"WEBM";
  if ((ext.empty() || container) && !codec.empty()) return codec;
  return ext;
}

std::wstring FormatLine(const TrackInfo& t) {
  std::wstring out;
  auto add = [&](const std::wstring& part) {
    if (part.empty()) return;
    if (!out.empty()) out += L" | ";
    out += part;
  };
  add(t.format);
  if (t.sampleRate > 0) {
    // 44100 -> 44.1, 48000 -> 48, 22050 -> 22.05
    wchar_t buf[32];
    swprintf(buf, 32, L"%.2f", t.sampleRate / 1000.0);
    std::wstring k = buf;
    while (!k.empty() && k.back() == L'0') k.pop_back();
    if (!k.empty() && k.back() == L'.') k.pop_back();
    add(k + L" kHz");
  }
  if (t.bits > 0) add(std::to_wstring(t.bits) + L" bit");
  if (t.bitrate > 0) add(std::to_wstring(t.bitrate) + L" kbps");
  return out;
}

}  // namespace glass
