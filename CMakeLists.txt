cmake_minimum_required(VERSION 3.16)
project(drumrom LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(DRUMROM_BUILD_GUI "Build desktop GUI target" ON)
option(DRUMROM_BUILD_HANDHELD "Build handheld GUI target" ON)
option(DRUMROM_BUILD_BUILD_BIN "Build ROM generator CLI target" ON)
option(DRUMROM_BUILD_SYNTH_DEMO "Build synth demo target" ON)
option(DRUMROM_BUILD_PLUGIN "Build OneROM pinbend plugin target" ON)

include(FetchContent)

# Fetch ImGui
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.90.1
)

# Fetch ImGui backends (SDLRenderer)
FetchContent_Declare(
    imgui_sdl2
    URL https://github.com/ocornut/imgui/archive/refs/tags/v1.90.1.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP OFF
)

FetchContent_MakeAvailable(imgui)

if (DRUMROM_BUILD_GUI OR DRUMROM_BUILD_HANDHELD)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL2 REQUIRED sdl2)
endif()

if (DRUMROM_BUILD_GUI)
    pkg_check_modules(SDL2_IMAGE REQUIRED SDL2_image)
    pkg_check_modules(SNDFILE REQUIRED sndfile)
    pkg_check_modules(ALSA REQUIRED alsa)
endif()

# Synthesis library: reusable drum sound synthesis
add_library(drumrom_synth STATIC
    src/synth.cpp
    src/synth_elements.cpp
    src/synth_elements_exact.cpp
    src/synth_freeverb.cpp
    third_party/mutable/elements/dsp/exciter.cc
    third_party/mutable/elements/dsp/multistage_envelope.cc
    third_party/mutable/elements/dsp/ominous_voice.cc
    third_party/mutable/elements/dsp/part.cc
    third_party/mutable/elements/dsp/resonator.cc
    third_party/mutable/elements/dsp/string.cc
    third_party/mutable/elements/dsp/tube.cc
    third_party/mutable/elements/dsp/voice.cc
    third_party/mutable/elements/resources.cc
    third_party/mutable/stmlib/dsp/units.cc
    third_party/mutable/stmlib/utils/random.cc
)
target_include_directories(drumrom_synth
    PUBLIC
        include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/mutable
)
target_compile_definitions(drumrom_synth PRIVATE TEST=1)

if (DRUMROM_BUILD_GUI)
    add_library(drumrom_app_core STATIC
        src/app_core_slot_layout.cpp
    )
    target_include_directories(drumrom_app_core PUBLIC include)
endif()

if (DRUMROM_BUILD_GUI OR DRUMROM_BUILD_HANDHELD)
    add_library(drumrom_sample_dsp STATIC
        src/sample_dsp_shared.cpp
    )
    target_include_directories(drumrom_sample_dsp PUBLIC include)
endif()

# ImGui library
if (DRUMROM_BUILD_GUI OR DRUMROM_BUILD_HANDHELD)
    add_library(imgui_lib STATIC
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_demo.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer2.cpp
    )
    target_include_directories(imgui_lib PUBLIC
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
        ${SDL2_INCLUDE_DIRS}
    )
    target_link_libraries(imgui_lib PUBLIC ${SDL2_LIBRARIES})
endif()

# Main GUI with ImGui
if (DRUMROM_BUILD_GUI)
    add_executable(drumrom_gui
        src/main_imgui.cpp
        src/sample_browser_fs.cpp
        src/preset_browser_fs.cpp
        src/settings_io.cpp
        src/onerom_usb_routing.cpp
        src/ui_bottom_toolbar.cpp
        src/ui_editor_page.cpp
        src/ui_pin_matrix_page.cpp
        src/ui_settings_page.cpp
        src/main_ui_editor_dispatch.cpp
        src/main_ui_icons.cpp
        src/main_ui_midi.cpp
        src/main_ui_orchestration.cpp
        src/main_ui_overlay_controls.cpp
        src/main_ui_overlay_waveform.cpp
        src/main_ui_runtime_bootstrap.cpp
        src/main_ui_runtime_cleanup.cpp
        src/main_ui_runtime_events.cpp
        src/main_ui_runtime_frame.cpp
        src/main_ui_runtime_input.cpp
        src/main_ui_runtime_startup.cpp
    )
endif()

if (DRUMROM_BUILD_HANDHELD)
    add_executable(drumrom_handheld
        src/main_handheld.cpp
    )
endif()

if (DRUMROM_BUILD_BUILD_BIN)
    add_executable(drumrom_build_bin
        src/build_bin.cpp
    )
endif()

if (DRUMROM_BUILD_SYNTH_DEMO)
    add_executable(synth_demo
        tools/synth_demo.cpp
    )
endif()

if (DRUMROM_BUILD_PLUGIN)
    add_library(onerom_pinbend_plugin MODULE
        src/onerom_pinbend_plugin.cpp
    )

    target_compile_features(onerom_pinbend_plugin PRIVATE cxx_std_17)
endif()

if (DRUMROM_BUILD_GUI)
    target_include_directories(drumrom_gui PRIVATE
        include
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
        ${SDL2_INCLUDE_DIRS}
        ${SDL2_IMAGE_INCLUDE_DIRS}
        ${SNDFILE_INCLUDE_DIRS}
        ${ALSA_INCLUDE_DIRS}
    )
endif()

if (DRUMROM_BUILD_BUILD_BIN)
    target_include_directories(drumrom_build_bin PRIVATE include)
endif()

if (DRUMROM_BUILD_SYNTH_DEMO)
    target_include_directories(synth_demo PRIVATE include)
endif()

if (DRUMROM_BUILD_HANDHELD)
    target_include_directories(drumrom_handheld PRIVATE
        include
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
        ${SDL2_INCLUDE_DIRS}
    )
endif()

if (DRUMROM_BUILD_GUI)
    target_link_directories(drumrom_gui PRIVATE
        ${SDL2_LIBRARY_DIRS}
        ${SDL2_IMAGE_LIBRARY_DIRS}
        ${SNDFILE_LIBRARY_DIRS}
        ${ALSA_LIBRARY_DIRS}
    )
endif()

if (DRUMROM_BUILD_GUI)
    target_link_libraries(drumrom_gui PRIVATE
        drumrom_synth
        drumrom_app_core
        drumrom_sample_dsp
        imgui_lib
        ${SDL2_LIBRARIES}
        ${SDL2_IMAGE_LIBRARIES}
        ${ALSA_LIBRARIES}
        ${SNDFILE_LIBRARIES}
    )
endif()

if (DRUMROM_BUILD_BUILD_BIN)
    target_link_libraries(drumrom_build_bin PRIVATE drumrom_synth)
endif()

if (DRUMROM_BUILD_SYNTH_DEMO)
    target_link_libraries(synth_demo PRIVATE drumrom_synth)
endif()

if (DRUMROM_BUILD_HANDHELD)
    target_link_libraries(drumrom_handheld PRIVATE
        drumrom_synth
        drumrom_sample_dsp
        imgui_lib
        ${SDL2_LIBRARIES}
    )
endif()
