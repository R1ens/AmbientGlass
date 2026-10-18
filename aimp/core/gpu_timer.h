#pragma once
// GPU time between points of a frame, from Direct3D 11 timestamp queries. For measuring (aimp_tests --bench),
// not for the running window: reading the results waits for the GPU.

#include <d3d11.h>

#include <string>
#include <utility>
#include <vector>

#include "d3d_helpers.h"

namespace glass {

class GpuTimer {
 public:
  bool Init(ID3D11Device* device, ID3D11DeviceContext* ctx);
  void Begin();                // a new frame
  void Mark(const char* name);  // the GPU time when the work issued so far is done
  // Waits for the frame and returns the time of each step (from the previous mark to this one), in ms; false
  // when the GPU clock was unreliable for this frame.
  bool End(std::vector<std::pair<std::string, double>>& steps);

 private:
  static constexpr int kMax = 16;
  ComPtr<ID3D11Device> m_device;
  ComPtr<ID3D11DeviceContext> m_ctx;
  ComPtr<ID3D11Query> m_disjoint, m_stamps[kMax];
  const char* m_names[kMax] = {};
  int m_count = 0;
};

}  // namespace glass
