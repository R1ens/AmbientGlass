#pragma once
// Helpers for render tests: a shared WARP renderer, PNG loading to BGRA, luminance and PSNR.

#include <windows.h>
#include <objbase.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "../../aimp/core/offscreen.h"

namespace rt {

inline glass::Offscreen* Warp() {
  static std::unique_ptr<glass::Offscreen> off;
  static bool tried = false;
  if (!tried) {
    tried = true;
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    off = std::make_unique<glass::Offscreen>();
    std::wstring err;
    if (!off->Init(true, err)) off.reset();
  }
  return off.get();
}

struct Image {
  UINT w = 0, h = 0;
  std::vector<uint8_t> bgra;
  float Luma(UINT x, UINT y) const {
    const uint8_t* p = &bgra[((size_t)y * w + x) * 4];
    return 0.0722f * p[0] + 0.7152f * p[1] + 0.2126f * p[2];
  }
};

inline bool LoadPng(const std::wstring& path, Image& img) {
  Microsoft::WRL::ComPtr<IWICImagingFactory> wic;
  Microsoft::WRL::ComPtr<IWICBitmapDecoder> dec;
  Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
  Microsoft::WRL::ComPtr<IWICFormatConverter> conv;
  if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))) ||
      FAILED(wic->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec)) ||
      FAILED(dec->GetFrame(0, &frame)) || FAILED(wic->CreateFormatConverter(&conv)) ||
      FAILED(conv->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0,
                              WICBitmapPaletteTypeCustom)))
    return false;
  conv->GetSize(&img.w, &img.h);
  img.bgra.resize((size_t)img.w * img.h * 4);
  return SUCCEEDED(conv->CopyPixels(nullptr, img.w * 4, (UINT)img.bgra.size(), img.bgra.data()));
}

// PSNR over the RGB channels, dB; 99 for identical images, 0 for a size mismatch.
inline double Psnr(const Image& a, const Image& b) {
  if (a.w != b.w || a.h != b.h || a.bgra.size() != b.bgra.size()) return 0;
  double se = 0;
  size_t n = 0;
  for (size_t i = 0; i < a.bgra.size(); i++) {
    if (i % 4 == 3) continue;
    double d = (double)a.bgra[i] - b.bgra[i];
    se += d * d;
    n++;
  }
  if (se == 0) return 99;
  return 10 * std::log10(255.0 * 255.0 / (se / n));
}

}  // namespace rt
