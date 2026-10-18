// Search normalisation and matching.
#include <string>

#include "../../aimp/core/search.h"
#include "../test_framework.h"

using namespace glass;

TEST(search_case_insensitive_in_any_script) {
  CHECK(NormalizeForSearch(L"Midnight SIGNALS") == L"midnight signals");
  CHECK(NormalizeForSearch(L"Тёмный РЫЦАРЬ") == L"темный рыцарь");
  CHECK(NormalizeForSearch(L"ÉTÉ Ångström") == L"été ångström");
}

TEST(search_yo_equals_ye) {
  std::wstring text = NormalizeForSearch(L"Ёлки — Зелёная");
  CHECK(MatchesAll(text, QueryTokens(L"елки")));
  CHECK(MatchesAll(text, QueryTokens(L"ЗЕЛЕНАЯ")));
}

TEST(search_all_words_any_order) {
  std::wstring text = NormalizeForSearch(L"Glass Horizon\tSable Haze EP");
  CHECK(MatchesAll(text, QueryTokens(L"sable horizon")));
  CHECK(MatchesAll(text, QueryTokens(L"  haze   glass ")));
  CHECK(!MatchesAll(text, QueryTokens(L"sable dreams")));
}

TEST(search_empty_query_matches_everything) {
  CHECK(QueryTokens(L"   ").empty());
  CHECK(MatchesAll(NormalizeForSearch(L"anything"), QueryTokens(L"")));
}

TEST(search_separators_collapse) {
  CHECK(NormalizeForSearch(L"a__b  c—d") == L"a b c d");
  CHECK(NormalizeForSearch(L"") == L"");
}
