// Window layout at many sizes: everything inside the window, panels apart, nothing overlapping its neighbours.
#include <string>
#include <vector>

#include "../../aimp/core/layout.h"
#include "../../aimp/core/ui_state.h"
#include "../test_framework.h"

using namespace glass;

namespace {

const float kSizes[][2] = {{880, 600},   {1024, 640},  {1280, 800},  {1440, 900}, {1920, 1080},
                           {2560, 1440}, {3840, 2160}, {3840, 1080}, {900, 1600}, {1100, 700}};

bool Inside(const Rc& in, const Rc& out, float eps = 0.01f) {
  return in.l >= out.l - eps && in.t >= out.t - eps && in.r <= out.r + eps && in.b <= out.b + eps;
}
bool Apart(const Rc& a, const Rc& b) { return a.r <= b.l || b.r <= a.l || a.b <= b.t || b.b <= a.t; }

}  // namespace

TEST(layout_panels_inside_window_and_apart) {
  for (auto& s : kSizes) {
    Layout L = ComputeLayout(s[0], s[1]);
    Rc win{0, 0, L.w, L.h};
    const Rc* panels[] = {&L.titlebar, &L.sidebar, &L.card, &L.list, &L.bottom};
    for (const Rc* p : panels) {
      CHECK(p->Valid());
      CHECK(Inside(*p, win));
    }
    for (int i = 0; i < 5; i++)
      for (int j = i + 1; j < 5; j++) CHECK(Apart(*panels[i], *panels[j]));
  }
}

TEST(layout_small_sizes_clamp_to_minimum) {
  Layout L = ComputeLayout(400, 300);
  CHECK_EQ(L.w, kMinWindowW);
  CHECK_EQ(L.h, kMinWindowH);
}

TEST(layout_card_contents_fit_and_do_not_collide) {
  for (auto& s : kSizes) {
    Layout L = ComputeLayout(s[0], s[1]);
    CHECK_NEAR(L.cover.W(), L.cover.H(), 0.01);
    CHECK(L.cover.W() >= 96);
    CHECK(Inside(L.cover, L.card));
    CHECK(Inside(L.spectrum, L.card));
    CHECK(L.spectrum.t >= L.cover.b);
    CHECK(Inside(L.info, L.card));
    CHECK(L.info.l >= L.cover.r);
    // Top to bottom: badge, title, artist, times, waveform, controls.
    if (L.badge.Valid()) CHECK(L.badge.b <= L.title.t);
    CHECK(L.title.b <= L.artist.t);
    CHECK(L.artist.b <= L.waveTimes.t);
    CHECK(L.waveTimes.b <= L.wave.t);
    CHECK(L.wave.b <= L.btnPlay.t);
    const Rc* ctl[] = {&L.btnShuffle, &L.btnPrev, &L.btnPlay, &L.btnNext, &L.btnRepeat, &L.btnLike, &L.btnMore};
    for (const Rc* c : ctl) CHECK(Inside(*c, L.info));
    for (int i = 0; i < 7; i++)
      for (int j = i + 1; j < 7; j++) CHECK(Apart(*ctl[i], *ctl[j]));
  }
}

TEST(layout_list_has_room_for_rows) {
  for (auto& s : kSizes) {
    Layout L = ComputeLayout(s[0], s[1]);
    CHECK(L.rows.H() >= L.rowHeight * 2);
    CHECK(Inside(L.rows, L.list));
    CHECK(Inside(L.search, L.listHead));
    CHECK(L.search.l > L.listHead.l + 120);  // the list title keeps some room
  }
}

TEST(layout_bottom_bar_items_in_order) {
  for (auto& s : kSizes) {
    Layout L = ComputeLayout(s[0], s[1]);
    const Rc* order[] = {&L.miniCover, &L.miniText, &L.bPrev, &L.bPlay, &L.bNext, &L.bTime, &L.volume, &L.btnSettings};
    for (int i = 0; i + 1 < 8; i++) CHECK(order[i]->r <= order[i + 1]->l);
    for (const Rc* r : order) CHECK(Inside(*r, L.bottom));
    CHECK(L.miniText.W() >= 120);
  }
}

TEST(layout_sidebar_items_inside_and_compact_on_narrow_windows) {
  for (auto& s : kSizes) {
    Layout L = ComputeLayout(s[0], s[1]);
    CHECK_EQ(L.compactSidebar, s[0] < 1080);
    for (const Rc& n : L.nav) CHECK(Inside(n, L.sidebar));
    CHECK(Inside(L.navSettings, L.sidebar));
    CHECK(L.addBox.b <= L.navSettings.t);
  }
}

TEST(layout_title_bar_buttons_and_caption) {
  Layout L = ComputeLayout(1280, 800);
  CHECK_EQ(L.btnClose.r, 1280.f);
  CHECK(L.caption.r <= L.btnMin.l);
  CHECK(L.btnMin.r <= L.btnMax.l);
  CHECK(L.btnMax.r <= L.btnClose.l);
}

TEST(layout_list_title_is_left_of_search_and_clickable_with_a_choice) {
  for (auto& s : kSizes) {
    Layout L = ComputeLayout(s[0], s[1]);
    CHECK(L.listTitle.Valid());
    CHECK(L.listTitle.r <= L.search.l);
    CHECK(Inside(L.listTitle, L.listHead));
  }
  Layout L = ComputeLayout(1280, 800);
  Model m;
  UiState st;
  float x = L.listTitle.l + 20, y = L.listTitle.Cy();
  m.playlists = {L"Default"};
  CHECK(HitTest(L, m, st, x, y).hit != Hit::ListTitle);  // one playlist: nothing to choose
  m.playlists = {L"Default", L"Rock"};
  CHECK(HitTest(L, m, st, x, y).hit == Hit::ListTitle);
  m.nav = Nav::Favorites;
  CHECK(HitTest(L, m, st, x, y).hit != Hit::ListTitle);  // only in the Playlists view
}

TEST(layout_quick_settings_rows_fit_above_the_buttons) {
  for (auto& s : kSizes) {
    Layout L = ComputeLayout(s[0], s[1]);
    QuickRow rows[(int)QuickItem::Count];
    int n = QuickRows(L, rows, (int)QuickItem::Count);
    CHECK_EQ(n, (int)QuickItem::Count);
    CHECK(rows[n - 1].rc.b <= QuickAimpSettingsRect(L).t);
    CHECK(rows[0].rc.t >= L.quick.t + 40);
    for (int i = 0; i + 1 < n; i++) CHECK(rows[i].rc.b <= rows[i + 1].rc.t);
    for (int i = 0; i < n; i++) CHECK(rows[i].rc.H() >= 26);  // still easy to hit
    CHECK(QuickAimpSettingsRect(L).r <= QuickClassicRect(L).l || QuickAimpSettingsRect(L).b <= QuickClassicRect(L).t);
  }
}

TEST(layout_quick_settings_on_the_right) {
  Layout L = ComputeLayout(1920, 1080);
  CHECK_NEAR(L.quick.W(), 360.f, 0.01);
  CHECK(L.quick.b <= L.bottom.t);  // tall windows: above the bottom bar
  CHECK(L.quick.t >= L.titlebar.b);
  Layout low = ComputeLayout(880, 600);
  CHECK(low.quick.b <= low.h);     // low windows: may cover the bottom bar, never past the window
  CHECK(low.quick.b > low.bottom.t);
}
