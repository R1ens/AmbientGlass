#include "d3d_helpers.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "log.h"

// Shader bytecode, produced from shared/blur.hlsl by fxc at build time.
#include "shared_ps_blur.h"
#include "shared_vs.h"

namespace {

constexpr UINT kMaxSrvs = 5;
constexpr UINT kMaxRtvs = D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT;

// Mirrors cbuffer BlurCB in blur.hlsl.
struct alignas(16) BlurCB {
  float blur[4];
};

}  // namespace

bool MakeTex(ID3D11Device* device, Tex& t, UINT w, UINT h, UINT mips, DXGI_FORMAT format) {
  t = Tex{};
  D3D11_TEXTURE2D_DESC d{};
  d.Width = w;
  d.Height = h;
  d.MipLevels = mips;
  d.ArraySize = 1;
  d.Format = format;
  d.SampleDesc.Count = 1;
  d.Usage = D3D11_USAGE_DEFAULT;
  d.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
  d.MiscFlags = mips != 1 ? D3D11_RESOURCE_MISC_GENERATE_MIPS : 0;
  HRESULT hr = device->CreateTexture2D(&d, nullptr, &t.tex);
  if (SUCCEEDED(hr)) hr = device->CreateShaderResourceView(t.tex.Get(), nullptr, &t.srv);
  if (SUCCEEDED(hr)) hr = device->CreateRenderTargetView(t.tex.Get(), nullptr, &t.rtv);
  if (FAILED(hr)) {
    Log::Error("d3d: cannot create a %ux%u texture: %s", w, h, Log::HrText(hr).c_str());
    t = Tex{};
    return false;
  }
  t.tex->GetDesc(&d);
  t.w = w;
  t.h = h;
  t.mips = d.MipLevels;
  return true;
}

bool PassRunner::Init(ID3D11Device* device, ID3D11DeviceContext* ctx, UINT cbBytes, std::wstring& error) {
  m_ctx = ctx;
  m_cbBytes = cbBytes;
  HRESULT hr = device->CreateVertexShader(g_VSMain, sizeof(g_VSMain), nullptr, &m_vs);
  if (SUCCEEDED(hr)) hr = device->CreatePixelShader(g_PSBlur, sizeof(g_PSBlur), nullptr, &m_psBlur);
  if (FAILED(hr)) {
    error = L"Shader creation failed: " + FromUtf8(Log::HrText(hr));
    return false;
  }

  D3D11_BUFFER_DESC bd{};
  bd.Usage = D3D11_USAGE_DYNAMIC;
  bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  bd.ByteWidth = cbBytes;
  device->CreateBuffer(&bd, nullptr, &m_cb);
  bd.ByteWidth = sizeof(BlurCB);
  device->CreateBuffer(&bd, nullptr, &m_blurCb);

  D3D11_SAMPLER_DESC sd{};
  sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
  sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
  sd.MaxLOD = D3D11_FLOAT32_MAX;
  device->CreateSamplerState(&sd, &m_sampler);
  if (!m_cb || !m_blurCb || !m_sampler) {
    error = L"Renderer state creation failed";
    return false;
  }
  return true;
}

void PassRunner::Pass(ID3D11RenderTargetView* rtv, UINT w, UINT h, ID3D11PixelShader* ps,
                      std::initializer_list<ID3D11ShaderResourceView*> srvs, const void* cb) {
  Draw({rtv}, w, h, ps, srvs, m_cb.Get(), cb, m_cbBytes);
}

void PassRunner::Pass(std::initializer_list<ID3D11RenderTargetView*> rtvs, UINT w, UINT h, ID3D11PixelShader* ps,
                      std::initializer_list<ID3D11ShaderResourceView*> srvs, const void* cb) {
  Draw(rtvs, w, h, ps, srvs, m_cb.Get(), cb, m_cbBytes);
}

void PassRunner::Blur(const Tex& in, Tex& tmp, Tex& out, float sigma) {
  BlurCB cb{};
  cb.blur[2] = sigma;
  cb.blur[3] = std::min(std::ceil(sigma * 3.f), kMaxBlurTaps);
  cb.blur[0] = 1.f / in.w;
  cb.blur[1] = 0;
  Draw({tmp.rtv.Get()}, tmp.w, tmp.h, m_psBlur.Get(), {in.srv.Get()}, m_blurCb.Get(), &cb, sizeof(cb));
  cb.blur[0] = 0;
  cb.blur[1] = 1.f / in.h;
  Draw({out.rtv.Get()}, out.w, out.h, m_psBlur.Get(), {tmp.srv.Get()}, m_blurCb.Get(), &cb, sizeof(cb));
}

void PassRunner::Draw(std::initializer_list<ID3D11RenderTargetView*> rtvList, UINT w, UINT h, ID3D11PixelShader* ps,
                      std::initializer_list<ID3D11ShaderResourceView*> srvList, ID3D11Buffer* cb, const void* data,
                      UINT bytes) {
  D3D11_MAPPED_SUBRESOURCE m;
  if (SUCCEEDED(m_ctx->Map(cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
    memcpy(m.pData, data, bytes);
    m_ctx->Unmap(cb, 0);
  }
  D3D11_VIEWPORT vp{0, 0, (float)w, (float)h, 0, 1};
  ID3D11RenderTargetView* rtvs[kMaxRtvs] = {};
  UINT nr = 0;
  for (ID3D11RenderTargetView* v : rtvList)
    if (nr < kMaxRtvs) rtvs[nr++] = v;
  m_ctx->OMSetRenderTargets(nr, rtvs, nullptr);
  m_ctx->RSSetViewports(1, &vp);
  m_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  m_ctx->IASetInputLayout(nullptr);
  m_ctx->VSSetShader(m_vs.Get(), nullptr, 0);
  m_ctx->PSSetShader(ps, nullptr, 0);
  m_ctx->PSSetConstantBuffers(0, 1, &cb);
  m_ctx->PSSetSamplers(0, 1, m_sampler.GetAddressOf());
  ID3D11ShaderResourceView* srvs[kMaxSrvs] = {};
  UINT n = 0;
  for (ID3D11ShaderResourceView* v : srvList)
    if (n < kMaxSrvs) srvs[n++] = v;
  m_ctx->PSSetShaderResources(0, kMaxSrvs, srvs);
  m_ctx->Draw(3, 0);
  ID3D11ShaderResourceView* none[kMaxSrvs] = {};
  m_ctx->PSSetShaderResources(0, kMaxSrvs, none);
  ID3D11RenderTargetView* noRtv[kMaxRtvs] = {};
  m_ctx->OMSetRenderTargets(kMaxRtvs, noRtv, nullptr);
}
