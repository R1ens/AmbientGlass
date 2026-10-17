#include "display_info.h"

#include <vector>

float SdrWhiteLevel(HMONITOR monitor) {
  MONITORINFOEXW mi{};
  mi.cbSize = sizeof(mi);
  if (!GetMonitorInfoW(monitor, &mi)) return 1.f;
  UINT32 pathCount = 0, modeCount = 0;
  if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) != ERROR_SUCCESS) return 1.f;
  std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
  std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
  if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount, modes.data(), nullptr) !=
      ERROR_SUCCESS)
    return 1.f;
  for (UINT32 i = 0; i < pathCount; i++) {
    DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
    source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
    source.header.size = sizeof(source);
    source.header.adapterId = paths[i].sourceInfo.adapterId;
    source.header.id = paths[i].sourceInfo.id;
    if (DisplayConfigGetDeviceInfo(&source.header) != ERROR_SUCCESS || wcscmp(source.viewGdiDeviceName, mi.szDevice))
      continue;
    DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO color{};
    color.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO;
    color.header.size = sizeof(color);
    color.header.adapterId = paths[i].targetInfo.adapterId;
    color.header.id = paths[i].targetInfo.id;
    if (DisplayConfigGetDeviceInfo(&color.header) != ERROR_SUCCESS || !color.advancedColorEnabled) return 1.f;
    DISPLAYCONFIG_SDR_WHITE_LEVEL white{};
    white.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL;
    white.header.size = sizeof(white);
    white.header.adapterId = paths[i].targetInfo.adapterId;
    white.header.id = paths[i].targetInfo.id;
    if (DisplayConfigGetDeviceInfo(&white.header) != ERROR_SUCCESS) return 1.f;
    return white.SDRWhiteLevel / 1000.f;
  }
  return 1.f;
}
