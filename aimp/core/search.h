#pragma once
// Playlist and library search: case-insensitive in any script, "ё" equals "е", every word of the query
// must appear somewhere in the track's text (title, artist, album, file name), in any order.

#include <string>
#include <string_view>
#include <vector>

namespace glass {

std::wstring NormalizeForSearch(std::wstring_view text);
std::vector<std::wstring> QueryTokens(std::wstring_view query);  // normalised, empty tokens dropped
bool MatchesAll(std::wstring_view normalizedText, const std::vector<std::wstring>& tokens);

}  // namespace glass
