# ---------------------------------------------------------------------------
# AimpGlass: a single AIMP 6 plugin that replaces AIMP's window with its own Direct3D 11 "liquid glass" one
# ---------------------------------------------------------------------------

# aimp_core: everything that does not need the AIMP SDK (state, palette, beat, waveform, search, library,
# settings, layout, rendering), so it builds and is tested without AIMP.
set(AIMP_SHADER_SRC "${CMAKE_CURRENT_SOURCE_DIR}/core/glass.hlsl")
set(AIMP_SHADERS)
add_shader(AIMP_SHADERS "${AIMP_SHADER_SRC}" ps_5_0 PSRibbons   aimp_ps_ribbons.h)
add_shader(AIMP_SHADERS "${AIMP_SHADER_SRC}" ps_5_0 PSDown      aimp_ps_down.h)
add_shader(AIMP_SHADERS "${AIMP_SHADER_SRC}" ps_5_0 PSComposite aimp_ps_composite.h)
add_shader(AIMP_SHADERS "${AIMP_SHADER_SRC}" ps_5_0 PSOverlay   aimp_ps_overlay.h)
add_custom_target(aimp_shaders DEPENDS ${AIMP_SHADERS} SOURCES "${AIMP_SHADER_SRC}")

file(GLOB AIMP_CORE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/core/*.cpp")
file(GLOB AIMP_CORE_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/core/*.h")
add_library(aimp_core STATIC ${AIMP_CORE_SOURCES} ${AIMP_CORE_HEADERS})
add_dependencies(aimp_core aimp_shaders)
target_include_directories(aimp_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/core")
target_link_libraries(aimp_core PUBLIC shared_core d2d1 dwrite dcomp dwmapi)

file(GLOB AIMP_TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/tests/aimp/*.cpp")
if(AIMP_TEST_SOURCES)
  add_executable(aimp_tests ${AIMP_TEST_SOURCES})
  target_link_libraries(aimp_tests PRIVATE aimp_core test_framework)
  add_test(NAME aimp_unit COMMAND aimp_tests)
  # A few window states rendered on WARP against tests/aimp/golden/*.png (PSNR >= 40 dB);
  # `aimp_tests --golden <dir> --update` rewrites them after a deliberate change of the look.
  add_test(NAME aimp_golden COMMAND aimp_tests --golden "${CMAKE_SOURCE_DIR}/tests/aimp/golden")
endif()

# The plugin itself needs the AIMP SDK 6.00 C++ headers (not part of this repository). By default they are
# looked for next to the repository: ../AimpSDK/6.00/Sources/Cpp; pass -DAIMP_SDK_DIR=<...Sources/Cpp> or set
# the AIMP_SDK_DIR environment variable to use another place.
if(NOT AIMP_SDK_DIR)
  if(DEFINED ENV{AIMP_SDK_DIR})
    set(AIMP_SDK_DIR "$ENV{AIMP_SDK_DIR}")
  else()
    get_filename_component(_aimp_sdk_guess "${CMAKE_SOURCE_DIR}/../AimpSDK/6.00/Sources/Cpp" ABSOLUTE)
    set(AIMP_SDK_DIR "${_aimp_sdk_guess}")
  endif()
endif()
set(AIMP_SDK_DIR "${AIMP_SDK_DIR}" CACHE PATH "AIMP SDK 6.00 C++ headers (Sources/Cpp)")

file(GLOB AIMP_PLUGIN_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/plugin/*.cpp")
if(AIMP_PLUGIN_SOURCES AND EXISTS "${AIMP_SDK_DIR}/apiPlugin.h")
  message(STATUS "AIMP SDK: ${AIMP_SDK_DIR}")
  file(GLOB AIMP_PLUGIN_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/plugin/*.h")
  add_library(aimp_glass SHARED ${AIMP_PLUGIN_SOURCES} ${AIMP_PLUGIN_HEADERS})
  # The SDK headers trip /W4 in places we do not own: include them as external, without warnings.
  target_include_directories(aimp_glass SYSTEM PRIVATE "${AIMP_SDK_DIR}")
  target_compile_options(aimp_glass PRIVATE /external:W0)
  target_link_libraries(aimp_glass PRIVATE aimp_core)
  # Laid out as the add-on package wants it: aimp_glass/x64/aimp_glass.dll, zipped into aimp_glass.aimppack.
  set(AIMP_PACKAGE_DIR "${CMAKE_BINARY_DIR}/aimp_package")
  set_target_properties(aimp_glass PROPERTIES
    OUTPUT_NAME aimp_glass
    RUNTIME_OUTPUT_DIRECTORY "$<1:${AIMP_PACKAGE_DIR}/aimp_glass/x64>")
  add_custom_command(TARGET aimp_glass POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E chdir "${AIMP_PACKAGE_DIR}"
            "${CMAKE_COMMAND}" -E tar cf "${CMAKE_BINARY_DIR}/aimp_glass.aimppack" --format=zip aimp_glass
    COMMENT "package -> aimp_glass.aimppack"
    VERBATIM)
elseif(AIMP_PLUGIN_SOURCES)
  message(STATUS "AIMP SDK not found at '${AIMP_SDK_DIR}' - aimp_glass plugin skipped (core and tests still build)")
endif()
