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
# Shaders: *.hlsl -> ${CMAKE_BINARY_DIR}/gen/*.h (one header per entry point)
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

set(SHADER_GEN_DIR "${CMAKE_BINARY_DIR}/gen")
file(MAKE_DIRECTORY "${SHADER_GEN_DIR}")
set(SHARED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shared")
# Every shader may #include the shared .hlsli files (fullscreen.hlsli: VSOut), so all of them rebuild on a change.
file(GLOB SHARED_HLSLI CONFIGURE_DEPENDS "${SHARED_DIR}/*.hlsli")

# add_shader(<list var> <source.hlsl> <profile> <entry> <header>) -> gen/<header> with byte array g_<entry>,
# appended to <list var> in the caller's scope (the DEPENDS of that app's shader target).
function(add_shader list source profile entry header)
  set(out "${SHADER_GEN_DIR}/${header}")
  add_custom_command(
    OUTPUT "${out}"
    COMMAND "${FXC_EXECUTABLE}" /nologo /T ${profile} /E ${entry} /O3 /I "${SHARED_DIR}"
            /Fh "${out}" /Vn g_${entry} "${source}"
    MAIN_DEPENDENCY "${source}"
    DEPENDS ${SHARED_HLSLI}
    COMMENT "fxc ${profile} ${entry} -> gen/${header}"
    VERBATIM)
  set(${list} ${${list}} "${out}" PARENT_SCOPE)
endfunction()

# ---------------------------------------------------------------------------
# shared_core: code any app in this repository can link (logging, ini settings, D3D11 passes and blur,
# image loading), with the compile options every target here uses
# ---------------------------------------------------------------------------
set(SHARED_SHADERS)
add_shader(SHARED_SHADERS "${SHARED_DIR}/blur.hlsl" vs_5_0 VSMain shared_vs.h)
add_shader(SHARED_SHADERS "${SHARED_DIR}/blur.hlsl" ps_5_0 PSBlur shared_ps_blur.h)
add_custom_target(shared_shaders DEPENDS ${SHARED_SHADERS} SOURCES "${SHARED_DIR}/blur.hlsl" ${SHARED_HLSLI})

file(GLOB SHARED_SOURCES CONFIGURE_DEPENDS "${SHARED_DIR}/*.cpp")
file(GLOB SHARED_HEADERS CONFIGURE_DEPENDS "${SHARED_DIR}/*.h")
add_library(shared_core STATIC ${SHARED_SOURCES} ${SHARED_HEADERS})
add_dependencies(shared_core shared_shaders)
target_include_directories(shared_core PUBLIC "${SHARED_DIR}" "${SHADER_GEN_DIR}")
target_compile_definitions(shared_core PUBLIC UNICODE _UNICODE NOMINMAX)
target_compile_options(shared_core PUBLIC
  "$<$<COMPILE_LANGUAGE:CXX>:/utf-8;/W4;/EHsc;/permissive-;/bigobj;/MP>")
target_link_libraries(shared_core PUBLIC d3d11 dxgi windowscodecs shell32 user32 ole32)

# ---------------------------------------------------------------------------
# VlcAmbient shaders: src/glow.hlsl
# ---------------------------------------------------------------------------
set(SHADER_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src/glow.hlsl")
set(GLOW_SHADERS)
add_shader(GLOW_SHADERS "${SHADER_SRC}" ps_5_0 PSProject glow_ps_project.h)
add_shader(GLOW_SHADERS "${SHADER_SRC}" ps_5_0 PSGlow    glow_ps_glow.h)
add_shader(GLOW_SHADERS "${SHADER_SRC}" ps_5_0 PSFinal   glow_ps_final.h)
add_shader(GLOW_SHADERS "${SHADER_SRC}" ps_5_0 PSDiff    glow_ps_diff.h)

add_custom_target(shaders DEPENDS ${GLOW_SHADERS} SOURCES "${SHADER_SRC}")

# ---------------------------------------------------------------------------
# vlcambient_core: everything in src/ except main.cpp, plus Dear ImGui; on top of shared_core
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
  "${IMGUI_DIR}/backends")

# Definitions and compile options come from shared_core.
target_link_libraries(vlcambient_core PUBLIC
  shared_core dcomp dwmapi windowsapp advapi32)

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

# The single-header test framework (tests/test_framework.h), for any test executable in this repository.
add_library(test_framework INTERFACE)
target_include_directories(test_framework INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/tests")

file(GLOB TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp")
if(TEST_SOURCES)
  add_executable(vlcambient_tests ${TEST_SOURCES})
  target_link_libraries(vlcambient_tests PRIVATE vlcambient_core test_framework)
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

# ---------------------------------------------------------------------------
# AimpGlass (aimp/): the second program in this repository
# ---------------------------------------------------------------------------
add_subdirectory(aimp)
