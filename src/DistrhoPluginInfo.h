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

#define DISTRHO_PLUGIN_HAS_UI        0
#define DISTRHO_PLUGIN_IS_RT_SAFE    1
#define DISTRHO_PLUGIN_NUM_INPUTS    2
#define DISTRHO_PLUGIN_NUM_OUTPUTS   2
#define DISTRHO_PLUGIN_WANT_MIDI_INPUT  0
#define DISTRHO_PLUGIN_WANT_MIDI_OUTPUT 0

#endif // DISTRHO_PLUGIN_INFO_H_INCLUDED
