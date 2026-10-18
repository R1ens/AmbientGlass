// AimpGlass tests. Without arguments: runs every test. With --render <dir> [--warp]: renders the demo states to
// PNG files for looking at the design (not a test).
#include <windows.h>
#include <objbase.h>

#include <cstdio>
#include <string>
#include <vector>

#include "../../aimp/core/offscreen.h"
#include "../test_framework.h"
#include "demo_model.h"
#include "render_util.h"

namespace {

int RenderDemo(const std::wstring& dir, bool warp) {
  CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  glass::Offscreen off;
  std::wstring err;
  if (!off.Init(warp, err)) {
    std::wprintf(L"init failed: %ls\n", err.c_str());
    return 2;
  }
  struct Shot {
    const wchar_t* name;
    int variant;
    UINT w, h;
    float scale;
    bool ui;
    bool menu = false;
    bool glow = true;
  } shots[] = {
      {L"playing", 0, 1600, 1000, 1.f, true},   {L"background_only", 0, 1600, 1000, 1.f, false},
      {L"long_title", 1, 1280, 800, 1.f, true}, {L"liked", 2, 1440, 900, 1.f, true},
      {L"paused", 3, 1280, 800, 1.f, true},     {L"quick_settings", 4, 1600, 1000, 1.f, true},
      {L"light", 5, 1600, 1000, 1.f, true},     {L"light_background", 5, 1600, 1000, 1.f, false},     {L"min_size", 0, 880, 600, 1.f, true},
      {L"hidpi_150", 0, 2400, 1500, 1.5f, true},
      {L"clear_glass", 7, 1600, 1000, 1.f, true}, {L"mono", 8, 1600, 1000, 1.f, true},
      {L"mono_clear", 9, 1600, 1000, 1.f, true}, {L"menu", 0, 1600, 1000, 1.f, true, true},
      {L"no_cover_glow", 0, 1600, 1000, 1.f, true, false, false}, {L"light_mono", 10, 1600, 1000, 1.f, true},
      {L"grey_clear", 11, 1600, 1000, 1.f, true}, {L"grey_clear_hour", 12, 1600, 1000, 1.f, true},
  };
  CreateDirectoryW(dir.c_str(), nullptr);
  for (const Shot& s : shots) {
    glass::Model m = demo::MakeModel(s.variant);
    glass::SceneOptions o;
    o.coverGlow = s.glow;
    std::vector<uint8_t> px;
    glass::UiState st = s.menu ? demo::MoreMenuOpen(s.w, s.h, s.scale) : glass::UiState{};
    if (!off.Render(m, s.w, s.h, s.scale, o, s.ui, px, &st)) {
      std::wprintf(L"%ls: render failed\n", s.name);
      return 3;
    }
    std::string e;
    std::wstring path = dir + L"\\" + s.name + L".png";
    if (!off.SavePng(path, s.w, s.h, px, e)) {
      std::printf("save failed: %s\n", e.c_str());
      return 4;
    }
    std::wprintf(L"%ls\n", path.c_str());
  }
  return 0;
}

}  // namespace

// Golden images: a few states rendered on WARP compared with tests/aimp/golden/*.png by PSNR; --update writes
// them instead (after a deliberate change of the look).
int Golden(const std::wstring& dir, bool update) {
  glass::Offscreen* off = rt::Warp();
  if (!off) {
    std::printf("golden: WARP device unavailable\n");
    return 2;
  }
  struct Shot {
    const wchar_t* name;
    int variant;
    UINT w, h;
    bool menu = false;
  } shots[] = {{L"playing", 0, 1100, 700}, {L"quick_settings", 4, 1100, 700}, {L"light", 5, 1100, 700},
               {L"min_size", 0, 880, 600}, {L"long_title", 1, 1100, 700}, {L"mono_cover", 6, 1100, 700},
               {L"clear_mono", 9, 1100, 700}, {L"menu", 0, 1100, 700, true}};
  constexpr double kMinPsnr = 40;
  int failed = 0;
  for (const Shot& s : shots) {
    glass::Model m = demo::MakeModel(s.variant);
    glass::SceneOptions o;
    rt::Image got;
    got.w = s.w, got.h = s.h;
    glass::UiState st = s.menu ? demo::MoreMenuOpen(s.w, s.h, 1.f) : glass::UiState{};
    if (!off->Render(m, s.w, s.h, 1.f, o, true, got.bgra, &st)) {
      std::wprintf(L"FAIL %ls: render\n", s.name);
      failed++;
      continue;
    }
    std::wstring path = dir + L"\\" + s.name + L".png";
    if (update) {
      std::string e;
      if (!off->SavePng(path, s.w, s.h, got.bgra, e)) {
        std::printf("FAIL save: %s\n", e.c_str());
        failed++;
      } else {
        std::wprintf(L"updated %ls\n", path.c_str());
      }
      continue;
    }
    rt::Image want;
    if (!rt::LoadPng(path, want)) {
      std::wprintf(L"FAIL %ls: no expected image %ls\n", s.name, path.c_str());
      failed++;
      continue;
    }
    double p = rt::Psnr(got, want);
    std::wprintf(L"%ls %ls: PSNR %.1f dB\n", p >= kMinPsnr ? L"PASS" : L"FAIL", s.name, p);
    if (p < kMinPsnr) failed++;
  }
  return failed;
}

int wmain(int argc, wchar_t** argv) {
  if (argc >= 3 && std::wstring(argv[1]) == L"--render")
    return RenderDemo(argv[2], argc >= 4 && std::wstring(argv[3]) == L"--warp");
  if (argc >= 2 && std::wstring(argv[1]) == L"--bench") {
    // --bench [W H]: GPU time of each pass on the real graphics card.
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    glass::Offscreen off;
    std::wstring err;
    if (!off.Init(false, err)) {
      std::wprintf(L"init failed: %ls\n", err.c_str());
      return 2;
    }
    UINT w = argc >= 4 ? (UINT)_wtoi(argv[2]) : 1600, h = argc >= 4 ? (UINT)_wtoi(argv[3]) : 1000;
    std::wprintf(L"%ls", off.Bench(demo::MakeModel(0), w, h, 1.f, 600).c_str());
    return 0;
  }
  if (argc >= 3 && std::wstring(argv[1]) == L"--golden")
    return Golden(argv[2], argc >= 4 && std::wstring(argv[3]) == L"--update");
  return RUN_ALL_TESTS();
}
