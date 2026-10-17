#include "panel.h"

#include <dwmapi.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"
#include "common.h"
#include "glow_math.h"
#include "resource.h"


extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

using Microsoft::WRL::ComPtr;

namespace {

const wchar_t* kClass = L"VlcAmbientPanel";
constexpr DXGI_FORMAT kFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
constexpr float kBaseW = 600.f, kBaseH = 860.f;  // logical window size
constexpr float kFontSize = 17.f, kFontSizeLarge = 21.f;

// Slider ranges straight from the settings list: kMin_blur, kMax_blur, ...
#define VA_RANGE(t, n, d, lo, hi) constexpr int kMin_##n = (lo), kMax_##n = (hi);
VA_SETTINGS(VA_RANGE)
#undef VA_RANGE

const Settings kDefaults{};

const ImVec4 kAmber(1.00f, 0.66f, 0.22f, 1.f);
const ImVec4 kGreen(0.45f, 0.85f, 0.45f, 1.f);
const ImVec4 kGrey(0.55f, 0.55f, 0.55f, 1.f);

ImGuiStyle g_baseStyle;  // unscaled style; scaled copies are applied on DPI changes
ImFont* g_fontLarge = nullptr;

std::string FontPath(const wchar_t* file) {
  wchar_t dir[MAX_PATH];
  UINT n = GetWindowsDirectoryW(dir, MAX_PATH);
  std::wstring path = (n ? std::wstring(dir, n) : L"C:\\Windows") + L"\\Fonts\\" + file;
  if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) return {};
  return ToUtf8(path);  // ImGui opens files by UTF-8 name
}

void SetupBaseStyle() {
  ImGui::StyleColorsDark();
  ImGuiStyle& st = ImGui::GetStyle();
  st.WindowRounding = 0.f;
  st.WindowBorderSize = 0.f;
  st.WindowPadding = ImVec2(16, 12);
  st.FramePadding = ImVec2(8, 5);
  st.ItemSpacing = ImVec2(8, 7);
  st.FrameRounding = 5.f;
  st.GrabRounding = 5.f;
  st.ChildRounding = 8.f;
  st.PopupRounding = 6.f;
  st.ScrollbarRounding = 9.f;
  st.GrabMinSize = 12.f;

  ImVec4* c = st.Colors;
  auto amber = [](float a) { return ImVec4(kAmber.x, kAmber.y, kAmber.z, a); };
  c[ImGuiCol_WindowBg] = ImVec4(0.078f, 0.078f, 0.078f, 1.f);
  c[ImGuiCol_ChildBg] = ImVec4(0.11f, 0.11f, 0.11f, 1.f);
  c[ImGuiCol_PopupBg] = ImVec4(0.12f, 0.12f, 0.12f, 0.97f);
  c[ImGuiCol_Border] = ImVec4(0.26f, 0.26f, 0.26f, 0.6f);
  c[ImGuiCol_FrameBg] = ImVec4(0.16f, 0.16f, 0.16f, 1.f);
  c[ImGuiCol_FrameBgHovered] = ImVec4(0.22f, 0.20f, 0.18f, 1.f);
  c[ImGuiCol_FrameBgActive] = ImVec4(0.28f, 0.23f, 0.17f, 1.f);
  c[ImGuiCol_CheckMark] = kAmber;
  c[ImGuiCol_SliderGrab] = amber(0.80f);
  c[ImGuiCol_SliderGrabActive] = amber(1.f);
  c[ImGuiCol_Button] = ImVec4(0.21f, 0.20f, 0.19f, 1.f);
  c[ImGuiCol_ButtonHovered] = amber(0.45f);
  c[ImGuiCol_ButtonActive] = amber(0.70f);
  c[ImGuiCol_Header] = ImVec4(0.19f, 0.17f, 0.15f, 1.f);
  c[ImGuiCol_HeaderHovered] = amber(0.30f);
  c[ImGuiCol_HeaderActive] = amber(0.45f);
  c[ImGuiCol_Separator] = ImVec4(0.26f, 0.26f, 0.26f, 1.f);
  c[ImGuiCol_TextSelectedBg] = amber(0.35f);
  c[ImGuiCol_ResizeGrip] = amber(0.15f);
  c[ImGuiCol_ResizeGripHovered] = amber(0.45f);
  c[ImGuiCol_ResizeGripActive] = amber(0.70f);
  g_baseStyle = st;
}

void ApplyScaledStyle(float scale) {
  ImGuiStyle& st = ImGui::GetStyle();
  st = g_baseStyle;
  st.ScaleAllSizes(scale);
}

void Tooltip(const char* text, const char* extra = nullptr) {
  if (!ImGui::BeginItemTooltip()) return;
  ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24.f);
  ImGui::TextUnformatted(text);
  if (extra) ImGui::TextDisabled("%s", extra);
  ImGui::PopTextWrapPos();
  ImGui::EndTooltip();
}

void GreyWrapped(const char* text) {
  ImGui::PushStyleColor(ImGuiCol_Text, kGrey);
  ImGui::TextWrapped("%s", text);
  ImGui::PopStyleColor();
}

// Every slider label, to size the label column so none of them gets cut off.
const char* const kSliderLabels[] = {
    "Размытие", "Размер края", "Охват", "Начало затухания", "Кривая затухания", "Переход от картинки",
    "Плавность смены цвета", "Смена сцены и перемотка", "Шум против ступенек", "Яркость", "Контраст", "Насыщенность", "Сочность цветов",
};
float g_labelW = 0.f;

// Label on the left, slider filling the rest; right click resets to the default.
void SliderRow(const char* label, const char* id, int& v, int def, int lo, int hi, const char* fmt, const char* tip,
               ImGuiSliderFlags flags = 0) {
  char defText[32], defLine[64];
  std::snprintf(defText, sizeof(defText), fmt, def);
  std::snprintf(defLine, sizeof(defLine), "По умолчанию: %s (ПКМ — сброс)", defText);

  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted(label);
  Tooltip(tip, defLine);
  ImGui::SameLine(g_labelW);
  ImGui::SetNextItemWidth(-FLT_MIN);
  ImGui::SliderInt(id, &v, lo, hi, fmt, ImGuiSliderFlags_AlwaysClamp | flags);
  if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) v = def;
  Tooltip(tip, defLine);
}

#define SLIDER(field, label, fmt, tip, ...) \
  SliderRow(label, "##" #field, s.field, kDefaults.field, kMin_##field, kMax_##field, fmt, tip, ##__VA_ARGS__)

// Presets. Each one only touches the look of the glow, not where it is shown.
void PresetStandard(Settings& s) {
  Settings d;
  d.enabled = s.enabled;
  d.autostart = s.autostart;
  d.aroundWindow = s.aroundWindow;
  d.inBars = s.inBars;
  d.detectBars = s.detectBars;
  d.detectSideBars = s.detectSideBars;
  s = d;
}

void PresetSoft(Settings& s) {
  s.blur = 50;
  s.spread = 30;
  s.fadeCurve = 55;
  s.fadeStart = 10;
  s.brightness = 90;
  s.smoothingMs = 400;
}

void PresetBright(Settings& s) {
  s.blur = 25;
  s.spread = 45;
  s.fadeStart = 25;
  s.brightness = 120;
  s.saturation = 130;
  s.vibrance = 120;
}

void ResetAll(Settings& s) {
  Settings d;
  d.autostart = s.autostart;
  s = d;
}

void DrawStatus(const PanelStatus& st) {
  ImGui::BeginChild("##status", ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
  if (!st.vlcFound) {
    ImGui::TextColored(kGrey, "VLC не найден");
    GreyWrapped("Запустите видео в VLC — свечение появится автоматически.");
  } else {
    ImGui::TextColored(kGreen, "VLC найден");
    if (ImGui::BeginTable("##st", 2, ImGuiTableFlags_SizingStretchSame)) {
      ImGui::TableNextColumn();
      ImGui::Text("Режим: %s", st.fullscreen ? "полноэкранный" : "в окне");
      ImGui::TableNextColumn();
      ImGui::Text("Картинка: %d×%d", st.pictureW, st.pictureH);
      ImGui::TableNextColumn();
      ImGui::Text("Кадров от VLC: %.0f/с", st.captureFps);
      ImGui::TableNextColumn();
      ImGui::Text("Обновление свечения: %.1f мс", st.renderMs);
      ImGui::TableNextColumn();
      ImGui::Text("Задержка от кадра VLC: %.1f мс", st.latencyMs);
      Tooltip("Сколько проходит от момента, когда Windows вывела кадр VLC, до отправки свечения на экран. "
              "Ещё один кадр монитора добавляет сам рабочий стол.");
      ImGui::TableNextColumn();
      ImGui::Text("HDR: %s", st.hdr ? "да" : "нет");
      ImGui::EndTable();
    }
    if (st.bars[0] > 0 || st.bars[1] > 0 || st.bars[2] > 0 || st.bars[3] > 0)
      ImGui::TextWrapped("Вшитые полосы: сверху %.0f%%, снизу %.0f%%, слева %.0f%%, справа %.0f%%",
                         st.bars[0] * 100.f, st.bars[1] * 100.f, st.bars[2] * 100.f, st.bars[3] * 100.f);
    if (!st.captureOk) ImGui::TextColored(kAmber, "Не удаётся захватить окно VLC");
  }
  if (!st.lastError.empty()) {
    ImGui::TextColored(kAmber, "Последняя ошибка:");
    ImGui::SameLine();
    ImGui::TextWrapped("%s", st.lastError.c_str());
  }
  ImGui::EndChild();
}

}  // namespace

bool Panel::Create(HINSTANCE inst, ID3D11Device* device, ID3D11DeviceContext* ctx) {
  m_device = device;
  m_ctx = ctx;

  static bool registered = false;
  if (!registered) {
    WNDCLASSEXW wc{sizeof(wc)};
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.lpszClassName = kClass;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(IDI_APP));
    if (!wc.hIcon) wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    if (!RegisterClassExW(&wc)) return false;
    registered = true;
  }

  // Size and center on the primary monitor's work area.
  HMONITOR mon = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
  MONITORINFO mi{sizeof(mi)};
  GetMonitorInfoW(mon, &mi);
  const RECT& wa = mi.rcWork;
  float scale = ImGui_ImplWin32_GetDpiScaleForMonitor(mon);
  int w = std::min<int>((int)std::lround(kBaseW * scale), wa.right - wa.left);
  int h = std::min<int>((int)std::lround(kBaseH * scale), wa.bottom - wa.top);
  int x = wa.left + (wa.right - wa.left - w) / 2;
  int y = wa.top + (wa.bottom - wa.top - h) / 2;

  m_hwnd = CreateWindowExW(0, kClass, L"VLC Ambient Light — настройки", WS_OVERLAPPEDWINDOW, x, y, w, h, nullptr,
                           nullptr, inst, this);
  if (!m_hwnd) return false;
  m_dpiScale = ImGui_ImplWin32_GetDpiScaleForHwnd(m_hwnd);

  BOOL dark = TRUE;
  DwmSetWindowAttribute(m_hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));

  ComPtr<IDXGIDevice> dxgiDevice;
  ComPtr<IDXGIAdapter> adapter;
  ComPtr<IDXGIFactory2> factory;
  if (FAILED(device->QueryInterface(IID_PPV_ARGS(&dxgiDevice))) || FAILED(dxgiDevice->GetAdapter(&adapter)) ||
      FAILED(adapter->GetParent(IID_PPV_ARGS(&factory))))
    return false;

  DXGI_SWAP_CHAIN_DESC1 sd{};  // width/height 0 = take the client size
  sd.Format = kFormat;
  sd.SampleDesc.Count = 1;
  sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  sd.BufferCount = 2;
  sd.Scaling = DXGI_SCALING_STRETCH;
  sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  sd.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
  if (FAILED(factory->CreateSwapChainForHwnd(device, m_hwnd, &sd, nullptr, nullptr, &m_swap))) return false;
  factory->MakeWindowAssociation(m_hwnd, DXGI_MWA_NO_ALT_ENTER);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.IniFilename = nullptr;
  io.LogFilename = nullptr;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

  SetupBaseStyle();
  ApplyScaledStyle(m_dpiScale);
  LoadFonts(m_dpiScale);

  if (!ImGui_ImplWin32_Init(m_hwnd) || !ImGui_ImplDX11_Init(device, ctx)) return false;
  return true;
}

void Panel::Destroy() {
  if (ImGui::GetCurrentContext()) {
    ImGuiIO& io = ImGui::GetIO();
    if (io.BackendRendererUserData) ImGui_ImplDX11_Shutdown();
    if (io.BackendPlatformUserData) ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
  }
  g_fontLarge = nullptr;
  m_rtv.Reset();
  m_swap.Reset();
  if (m_hwnd) DestroyWindow(m_hwnd);
  m_hwnd = nullptr;
}

void Panel::Show() {
  if (!m_hwnd) return;
  ShowWindow(m_hwnd, IsIconic(m_hwnd) ? SW_RESTORE : SW_SHOW);
  SetForegroundWindow(m_hwnd);
}

void Panel::Hide() {
  if (m_hwnd) ShowWindow(m_hwnd, SW_HIDE);
}

bool Panel::Visible() const { return m_hwnd && IsWindowVisible(m_hwnd); }

void Panel::CreateRenderTarget() {
  ComPtr<ID3D11Texture2D> back;
  if (SUCCEEDED(m_swap->GetBuffer(0, IID_PPV_ARGS(&back))))
    m_device->CreateRenderTargetView(back.Get(), nullptr, &m_rtv);
}

void Panel::LoadFonts(float dpiScale) {
  ImGuiIO& io = ImGui::GetIO();
  io.Fonts->Clear();

  // Cyrillic plus the few typographic symbols the labels use.
  static ImVector<ImWchar> ranges;
  if (ranges.empty()) {
    ImFontGlyphRangesBuilder b;
    b.AddRanges(io.Fonts->GetGlyphRangesCyrillic());
    b.AddText("—–…×→←«»№°");
    b.BuildRanges(&ranges);
  }

  ImFontConfig cfg;
  cfg.OversampleH = 2;
  cfg.OversampleV = 1;
  float size = std::round(kFontSize * dpiScale), sizeLarge = std::round(kFontSizeLarge * dpiScale);

  ImFont* main = nullptr;
  std::string path;
  for (const wchar_t* file : {L"segoeui.ttf", L"arial.ttf", L"tahoma.ttf"})
    if (!(path = FontPath(file)).empty()) break;
  if (!path.empty()) main = io.Fonts->AddFontFromFileTTF(path.c_str(), size, &cfg, ranges.Data);
  if (!main) {
    ImFontConfig def;
    def.SizePixels = std::round(13.f * dpiScale);
    main = io.Fonts->AddFontDefault(&def);
  }

  g_fontLarge = nullptr;
  std::string semibold = FontPath(L"seguisb.ttf");
  if (semibold.empty()) semibold = path;
  if (!semibold.empty()) g_fontLarge = io.Fonts->AddFontFromFileTTF(semibold.c_str(), sizeLarge, &cfg, ranges.Data);
  if (!g_fontLarge) g_fontLarge = main;
  io.FontDefault = main;
}

int Panel::Frame(Settings& s, const PanelStatus& status, bool& changed) {
  changed = false;
  if (!m_hwnd || !m_swap || !IsWindowVisible(m_hwnd) || IsIconic(m_hwnd)) return PanelAction_None;

  if (m_resizeW && m_resizeH) {
    m_rtv.Reset();
    m_swap->ResizeBuffers(0, m_resizeW, m_resizeH, DXGI_FORMAT_UNKNOWN, 0);
    m_resizeW = m_resizeH = 0;
  }
  if (!m_rtv) CreateRenderTarget();
  if (!m_rtv) return PanelAction_None;

  if (m_fontsDirty) {
    m_fontsDirty = false;
    LoadFonts(m_dpiScale);
    ApplyScaledStyle(m_dpiScale);
    ImGui_ImplDX11_InvalidateDeviceObjects();  // NewFrame recreates them, font texture included
  }

  ImGui_ImplDX11_NewFrame();
  ImGui_ImplWin32_NewFrame();
  ImGui::NewFrame();

  const Settings before = s;
  int action = PanelAction_None;

  ImGuiIO& io = ImGui::GetIO();
  ImGui::SetNextWindowPos(ImVec2(0, 0));
  ImGui::SetNextWindowSize(io.DisplaySize);
  ImGui::Begin("##panel", nullptr,
               ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings);

  const ImGuiStyle& style = ImGui::GetStyle();
  g_labelW = 0.f;
  for (const char* l : kSliderLabels) g_labelW = std::max(g_labelW, ImGui::CalcTextSize(l).x);
  g_labelW += style.WindowPadding.x + style.ItemSpacing.x * 2.f;

  // 1. Master switch and status.
  ImGui::PushFont(g_fontLarge);
  ImGui::Checkbox("Свечение включено", &s.enabled);
  ImGui::PopFont();
  DrawStatus(status);
  ImGui::Spacing();

  // 2. Presets.
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted("Пресеты:");
  ImGui::SameLine();
  if (ImGui::Button("Стандарт")) PresetStandard(s);
  Tooltip("Значения по умолчанию для вида свечения. Где светить — не меняется.");
  ImGui::SameLine();
  if (ImGui::Button("Мягкий")) PresetSoft(s);
  Tooltip("Широкое размытое свечение, спокойная смена цвета.");
  ImGui::SameLine();
  if (ImGui::Button("Яркий")) PresetBright(s);
  Tooltip("Более яркое, насыщенное и широкое свечение.");
  ImGui::SameLine();
  if (ImGui::Button("Сброс всего")) ResetAll(s);
  Tooltip("Вернуть все настройки к значениям по умолчанию (кроме автозапуска).");
  ImGui::Spacing();

  // 3. Glow shape.
  if (ImGui::CollapsingHeader("Свечение", ImGuiTreeNodeFlags_DefaultOpen)) {
    SLIDER(blur, "Размытие", "%d", "Насколько размыто свечение. Больше — мягче и без резких пятен.");
    SLIDER(edge, "Размер края", "%d",
           "Толщина полосы по краю картинки, которая усредняется в цвет свечения. Меньше — свечение точнее "
           "повторяет самый край, больше — цвета берутся глубже и спокойнее.");
    SLIDER(spread, "Охват", "%d",
           "Как далеко свечение расходится за пределы картинки, до размытия. Размытие добавляет ещё около трёх "
           "своих радиусов.");
    SLIDER(fadeStart, "Начало затухания", "%d",
           "Где в пределах охвата свечение начинает гаснуть. Меньше — гаснет сразу у картинки, больше — дольше "
           "остаётся ярким. Отрицательные значения затемняют уже у самого края.");
    SLIDER(fadeCurve, "Кривая затухания", "%d",
           "Где в зоне затухания теряется половина света. Меньше — свет держится почти до конца и обрывается, "
           "больше — гаснет сразу и долго тлеет.");
    SLIDER(edgeBlend, "Переход от картинки", "%d",
           "Ширина полосы в чёрных полях, где свечение начинается как точный край картинки и постепенно "
           "размывается до обычного. 0 — резкая граница.");
    SLIDER(smoothingMs, "Плавность смены цвета", "%d мс",
           "Как медленно свечение догоняет обычные изменения цвета в кадре. 0 — без задержки: свечение идёт "
           "за видео кадр в кадр.",
           ImGuiSliderFlags_Logarithmic);
    SLIDER(cutFadeMs, "Смена сцены и перемотка", "%d мс",
           "За сколько свечение перетекает в новые цвета при резкой смене сцены или перемотке, вместо вспышки. "
           "При быстрой перемотке подряд перетекание становится длиннее. Обычное движение не задерживает. "
           "0 — мгновенно.",
           ImGuiSliderFlags_Logarithmic);
    SLIDER(debanding, "Шум против ступенек", "%d",
           "Добавляет едва заметный шум, чтобы на плавных переходах не было видно полос-ступенек.");
    if (status.vlcFound && status.pictureW > 0 && status.pictureH > 0) {
      GlowMetrics m = ComputeGlowMetrics(s, (float)status.pictureW, (float)status.pictureH);
      ImGui::TextColored(kGrey, "При этой картинке: охват %.0f px, размытие σ %.0f px, край %.0f px, переход %.0f px",
                         m.marginPx, m.sigmaPx, m.edgePx, m.transitionPx);
    } else {
      ImGui::TextColored(kGrey, "Размеры в пикселях появятся, когда VLC покажет видео.");
    }
  }

  // 4. Colors (SDR).
  if (ImGui::CollapsingHeader("Цвет", ImGuiTreeNodeFlags_DefaultOpen)) {
    SLIDER(brightness, "Яркость", "%d%%", "Яркость свечения. 100% — как в картинке.");
    SLIDER(contrast, "Контраст", "%d%%", "Контраст свечения. Больше — тёмное темнее, светлое светлее.");
    SLIDER(saturation, "Насыщенность", "%d%%", "Насыщенность цветов свечения. 0% — серое, 100% — как в картинке.");
    SLIDER(vibrance, "Сочность цветов", "%d%%",
           "Усиливает бледные цвета сильнее, чем и так насыщенные, — свечение выглядит живее без кислотности.");
  }

  // 5. Colors for HDR video.
  if (ImGui::CollapsingHeader("Цвет для HDR-видео")) {
    if (status.hdr)
      GreyWrapped("В видео найдены HDR-яркости: эти поправки прибавляются к обычным значениям.");
    else
      GreyWrapped("Сейчас не используется: HDR-яркостей в видео нет. Поправки прибавятся к обычным значениям, "
                  "когда они появятся (нужен включённый HDR в Windows).");
    ImGui::BeginDisabled(!status.hdr);
    SLIDER(hdrBrightness, "Яркость", "%d%%", "Поправка яркости для HDR-видео: 100% — без изменений.");
    SLIDER(hdrContrast, "Контраст", "%d%%", "Поправка контраста для HDR-видео: 100% — без изменений.");
    SLIDER(hdrSaturation, "Насыщенность", "%d%%", "Поправка насыщенности для HDR-видео: 100% — без изменений.");
    ImGui::EndDisabled();
  }

  // 6. Where to glow.
  if (ImGui::CollapsingHeader("Где светить", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::Checkbox("Вокруг окна VLC на рабочем столе (оконный режим)", &s.aroundWindow);
    Tooltip("Свечение вокруг окна VLC на рабочем столе.");
    ImGui::Checkbox("В чёрных полях (в т.ч. на весь экран)", &s.inBars);
    Tooltip("Свечение в чёрных полях вокруг картинки внутри окна VLC и в полноэкранном режиме.");
    ImGui::Checkbox("Находить вшитые полосы сверху и снизу", &s.detectBars);
    Tooltip("Чёрные полосы, вшитые в само видео, не считаются картинкой: цвета берутся с настоящего края, "
            "а свечение заполняет полосы. Полоса принимается после пары секунд меняющихся кадров.");
    ImGui::Checkbox("Находить вшитые полосы по бокам", &s.detectSideBars);
    Tooltip("То же для полос слева и справа (например, 4:3 внутри 16:9).");
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Стороны:");
    ImGui::SameLine();
    ImGui::Checkbox("Сверху", &s.dirTop);
    ImGui::SameLine();
    ImGui::Checkbox("Справа", &s.dirRight);
    ImGui::SameLine();
    ImGui::Checkbox("Снизу", &s.dirBottom);
    ImGui::SameLine();
    ImGui::Checkbox("Слева", &s.dirLeft);
  }

  // 7. General.
  if (ImGui::CollapsingHeader("Общее", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::Checkbox("Запускать вместе с Windows", &s.autostart);

    std::string luaTip = (status.luaInstalled ? "Файл: " : "Ожидаемое место: ") +
                         (status.luaPath.empty() ? std::string("неизвестно") : ToUtf8(status.luaPath));
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Пункт в меню VLC: Вид → Ambient light —");
    Tooltip(luaTip.c_str());
    ImGui::SameLine();
    if (status.luaInstalled)
      ImGui::TextColored(kGreen, "установлен");
    else
      ImGui::TextColored(kAmber, "не установлен");
    Tooltip(luaTip.c_str());
    if (ImGui::Button("Переустановить пункт меню")) action |= PanelAction_ReinstallLua;
    Tooltip("Заново скопировать файл пункта меню в папку расширений VLC.");
    GreyWrapped("После установки перезапустите VLC.");
  }

  // 8. Bottom row.
  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();
  if (ImGui::Button("Скрыть панель")) Hide();
  Tooltip("Панель скроется; программа продолжит работать в трее.");
  ImGui::SameLine();
  if (ImGui::Button("Выход")) action |= PanelAction_Quit;
  Tooltip("Выключить свечение и закрыть программу.");
  ImGui::Spacing();
  ImGui::TextDisabled("ПКМ по ползунку — сброс к значению по умолчанию");

  ImGui::End();
  ImGui::Render();

  const float clear[4] = {0.078f, 0.078f, 0.078f, 1.f};
  ID3D11RenderTargetView* rtv = m_rtv.Get();
  m_ctx->OMSetRenderTargets(1, &rtv, nullptr);
  m_ctx->ClearRenderTargetView(rtv, clear);
  ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
  m_ctx->OMSetRenderTargets(0, nullptr, nullptr);
  // No vsync wait: the panel shares the thread with the glow, and waiting would hold back fresh frames.
  m_swap->Present(0, 0);

  changed = s != before;
  return action;
}

LRESULT CALLBACK Panel::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == WM_NCCREATE) {
    auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
  }
  auto* self = reinterpret_cast<Panel*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (self) return self->HandleMessage(hwnd, msg, wp, lp);
  return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT Panel::HandleMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp)) return 1;

  switch (msg) {
    case WM_SIZE:
      if (wp != SIZE_MINIMIZED) {
        m_resizeW = LOWORD(lp);
        m_resizeH = HIWORD(lp);
      }
      return 0;
    case WM_GETMINMAXINFO: {
      auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
      mmi->ptMinTrackSize.x = (LONG)(380 * m_dpiScale);
      mmi->ptMinTrackSize.y = (LONG)(420 * m_dpiScale);
      return 0;
    }
    case WM_DPICHANGED: {
      m_dpiScale = HIWORD(wp) / 96.f;
      m_fontsDirty = true;
      const RECT* r = reinterpret_cast<const RECT*>(lp);
      SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                   SWP_NOZORDER | SWP_NOACTIVATE);
      return 0;
    }
    case WM_SYSCOMMAND:
      if ((wp & 0xfff0) == SC_KEYMENU) return 0;  // no Alt menu beep
      break;
    case WM_CLOSE:
      Hide();
      return 0;
    case WM_DESTROY:
      SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
      return 0;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}
