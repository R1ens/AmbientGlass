#include "ui_text.h"

#include <cmath>
#include <cstdio>

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

}  // namespace glass
