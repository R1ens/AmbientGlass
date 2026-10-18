#include "gpu_timer.h"

namespace glass {

bool GpuTimer::Init(ID3D11Device* device, ID3D11DeviceContext* ctx) {
  m_device = device;
  m_ctx = ctx;
  D3D11_QUERY_DESC d{D3D11_QUERY_TIMESTAMP_DISJOINT, 0};
  if (FAILED(device->CreateQuery(&d, &m_disjoint))) return false;
  d.Query = D3D11_QUERY_TIMESTAMP;
  for (auto& q : m_stamps)
    if (FAILED(device->CreateQuery(&d, &q))) return false;
  return true;
}

void GpuTimer::Begin() {
  m_count = 0;
  m_ctx->Begin(m_disjoint.Get());
}

void GpuTimer::Mark(const char* name) {
  if (m_count >= kMax) return;
  m_names[m_count] = name;
  m_ctx->End(m_stamps[m_count++].Get());
}

bool GpuTimer::End(std::vector<std::pair<std::string, double>>& steps) {
  m_ctx->End(m_disjoint.Get());
  D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj{};
  while (m_ctx->GetData(m_disjoint.Get(), &dj, sizeof(dj), 0) == S_FALSE) {
  }
  if (dj.Disjoint || !dj.Frequency) return false;
  UINT64 prev = 0;
  steps.clear();
  for (int i = 0; i < m_count; i++) {
    UINT64 t = 0;
    while (m_ctx->GetData(m_stamps[i].Get(), &t, sizeof(t), 0) == S_FALSE) {
    }
    if (i > 0) steps.emplace_back(m_names[i], (double)(t - prev) * 1000.0 / (double)dj.Frequency);
    prev = t;
  }
  return true;
}

}  // namespace glass
