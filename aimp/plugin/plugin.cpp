// AimpGlass plugin: replaces AIMP's window with the glass window and keeps the two in step.
//
// Threads: every SDK callback arrives on AIMP's main thread, and every SDK call is made there too. The glass
// window runs on its own thread; its commands come back to the main thread through a message-only window, and
// what it shows goes to it through GlassWindow::Post. Waveforms are decoded in AIMP's worker tasks.

#include <windows.h>
#include <shlobj.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "common.h"
#include "glass_window.h"
#include "library.h"
#include "log.h"
#include "palette.h"
#include "sdk.h"
#include "search.h"
#include "ui_text.h"
#include "waveform.h"

// Declared in the SDK's IUnknownImpl.h and left for the plugin to define.
bool EqualGUID(REFIID a, REFIID b) { return IsEqualGUID(a, b) != FALSE; }

using namespace aimp;
using glass::Act;
using glass::Action;
using Microsoft::WRL::ComPtr;

namespace {

constexpr UINT WM_GLASS_ACTION = WM_APP + 10;
constexpr UINT WM_GLASS_WAVE = WM_APP + 11;
constexpr UINT WM_GLASS_SCANNED = WM_APP + 12;
constexpr int kCoverSide = 512;
constexpr int kWaveColumns = 1024;
const wchar_t kMenuId[] = L"aimp_glass.show";

std::wstring DataDir() {
  PWSTR p = nullptr;
  std::wstring dir;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &p))) dir = std::wstring(p) + L"\\AimpGlass";
  CoTaskMemFree(p);
  CreateDirectoryW(dir.c_str(), nullptr);
  CreateDirectoryW((dir + L"\\waves").c_str(), nullptr);
  return dir;
}

// Encoded image bytes (album art) to 8-bit RGBA, at most kCoverSide on the long side.
std::shared_ptr<glass::Cover> DecodeCover(const void* data, size_t size) {
  ComPtr<IWICImagingFactory> wic;
  ComPtr<IWICStream> stream;
  ComPtr<IWICBitmapDecoder> dec;
  ComPtr<IWICBitmapFrameDecode> frame;
  if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))) ||
      FAILED(wic->CreateStream(&stream)) ||
      FAILED(stream->InitializeFromMemory((BYTE*)data, (DWORD)size)) ||
      FAILED(wic->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &dec)) ||
      FAILED(dec->GetFrame(0, &frame)))
    return nullptr;
  UINT w = 0, h = 0;
  frame->GetSize(&w, &h);
  if (!w || !h) return nullptr;
  ComPtr<IWICBitmapSource> src = frame;
  const UINT longSide = std::max(w, h);
  if (longSide > kCoverSide) {
    ComPtr<IWICBitmapScaler> sc;
    UINT nw = std::max(1u, w * kCoverSide / longSide), nh = std::max(1u, h * kCoverSide / longSide);
    if (SUCCEEDED(wic->CreateBitmapScaler(&sc)) &&
        SUCCEEDED(sc->Initialize(frame.Get(), nw, nh, WICBitmapInterpolationModeHighQualityCubic)))
      src = sc, w = nw, h = nh;
  }
  ComPtr<IWICFormatConverter> conv;
  if (FAILED(wic->CreateFormatConverter(&conv)) ||
      FAILED(conv->Initialize(src.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0,
                              WICBitmapPaletteTypeCustom)))
    return nullptr;
  auto c = std::make_shared<glass::Cover>();
  c->w = (int)w, c->h = (int)h;
  c->rgba.resize((size_t)w * h * 4);
  if (FAILED(conv->CopyPixels(nullptr, w * 4, (UINT)c->rgba.size(), c->rgba.data()))) return nullptr;
  return c;
}

// An AIMP image without its encoded bytes: draw it into a DIB.
std::shared_ptr<glass::Cover> DrawCover(IAIMPImage* img) {
  SIZE sz{};
  if (!img || FAILED(img->GetSize(&sz)) || sz.cx <= 0 || sz.cy <= 0) return nullptr;
  int side = std::min<int>(kCoverSide, std::max(sz.cx, sz.cy));
  BITMAPINFO bi{};
  bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
  bi.bmiHeader.biWidth = side;
  bi.bmiHeader.biHeight = -side;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  void* bits = nullptr;
  HDC dc = CreateCompatibleDC(nullptr);
  HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
  if (!bmp) {
    DeleteDC(dc);
    return nullptr;
  }
  HGDIOBJ old = SelectObject(dc, bmp);
  RECT r{0, 0, side, side};
  img->Draw(dc, r, AIMP_IMAGE_DRAW_STRETCHMODE_FILL | AIMP_IMAGE_DRAW_QUALITY_HIGH, nullptr);
  GdiFlush();
  auto c = std::make_shared<glass::Cover>();
  c->w = c->h = side;
  c->rgba.resize((size_t)side * side * 4);
  const uint8_t* s = static_cast<const uint8_t*>(bits);
  for (size_t i = 0; i < (size_t)side * side; i++) {
    c->rgba[i * 4] = s[i * 4 + 2];
    c->rgba[i * 4 + 1] = s[i * 4 + 1];
    c->rgba[i * 4 + 2] = s[i * 4];
    c->rgba[i * 4 + 3] = 255;
  }
  SelectObject(dc, old);
  DeleteObject(bmp);
  DeleteDC(dc);
  return c;
}

uint64_t NextCoverId() {
  static std::atomic<uint64_t> id{1};
  return id++;
}

bool FileStamp(const std::wstring& path, uint64_t& size, uint64_t& mtime) {
  WIN32_FILE_ATTRIBUTE_DATA a;
  if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &a)) return false;
  size = ((uint64_t)a.nFileSizeHigh << 32) | a.nFileSizeLow;
  mtime = ((uint64_t)a.ftLastWriteTime.dwHighDateTime << 32) | a.ftLastWriteTime.dwLowDateTime;
  return true;
}

glass::LibTrack TrackFromInfo(IAIMPFileInfo* fi) {
  glass::LibTrack t;
  if (!fi) return t;
  t.path = PropString(fi, AIMP_FILEINFO_PROPID_FILENAME);
  t.title = PropString(fi, AIMP_FILEINFO_PROPID_TITLE);
  t.artist = PropString(fi, AIMP_FILEINFO_PROPID_ARTIST);
  t.album = PropString(fi, AIMP_FILEINFO_PROPID_ALBUM);
  fi->GetValueAsFloat(AIMP_FILEINFO_PROPID_DURATION, &t.duration);
  if (t.title.empty()) {
    std::wstring name = glass::FolderName(t.path);
    size_t dot = name.find_last_of(L'.');
    t.title = dot == std::wstring::npos ? name : name.substr(0, dot);
  }
  return t;
}

glass::ListRow RowFromTrack(const glass::LibTrack& t) {
  glass::ListRow r;
  r.title = t.title, r.artist = t.artist, r.album = t.album, r.duration = t.duration;
  r.search = glass::NormalizeForSearch(t.title + L" " + t.artist + L" " + t.album + L" " + glass::FolderName(t.path));
  return r;
}

// ---------------------------------------------------------------------------------------------------------

class Controller;

class Hook : public IUnknownImpl<IAIMPMessageHook> {
 public:
  explicit Hook(Controller* c) : m_c(c) {}
  void WINAPI CoreMessage(DWORD msg, INT32 p1, void* p2, HRESULT* result) override;

 private:
  Controller* m_c;
};

class Visual : public IUnknownImpl<IAIMPExtensionCustomVisualization> {
 public:
  explicit Visual(Controller* c) : m_c(c) {}
  DWORD WINAPI GetFlags() override {
    return AIMP_VISUAL_FLAGS_RQD_DATA_SPECTRUM | AIMP_VISUAL_FLAGS_NOT_SUSPEND;
  }
  void WINAPI Draw(PAIMPVisualData d) override;
  HRESULT WINAPI QueryInterface(REFIID riid, LPVOID* ppv) override {
    if (!ppv) return E_POINTER;
    if (IsEqualGUID(riid, IID_IUnknown) || IsEqualGUID(riid, IID_IAIMPExtensionCustomVisualization)) {
      *ppv = static_cast<IAIMPExtensionCustomVisualization*>(this);
      AddRef();
      return S_OK;
    }
    return E_NOINTERFACE;
  }

 private:
  Controller* m_c;
  LARGE_INTEGER m_last{};
};

class PlaylistEvents : public IUnknownImpl<IAIMPExtensionPlaylistManagerListener> {
 public:
  explicit PlaylistEvents(Controller* c) : m_c(c) {}
  void WINAPI PlaylistActivated(IAIMPPlaylist*) override;
  void WINAPI PlaylistAdded(IAIMPPlaylist*) override;
  void WINAPI PlaylistRemoved(IAIMPPlaylist*) override;
  HRESULT WINAPI QueryInterface(REFIID riid, LPVOID* ppv) override {
    if (!ppv) return E_POINTER;
    if (IsEqualGUID(riid, IID_IUnknown) || IsEqualGUID(riid, IID_IAIMPExtensionPlaylistManagerListener)) {
      *ppv = static_cast<IAIMPExtensionPlaylistManagerListener*>(this);
      AddRef();
      return S_OK;
    }
    return E_NOINTERFACE;
  }

 private:
  Controller* m_c;
};

class ListEvents : public IUnknownImpl<IAIMPPlaylistListener> {
 public:
  explicit ListEvents(Controller* c) : m_c(c) {}
  void WINAPI Activated() override {}
  void WINAPI Changed(DWORD flags) override;
  void WINAPI Removed() override {}
  HRESULT WINAPI QueryInterface(REFIID riid, LPVOID* ppv) override {
    if (!ppv) return E_POINTER;
    if (IsEqualGUID(riid, IID_IUnknown) || IsEqualGUID(riid, IID_IAIMPPlaylistListener)) {
      *ppv = static_cast<IAIMPPlaylistListener*>(this);
      AddRef();
      return S_OK;
    }
    return E_NOINTERFACE;
  }

 private:
  Controller* m_c;
};

class MenuEvent : public IUnknownImpl<IAIMPActionEvent> {
 public:
  explicit MenuEvent(Controller* c) : m_c(c) {}
  void WINAPI OnExecute(IUnknown*) override;
  HRESULT WINAPI QueryInterface(REFIID riid, LPVOID* ppv) override {
    if (!ppv) return E_POINTER;
    if (IsEqualGUID(riid, IID_IUnknown) || IsEqualGUID(riid, IID_IAIMPActionEvent)) {
      *ppv = static_cast<IAIMPActionEvent*>(this);
      AddRef();
      return S_OK;
    }
    return E_NOINTERFACE;
  }

 private:
  Controller* m_c;
};

// Decodes one track into a waveform on an AIMP worker thread.
class WaveTask : public IUnknownImpl<IAIMPTask> {
 public:
  WaveTask(HWND notify, std::wstring path, std::wstring cacheFile, uint64_t generation)
      : m_notify(notify), m_path(std::move(path)), m_cache(std::move(cacheFile)), m_gen(generation) {}
  void WINAPI Execute(IAIMPTaskOwner* owner) override;
  HRESULT WINAPI QueryInterface(REFIID riid, LPVOID* ppv) override {
    if (!ppv) return E_POINTER;
    if (IsEqualGUID(riid, IID_IUnknown) || IsEqualGUID(riid, IID_IAIMPTask)) {
      *ppv = static_cast<IAIMPTask*>(this);
      AddRef();
      return S_OK;
    }
    return E_NOINTERFACE;
  }
  std::atomic<bool> cancel{false};

 private:
  HWND m_notify;
  std::wstring m_path, m_cache;
  uint64_t m_gen;
};

struct WaveResult {
  uint64_t generation;
  std::vector<glass::WaveColumn> columns;
  float progress;
  bool done;
};

// ---------------------------------------------------------------------------------------------------------

class Controller : public glass::Commands {
 public:
  bool Start();
  void Stop();

  // Main thread
  void OnMessage(DWORD msg, INT32 p1, void* p2);
  void OnSpectrum(const float* s, float dtMs) { m_window.Audio().Feed(s, dtMs); }
  void OnPlaylistsChanged() { RefreshList(); RefreshCounts(); }
  void OnListChanged(DWORD flags) {
    if (flags & (AIMP_PLAYLIST_NOTIFY_CONTENT | AIMP_PLAYLIST_NOTIFY_FILEINFO | AIMP_PLAYLIST_NOTIFY_PLAYBACKCURSOR))
      RefreshList();
  }
  void ShowGlass();

  // Window thread
  void Command(const Action& a) override {
    PostMessageW(m_msgWnd, WM_GLASS_ACTION, 0, (LPARAM) new Action(a));
  }

  static LRESULT CALLBACK MsgProc(HWND h, UINT msg, WPARAM wp, LPARAM lp);
  static void CALLBACK OnWinEvent(HWINEVENTHOOK, DWORD, HWND h, LONG obj, LONG, DWORD, DWORD);
  static Controller* s_self;

 private:
  void OnAction(const Action& a);
  void HideAimp();
  void ShowClassic();
  void RefreshTrack();
  void RefreshState();
  void RefreshList();
  void RefreshCounts();
  void RequestCover(IAIMPFileInfo* fi);
  static void WINAPI OnCover(IAIMPImage* image, IAIMPImageContainer* c, void* user);
  void SetCover(std::shared_ptr<glass::Cover> c);
  void StartWave(const std::wstring& path);
  void OpenFolder(const std::wstring& path);
  void ScanFolders();
  void SaveLibrary() { m_lib.Save(m_dir + L"\\library.txt"); }
  void SaveSettings() { SaveSettingsIni(m_dir + L"\\settings.ini", m_settings); }
  Ref<IAIMPPlaylist> CurrentPlaylist();
  void ListenTo(IAIMPPlaylist* pl);
  void AddMenu();
  bool ReadFading();

  glass::GlassWindow m_window;
  glass::GlassSettings m_settings;
  glass::Library m_lib;
  std::wstring m_dir;
  HWND m_msgWnd = nullptr;
  HWND m_aimpMain = nullptr, m_aimpApp = nullptr;
  HWINEVENTHOOK m_showHook = nullptr;
  bool m_classic = false, m_loaded = false;
  Hook* m_hook = nullptr;
  Visual* m_visual = nullptr;
  PlaylistEvents* m_plEvents = nullptr;
  ListEvents* m_listEvents = nullptr;
  MenuEvent* m_menuEvent = nullptr;
  Ref<IAIMPMenuItem> m_menuItems[2];
  Ref<IAIMPPlaylist> m_listened;
  glass::Nav m_nav = glass::Nav::Playlists;
  std::vector<glass::LibTrack> m_navTracks;     // rows of the Recent / Favorites views
  std::vector<std::wstring> m_navFolders;       // rows of the Folders view
  std::vector<int> m_folderCounts;
  glass::LibTrack m_track;
  uint64_t m_coverGen = 0, m_waveGen = 0;
  WaveTask* m_waveTask = nullptr;
  TTaskHandle m_waveHandle = 0;
  std::thread m_scan;
  volatile bool m_scanCancel = false;
};

Controller* Controller::s_self = nullptr;

// ---------------------------------------------------------------------------------------------------------

void WINAPI Hook::CoreMessage(DWORD msg, INT32 p1, void* p2, HRESULT*) {
  // The hook sees every command and property call too; only events matter here.
  if (msg >= AIMP_MSG_EVENT_BASE) m_c->OnMessage(msg, p1, p2);
}

void WINAPI Visual::Draw(PAIMPVisualData d) {
  LARGE_INTEGER now, f;
  QueryPerformanceCounter(&now);
  QueryPerformanceFrequency(&f);
  float dt = m_last.QuadPart ? (float)((now.QuadPart - m_last.QuadPart) * 1000.0 / f.QuadPart) : 33.f;
  m_last = now;
  float mix[AIMP_VISUAL_SPECTRUM_MAX];
  for (int i = 0; i < AIMP_VISUAL_SPECTRUM_MAX; i++) mix[i] = 0.5f * (d->Spectrum[0][i] + d->Spectrum[1][i]);
  m_c->OnSpectrum(mix, dt);
}

void WINAPI PlaylistEvents::PlaylistActivated(IAIMPPlaylist*) { m_c->OnPlaylistsChanged(); }
void WINAPI PlaylistEvents::PlaylistAdded(IAIMPPlaylist*) { m_c->OnPlaylistsChanged(); }
void WINAPI PlaylistEvents::PlaylistRemoved(IAIMPPlaylist*) { m_c->OnPlaylistsChanged(); }
void WINAPI ListEvents::Changed(DWORD flags) { m_c->OnListChanged(flags); }
void WINAPI MenuEvent::OnExecute(IUnknown*) { m_c->ShowGlass(); }

void WINAPI WaveTask::Execute(IAIMPTaskOwner* owner) {
  auto post = [&](std::vector<glass::WaveColumn> cols, float progress, bool done) {
    auto* r = new WaveResult{m_gen, std::move(cols), progress, done};
    if (!PostMessageW(m_notify, WM_GLASS_WAVE, 0, (LPARAM)r)) delete r;
  };
  Ref<IAIMPServiceAudioDecoders> decs = Service<IAIMPServiceAudioDecoders>(IID_IAIMPServiceAudioDecoders);
  Ref<IAIMPAudioDecoder> dec;
  if (!decs || FAILED(decs->CreateDecoderForFileURI(MakeString(m_path).Get(), 0, nullptr, dec.Put())) || !dec) {
    Log::Warn("wave: no decoder for %s", ToUtf8(m_path).c_str());
    post({}, 1, true);
    return;
  }
  INT32 rate = 0, ch = 0, fmt = 0;
  if (!dec->GetStreamInfo(&rate, &ch, &fmt) || ch <= 0 || dec->IsRealTimeStream()) {
    post({}, 1, true);
    return;
  }
  const int bps = fmt == AIMP_DECODER_SAMPLEFORMAT_08BIT ? 1 : fmt == AIMP_DECODER_SAMPLEFORMAT_16BIT ? 2
                : fmt == AIMP_DECODER_SAMPLEFORMAT_24BIT ? 3 : fmt == AIMP_DECODER_SAMPLEFORMAT_64BITFLOAT ? 8 : 4;
  const int frameBytes = bps * ch;
  const INT64 total = dec->GetSize() > 0 ? dec->GetSize() / frameBytes : 0;
  glass::WaveformBuilder b(kWaveColumns, total);
  std::vector<uint8_t> buf(65536 - 65536 % frameBytes);
  std::vector<float> f(buf.size() / bps);
  DWORD lastPost = GetTickCount();
  while (!cancel && !(owner && owner->IsCanceled())) {
    int got = dec->Read(buf.data(), (INT32)buf.size());
    if (got <= 0) break;
    int samples = got / bps;
    for (int i = 0; i < samples; i++) {
      const uint8_t* p = &buf[(size_t)i * bps];
      switch (fmt) {
        case AIMP_DECODER_SAMPLEFORMAT_08BIT: f[i] = (p[0] - 128) / 128.f; break;
        case AIMP_DECODER_SAMPLEFORMAT_16BIT: f[i] = *(const int16_t*)p / 32768.f; break;
        case AIMP_DECODER_SAMPLEFORMAT_24BIT: f[i] = ((int32_t)((p[0] << 8) | (p[1] << 16) | (p[2] << 24)) >> 8) / 8388608.f; break;
        case AIMP_DECODER_SAMPLEFORMAT_32BIT: f[i] = *(const int32_t*)p / 2147483648.f; break;
        case AIMP_DECODER_SAMPLEFORMAT_64BITFLOAT: f[i] = (float)*(const double*)p; break;
        default: f[i] = *(const float*)p; break;
      }
    }
    b.Add(f.data(), samples / ch, ch);
    if (GetTickCount() - lastPost > 250) {
      post({}, b.Progress(), false);
      lastPost = GetTickCount();
    }
  }
  if (cancel || (owner && owner->IsCanceled())) return;
  std::vector<glass::WaveColumn> cols = b.Finish();
  if (!glass::SaveWave(m_cache, cols)) Log::Warn("wave: cache not written");
  post(std::move(cols), 1, true);
}

// ---------------------------------------------------------------------------------------------------------

bool Controller::Start() {
  s_self = this;
  m_dir = DataDir();
  Log::Init(m_dir + L"\\log.txt");
  LoadSettingsIni(m_dir + L"\\settings.ini", m_settings);
  m_lib.Load(m_dir + L"\\library.txt");

  WNDCLASSW wc{};
  wc.lpfnWndProc = MsgProc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = L"AimpGlassController";
  RegisterClassW(&wc);
  m_msgWnd = CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, nullptr);

  std::wstring err;
  if (!m_window.Start(this, m_settings, err)) {
    Log::Error("glass window: %s", ToUtf8(err).c_str());
    return false;
  }

  if (auto d = Service<IAIMPServiceMessageDispatcher>(IID_IAIMPServiceMessageDispatcher)) {
    m_hook = new Hook(this);
    m_hook->AddRef();
    d->Hook(m_hook);
  }
  m_visual = new Visual(this);
  m_visual->AddRef();
  Core()->RegisterExtension(IID_IAIMPServiceVisualizations, m_visual);
  m_plEvents = new PlaylistEvents(this);
  m_plEvents->AddRef();
  Core()->RegisterExtension(IID_IAIMPServicePlaylistManager, m_plEvents);
  m_listEvents = new ListEvents(this);
  m_listEvents->AddRef();
  AddMenu();
  m_window.Post([fading = ReadFading()](glass::Model& m) { m.aimpFading = fading; });
  Log::Info("plugin: started");
  return true;
}

void Controller::Stop() {
  m_scanCancel = true;
  if (m_scan.joinable()) m_scan.join();
  if (m_waveTask) {
    m_waveTask->cancel = true;
    if (auto th = Service<IAIMPServiceThreads>(IID_IAIMPServiceThreads)) th->Cancel(m_waveHandle, AIMP_SERVICE_THREADS_FLAGS_WAITFOR);
    m_waveTask->Release();
    m_waveTask = nullptr;
  }
  if (m_showHook) UnhookWinEvent(m_showHook);
  m_showHook = nullptr;
  if (m_aimpMain && IsWindow(m_aimpMain)) ShowWindow(m_aimpMain, SW_SHOW);  // AIMP keeps a usable window
  if (m_listened) m_listened->ListenerRemove(m_listEvents);
  m_listened.Reset();
  if (auto d = Service<IAIMPServiceMessageDispatcher>(IID_IAIMPServiceMessageDispatcher))
    if (m_hook) d->Unhook(m_hook);
  for (IUnknown* ext : {(IUnknown*)m_visual, (IUnknown*)m_plEvents})
    if (ext) Core()->UnregisterExtension(ext);
  for (auto& item : m_menuItems)
    if (item) Core()->UnregisterExtension(item.Get()), item.Reset();
  m_window.Stop();
  SaveSettings();
  SaveLibrary();
  for (IUnknown* o : {(IUnknown*)m_hook, (IUnknown*)m_visual, (IUnknown*)m_plEvents, (IUnknown*)m_listEvents,
                      (IUnknown*)m_menuEvent})
    if (o) o->Release();
  m_hook = nullptr, m_visual = nullptr, m_plEvents = nullptr, m_listEvents = nullptr, m_menuEvent = nullptr;
  if (m_msgWnd) DestroyWindow(m_msgWnd);
  m_msgWnd = nullptr;
  UnregisterClassW(L"AimpGlassController", GetModuleHandleW(nullptr));
  s_self = nullptr;
}

void Controller::AddMenu() {
  // "AimpGlass" in the tray menu and in AIMP's main menu: the way back from the classic window.
  auto mm = Service<IAIMPServiceMenuManager>(IID_IAIMPServiceMenuManager);
  if (!mm) return;
  m_menuEvent = new MenuEvent(this);
  m_menuEvent->AddRef();
  const int parents[2] = {AIMP_MENUID_PLAYER_TRAY, AIMP_MENUID_PLAYER_MAIN_FUNCTIONS};
  for (int i = 0; i < 2; i++) {
    Ref<IAIMPMenuItem> parent, item;
    if (FAILED(mm->GetBuiltIn(parents[i], parent.Put())) ||
        FAILED(Core()->CreateObject(IID_IAIMPMenuItem, item.PutVoid())) || !item)
      continue;
    item->SetValueAsObject(AIMP_MENUITEM_PROPID_ID, MakeString(std::wstring(kMenuId) + (i ? L".main" : L".tray")).Get());
    item->SetValueAsObject(AIMP_MENUITEM_PROPID_CAPTION, MakeString(L"Открыть AimpGlass").Get());
    item->SetValueAsObject(AIMP_MENUITEM_PROPID_EVENT, m_menuEvent);
    item->SetValueAsObject(AIMP_MENUITEM_PROPID_PARENT, parent.Get());
    if (i == 0) item->SetValueAsInt32(AIMP_MENUITEM_PROPID_DEFAULT, 1);
    if (SUCCEEDED(Core()->RegisterExtension(IID_IAIMPServiceMenuManager, item.Get()))) m_menuItems[i] = item;
  }
}

bool Controller::ReadFading() {
  auto cfg = Service<IAIMPServiceConfig>(IID_IAIMPServiceConfig);
  INT32 v = 0;
  return cfg && SUCCEEDED(cfg->GetValueAsInt32(MakeString(L"AIMPSoundOut\\FadingEnabled").Get(), &v)) && v != 0;
}

// ---------------------------------------------------------------------------------------------------------

void Controller::OnMessage(DWORD msg, INT32 p1, void*) {
  switch (msg) {
    case AIMP_MSG_EVENT_LOADED:
      m_loaded = true;
      HideAimp();
      RefreshTrack();
      RefreshState();
      RefreshList();
      RefreshCounts();
      if (m_settings.autoScan) ScanFolders();
      ShowGlass();
      break;
    case AIMP_MSG_EVENT_STREAM_START:
    case AIMP_MSG_EVENT_STREAM_START_SUBTRACK:
      RefreshTrack();
      RefreshList();
      break;
    case AIMP_MSG_EVENT_PLAYING_FILE_INFO:
      RefreshTrack();
      break;
    case AIMP_MSG_EVENT_PLAYER_STATE:
      RefreshState();
      if (p1 == AIMP_PLAYER_STATE_STOPPED) m_window.Post([](glass::Model& m) { m.position = 0; });
      break;
    case AIMP_MSG_EVENT_PLAYER_UPDATE_POSITION_HR: {
      auto pl = Service<IAIMPServicePlayer>(IID_IAIMPServicePlayer);
      double pos = 0;
      if (pl && SUCCEEDED(pl->GetPosition(&pos))) m_window.Post([pos](glass::Model& m) { m.position = pos; });
      break;
    }
    case AIMP_MSG_EVENT_PROPERTY_VALUE:
      if (p1 == AIMP_MSG_PROPERTY_VOLUME || p1 == AIMP_MSG_PROPERTY_SHUFFLE || p1 == AIMP_MSG_PROPERTY_REPEAT) RefreshState();
      break;
    case AIMP_MSG_EVENT_OPTIONS:
      m_window.Post([fading = ReadFading()](glass::Model& m) { m.aimpFading = fading; });
      break;
    case AIMP_MSG_EVENT_TERMINATING:
      m_window.Hide();
      break;
    default:
      break;
  }
}

void Controller::HideAimp() {
  auto d = Service<IAIMPServiceMessageDispatcher>(IID_IAIMPServiceMessageDispatcher);
  if (!d) return;
  d->Send(AIMP_MSG_PROPERTY_HWND, AIMP_MPH_MAINFORM, &m_aimpMain);
  d->Send(AIMP_MSG_PROPERTY_HWND, AIMP_MPH_APPLICATION, &m_aimpApp);
  if (!m_settings.hideAimp || m_classic) return;
  if (m_aimpMain) ShowWindow(m_aimpMain, SW_HIDE);
  Log::Info("aimp: main window %p hidden", (void*)m_aimpMain);
  // AIMP shows its window again on its own (when playback starts, from the tray): hide it each time and bring
  // the glass window up instead.
  if (!m_showHook)
    m_showHook = SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_SHOW, nullptr, OnWinEvent, GetCurrentProcessId(), 0,
                                 WINEVENT_OUTOFCONTEXT);
}

void CALLBACK Controller::OnWinEvent(HWINEVENTHOOK, DWORD, HWND h, LONG obj, LONG, DWORD, DWORD) {
  Controller* c = s_self;
  if (!c || obj != OBJID_WINDOW || c->m_classic || !c->m_settings.hideAimp) return;
  if (h == c->m_aimpMain) {
    ShowWindow(h, SW_HIDE);
    if (!IsWindowVisible(c->m_window.Hwnd()) || IsIconic(c->m_window.Hwnd())) c->m_window.Show();
  }
}

void Controller::ShowGlass() {
  m_classic = false;
  HideAimp();
  m_window.Show();
}

void Controller::ShowClassic() {
  m_classic = true;
  m_window.Hide();
  if (m_aimpMain) {
    ShowWindow(m_aimpMain, SW_SHOW);
    SetForegroundWindow(m_aimpMain);
  }
}

// ---------------------------------------------------------------------------------------------------------

void Controller::RefreshTrack() {
  auto pl = Service<IAIMPServicePlayer>(IID_IAIMPServicePlayer);
  if (!pl) return;
  Ref<IAIMPFileInfo> fi;
  if (FAILED(pl->GetInfo(fi.Put())) || !fi) return;
  glass::LibTrack t = TrackFromInfo(fi.Get());
  double dur = 0;
  if (SUCCEEDED(pl->GetDuration(&dur)) && dur > 0) t.duration = dur;
  const bool newTrack = !glass::SamePath(t.path, m_track.path);
  m_track = t;
  bool fav = m_lib.IsFavorite(t.path);
  m_window.Post([t, fav, newTrack](glass::Model& m) {
    m.track.title = t.title, m.track.artist = t.artist, m.track.album = t.album, m.track.path = t.path;
    m.track.duration = t.duration;
    m.track.favorite = fav;
    if (newTrack) {
      m.wave = glass::Wave{};
      m.position = 0;
    }
  });
  if (newTrack) {
    if (!t.path.empty()) {
      m_lib.Played(t);
      SaveLibrary();
    }
    RequestCover(fi.Get());
    StartWave(t.path);
    RefreshCounts();
    if (m_nav == glass::Nav::Recent) RefreshList();
  }
}

void Controller::RefreshState() {
  auto pl = Service<IAIMPServicePlayer>(IID_IAIMPServicePlayer);
  auto d = Service<IAIMPServiceMessageDispatcher>(IID_IAIMPServiceMessageDispatcher);
  if (!pl || !d) return;
  int st = pl->GetState();
  float vol = 1;
  pl->GetVolume(&vol);
  BOOL shuffle = FALSE, repeat = FALSE;
  d->Send(AIMP_MSG_PROPERTY_SHUFFLE, AIMP_MSG_PROPVALUE_GET, &shuffle);
  d->Send(AIMP_MSG_PROPERTY_REPEAT, AIMP_MSG_PROPVALUE_GET, &repeat);
  double pos = 0;
  pl->GetPosition(&pos);
  m_window.Post([st, vol, shuffle, repeat, pos](glass::Model& m) {
    m.state = st == AIMP_PLAYER_STATE_PLAYING ? glass::PlayState::Playing
            : st == AIMP_PLAYER_STATE_PAUSED  ? glass::PlayState::Paused
                                              : glass::PlayState::Stopped;
    m.volume = vol;
    m.shuffle = shuffle != FALSE;
    m.repeat = repeat ? glass::Repeat::All : glass::Repeat::Off;
    m.position = pos;
  });
  if (st == AIMP_PLAYER_STATE_STOPPED) m_window.Audio().Silence(200);
}

void Controller::RequestCover(IAIMPFileInfo* fi) {
  const uint64_t gen = ++m_coverGen;
  // Embedded art first: it comes with the file info, no lookup needed.
  Ref<IAIMPImageContainer> c;
  if (SUCCEEDED(fi->GetValueAsObject(AIMP_FILEINFO_PROPID_ALBUMART, IID_IAIMPImageContainer, c.PutVoid())) && c &&
      c->GetDataSize() > 0) {
    SetCover(DecodeCover(c->GetData(), c->GetDataSize()));
    return;
  }
  auto art = Service<IAIMPServiceAlbumArt>(IID_IAIMPServiceAlbumArt);
  TTaskHandle task = 0;
  if (!art || FAILED(art->Get2(fi, 0, OnCover, (void*)(uintptr_t)gen, &task))) SetCover(nullptr);
}

void WINAPI Controller::OnCover(IAIMPImage* image, IAIMPImageContainer* c, void* user) {
  Controller* self = s_self;
  if (!self || (uint64_t)(uintptr_t)user != self->m_coverGen) return;  // a newer track asked since
  std::shared_ptr<glass::Cover> cover;
  if (c && c->GetDataSize() > 0) cover = DecodeCover(c->GetData(), c->GetDataSize());
  if (!cover && image) cover = DrawCover(image);
  self->SetCover(cover);
}

void Controller::SetCover(std::shared_ptr<glass::Cover> c) {
  Log::Info("cover: %s", c && !c->Empty() ? (std::to_string(c->w) + "x" + std::to_string(c->h)).c_str() : "none");
  glass::Palette pal = glass::DefaultPalette();
  if (c && !c->Empty()) {
    c->id = NextCoverId();
    pal = glass::ExtractPalette(c->rgba.data(), c->w, c->h, c->w * 4);
  } else {
    c.reset();
  }
  std::shared_ptr<const glass::Cover> cc = c;
  m_window.Post([cc, pal](glass::Model& m) {
    m.cover = cc;
    m.palette = pal;
  });
}

void Controller::StartWave(const std::wstring& path) {
  const uint64_t gen = ++m_waveGen;
  if (m_waveTask) {
    m_waveTask->cancel = true;
    m_waveTask->Release();
    m_waveTask = nullptr;
  }
  uint64_t size = 0, mtime = 0;
  if (path.empty() || !FileStamp(path, size, mtime)) return;  // streams and missing files: no waveform
  std::wstring cache = m_dir + L"\\waves\\" + glass::WaveCacheKey(path, size, mtime);
  std::vector<glass::WaveColumn> cols;
  if (glass::LoadWave(cache, cols)) {
    m_window.Post([cols](glass::Model& m) { m.wave.columns = cols, m.wave.progress = 1; });
    return;
  }
  auto th = Service<IAIMPServiceThreads>(IID_IAIMPServiceThreads);
  if (!th) return;
  m_waveTask = new WaveTask(m_msgWnd, path, cache, gen);
  m_waveTask->AddRef();
  if (FAILED(th->ExecuteInThread(m_waveTask, &m_waveHandle))) {
    m_waveTask->Release();
    m_waveTask = nullptr;
  }
}

// ---------------------------------------------------------------------------------------------------------

Ref<IAIMPPlaylist> Controller::CurrentPlaylist() {
  Ref<IAIMPPlaylist> pl;
  if (auto mgr = Service<IAIMPServicePlaylistManager>(IID_IAIMPServicePlaylistManager)) {
    if (FAILED(mgr->GetPlayingPlaylist(pl.Put())) || !pl) mgr->GetActivePlaylist(pl.Put());
  }
  return pl;
}

void Controller::ListenTo(IAIMPPlaylist* pl) {
  if (m_listened.Get() == pl) return;
  if (m_listened) m_listened->ListenerRemove(m_listEvents);
  m_listened.Reset();
  if (pl) {
    pl->ListenerAdd(m_listEvents);
    pl->AddRef();
    *m_listened.Put() = pl;
  }
}

void Controller::RefreshList() {
  std::vector<glass::ListRow> rows;
  std::wstring title;
  switch (m_nav) {
    case glass::Nav::Playlists: {
      Ref<IAIMPPlaylist> pl = CurrentPlaylist();
      ListenTo(pl.Get());
      if (!pl) break;
      Ref<IAIMPPropertyList> props;
      int playing = -1;
      if (SUCCEEDED(pl->QueryInterface(IID_IAIMPPropertyList, props.PutVoid())) && props) {
        title = PropString(props.Get(), AIMP_PLAYLIST_PROPID_NAME);
        INT32 v = -1;
        if (SUCCEEDED(props->GetValueAsInt32(AIMP_PLAYLIST_PROPID_PLAYINGINDEX, &v))) playing = v;
      }
      const int n = pl->GetItemCount();
      rows.reserve(n);
      for (int i = 0; i < n; i++) {
        Ref<IAIMPPlaylistItem> item;
        Ref<IAIMPFileInfo> fi;
        glass::LibTrack t;
        if (SUCCEEDED(pl->GetItem(i, IID_IAIMPPlaylistItem, item.PutVoid())) && item &&
            SUCCEEDED(item->GetValueAsObject(AIMP_PLAYLISTITEM_PROPID_FILEINFO, IID_IAIMPFileInfo, fi.PutVoid())))
          t = TrackFromInfo(fi.Get());
        glass::ListRow r = RowFromTrack(t);
        r.playing = i == playing;
        rows.push_back(std::move(r));
      }
      if (title.empty()) title = L"Плейлист";
      break;
    }
    case glass::Nav::Folders:
      title = L"Папки";
      m_navFolders = m_lib.Folders();
      for (size_t i = 0; i < m_navFolders.size(); i++) {
        glass::ListRow r;
        r.title = glass::FolderName(m_navFolders[i]);
        r.artist = m_navFolders[i];
        int count = i < m_folderCounts.size() ? m_folderCounts[i] : -1;
        if (count >= 0) r.album = glass::TrackCount(count);
        r.folder = true;
        r.search = glass::NormalizeForSearch(r.title + L" " + r.artist);
        rows.push_back(std::move(r));
      }
      break;
    case glass::Nav::Recent:
    case glass::Nav::Favorites:
      title = m_nav == glass::Nav::Recent ? L"Недавнее" : L"Избранное";
      m_navTracks = m_nav == glass::Nav::Recent ? m_lib.Recent() : m_lib.Favorites();
      for (const glass::LibTrack& t : m_navTracks) {
        glass::ListRow r = RowFromTrack(t);
        r.playing = glass::SamePath(t.path, m_track.path);
        rows.push_back(std::move(r));
      }
      break;
  }
  // Upper case in any script (towupper only knows ASCII in the C locale).
  if (!title.empty()) {
    std::wstring up(title.size() + 8, L'\0');
    int n = LCMapStringEx(LOCALE_NAME_USER_DEFAULT, LCMAP_UPPERCASE | LCMAP_LINGUISTIC_CASING, title.c_str(),
                          (int)title.size(), up.data(), (int)up.size(), nullptr, nullptr, 0);
    if (n > 0) title = up.substr(0, n);
  }
  const glass::Nav nav = m_nav;
  m_window.Post([rows = std::move(rows), title, nav](glass::Model& m) mutable {
    m.rows = std::move(rows);
    m.listTitle = title;
    m.nav = nav;
  });
}

void Controller::RefreshCounts() {
  int playlists = 0;
  if (auto mgr = Service<IAIMPServicePlaylistManager>(IID_IAIMPServicePlaylistManager))
    playlists = mgr->GetLoadedPlaylistCount();
  int folders = (int)m_lib.Folders().size(), recent = (int)m_lib.Recent().size(), favs = (int)m_lib.Favorites().size();
  m_window.Post([=](glass::Model& m) {
    m.navCount[0] = playlists, m.navCount[1] = folders, m.navCount[2] = 0, m.navCount[3] = favs;
    (void)recent;
  });
}

void Controller::OpenFolder(const std::wstring& path) {
  auto mgr = Service<IAIMPServicePlaylistManager>(IID_IAIMPServicePlaylistManager);
  auto player = Service<IAIMPServicePlayer>(IID_IAIMPServicePlayer);
  if (!mgr || !player) return;
  // One AIMP playlist per folder, reused: "Glass: <folder>".
  std::wstring name = L"Glass: " + glass::FolderName(path);
  Ref<IAIMPPlaylist> pl;
  if (FAILED(mgr->GetLoadedPlaylistByName(MakeString(name).Get(), pl.Put())) || !pl)
    mgr->CreatePlaylist(MakeString(name).Get(), TRUE, pl.Put());
  if (!pl) return;
  pl->BeginUpdate();
  pl->DeleteAll();
  pl->Add(MakeString(path).Get(), AIMP_PLAYLIST_ADD_FLAGS_NOTHREADING | AIMP_PLAYLIST_ADD_FLAGS_FILEINFO, -1);
  pl->EndUpdate();
  mgr->SetActivePlaylist(pl.Get());
  player->Play3(pl.Get());
  m_nav = glass::Nav::Playlists;
  RefreshList();
  RefreshCounts();
}

void Controller::ScanFolders() {
  if (m_scan.joinable()) {
    m_scanCancel = true;
    m_scan.join();
  }
  m_scanCancel = false;
  std::vector<std::wstring> folders = m_lib.Folders();
  HWND notify = m_msgWnd;
  const volatile bool* cancel = &m_scanCancel;
  m_scan = std::thread([folders, notify, cancel] {
    auto* counts = new std::vector<int>();
    for (const std::wstring& f : folders) counts->push_back(glass::CountAudioFiles(f, cancel));
    if (*cancel || !PostMessageW(notify, WM_GLASS_SCANNED, 0, (LPARAM)counts)) delete counts;
  });
}

// ---------------------------------------------------------------------------------------------------------

void Controller::OnAction(const Action& a) {
  auto player = Service<IAIMPServicePlayer>(IID_IAIMPServicePlayer);
  auto d = Service<IAIMPServiceMessageDispatcher>(IID_IAIMPServiceMessageDispatcher);
  if (!player || !d) return;
  switch (a.type) {
    case Act::PlayPause: d->Send(AIMP_MSG_CMD_PLAYPAUSE, 0, nullptr); break;
    case Act::Next: player->GoToNext(); break;
    case Act::Prev: player->GoToPrev(); break;
    case Act::Seek: player->SetPosition(a.value); break;
    case Act::Volume: player->SetVolume((float)std::clamp(a.value, 0.0, 1.0)); break;
    case Act::Shuffle: {
      BOOL v = a.value != 0;
      d->Send(AIMP_MSG_PROPERTY_SHUFFLE, AIMP_MSG_PROPVALUE_SET, &v);
      break;
    }
    case Act::Repeat: {
      BOOL v = a.value != 0;
      d->Send(AIMP_MSG_PROPERTY_REPEAT, AIMP_MSG_PROPVALUE_SET, &v);
      break;
    }
    case Act::PlayRow:
      if (m_nav == glass::Nav::Playlists) {
        Ref<IAIMPPlaylist> pl = CurrentPlaylist();
        Ref<IAIMPPlaylistItem> item;
        if (pl && SUCCEEDED(pl->GetItem(a.index, IID_IAIMPPlaylistItem, item.PutVoid())) && item) player->Play2(item.Get());
      } else if (m_nav == glass::Nav::Folders) {
        if (a.index >= 0 && a.index < (int)m_navFolders.size()) OpenFolder(m_navFolders[a.index]);
      } else if (a.index >= 0 && a.index < (int)m_navTracks.size()) {
        player->Play4(MakeString(m_navTracks[a.index].path).Get(),
                      AIMP_SERVICE_PLAYER_FLAGS_PLAY_FROM_PLAYLIST | AIMP_SERVICE_PLAYER_FLAGS_PLAY_FROM_PLAYLIST_CAN_ADD);
      }
      break;
    case Act::Nav:
      m_nav = (glass::Nav)std::clamp(a.index, 0, 3);
      RefreshList();
      break;
    case Act::Like:
      if (!m_track.path.empty()) {
        m_lib.SetFavorite(m_track, a.value != 0);
        SaveLibrary();
        RefreshCounts();
        if (m_nav == glass::Nav::Favorites) RefreshList();
      }
      break;
    case Act::AddFolder:
      if (m_lib.AddFolder(a.text)) {
        SaveLibrary();
        m_folderCounts.clear();
        m_nav = glass::Nav::Folders;
        RefreshList();
        RefreshCounts();
        ScanFolders();
      }
      break;
    case Act::Scan:
      ScanFolders();
      break;
    case Act::AimpOption:
      if (a.index == (int)glass::QuickItem::Fading)
        if (auto cfg = Service<IAIMPServiceConfig>(IID_IAIMPServiceConfig))
          cfg->SetValueAsInt32(MakeString(L"AIMPSoundOut\\FadingEnabled").Get(), a.value != 0 ? 1 : 0);
      break;
    case Act::AimpSettings: d->Send(AIMP_MSG_CMD_OPTIONS, 0, nullptr); break;
    case Act::Classic: ShowClassic(); break;
    case Act::Quit: d->Send(AIMP_MSG_CMD_QUIT, 0, nullptr); break;
    case Act::SettingsChanged:
      m_settings = a.settings;
      SaveSettings();
      break;
  }
}

LRESULT CALLBACK Controller::MsgProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  Controller* c = s_self;
  if (msg == WM_GLASS_ACTION) {
    std::unique_ptr<Action> a(reinterpret_cast<Action*>(lp));
    if (c) c->OnAction(*a);
    return 0;
  }
  if (msg == WM_GLASS_WAVE) {
    std::unique_ptr<WaveResult> r(reinterpret_cast<WaveResult*>(lp));
    if (c && r->generation == c->m_waveGen) {
      float progress = r->progress;
      bool done = r->done;
      std::vector<glass::WaveColumn> cols = std::move(r->columns);
      c->m_window.Post([cols = std::move(cols), progress, done](glass::Model& m) mutable {
        m.wave.progress = progress;
        if (done) m.wave.columns = std::move(cols);
      });
    }
    return 0;
  }
  if (msg == WM_GLASS_SCANNED) {
    std::unique_ptr<std::vector<int>> counts(reinterpret_cast<std::vector<int>*>(lp));
    if (c) {
      c->m_folderCounts = *counts;
      if (c->m_nav == glass::Nav::Folders) c->RefreshList();
    }
    return 0;
  }
  return DefWindowProcW(h, msg, wp, lp);
}

// ---------------------------------------------------------------------------------------------------------

class Plugin : public IUnknownImpl<IAIMPPlugin> {
 public:
  PChar WINAPI InfoGet(INT32 i) override {
    switch (i) {
      case AIMP_PLUGIN_INFO_NAME: return (PChar)L"AimpGlass";
      case AIMP_PLUGIN_INFO_AUTHOR: return (PChar)L"R1ens";
      case AIMP_PLUGIN_INFO_SHORT_DESCRIPTION: return (PChar)L"Liquid glass window for AIMP";
      case AIMP_PLUGIN_INFO_FULL_DESCRIPTION:
        return (PChar)L"Replaces AIMP's window with a glass one: living background from the cover, real waveform, "
                      L"playlist search, folders, recent and favourites.";
    }
    return nullptr;
  }
  DWORD WINAPI InfoGetCategories() override { return AIMP_PLUGIN_CATEGORY_ADDONS | AIMP_PLUGIN_CATEGORY_VISUALS; }
  HRESULT WINAPI Initialize(IAIMPCore* core) override {
    Core() = core;
    core->AddRef();
    m_ctl = std::make_unique<Controller>();
    if (!m_ctl->Start()) {
      m_ctl->Stop();
      m_ctl.reset();
      return E_FAIL;
    }
    return S_OK;
  }
  HRESULT WINAPI Finalize() override {
    if (m_ctl) m_ctl->Stop();
    m_ctl.reset();
    if (Core()) Core()->Release();
    Core() = nullptr;
    return S_OK;
  }
  void WINAPI SystemNotification(INT32, IUnknown*) override {}

 private:
  std::unique_ptr<Controller> m_ctl;
};

}  // namespace

extern "C" __declspec(dllexport) HRESULT WINAPI AIMPPluginGetHeader(IAIMPPlugin** header) {
  if (!header) return E_POINTER;
  Plugin* p = new Plugin();
  p->AddRef();
  *header = p;
  return S_OK;
}
