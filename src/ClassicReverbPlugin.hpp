/*
 * Classic Reverb - DPF Recreation
 * Plugin header
 *
 * DSP algorithm reconstructed from Ghidra decompilation.
 *
 * Architecture summary (from reversed FUN_0048490c / FUN_004845b8):
 *
 *  1. Input noise modulation  (tiny random signal for de-correlation)
 *  2. Early reflections        (7-tap FIR from 65536-sample stereo FIFO,
 *                               cross-mixed L/R outputs for spread)
 *  3. 1-pole LP on early reflections
 *  4. 2nd-order presence EQ on 3 of the 7 ER taps
 *  5. 16 comb-filter delay lines (shared mono feedback, stereo output
 *     via mixing matrix derived from alternating ±signs)
 *        - input = inL+inR+noise − 1/8 * sum_of_all * decay
 *        - write: buf[pos] = old * decay + input
 *        - per-line 1-pole HF damping (allpass-like: subtracts HP output)
 *  6. 3 allpass diffusion stages (Schroeder AP on the summed L+R signal)
 *  7. Stereo width delay buffer
 *  8. Wet/dry mix + output gain
 *
 * Parameter offsets in original Delphi object (for reference):
 *   +0xBC : Room Size (decay control)
 *   +0xC0 : Stereo Width  (<=0.5 → mono/blend, >0.5 → full stereo)
 *   +0xC4 : Damping       (HF roll-off per comb line)
 *   +0xC8 : HF Colour     (allpass/EQ frequency shaping)
 *   +0xCC : Early Mix     (early-reflections level)
 *   +0xD0 : Wet/Dry       (0=dry, 1=wet)
 *   +0xD4 : Output Volume (maps to gain via exp(value))
 *   +0xD8 : (double) sample rate – NOT a user parameter
 */

#pragma once

#include "DistrhoPlugin.hpp"
#include <cmath>
#include <cstring>

#include "Defines.h"

// ─── plugin class ─────────────────────────────────────────────────────────────
class ClassicReverbPlugin : public DISTRHO::Plugin
{
public:
    ClassicReverbPlugin();

    // DPF overrides
    const char* getLabel()   const override { return DISTRHO_PLUGIN_NAME; }
    const char* getMaker()   const override { return DISTRHO_PLUGIN_BRAND; }
    const char* getLicense() const override { return "ISC"; }
    uint32_t    getVersion() const override { return d_version(1, 0, 0); }
    int64_t     getUniqueId() const override { return d_cconst('C','R','v','B'); }

    void   initParameter(uint32_t index, Parameter& parameter) override;
    float  getParameterValue(uint32_t index) const override;
    void   setParameterValue(uint32_t index, float value) override;
    void   activate() override;
    void   sampleRateChanged(double newSampleRate) override;
    void   run(const float** inputs, float** outputs, uint32_t frames) override;

private:
    // ── parameters (0..1 normalised, matching original offsets) ──────────────
    float fRoomSize;      // room size in m² (0.625..640, log scale; 20 m² = default)
    float fStereo;        // +0xC0
    float fDamping;       // +0xC4
    float fHFColour;      // +0xC8
    float fEarlyMix;      // +0xCC
    float fWetDry;        // +0xD0
    float fOutputVolume;  // +0xD4
    float fPreDelay;      // pre-delay in ms (-150..+150); negative means R channel leads L

    // ── pre-computed coefficients (recalculated in updateCoefficients()) ──────
    float decay;        // 0x127BAC  feedback coefficient
    float decayNorm;    // 0x127BA8  sqrt(1-decay), output normaliser
    float noiseAmp;     // 0x127BA4  noise modulation amplitude
    float outputGain;   // 0x127BE8  linear output gain
    float dampC;              // 0x127BB0  per-line LP damping coeff (0=bright, ~1=dark)
    float apCoeff;      // _DAT_00485f68 = ALLPASS_K (fixed)
    float eqA, eqB, eqC, eqD, eqE; // 2nd-order section coefficients
    float lp1A, lp1B, lp1C;        // 1st-order filter coefficients

    // ── delay buffers ─────────────────────────────────────────────────────────
    // 16 comb filter delay lines (mono — stereo produced via mixing matrix)
    float combBuf[16][MAX_COMB_BUF];
    int   combPos[16];   // write heads
    int   combLen[16];   // actual delay lengths (samples)

    // 3 allpass diffusion stages
    float apBuf[3][MAX_AP_BUF];
    int   apPos[3];
    int   apLen[3];

    // Early-reflection stereo circular buffer (ushort-addressed, 65536 entries)
    float erBufL[ER_BUF_SIZE];
    float erBufR[ER_BUF_SIZE];
    uint16_t erPos;          // write head (wraps naturally as ushort)
    int  erDelays[7];        // tap delays (scaled to current sample rate)

    // Stereo width / pre-delay buffer
    float sdBufL[MAX_SD_SIZE];
    float sdBufR[MAX_SD_SIZE];
    int   sdPos;
    int   sdLen;             // length in samples (0 = bypass)

    // ── filter states ─────────────────────────────────────────────────────────
    float dampState[16];      // 0x127B24..0x127B60  per-comb damping state

    float lp1StateL, lp1StateR;   // 0x127B7C/7B80  1st-order LP on ER
    float eq_xL[2], eq_xR[2];     // 0x127B84-90    2nd-order section states (Z^-1, Z^-2)
    float hpStateL, hpStateR;     // 0x127B9C/A0    output HP/AP state

    // ── misc state ────────────────────────────────────────────────────────────
    uint32_t rngState;             // simple PRNG state (replaces _RandExt)
    double   sampleRate;

    // ── private helpers ───────────────────────────────────────────────────────
    void updateCoefficients();
    void clearBuffers();
    inline float nextNoise();
};

