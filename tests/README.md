# Unit tests

GPU-free tests for the pure parts of VLC Ambient Light: settings (X-macro, ini load/save, Clamp),
glow geometry (`glow_math`), the burned-in bar detector, overlay layout / capture box, and the small
helpers in `common.h`. They need no VLC, no window and no D3D device, so they run anywhere `cl` does.

| File | Covers |
|---|---|
| `test_framework.h` | Tiny single-header framework: `TEST(name)`, `CHECK`, `CHECK_EQ`, `CHECK_NEAR`, `RUN_ALL_TESTS()` |
| `test_main.cpp` | `main()`: runs everything, exit code = number of failed tests |
| `test_settings.cpp` | Save/Load round trips, Clamp for every `VA_SETTINGS` range, tolerance of CRLF / BOM / spaces / junk |
| `test_glow_math.cpp` | `ComputeGlowMetrics`, `GlowReach` monotonicity, `FadeExponent`, `VibranceStrength` |
| `test_bar_detector.cpp` | `BarDetector::Feed` on synthetic 320x180 luma grids: letterbox, pillarbox, title cards, gradients, shrink, static frames, disable |
| `test_layout.cpp` | `ComputeLayouts` (inner/outer overlays, bars, settings switches) and `CaptureBox` |
| `test_common.cpp` | `Trim`, `ToUtf8`/`FromUtf8`, `RectW`/`RectH`/`SameRect` |

Comments marked `current behaviour` pin down what the code does today where it might reasonably be
changed later. Update those checks together with the code.

`golden/` is a separate, GPU-based golden-image suite driven by `VlcAmbient.exe --render`; it is not part
of this executable.

## Build and run with plain `cl`

From a *x64 Native Tools Command Prompt for VS 2022* (or after `vcvars64.bat`) in the project root:

```bat
cl /nologo /std:c++20 /EHsc /utf-8 /W4 /DUNICODE /D_UNICODE /DNOMINMAX /Isrc /Fo:%TEMP%\ /Fe:%TEMP%\vlcambient_tests.exe tests\*.cpp src\settings.cpp src\glow_math.cpp src\bar_detector.cpp src\layout.cpp src\common.cpp user32.lib shell32.lib ole32.lib && %TEMP%\vlcambient_tests.exe
```

The same from PowerShell in the project root, with the VS environment set up inline:

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"" >nul 2>nul && cl /nologo /std:c++20 /EHsc /utf-8 /W4 /DUNICODE /D_UNICODE /DNOMINMAX /Isrc /Fo:%TEMP%\\ /Fe:%TEMP%\vlcambient_tests.exe tests\*.cpp src\settings.cpp src\glow_math.cpp src\bar_detector.cpp src\layout.cpp src\common.cpp user32.lib shell32.lib ole32.lib && %TEMP%\vlcambient_tests.exe"
```

Output is one `PASS name` line per test, `FAIL name (file:line: expr)` per failed check, and a summary.
The exit code is the number of failed tests, so it plugs straight into CI or `ctest`.

The settings tests write a temporary ini under `%TEMP%` (`vlcambient_test_*_<pid>.ini`) and delete it.

## CMake

`CMakeLists.txt` adds a `vlcambient_tests` target built from `tests/*.cpp` plus the source files listed
above, registered with `ctest`; `build.cmd test` builds and runs it.
