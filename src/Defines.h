#pragma once

#include "DistrhoDetails.hpp"   // For DISTHRO::ParameterRanges

// ─── parameter indices ───────────────────────────────────────────────────────
enum Parameters {
    PARAM_ROOM_SIZE = 0,  // 0xBC  decay coefficient
    PARAM_STEREO,          // 0xC0  stereo width
    PARAM_DAMPING,         // 0xC4  HF damping per comb line
    PARAM_HF_COLOUR,       // 0xC8  EQ / allpass shape
    PARAM_EARLY_MIX,       // 0xCC  early reflections level
    PARAM_WET_DRY,         // 0xD0  wet/dry mix
    PARAM_OUTPUT_VOLUME,   // 0xD4  output gain (0–1 → dB)
    PARAM_PREDELAY,        // 0xE0  pre-delay in ms (-150..+150 ms; negative = R leads L)
    PARAM_COUNT
};

static constexpr DISTRHO::ParameterRanges kParamRanges[PARAM_COUNT] = {
    // def, min, max
    {  80.0f, 0.625f, 640.0f   },   // Room Size, m²
    {   1.0f, 0.0f,   1.0f     },   // Stereo Width
    {  0.28f, 0.0f,   1.0f     },   // Damping
    {  0.08f, 0.0f,   1.0f     },   // HF Colour
    {   0.3f, 0.0f,   1.0f     },   // Early Mix
    {   0.5f, 0.0f,   1.0f     },   // Wet/Dry Mix
    {   0.5f, 0.0f,   1.0f     },   // Output Volume (gain)
    {   0.0f, -150.0f, +150.0f }    // Pre-Delay (ms)
};

// ─── buffer size constants ────────────────────────────────────────────────────
// MAX_COMB_SIZES / MAX_AP_SIZES: target delay lengths at 44100 Hz (from DLL).
// MAX_COMB_BUF  / MAX_AP_BUF   : static buffer capacity, sized for up to ~220 kHz
//   (5× the 44100 Hz target – combBuf alone is ~3.9 MB, acceptable for a plugin).
static constexpr int MAX_COMB_SIZES[16] = {
    6400,  6720,  7104,  7296,
    6976,  7936,  8576,  8704,
    9088,  9472,  9888, 10608,
    11200, 11648, 11776, 12288
};
static constexpr int MAX_AP_SIZES[3]     = { 640, 784, 992 };
static constexpr int MAX_COMB_BUF        = 12288 * 5;   // 61440 – largest comb × 5×
static constexpr int MAX_AP_BUF          = 992   * 5;   // 4960  – largest AP   × 5×
static constexpr int MAX_SD_SIZE         = 32768;  // stereo pre-delay: 150 ms @ 192 kHz = 28800 samples
static constexpr int ER_BUF_SIZE         = 65536;  // ushort-addressed ER buffer (stereo)
// ─── early-reflection tap gains (DAT_00488D0C, extracted from DLL DATA section) ─
static constexpr float ER_GAINS[7] = { 0.65f, 0.45f, 0.41f, 0.34f, 0.31f, 0.28f, 0.24f };

// ─── ER tap delays at 44100 Hz (approximate; scaled at run-time) ─────────────
static constexpr int ER_DELAYS_44100[7] = { 220, 397, 573, 751, 974, 1249, 1607 };

// ─── Comb mixing matrix (DAT_00488D74..DF0, extracted from DLL DATA section) ─
// Interleaved L/R pairs, 16 entries.  L at even offsets, R at odd.
static constexpr float COMB_MIX_L[16] = {
     0.1985f, -0.1235f,  0.3015f, -0.1323f,
     0.1665f,  0.3416f,  0.4674f, -0.06785f,
    -0.1879f,  0.3703f,  0.02341f,-0.3926f,
     0.1508f,  0.2206f, -0.08366f,-0.2529f
};
static constexpr float COMB_MIX_R[16] = {
     0.1792f,  0.3493f,  0.2960f, -0.0251f,
    -0.08528f,-0.2383f,  0.3550f,  0.3731f,
     0.04845f,-0.02328f,-0.3536f,  0.2949f,
    -0.03428f, 0.3232f, -0.2635f, -0.1959f
};

// ─── Allpass coefficient (DAT_00485F68, extracted from DLL) ──────────────────
static constexpr float ALLPASS_K = 0.6f;
