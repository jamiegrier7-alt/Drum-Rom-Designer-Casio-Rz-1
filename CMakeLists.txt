cmake_minimum_required(VERSION 3.16)
project(drumrom LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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

# Fetch RtMidi
FetchContent_Declare(
    rtmidi
    GIT_REPOSITORY https://github.com/thestk/rtmidi.git
    GIT_TAG master
)

FetchContent_MakeAvailable(imgui rtmidi)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED sdl2)
pkg_check_modules(SDL2_IMAGE REQUIRED SDL2_image)
pkg_check_modules(SNDFILE REQUIRED sndfile)

# Synthesis library: reusable drum sound synthesis
add_library(drumrom_synth STATIC
    src/synth.cpp
    src/synth_elements.cpp
    src/synth_freeverb.cpp
    src/synth_elements_exact.cpp
    third_party/mutable/elements/resources.cc
    third_party/mutable/elements/dsp/part.cc
    third_party/mutable/elements/dsp/voice.cc
    third_party/mutable/elements/dsp/ominous_voice.cc
    third_party/mutable/elements/dsp/exciter.cc
    third_party/mutable/elements/dsp/resonator.cc
    third_party/mutable/elements/dsp/string.cc
    third_party/mutable/elements/dsp/tube.cc
    third_party/mutable/elements/dsp/multistage_envelope.cc
    third_party/mutable/stmlib/dsp/units.cc
    third_party/mutable/stmlib/utils/random.cc
)
target_include_directories(drumrom_synth PUBLIC include third_party/mutable)

set_source_files_properties(
    third_party/mutable/elements/resources.cc
    third_party/mutable/elements/dsp/part.cc
    third_party/mutable/elements/dsp/voice.cc
    third_party/mutable/elements/dsp/ominous_voice.cc
    third_party/mutable/elements/dsp/exciter.cc
    third_party/mutable/elements/dsp/resonator.cc
    third_party/mutable/elements/dsp/string.cc
    third_party/mutable/elements/dsp/tube.cc
    third_party/mutable/elements/dsp/multistage_envelope.cc
    third_party/mutable/stmlib/dsp/units.cc
    third_party/mutable/stmlib/utils/random.cc
    PROPERTIES COMPILE_DEFINITIONS TEST
)

# ImGui library
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

# RtMidi library
add_library(rtmidi_lib STATIC
    ${rtmidi_SOURCE_DIR}/RtMidi.cpp
)
target_include_directories(rtmidi_lib PUBLIC ${rtmidi_SOURCE_DIR})
if(UNIX AND NOT APPLE)
    # Linux: use ALSA
    pkg_check_modules(ALSA REQUIRED alsa)
    target_include_directories(rtmidi_lib PUBLIC ${ALSA_INCLUDE_DIRS})
    target_link_libraries(rtmidi_lib PUBLIC ${ALSA_LIBRARIES})
    target_compile_definitions(rtmidi_lib PUBLIC __LINUX_ALSA__)
elseif(APPLE)
    # macOS: use CoreMIDI
    target_link_libraries(rtmidi_lib PUBLIC "-framework CoreMIDI" "-framework CoreFoundation")
    target_compile_definitions(rtmidi_lib PUBLIC __MACOSX_CORE__)
elseif(WIN32)
    # Windows: use WinMM
    target_link_libraries(rtmidi_lib PUBLIC winmm)
    target_compile_definitions(rtmidi_lib PUBLIC __WINDOWS_MM__)
endif()

# Main GUI with ImGui
add_executable(drumrom_gui
    src/main_imgui.cpp
    src/settings_io.cpp
    src/sample_browser_fs.cpp
    src/preset_browser_fs.cpp
    src/ui_bottom_toolbar.cpp
    src/ui_editor_page.cpp
    src/ui_pin_matrix_page.cpp
    src/ui_settings_page.cpp
    src/onerom_usb_routing.cpp
    src/main_ui_orchestration.cpp
    src/main_ui_editor_dispatch.cpp
    src/main_ui_midi.cpp
    src/main_ui_overlay_controls.cpp
    src/main_ui_overlay_waveform.cpp
    src/main_ui_icons.cpp
    src/main_ui_runtime_input.cpp
    src/main_ui_runtime_cleanup.cpp
    src/main_ui_runtime_startup.cpp
    src/main_ui_runtime_frame.cpp
    src/main_ui_runtime_events.cpp
    src/main_ui_runtime_bootstrap.cpp
)

add_executable(drumrom_build_bin
    src/build_bin.cpp
)

add_executable(synth_demo
    tools/synth_demo.cpp
)

set_target_properties(drumrom_gui drumrom_build_bin synth_demo PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/build"
)


target_include_directories(drumrom_gui PRIVATE
    include
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${SDL2_INCLUDE_DIRS}
    ${SDL2_IMAGE_INCLUDE_DIRS}
    ${SNDFILE_INCLUDE_DIRS}
    ${rtmidi_SOURCE_DIR}
)
target_include_directories(drumrom_build_bin PRIVATE include)
target_include_directories(synth_demo PRIVATE include)

target_link_directories(drumrom_gui PRIVATE
    ${SDL2_LIBRARY_DIRS}
    ${SDL2_IMAGE_LIBRARY_DIRS}
    ${SNDFILE_LIBRARY_DIRS}
)

target_link_libraries(drumrom_gui PRIVATE
    drumrom_synth
    imgui_lib
    rtmidi_lib
    ${SDL2_LIBRARIES}
    ${SDL2_IMAGE_LIBRARIES}
    ${SNDFILE_LIBRARIES}
)
target_link_libraries(drumrom_build_bin PRIVATE drumrom_synth)
target_link_libraries(synth_demo PRIVATE drumrom_synth)
