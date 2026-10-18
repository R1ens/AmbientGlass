#pragma once
// The AimpGlass window: borderless, drawn entirely with GpuScene + UiPainter, on a thread of its own so the
// player's thread never waits for rendering. The plugin feeds it model updates and audio, and receives the
// user's commands through Commands.

#include <windows.h>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "audio.h"
#include "glass_settings.h"
#include "model.h"
#include "ui_state.h"

namespace glass {

enum class Act {
  PlayPause, Next, Prev, Seek, Volume, Shuffle, Repeat,
  PlayRow,          // index: Model::rows index
  Nav,              // index: Nav
  Playlist,         // text: id of AIMP's loaded playlist to show
  Like,
  AddFolder,        // text: chosen folder
  Scan,
  AimpOption,       // index: QuickItem::Fading / PauseBetween / Equalizer, value: 0/1
  AimpSettings,     // open AIMP's own settings
  Classic,          // show AIMP's own window again
  Quit,
  SettingsChanged,  // settings: the new values
};

struct Action {
  Act type;
  double value = 0;
  int index = -1;
  std::wstring text;
  GlassSettings settings;
};

class Commands {
 public:
  virtual ~Commands() = default;
  // Called on the window's thread.
  virtual void Command(const Action& a) = 0;
};

class GlassWindow {
 public:
  GlassWindow();
  ~GlassWindow();
  // Starts the window thread and creates the (hidden) window; false with `error` when the GPU or window fails.
  bool Start(Commands* commands, const GlassSettings& settings, std::wstring& error);
  void Stop();

  // Thread-safe: queue a change of the model; applied on the window thread before the next frame.
  void Post(std::function<void(Model&)> update);
  // Thread-safe: audio for the spectrum and the background.
  AudioAnalyzer& Audio() { return m_audio; }

  void Show();   // shows and activates (restores when minimised)
  void Hide();
  HWND Hwnd() const { return m_hwnd; }

 private:
  struct Gpu;
  void ThreadMain(GlassSettings settings, std::wstring* error, HANDLE ready);
  static LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp);
  LRESULT Handle(UINT msg, WPARAM wp, LPARAM lp);
  void Frame();
  void Resize();
  void UpdateHover(float x, float y);
  void Click(const HitResult& h, float x, float y);
  void DragSlider(float x);
  void Key(UINT vk);
  void Char(wchar_t c);
  void Send(Action a);
  void SettingsChanged();
  // px per layout unit as drawn: the monitor's DPI, reduced when the window is smaller than the minimum
  // layout (window managers can size it below WM_GETMINMAXINFO), so everything still fits.
  float Scale() const { return m_drawScale; }
  float DpiScale() const { return m_dpi / 96.f; }

  Commands* m_cmd = nullptr;
  std::thread m_thread;
  HWND m_hwnd = nullptr;
  std::atomic<bool> m_running{false};
  std::unique_ptr<Gpu> m_gpu;
  UINT m_dpi = 96;
  float m_drawScale = 1;

  std::mutex m_postMu;
  std::vector<std::function<void(Model&)>> m_posted;

  AudioAnalyzer m_audio;
  Model m_model;
  UiState m_ui;
  Layout m_layout;
  Palette m_palFrom, m_palTo;
  float m_palT = 1;
  double m_posBase = 0, m_posAt = 0;  // last reported position and when it came (seconds of m_clock)
  double m_clock = 0;
  double m_animClock = 0;  // the background's clock, faster for faster music
  float m_rate = 1;
  LARGE_INTEGER m_qpcFreq{}, m_qpcLast{};
  bool m_tracking = false;
  float m_scrollTarget = 0;
};

}  // namespace glass
