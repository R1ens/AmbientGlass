// VLC Ambient Light: draws an ambient glow around VLC's video.
// Captures the VLC window, computes the glow on the GPU and shows it in two click-through overlays:
//   inner - directly above VLC, over the black areas around the picture (fullscreen bars, burned-in bars);
//   outer - directly below VLC, around the whole window on the desktop (windowed mode).
// See app.cpp for the main loop and README.md for the module map.
#include <windows.h>
#include <shellapi.h>
#include <winrt/base.h>

#include <string>

#include "app.h"
#include "common.h"
#include "ipc.h"
#include "render_cli.h"

namespace {

constexpr wchar_t kMutexName[] = L"Local\\VlcAmbient.Instance";
constexpr int kMaxRestarts = 3;  // after a lost GPU device; more than this is a driver crash loop

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  winrt::init_apartment(winrt::apartment_type::multi_threaded);

  int argc = 0;
  LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  if (argc >= 2 && std::wstring(argv[1]) == L"--render") {
    int code = RunRenderCli(argc, argv);
    LocalFree(argv);
    return code;
  }

  std::string command;
  bool background = false;
  int restarts = 0;
  for (int i = 1; i < argc; i++) {
    std::wstring a = argv[i];
    if (a == L"--background") background = true;
    else if (a == L"--show") command = "show";
    else if (a == L"--hide") command = "hide";
    else if (a == L"--toggle") command = "toggle";
    else if (a == L"--quit") command = "quit";
    else if (a.rfind(L"--restart=", 0) == 0) restarts = _wtoi(a.c_str() + 10);
  }
  LocalFree(argv);

  HANDLE mutex = CreateMutexW(nullptr, TRUE, kMutexName);
  if (GetLastError() == ERROR_ALREADY_EXISTS) {
    // A plain second launch opens the panel; an autostart duplicate stays silent.
    if (!command.empty() || !background) Ipc::Send(command.empty() ? "show" : command);
    return 0;
  }
  if (command == "quit") return 0;

  static App app;
  int code = app.Run(inst, command, background);
  if (mutex) {
    ReleaseMutex(mutex);
    CloseHandle(mutex);
  }
  if (code == App::kRestart && restarts < kMaxRestarts) {
    // The GPU device was lost (driver update, GPU reset): start over with fresh resources.
    std::wstring cmdLine = L"\"" + ExePath() + L"\" --background --restart=" + std::to_wstring(restarts + 1);
    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    if (CreateProcessW(nullptr, cmdLine.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
      CloseHandle(pi.hThread);
      CloseHandle(pi.hProcess);
    }
    code = 0;
  }
  return code;
}
