cmake_minimum_required (VERSION 3.20)

project (ClassicReverb_RE2nd
    LANGUAGES C CXX
)

add_subdirectory (dpf)

include_directories (src)

dpf_add_plugin (${PROJECT_NAME}
    TARGETS vst2 vst3 lv2 clap
    FILES_DSP
        src/ClassicReverbPlugin.cpp
)
