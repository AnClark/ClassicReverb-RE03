cmake_minimum_required (VERSION 3.20)

project (ClassicReverb_RE03 VERSION 1.0.0)

add_subdirectory (dpf)

include_directories (src)

dpf_add_plugin (${PROJECT_NAME}
    TARGETS vst2 vst3 lv2 clap
    FILES_DSP
        src/ClassicReverbPlugin.cpp
        src/CoreDSP.cpp
)
