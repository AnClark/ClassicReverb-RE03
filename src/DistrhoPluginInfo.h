/*
 * Classic Reverb - DPF Recreation
 *
 * Reconstructed from Ghidra decompilation of "Classic Reverb" VST2 plugin
 * (originally by Acoustica, written in Delphi).
 *
 * Algorithm: 16-line FDN (Feedback Delay Network) reverb with:
 *   - 7-tap early reflections
 *   - Per-line 1-pole HF damping
 *   - 3 allpass diffusion stages
 *   - Stereo width delay
 */

#ifndef DISTRHO_PLUGIN_INFO_H_INCLUDED
#define DISTRHO_PLUGIN_INFO_H_INCLUDED

#define DISTRHO_PLUGIN_BRAND   "AnClark Liu"
#define DISTRHO_PLUGIN_NAME    "Classic Reverb RE-03"
#define DISTRHO_PLUGIN_URI     "https://github.com/AnClark/ClassicReverb-RE03"
#define DISTRHO_PLUGIN_CLAP_ID "studio.anclark.classic.reverb.re03"

#define DISTRHO_PLUGIN_BRAND_ID AcRk
#define DISTRHO_PLUGIN_UNIQUE_ID CRv3

#define DISTRHO_PLUGIN_HAS_UI          1
#define DISTRHO_UI_USE_CUSTOM           1
#define DISTRHO_UI_CUSTOM_INCLUDE_PATH  "DearImGui.hpp"
#define DISTRHO_UI_CUSTOM_WIDGET_TYPE   DGL_NAMESPACE::ImGuiTopLevelWidget
#define DISTRHO_UI_DEFAULT_WIDTH        (750 + 120 - 6)   // Base width + right panel
#define DISTRHO_UI_DEFAULT_HEIGHT       120

#define DISTRHO_PLUGIN_WANT_STATE       1

#define CLASSIC_REVERB_APPDATA_DIR_NAME "ClassicReverbRE03" // Subdirectory in user appdata folder for storing presets, etc.
#define CLASSIC_REVERB_PRESET_FILE_NAME "presets.json"      // Filename for storing user presets on disk

#define DISTRHO_PLUGIN_IS_RT_SAFE    1
#define DISTRHO_PLUGIN_NUM_INPUTS    2
#define DISTRHO_PLUGIN_NUM_OUTPUTS   2
#define DISTRHO_PLUGIN_WANT_MIDI_INPUT  0
#define DISTRHO_PLUGIN_WANT_MIDI_OUTPUT 0

#endif // DISTRHO_PLUGIN_INFO_H_INCLUDED
