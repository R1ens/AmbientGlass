cmake_minimum_required(VERSION 3.21)

# Single-config generators (Ninja, NMake): default to Release, as build.cmd does.
if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# No linker-generated manifests anywhere (the app embeds its own via src/app.rc).
# This also sidesteps CMake's `cmake -E vs_link_exe` post-link step: for an incrementally
# linked exe (every Debug link, including the compiler try-compiles inside project())
# it runs link -> mt.exe -> writes CMakeFiles/<target>.dir/manifest.rc
# ("#pragma code_page(65001)" + `1 24 "<path>/embed.manifest"`) -> rc.exe -> relink.
# An earlier configure attempt on this machine died in that rc.exe call with
# "RC2136: missing '=' in EXSTYLE"; it could not be reproduced afterwards (SDK 10.0.22621 rc.exe
# accepts that file under code pages 65001/866/1251, from BuildTools and Community, from cmd,
# PowerShell and Git Bash, and with a Cyrillic path). With /MANIFEST:NO in the link flags
# vs_link_exe never writes or compiles manifest.rc, so the step cannot fail.
# Must be set before project() to reach the try-compiles.
set(CMAKE_EXE_LINKER_FLAGS_INIT "/MANIFEST:NO")

project(VlcAmbient LANGUAGES CXX RC)

if(NOT MSVC)
  message(FATAL_ERROR "VlcAmbient is a Windows/Direct3D 11 program and needs the MSVC toolchain (run from a vcvars64 prompt or use build.cmd).")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# Static CRT: /MT in Release, /MTd in Debug (one exe, no VC runtime dependency).
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# ---------------------------------------------------------------------------
# Shaders: src/glow.hlsl -> ${CMAKE_BINARY_DIR}/gen/*.h (one header per entry point)
# ---------------------------------------------------------------------------
set(FXC_EXECUTABLE "" CACHE FILEPATH "Path to fxc.exe (HLSL compiler from the Windows SDK)")
if(NOT FXC_EXECUTABLE)
  # 1) vcvars64 exports WindowsSdkVerBinPath (…\Windows Kits\10\bin\10.0.xxxxx.0\)
  if(DEFINED ENV{WindowsSdkVerBinPath} AND EXISTS "$ENV{WindowsSdkVerBinPath}/x64/fxc.exe")
    set(FXC_EXECUTABLE "$ENV{WindowsSdkVerBinPath}/x64/fxc.exe")
  else()
    # 2) newest installed Windows 10/11 SDK
    file(GLOB _fxc_candidates "$ENV{ProgramFiles\(x86\)}/Windows Kits/10/bin/10.*/x64/fxc.exe")
    if(_fxc_candidates)
      list(SORT _fxc_candidates COMPARE NATURAL ORDER DESCENDING)
      list(GET _fxc_candidates 0 FXC_EXECUTABLE)
    endif()
  endif()
  if(NOT FXC_EXECUTABLE)
    message(FATAL_ERROR
      "fxc.exe (HLSL compiler) not found. Install a Windows 10/11 SDK, run from a vcvars64 prompt, "
      "or pass -DFXC_EXECUTABLE=<path to fxc.exe>.")
  endif()
  # NORMALIZE: WindowsSdkVerBinPath ends with a backslash, which would leave a "//" in the path.
  cmake_path(SET FXC_EXECUTABLE NORMALIZE "${FXC_EXECUTABLE}")
  set(FXC_EXECUTABLE "${FXC_EXECUTABLE}" CACHE FILEPATH "Path to fxc.exe (HLSL compiler from the Windows SDK)" FORCE)
endif()
message(STATUS "fxc: ${FXC_EXECUTABLE}")

set(SHADER_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src/glow.hlsl")
set(SHADER_GEN_DIR "${CMAKE_BINARY_DIR}/gen")
file(MAKE_DIRECTORY "${SHADER_GEN_DIR}")

set(SHADER_HEADERS)
# add_shader(<profile> <entry> <header>) -> gen/<header> with byte array g_<entry>
function(add_shader profile entry header)
  set(out "${SHADER_GEN_DIR}/${header}")
  add_custom_command(
    OUTPUT "${out}"
    COMMAND "${FXC_EXECUTABLE}" /nologo /T ${profile} /E ${entry} /O3 /Fh "${out}" /Vn g_${entry} "${SHADER_SRC}"
    MAIN_DEPENDENCY "${SHADER_SRC}"
    COMMENT "fxc ${profile} ${entry} -> gen/${header}"
    VERBATIM)
  set(SHADER_HEADERS ${SHADER_HEADERS} "${out}" PARENT_SCOPE)
endfunction()

add_shader(vs_5_0 VSMain    glow_vs.h)
add_shader(ps_5_0 PSProject glow_ps_project.h)
add_shader(ps_5_0 PSGlow    glow_ps_glow.h)
add_shader(ps_5_0 PSBlur    glow_ps_blur.h)
add_shader(ps_5_0 PSFinal   glow_ps_final.h)
add_shader(ps_5_0 PSDiff    glow_ps_diff.h)

add_custom_target(shaders DEPENDS ${SHADER_HEADERS} SOURCES "${SHADER_SRC}")

# ---------------------------------------------------------------------------
# vlcambient_core: everything except main.cpp, plus Dear ImGui
# ---------------------------------------------------------------------------
file(GLOB CORE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
list(REMOVE_ITEM CORE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")
file(GLOB CORE_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h")

set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui")
set(IMGUI_SOURCES
  "${IMGUI_DIR}/imgui.cpp"
  "${IMGUI_DIR}/imgui_draw.cpp"
  "${IMGUI_DIR}/imgui_tables.cpp"
  "${IMGUI_DIR}/imgui_widgets.cpp"
  "${IMGUI_DIR}/backends/imgui_impl_win32.cpp"
  "${IMGUI_DIR}/backends/imgui_impl_dx11.cpp")

add_library(vlcambient_core STATIC ${CORE_SOURCES} ${CORE_HEADERS} ${IMGUI_SOURCES})
add_dependencies(vlcambient_core shaders)

target_include_directories(vlcambient_core PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
  "${IMGUI_DIR}"
  "${IMGUI_DIR}/backends"
  "${SHADER_GEN_DIR}")

target_compile_definitions(vlcambient_core PUBLIC UNICODE _UNICODE NOMINMAX)
target_compile_options(vlcambient_core PUBLIC
  "$<$<COMPILE_LANGUAGE:CXX>:/utf-8;/W4;/EHsc;/permissive-;/bigobj;/MP>")

target_link_libraries(vlcambient_core PUBLIC
  d3d11 dxgi dcomp dwmapi windowsapp windowscodecs shell32 user32 ole32 advapi32)

# ---------------------------------------------------------------------------
# VlcAmbient.exe
# ---------------------------------------------------------------------------
add_executable(VlcAmbient WIN32
  "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/app.rc"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/app.manifest")
set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/src/app.manifest" PROPERTIES HEADER_FILE_ONLY ON)
target_link_libraries(VlcAmbient PRIVATE vlcambient_core)
# The manifest is embedded by app.rc (1 24 "app.manifest"); /MANIFEST:NO comes from
# CMAKE_EXE_LINKER_FLAGS_INIT above.
set_target_properties(VlcAmbient PROPERTIES
  OUTPUT_NAME VlcAmbient
  # $<1:...> suppresses the per-config subdirectory of multi-config generators,
  # so the exe is always build\VlcAmbient.exe.
  RUNTIME_OUTPUT_DIRECTORY "$<1:${CMAKE_BINARY_DIR}>")

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
enable_testing()

file(GLOB TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp")
if(TEST_SOURCES)
  add_executable(vlcambient_tests ${TEST_SOURCES})
  target_link_libraries(vlcambient_tests PRIVATE vlcambient_core)
  add_test(NAME unit COMMAND vlcambient_tests)
else()
  message(STATUS "tests/*.cpp not found - vlcambient_tests target skipped")
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/golden.ps1")
  add_test(NAME golden
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass
            -File "${CMAKE_CURRENT_SOURCE_DIR}/tools/golden.ps1" -Exe "$<TARGET_FILE:VlcAmbient>")
else()
  message(STATUS "tools/golden.ps1 not found - golden test skipped")
endif()

# Image-quality checks of the seam between the picture and the glow (tests/quality/README.md).
find_program(PYTHON_EXECUTABLE NAMES python python3)
if(PYTHON_EXECUTABLE AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/quality/check_quality.py")
  add_test(NAME quality
    COMMAND "${PYTHON_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/quality/check_quality.py"
            --exe "$<TARGET_FILE:VlcAmbient>")
else()
  message(STATUS "python or tests/quality not found - quality test skipped")
endif()
