#include "render_cli.h"

#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <DirectXPackedVector.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>

#include "common.h"
#include "layout.h"
#include "log.h"
#include "renderer.h"
#include "settings.h"
#include "wic_image.h"

using Microsoft::WRL::ComPtr;

namespace {

constexpr float kDefaultMargin = 0.25f;  // of the picture size, on every side of the output

struct Options {
  std::wstring in, out, settings;
  std::vector<std::wstring> sets;  // key=value overrides, applied after the ini
  int w = 0, h = 0;                // output size; 0 = picture plus margins
  RECT frame{}, hole{}, pass{};
  bool hasFrame = false, hasHole = false, hasPass = false;
  bool opaque = false, hdr = false;
  float white = 1.f, seed = 0.f, dt = 1000.f / 60.f;
  std::wstring prev;  // an earlier frame: the glow starts from it and steps toward the input
  int steps = 1;
  float crop[4] = {0.f, 0.f, 1.f, 1.f};
};

bool g_console = false;

void Print(const char* fmt, ...) {
  if (!g_console) return;
  va_list args;
  va_start(args, fmt);
  vprintf(fmt, args);
  va_end(args);
}

void Usage() {
  Print(
      "usage: VlcAmbient.exe --render <in.png> <out.png> [options]\n"
      "  --settings <file.ini>  load settings from an ini (defaults otherwise)\n"
      "  --set key=value        override one setting (repeatable)\n"
      "  --size WxH             output size (default: the picture plus 25%% on every side)\n"
      "  --frame l,t,r,b        picture rectangle inside the output (default: centred)\n"
      "  --hole l,t,r,b         transparent rectangle (default: the frame; 0,0,0,0 for none)\n"
      "  --pass l,t,r,b         full picture rectangle for burned-in bars (default: the frame)\n"
      "  --crop l,t,r,b         content part of the picture as fractions (default: 0,0,1,1)\n"
      "  --opaque               draw over black bars: opaque output with the picture copied through\n"
      "  --white <f>            SDR white level in scRGB units (default 1); the PNG maps it to 255\n"
      "  --seed <f>             dither seed (default 0)\n"
      "  --prev <prev.png>      start from this frame (same size), then step toward <in.png>\n"
      "  --dt <ms>              time per step after --prev (default 16.7)\n"
      "  --steps <n>            steps toward <in.png> after --prev (default 1)\n"
      "  --hdr                  apply the HDR colour filters\n"
      "settings keys:");
#define VA_KEY(t, n, d, lo, hi) Print(" " #n);
  VA_SETTINGS(VA_KEY)
#undef VA_KEY
  Print("\n");
}

bool ParseRect(const wchar_t* s, RECT& r) {
  return swscanf_s(s, L"%ld,%ld,%ld,%ld", &r.left, &r.top, &r.right, &r.bottom) == 4;
}

bool ParseFloat(const wchar_t* s, float& f) {
  wchar_t* end = nullptr;
  f = (float)wcstod(s, &end);
  return end != s && *end == 0;
}

bool ParseArgs(int argc, wchar_t** argv, Options& o, std::string& err) {
  int positional = 0;
  for (int i = 2; i < argc; i++) {
    std::wstring a = argv[i];
    auto value = [&](const wchar_t*& v) {
      if (i + 1 >= argc) {
        err = "missing value after " + ToUtf8(a);
        return false;
      }
      v = argv[++i];
      return true;
    };
    const wchar_t* v = nullptr;
    if (a == L"--opaque") {
      o.opaque = true;
    } else if (a == L"--hdr") {
      o.hdr = true;
    } else if (a == L"--settings") {
      if (!value(v)) return false;
      o.settings = v;
    } else if (a == L"--set") {
      if (!value(v)) return false;
      o.sets.push_back(v);
    } else if (a == L"--size") {
      if (!value(v)) return false;
      if (swscanf_s(v, L"%dx%d", &o.w, &o.h) != 2 || o.w <= 0 || o.h <= 0) {
        err = "bad --size, want WxH";
        return false;
      }
    } else if (a == L"--frame" || a == L"--hole" || a == L"--pass") {
      if (!value(v)) return false;
      RECT& r = a == L"--frame" ? o.frame : a == L"--hole" ? o.hole : o.pass;
      bool& has = a == L"--frame" ? o.hasFrame : a == L"--hole" ? o.hasHole : o.hasPass;
      if (!ParseRect(v, r)) {
        err = "bad " + ToUtf8(a) + ", want l,t,r,b";
        return false;
      }
      has = true;
    } else if (a == L"--crop") {
      if (!value(v)) return false;
      if (swscanf_s(v, L"%f,%f,%f,%f", &o.crop[0], &o.crop[1], &o.crop[2], &o.crop[3]) != 4 ||
          o.crop[2] <= o.crop[0] || o.crop[3] <= o.crop[1]) {
        err = "bad --crop, want l,t,r,b fractions";
        return false;
      }
    } else if (a == L"--prev") {
      if (!value(v)) return false;
      o.prev = v;
    } else if (a == L"--steps") {
      if (!value(v)) return false;
      o.steps = _wtoi(v);
      if (o.steps < 1) {
        err = "bad --steps, want a positive number";
        return false;
      }
    } else if (a == L"--white" || a == L"--seed" || a == L"--dt") {
      if (!value(v)) return false;
      float& f = a == L"--white" ? o.white : a == L"--seed" ? o.seed : o.dt;
      if (!ParseFloat(v, f)) {
        err = "bad " + ToUtf8(a) + ", want a number";
        return false;
      }
    } else if (a.rfind(L"--", 0) == 0) {
      err = "unknown option " + ToUtf8(a);
      return false;
    } else if (positional == 0) {
      o.in = a;
      positional++;
    } else if (positional == 1) {
      o.out = a;
      positional++;
    } else {
      err = "unexpected argument " + ToUtf8(a);
      return false;
    }
  }
  if (positional < 2) {
    err = "input and output files are required";
    return false;
  }
  return true;
}

// `--set key=value` with the same key list as the ini.
bool ApplySet(Settings& s, const std::wstring& kv, std::string& err) {
  size_t eq = kv.find(L'=');
  std::string key = eq == std::wstring::npos ? "" : ToUtf8(kv.substr(0, eq));
  int v = eq == std::wstring::npos ? 0 : _wtoi(kv.c_str() + eq + 1);
#define VA_SET(t, n, d, lo, hi) \
  if (key == #n) {              \
    s.n = (t)v;                 \
    return true;                \
  }
  VA_SETTINGS(VA_SET)
#undef VA_SET
  err = "unknown setting in --set " + ToUtf8(kv);
  return false;
}

std::string Failed(const char* what, HRESULT hr) { return std::string(what) + ": " + Log::HrText(hr); }

bool LoadInput(IWICImagingFactory* wic, const std::wstring& path, Picture& pic, std::string& err) {
  if (!LoadPicture(wic, path, pic, err)) return false;
  if (pic.w < 2 || pic.h < 2) {
    err = "the picture is too small";
    return false;
  }
  return true;
}

// The final pass writes premultiplied linear scRGB; the PNG gets straight colour with SDR white at 255.
void ToBgra(const BYTE* src, UINT rowPitch, UINT w, UINT h, float white, std::vector<BYTE>& out) {
  using DirectX::PackedVector::XMConvertHalfToFloat;
  out.resize((size_t)w * h * 4);
  for (UINT y = 0; y < h; y++) {
    const uint16_t* row = reinterpret_cast<const uint16_t*>(src + (size_t)y * rowPitch);
    BYTE* dst = out.data() + (size_t)y * w * 4;
    for (UINT x = 0; x < w; x++) {
      float a = std::clamp(XMConvertHalfToFloat(row[x * 4 + 3]), 0.f, 1.f);
      float scale = a > 1e-4f ? 1.f / (a * white) : 0.f;
      for (UINT c = 0; c < 3; c++) {
        float lin = std::clamp(XMConvertHalfToFloat(row[x * 4 + c]) * scale, 0.f, 1.f);
        dst[x * 4 + 2 - c] = (BYTE)std::lround(LinearToSrgb(lin) * 255.f);
      }
      dst[x * 4 + 3] = (BYTE)std::lround(a * 255.f);
    }
  }
}

int Fail(const std::string& msg) {
  Print("error: %s\n", msg.c_str());
  std::string last = Log::LastError();
  if (!last.empty() && last != msg) Print("renderer: %s\n", last.c_str());
  return 1;
}

int Render(const Options& o) {
  ComPtr<IWICImagingFactory> wic;
  HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic));
  if (FAILED(hr)) return Fail(Failed("WIC is not available", hr));

  std::string err;
  Picture pic;
  if (!LoadInput(wic.Get(), o.in, pic, err)) return Fail(err);

  Settings s;
  if (!o.settings.empty() && !LoadSettings(o.settings, s)) return Fail("cannot read settings " + ToUtf8(o.settings));
  for (const auto& kv : o.sets)
    if (!ApplySet(s, kv, err)) return Fail(err);
  s.Clamp();

  // Geometry: the overlay is the whole output; the picture sits centred unless placed explicitly.
  const UINT mx = (UINT)std::lround((float)pic.w * kDefaultMargin), my = (UINT)std::lround((float)pic.h * kDefaultMargin);
  const UINT W = o.w > 0 ? (UINT)o.w : pic.w + 2 * mx;
  const UINT H = o.h > 0 ? (UINT)o.h : pic.h + 2 * my;
  GlowLayout L;
  L.overlay = RECT{0, 0, (LONG)W, (LONG)H};
  if (o.hasFrame) {
    L.frame = o.frame;
  } else {
    L.frame.left = ((LONG)W - (LONG)pic.w) / 2;
    L.frame.top = ((LONG)H - (LONG)pic.h) / 2;
    L.frame.right = L.frame.left + (LONG)pic.w;
    L.frame.bottom = L.frame.top + (LONG)pic.h;
  }
  L.hole = o.hasHole ? o.hole : L.frame;
  L.pass = o.hasPass ? o.pass : L.frame;
  L.scale = L.pass;
  L.usePass = !SameRect(L.frame, L.pass);  // as in the app: bars are content smaller than the picture
  L.opaque = o.opaque;
  if (RectW(L.frame) < 2 || RectH(L.frame) < 2 || RectW(L.pass) < 2 || RectH(L.pass) < 2)
    return Fail("the frame and pass rectangles must be at least 2x2");

  Renderer renderer;
  std::wstring rerr;
  if (!renderer.Init(rerr, /*warp=*/true)) return Fail(ToUtf8(rerr));
  renderer.SetWhiteLevel(o.white);
  renderer.SetDitherSeed(o.seed);
  ID3D11Device* dev = renderer.Device();

  D3D11_TEXTURE2D_DESC td{};
  td.MipLevels = 1;
  td.ArraySize = 1;
  td.Format = kGpuFormat;
  td.SampleDesc.Count = 1;
  td.Usage = D3D11_USAGE_DEFAULT;
  td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  auto ingest = [&](const Picture& p) {
    td.Width = p.w;
    td.Height = p.h;
    D3D11_SUBRESOURCE_DATA init{p.rgba.data(), p.w * 8, 0};
    ComPtr<ID3D11Texture2D> src;
    HRESULT h = dev->CreateTexture2D(&td, &init, &src);
    if (FAILED(h)) return Failed("cannot create the picture texture", h);
    D3D11_BOX box{0, 0, 0, p.w, p.h, 1};
    if (!renderer.Ingest(src.Get(), box, false)) return std::string("the renderer rejected the picture");
    return std::string();
  };
  // Without --prev the glow starts from the input itself. With it, from the earlier frame, then it
  // steps toward the input as the app would at the given frame interval.
  const float steady = (float)s.smoothingMs, cut = (float)s.cutFadeMs;
  if (!o.prev.empty()) {
    Picture before;
    if (!LoadInput(wic.Get(), o.prev, before, err)) return Fail(err);
    if (before.w != pic.w || before.h != pic.h) return Fail("--prev must be the same size as the input");
    if (std::string e = ingest(before); !e.empty()) return Fail(e);
    renderer.UpdateProjector(o.crop, 0.f, steady, cut);
    if (std::string e = ingest(pic); !e.empty()) return Fail(e);
    for (int k = 0; k < o.steps; k++) renderer.UpdateProjector(o.crop, o.dt, steady, cut);
  } else {
    if (std::string e = ingest(pic); !e.empty()) return Fail(e);
    renderer.UpdateProjector(o.crop, 0.f, steady, cut);
  }

  td.Width = W;
  td.Height = H;
  td.BindFlags = D3D11_BIND_RENDER_TARGET;
  ComPtr<ID3D11Texture2D> target;
  ComPtr<ID3D11RenderTargetView> rtv;
  hr = dev->CreateTexture2D(&td, nullptr, &target);
  if (SUCCEEDED(hr)) hr = dev->CreateRenderTargetView(target.Get(), nullptr, &rtv);
  td.BindFlags = 0;
  td.Usage = D3D11_USAGE_STAGING;
  td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Texture2D> staging;
  if (SUCCEEDED(hr)) hr = dev->CreateTexture2D(&td, nullptr, &staging);
  if (FAILED(hr)) return Fail(Failed("cannot create the output textures", hr));

  ID3D11DeviceContext* ctx = renderer.Context();
  const float clear[4] = {0, 0, 0, 0};
  ctx->ClearRenderTargetView(rtv.Get(), clear);
  GlowTargets targets;
  renderer.RenderTo(rtv.Get(), W, H, targets, L, s, o.hdr);
  ctx->CopyResource(staging.Get(), target.Get());
  D3D11_MAPPED_SUBRESOURCE m{};
  hr = ctx->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &m);
  if (FAILED(hr)) return Fail(Failed("cannot read the output back", hr));
  std::vector<BYTE> bgra;
  ToBgra(static_cast<const BYTE*>(m.pData), m.RowPitch, W, H, o.white > 0.1f ? o.white : 1.f, bgra);
  ctx->Unmap(staging.Get(), 0);

  if (!SavePng(wic.Get(), o.out, W, H, bgra, err)) return Fail(err);
  Print("rendered %ux%u -> %s\n", W, H, ToUtf8(o.out).c_str());
  return 0;
}

}  // namespace

int RunRenderCli(int argc, wchar_t** argv) {
  // A windowed exe has no console of its own; borrow the parent's when there is one, stay silent otherwise.
  if (AttachConsole(ATTACH_PARENT_PROCESS)) {
    FILE* f = nullptr;
    g_console = freopen_s(&f, "CONOUT$", "w", stdout) == 0;
    freopen_s(&f, "CONOUT$", "w", stderr);
    SetConsoleOutputCP(CP_UTF8);
  }

  Options o;
  std::string err;
  if (!ParseArgs(argc, argv, o, err)) {
    Print("error: %s\n", err.c_str());
    Usage();
    return 1;
  }

  HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  int code = Render(o);
  if (SUCCEEDED(hr)) CoUninitialize();
  return code;
}
