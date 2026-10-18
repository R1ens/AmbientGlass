#pragma once
#include <windows.h>
#include <wincodec.h>

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

// A decoded picture as linear scRGB halves, alpha 1, ready for a kGpuFormat texture (row pitch w * 8).
struct Picture {
  UINT w = 0, h = 0;
  std::vector<uint16_t> rgba;
};

inline float SrgbToLinear(float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }
inline float LinearToSrgb(float c) { return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.f / 2.4f) - 0.055f; }

// Decodes the first frame of any WIC-readable image (PNG, JPEG, BMP, ...) to 16-bit integer RGBA (a plain
// rescale, no colour management), then applies the sRGB EOTF: the result is linear like the frames VlcAmbient
// captures. Fills `err` (UTF-8) and returns false on failure. COM must be initialised on this thread.
bool LoadPicture(IWICImagingFactory* wic, const std::wstring& path, Picture& pic, std::string& err);
// The same from an encoded image in memory, e.g. cover art embedded in a tag.
bool LoadPicture(IWICImagingFactory* wic, const void* data, size_t size, Picture& pic, std::string& err);

// 8-bit BGRA rows (w * 4 bytes each) to a PNG file.
bool SavePng(IWICImagingFactory* wic, const std::wstring& path, UINT w, UINT h, const std::vector<BYTE>& bgra,
             std::string& err);
