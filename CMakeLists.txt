cmake_minimum_required (VERSION 3.20)

project (ClassicReverb_RE03 VERSION 1.0.0)

option (CONFIG_BUILD_JACK_STANDALONE "Build JACK standalone format (for debugging)")
if (CONFIG_BUILD_JACK_STANDALONE)
    set (TARGET_JACK jack)
endif ()

add_subdirectory (dpf)

dpf_add_plugin (${PROJECT_NAME}
    TARGETS vst2 vst3 lv2 clap ${TARGET_JACK}
    FILES_DSP
        src/ClassicReverbPlugin.cpp
        src/CoreDSP.cpp
    FILES_UI
        src/UI.cpp
        src/UISubroutines.cpp
        src/PresetManager.cpp
        src/subwindows/PresetManagerWindow.cpp
        src/subwindows/FileBrowserStuff.cpp
        src/subwindows/MessageBoxStuff.cpp
        widgets/dpf-imgui/DearImGui.cpp
        widgets/imgui-knobs-mod/imgui-knobs.cpp
    NO_SHARED_RESOURCES
)

target_include_directories (${PROJECT_NAME} PUBLIC
    src
    src/subwindows
    3rdparty
    widgets/dpf-imgui
    widgets/dpf-imgui/DearImGui
    widgets/imgui-knobs-mod
    widgets/imgui-ext
    ${PROJECT_BINARY_DIR}
)

# Enable hardware cursor changes in the knob widget
target_compile_definitions (${PROJECT_NAME}-ui PRIVATE IMGUI_KNOBS_SET_CURSOR=1)

# Require C++17 (needed for std::clamp, structured bindings, etc.)
target_compile_features (${PROJECT_NAME} PUBLIC cxx_std_17)

# Generate config.h with version numbers
configure_file (src/config.h.in ${PROJECT_BINARY_DIR}/config.h @ONLY)

# On Windows, link against shell32 and ole32 for SHGetKnownFolderPath / CoTaskMemFree
# (used by PresetManager::_getUserDataDir)
if (WIN32)
    target_link_libraries (${PROJECT_NAME}-ui PRIVATE shell32 ole32)
endif()
