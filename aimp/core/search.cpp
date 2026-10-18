#include "search.h"

#include <windows.h>

namespace glass {

std::wstring NormalizeForSearch(std::wstring_view text) {
  std::wstring out;
  if (text.empty()) return out;
  // Linguistic lower case: handles Cyrillic, Greek, accented Latin and the rest the same way.
  int n = LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE | LCMAP_LINGUISTIC_CASING, text.data(), (int)text.size(),
                        nullptr, 0, nullptr, nullptr, 0);
  if (n <= 0) {
    out.assign(text);
  } else {
    out.resize((size_t)n);
    LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE | LCMAP_LINGUISTIC_CASING, text.data(), (int)text.size(),
                  out.data(), n, nullptr, nullptr, 0);
  }
  // ё -> е, and every kind of space or separator to one plain space.
  std::wstring folded;
  folded.reserve(out.size());
  bool space = true;
  for (wchar_t c : out) {
    if (c == L'ё') c = L'е';
    bool sep = c == L' ' || c == L'\t' || c == L' ' || c == L'_' || c == L'–' || c == L'—' || c < 32;
    if (sep) {
      if (!space) folded.push_back(L' ');
      space = true;
    } else {
      folded.push_back(c);
      space = false;
    }
  }
  if (!folded.empty() && folded.back() == L' ') folded.pop_back();
  return folded;
}

std::vector<std::wstring> QueryTokens(std::wstring_view query) {
  std::vector<std::wstring> tokens;
  std::wstring n = NormalizeForSearch(query);
  size_t i = 0;
  while (i < n.size()) {
    size_t j = n.find(L' ', i);
    if (j == std::wstring::npos) j = n.size();
    if (j > i) tokens.emplace_back(n.substr(i, j - i));
    i = j + 1;
  }
  return tokens;
}

bool MatchesAll(std::wstring_view text, const std::vector<std::wstring>& tokens) {
  for (const std::wstring& t : tokens)
    if (text.find(t) == std::wstring_view::npos) return false;
  return true;
}

}  // namespace glass
