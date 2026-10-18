#include "wic_image.h"

#include <wrl/client.h>
#include <DirectXPackedVector.h>

#include "common.h"
#include "log.h"

using Microsoft::WRL::ComPtr;

namespace {

std::string Failed(const std::string& what, HRESULT hr) { return what + ": " + Log::HrText(hr); }

bool Decode(IWICImagingFactory* wic, IWICBitmapDecoder* decoder, const std::string& name, Picture& pic,
            std::string& err) {
  pic = Picture{};
  ComPtr<IWICBitmapFrameDecode> frame;
  HRESULT hr = decoder->GetFrame(0, &frame);
  if (SUCCEEDED(hr)) hr = frame->GetSize(&pic.w, &pic.h);
  ComPtr<IWICFormatConverter> conv;
  if (SUCCEEDED(hr)) hr = wic->CreateFormatConverter(&conv);
  if (SUCCEEDED(hr))
    hr = conv->Initialize(frame.Get(), GUID_WICPixelFormat64bppRGBA, WICBitmapDitherTypeNone, nullptr, 0,
                          WICBitmapPaletteTypeCustom);
  std::vector<uint16_t> raw;
  if (SUCCEEDED(hr)) {
    raw.resize((size_t)pic.w * pic.h * 4);
    hr = conv->CopyPixels(nullptr, pic.w * 8, (UINT)(raw.size() * sizeof(uint16_t)),
                          reinterpret_cast<BYTE*>(raw.data()));
  }
  if (FAILED(hr)) {
    err = Failed("cannot decode " + name, hr);
    pic = Picture{};
    return false;
  }

  using DirectX::PackedVector::XMConvertFloatToHalf;
  pic.rgba.resize(raw.size());
  const uint16_t one = XMConvertFloatToHalf(1.f);
  for (size_t i = 0; i < raw.size(); i += 4) {
    for (size_t c = 0; c < 3; c++) pic.rgba[i + c] = XMConvertFloatToHalf(SrgbToLinear((float)raw[i + c] / 65535.f));
    pic.rgba[i + 3] = one;
  }
  return true;
}

}  // namespace

bool LoadPicture(IWICImagingFactory* wic, const std::wstring& path, Picture& pic, std::string& err) {
  ComPtr<IWICBitmapDecoder> decoder;
  HRESULT hr = wic->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand,
                                              &decoder);
  if (FAILED(hr)) {
    err = Failed("cannot open " + ToUtf8(path), hr);
    return false;
  }
  return Decode(wic, decoder.Get(), ToUtf8(path), pic, err);
}

bool LoadPicture(IWICImagingFactory* wic, const void* data, size_t size, Picture& pic, std::string& err) {
  ComPtr<IWICStream> stream;
  HRESULT hr = size <= MAXDWORD ? wic->CreateStream(&stream) : E_INVALIDARG;
  if (SUCCEEDED(hr))
    hr = stream->InitializeFromMemory(const_cast<BYTE*>(static_cast<const BYTE*>(data)), (DWORD)size);
  ComPtr<IWICBitmapDecoder> decoder;
  if (SUCCEEDED(hr))
    hr = wic->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &decoder);
  if (FAILED(hr)) {
    err = Failed("cannot open the image in memory", hr);
    return false;
  }
  return Decode(wic, decoder.Get(), "the image in memory", pic, err);
}

bool SavePng(IWICImagingFactory* wic, const std::wstring& path, UINT w, UINT h, const std::vector<BYTE>& bgra,
             std::string& err) {
  ComPtr<IWICStream> stream;
  HRESULT hr = wic->CreateStream(&stream);
  if (SUCCEEDED(hr)) hr = stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE);
  if (FAILED(hr)) {
    err = Failed("cannot create " + ToUtf8(path), hr);
    return false;
  }
  ComPtr<IWICBitmapEncoder> encoder;
  hr = wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
  if (SUCCEEDED(hr)) hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
  ComPtr<IWICBitmapFrameEncode> frame;
  ComPtr<IPropertyBag2> props;
  if (SUCCEEDED(hr)) hr = encoder->CreateNewFrame(&frame, &props);
  if (SUCCEEDED(hr)) hr = frame->Initialize(props.Get());
  if (SUCCEEDED(hr)) hr = frame->SetSize(w, h);
  WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
  if (SUCCEEDED(hr)) hr = frame->SetPixelFormat(&fmt);
  if (SUCCEEDED(hr) && fmt != GUID_WICPixelFormat32bppBGRA) hr = WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT;
  if (SUCCEEDED(hr)) hr = frame->WritePixels(h, w * 4, (UINT)bgra.size(), const_cast<BYTE*>(bgra.data()));
  if (SUCCEEDED(hr)) hr = frame->Commit();
  if (SUCCEEDED(hr)) hr = encoder->Commit();
  if (FAILED(hr)) {
    err = Failed("cannot write " + ToUtf8(path), hr);
    return false;
  }
  return true;
}
