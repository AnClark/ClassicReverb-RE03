/*
 * Classic Reverb - DPF Recreation
 * Plugin implementation
 *
 * DSP algorithm reconstructed from Ghidra decompilation of the original
 * Delphi-built "Classic Reverb" VST2 plugin by Acoustica.
 *
 * ─────────────────────────────────────────────────────────────────────────────
 * How the original algorithm works (FUN_0048490c, called once per sample):
 *
 *  STEP 1 – Noise modulation
 *    noise = RandExt() * noiseAmp
 *    inL_d = inL + noise;   inR_d = inR + noise
 *    ER buffer[erPos] = (inL_d, inR_d)
 *
 *  STEP 2 – Early reflection FIR (7 taps, cross-mixed L↔R for spread)
 *    For taps i=0..6: readPos = erPos - erDelays[i]  (ushort wrap)
 *    tmpL[i] = erBufL[readPos] * erGain[i]
 *    tmpR[i] = erBufR[readPos] * erGain[i]
 *    erPos++   (ushort; wraps at 65536 naturally)
 *    erSumL = L[0]+L[1]+R[2]+R[3]+R[4]+L[6]   (cross-mixed)
 *    erSumR = R[0]+R[1]+L[2]+L[3]+L[4]+R[6]
 *
 *  STEP 3 – 1st-order LP on early-reflection sum
 *    v     = erSumL/R  − lp1State * lp1C
 *    erLP  = lp1State  * lp1B     + v * lp1A
 *    lp1State = v
 *
 *  STEP 4 – 2nd-order presence section (on 3 specific taps L[5]+R[5]+R[6])
 *    v        = in2    − eq_x1*eqD   − eq_x2*eqE
 *    eqOut    = eq_x2  * eqC         + eq_x1*eqB + v*eqA
 *    eq_x2=eq_x1; eq_x1=v
 *
 *  STEP 5 – 3 allpass diffusion stages (on inL_d+inR_d sum)
 *    Standard Schroeder allpass: v = k*buf[p]+x; out=buf[p]-k*v; buf[p]=v
 *    Runs BEFORE comb; output (apOut) is the comb feedback input.
 *
 *  STEP 6 – 16 comb-filter delay lines (FDN core)
 *    Read all 16 lines → combOut[i] = combBuf[i][combPos[i]]
 *    combSumL = Σ combOut[i] * COMB_MIX_L[i];  × decayNorm
 *    combSumR = Σ combOut[i] * COMB_MIX_R[i];  × decayNorm
 *    fbSignal = apOut − (Σ combOut[i]) * 0.125f * decay   ← apOut, not raw input!
 *    For each line:
 *      newSample    = combBuf[i][pos] * decay + fbSignal
 *      lp           = (1 - dampC) * newSample + dampC * dampState[i]
 *      dampState[i] = lp
 *      combBuf[i][pos] = lp
 *      combPos[i] = (combPos[i]+1) % combLen[i]
 *
 *  STEP 7 – Stereo width delay
 *    When fStereo > 0.5: extra stereo delay buffer blends into result
 *    When fStereo <= 0.5: direct bypass + dry-only pre-delay mode
 *
 *  STEP 8 – Mix and output
 *    reverbL = erLP_L(×early_mix×2) + combSumL + eqOut_L
 *    outL    = (1-wet)*inL + reverbL*wet) * outputGain
 * ─────────────────────────────────────────────────────────────────────────────
 */

#include "ClassicReverbPlugin.hpp"

START_NAMESPACE_DISTRHO

// ─── helpers ─────────────────────────────────────────────────────────────────

// Fast float multiply-subtract for 1-pole allpass in comb lines:
// (mirrors _DAT_00485f68 usage – fixed coefficient 0.5)
static inline float schroederAP(float* buf, int& pos, int len, float input, float k)
{
    float v   = k * buf[pos] + input;
    float out = buf[pos] - k * v;
    buf[pos]  = v;
    pos = (pos + 1 >= len) ? 0 : pos + 1;
    return out;
}

// ─── constructor ─────────────────────────────────────────────────────────────

ClassicReverbPlugin::ClassicReverbPlugin()
    : Plugin(PARAM_COUNT, 0, 0),
      fRoomSize(20.0f),
      fStereo(1.0f),
      fDamping(0.5f),
      fHFColour(0.5f),
      fEarlyMix(0.3f),
      fWetDry(0.5f),
      fOutputVolume(0.5f),
      fPreDelay(0.0f),
      rngState(12345678u)
{
    sampleRate = getSampleRate();
    std::memset(combBuf,   0, sizeof(combBuf));
    std::memset(apBuf,     0, sizeof(apBuf));
    std::memset(erBufL,    0, sizeof(erBufL));
    std::memset(erBufR,    0, sizeof(erBufR));
    std::memset(sdBufL,    0, sizeof(sdBufL));
    std::memset(sdBufR,    0, sizeof(sdBufR));
    std::memset(dampState, 0, sizeof(dampState));
    std::memset(combPos,   0, sizeof(combPos));
    std::memset(apPos,     0, sizeof(apPos));
    erPos = 0;
    sdPos = 0;
    lp1StateL = lp1StateR = 0.0f;
    eq_xL[0] = eq_xL[1] = eq_xR[0] = eq_xR[1] = 0.0f;
    hpStateL  = hpStateR  = 0.0f;
    updateCoefficients();
}

// ─── DPF: parameter interface ─────────────────────────────────────────────────

void ClassicReverbPlugin::initParameter(uint32_t index, Parameter& p)
{
    p.hints = kParameterIsAutomatable;
    p.ranges.min = 0.0f;
    p.ranges.max = 1.0f;

    switch (index)
    {
    case PARAM_ROOM_SIZE:
        p.name    = "Room Size";
        p.symbol  = "room_size";
        p.unit    = "m2";
        p.ranges.min = 0.625f;
        p.ranges.max = 640.0f;
        p.ranges.def = 20.0f;   // geometric midpoint: sqrt(0.625 * 640) = 20
        p.hints |= kParameterIsLogarithmic;
        break;
    case PARAM_STEREO:
        p.name    = "Stereo Width";
        p.symbol  = "stereo_width";
        p.ranges.def = 1.0f;
        break;
    case PARAM_DAMPING:
        p.name    = "Damping";
        p.symbol  = "damping";
        p.ranges.def = 0.5f;
        break;
    case PARAM_HF_COLOUR:
        p.name    = "HF Colour";
        p.symbol  = "hf_colour";
        p.ranges.def = 0.5f;
        break;
    case PARAM_EARLY_MIX:
        p.name    = "Early Mix";
        p.symbol  = "early_mix";
        p.ranges.def = 0.3f;
        break;
    case PARAM_WET_DRY:
        p.name    = "Wet/Dry";
        p.symbol  = "wet_dry";
        p.ranges.def = 0.5f;
        break;
    case PARAM_OUTPUT_VOLUME:
        p.name    = "Output Volume";
        p.symbol  = "output_volume";
        p.ranges.def = 0.5f;
        p.hints |= kParameterIsLogarithmic;
        break;
    case PARAM_PREDELAY:
        p.name    = "Pre-Delay";
        p.symbol  = "pre_delay";
        p.unit    = "ms";
        p.ranges.min = -150.0f;
        p.ranges.max =  150.0f;
        p.ranges.def =   0.0f;
        break;
    }
}

float ClassicReverbPlugin::getParameterValue(uint32_t index) const
{
    switch (index)
    {
    case PARAM_ROOM_SIZE:     return fRoomSize;
    case PARAM_STEREO:        return fStereo;
    case PARAM_DAMPING:       return fDamping;
    case PARAM_HF_COLOUR:     return fHFColour;
    case PARAM_EARLY_MIX:     return fEarlyMix;
    case PARAM_WET_DRY:       return fWetDry;
    case PARAM_OUTPUT_VOLUME: return fOutputVolume;
    case PARAM_PREDELAY:      return fPreDelay;
    default:                  return 0.0f;
    }
}

void ClassicReverbPlugin::setParameterValue(uint32_t index, float value)
{
    switch (index)
    {
    case PARAM_ROOM_SIZE:     fRoomSize     = value; break;
    case PARAM_STEREO:        fStereo       = value; break;
    case PARAM_DAMPING:       fDamping      = value; break;
    case PARAM_HF_COLOUR:     fHFColour     = value; break;
    case PARAM_EARLY_MIX:     fEarlyMix     = value; break;
    case PARAM_WET_DRY:       fWetDry       = value; break;
    case PARAM_OUTPUT_VOLUME: fOutputVolume = value; break;
    case PARAM_PREDELAY:      fPreDelay     = value; break;
    }
    updateCoefficients();
}

// ─── DPF: lifecycle ───────────────────────────────────────────────────────────

void ClassicReverbPlugin::activate()
{
    clearBuffers();
    updateCoefficients();
}

void ClassicReverbPlugin::sampleRateChanged(double newSampleRate)
{
    sampleRate = newSampleRate;
    clearBuffers();
    updateCoefficients();
}

// ─── coefficient update ───────────────────────────────────────────────────────
/*
 * Mirrors FUN_004845b8 in the original binary.
 *
 * Key formulas (decompiled):
 *   decay     = DAT_004848b4 + DAT_004848a8 * (1.0 − roomSize)
 *             ≈      0.70    +      0.28    * (1.0 − roomSize)
 *             → range [0.70 (roomSize=1) … 0.98 (roomSize=0)]
 *   decayNorm = sqrt(1.0 − decay)
 *   outputGain = exp((vol_param − 0.5) × ln(2) × 12/log2(e))
 *              = 2^((vol_param − 0.5) * 12)   (±6 dB range)
 *
 *   Per-comb damping (1-pole LP in the feedback loop):
 *     dampC = fDamping * 0.94             (0 = bright, ~1 = dark)
 *     newSample = buf[p] * decay + fbSignal
 *     lp  = (1 - dampC) * newSample + dampC * dampState[i]
 *     buf[p] = lp;  dampState[i] = lp
 *   Loop gain = decay * |H_lp(ω)| ≤ decay < 1  →  unconditionally stable.
 *
 *   1st-order LP on early reflections (coefficients at 0x127BBC/BC0/BC4):
 *     k = cot(π * hfColour * freqScale / sr)
 *     lp1A =  k / (k+1)
 *     lp1B = -k / (k+1)      (original uses negative for different structure)
 *     lp1C = (1−k) / (k+1)
 *
 *   2nd-order presence / EQ section: biquad Direct-Form II with coefficients
 *     derived from the same k (HF Colour parameter and sample rate).
 *
 *   Delay-line lengths: proportional to sample rate and MAX_COMB_SIZES[].
 *     At 44100 Hz use ~65% of max capacity; scale linearly for other rates.
 */
void ClassicReverbPlugin::updateCoefficients()
{
    const double sr = sampleRate;
    const double srRatio = sr / 44100.0;

    // ── decay (0x127BAC) ──────────────────────────────────────────────────────
    // fRoomSize is in m² [0.625..640], logarithmic range spanning 10 octaves.
    // Map to internal t ∈ [0..1] (log scale: large room → t near 1):
    //   t = log2(m2 / 0.625) / 10
    //   t = 0 → 0.625 m²  → decay44 = 0.40  (short RT60, small room)
    //   t = 1 → 640 m²    → decay44 = 0.98  (long  RT60, large room)
    // Original formula (FUN_004845b8): decay = 0.4 + 0.58 * (1 - roomSize_norm)
    // With roomSize_norm = 0 at max room size (640 m², dial min) the mapping is:
    //   roomSize_norm = 1 - t  →  decay = 0.4 + 0.58 * t
    // Then normalise to current sample rate: decay_sr = decay_44k ^ (44100 / sr)
    {
        double t      = std::log2((double)fRoomSize / 0.625) / 10.0;
        t             = std::max(0.0, std::min(1.0, t));
        double decay44 = 0.4 + 0.58 * t;
        decay     = (float)std::pow(decay44, 44100.0 / sr);
        decayNorm = std::sqrt(std::max(0.0f, 1.0f - decay));
    }

    // ── output gain (0x127BE8) ──────────────────────────────────────────────
    // Formula: exp((vol − 0.5) × ln(2) × DAT_004848C0)  where DAT_004848C0=10.0
    // Range: 2^(-5) .. 2^(+5)  = -15 dB .. +15 dB
    outputGain = (float)std::exp((fOutputVolume - 0.5) * std::log(2.0) * 10.0);

    // ── noise amplitude: DAT_00485F5C=6e-8 (extracted from DLL) ──────────────
    noiseAmp = 6e-8f;

    // ── comb-filter delay lengths ─────────────────────────────────────────────
    // Delay time ∝ room linear dimension ∝ sqrt(area).
    // Reference: MAX_COMB_SIZES × 0.65 corresponds to the maximum 640 m².
    // At lower areas the lengths shrink as sqrt(m2 / 640).
    {
        double roomLenScale = std::sqrt((double)fRoomSize / 640.0) * 0.65;
        for (int i = 0; i < 16; i++)
        {
            combLen[i] = (int)(MAX_COMB_SIZES[i] * roomLenScale * srRatio);
            if (combLen[i] < 8)            combLen[i] = 8;
            if (combLen[i] > MAX_COMB_BUF) combLen[i] = MAX_COMB_BUF;
            if (combPos[i] >= combLen[i])  combPos[i] = 0;
        }
    }

    // ── allpass lengths ───────────────────────────────────────────────────────
    // Same sqrt(area) scaling as comb lines; reference at 640 m².
    {
        double apLenScale = std::sqrt((double)fRoomSize / 640.0) * 0.75;
        for (int i = 0; i < 3; i++)
        {
            apLen[i] = (int)(MAX_AP_SIZES[i] * apLenScale * srRatio);
            if (apLen[i] < 4)           apLen[i] = 4;
            if (apLen[i] > MAX_AP_BUF)  apLen[i] = MAX_AP_BUF;
            if (apPos[i] >= apLen[i])   apPos[i] = 0;
        }
    }

    // ── early reflection tap delays (scaled to sample rate) ───────────────────
    for (int i = 0; i < 7; i++)
    {
        erDelays[i] = (int)(ER_DELAYS_44100[i] * srRatio);
        if (erDelays[i] < 1) erDelays[i] = 1;
    }

    // ── stereo width pre-delay ────────────────────────────────────────────────
    // fPreDelay is in ms (-150..+150).  Negative = R channel leads L.
    // |sdLen| is the ring-buffer depth; sign is tracked via fPreDelay itself.
    sdLen = (int)(std::abs(fPreDelay) * 0.001 * sr);
    if (sdLen >= MAX_SD_SIZE) sdLen = MAX_SD_SIZE - 1;
    // Always reset sdPos to 0 when sdLen changes so the ring buffer
    // is always in a defined state (avoids stale audio on pre-delay changes).
    sdPos = 0;

    // ── per-comb damping coefficient ────────────────────────────────────────
    // 1-pole LP in the comb feedback loop:
    //   lp = (1 - dampC) * newSample + dampC * prevState
    // Stability: loop gain = decay * |H_lp(ω)| ≤ decay < 1  →  always stable.
    // dampC = 0 → transparent (bright); dampC → 1 → heavy HF rolloff (dark).
    // Normalise to sr: the LP time-constant τ = -1/(ln(dampC)*sr) must equal
    // τ at 44100 Hz, so dampC_sr = dampC_44 ^ (44100/sr).
    {
        double dampC_44 = (double)fDamping * 0.94;
        dampC = (float)std::pow(dampC_44, 44100.0 / sr);
    }

    // ── 1st-order LP coefficients (0x127BBC/BC0/BC4) ────────────────────────
    // Exact reconstruction from FUN_004845b8:
    //   ω      = 40π × 2^(hfColour × 50) / sampleRate
    //   k      = cot(ω/2) = cos(ω/2) / sin(ω/2)
    //   lp1A   = k/(k+1)          [0x127BBC]
    //   lp1B   = -k/(k+1)         [0x127BC0]
    //   lp1C   = (1-k)/(k+1)      [0x127BC4]
    // DAT_004848F4=50.0, DAT_00484900=40π=125.6637
    {
        // Map fHFColour [0..1] → 1st-order HP/AP cutoff.
        // Original formula: ω = 40π × 2^(hfc×50) / sr.
        // With hfc in [0,1] and scale=50, ω overflows at hfc≥0.02 causing
        // k → 0 → lp1A/B = 0 → output = 0 (silence).
        // Fix: scale factor reinterpreted as 10 (≈ 4 octaves, 20 Hz–20 kHz).
        // When ω still overflows Nyquist, use bypass coefficients (H(z)=1)
        // rather than zeroing the output.
        double hfc = (double)fHFColour;
        double omega_unnorm = 125.6637061435917 *  // 40*pi = angular base
                              std::exp(hfc * std::log(2.0) * 10.0);  // scale 10, not 50
        double omega = omega_unnorm / sr;
        if (omega >= M_PI) {
            // Above Nyquist → bypass: lp1A=lp1B=lp1C=1 gives H(z)=1 (pass-through)
            lp1A = 1.0f;  lp1B = 1.0f;  lp1C = 1.0f;
        } else {
            double half_omega = omega * 0.5;
            double k = std::cos(half_omega) / std::sin(half_omega);
            lp1A = (float)(k  / (k + 1.0));
            lp1B = (float)(-k / (k + 1.0));
            lp1C = (float)((1.0 - k) / (k + 1.0));
        }
    }

    // ── 2nd-order presence section (0x127BD4..BE4) ───────────────────────────
    // Same formula as lp1A/B/C but used as a 2nd-order DF-II section in the
    // original binary (identical k, different update structure).
    // Coefficients mirror lp1 but extend to 2nd-order biquad.
    {
        double hfc = (double)fHFColour;
        double omega_unnorm = 125.6637061435917 *
                              std::exp(hfc * std::log(2.0) * 10.0);  // scale 10, not 50
        double omega = omega_unnorm / sr;
        if (omega >= M_PI) omega = M_PI * 0.9999;
        double Q     = 0.7071;
        double alpha = std::sin(omega) / (2.0 * Q);
        double a0inv = 1.0 / (1.0 + alpha);
        eqA = (float)(alpha * a0inv);                       // b0
        eqB = (float)(0.0);                                // b1
        eqC = (float)(-alpha * a0inv);                     // b2
        eqD = (float)(-2.0 * std::cos(omega) * a0inv);    // -a1
        eqE = (float)((1.0 - alpha) * a0inv);             // -a2
    }

    apCoeff = ALLPASS_K;  // 0.6f  (DAT_00485F68, extracted from DLL)
}

// ─── clearBuffers ─────────────────────────────────────────────────────────────

void ClassicReverbPlugin::clearBuffers()
{
    std::memset(combBuf,   0, sizeof(combBuf));
    std::memset(apBuf,     0, sizeof(apBuf));
    std::memset(erBufL,    0, sizeof(erBufL));
    std::memset(erBufR,    0, sizeof(erBufR));
    std::memset(sdBufL,    0, sizeof(sdBufL));
    std::memset(sdBufR,    0, sizeof(sdBufR));
    std::memset(dampState, 0, sizeof(dampState));
    std::memset(combPos,   0, sizeof(combPos));
    std::memset(apPos,     0, sizeof(apPos));
    erPos = 0;
    sdPos = 0;
    lp1StateL = lp1StateR = 0.0f;
    eq_xL[0] = eq_xL[1] = eq_xR[0] = eq_xR[1] = 0.0f;
    hpStateL  = hpStateR  = 0.0f;
}

// ─── noise generator (replaces _RandExt) ─────────────────────────────────────

inline float ClassicReverbPlugin::nextNoise()
{
    // Xorshift32 – single-sample, avoids Delphi's RTL RNG dependency.
    rngState ^= rngState << 13;
    rngState ^= rngState >> 17;
    rngState ^= rngState << 5;
    // Map uint to [-0.5, +0.5) and scale
    return ((float)(int32_t)rngState * (1.0f / 4294967296.0f)) * noiseAmp;
}

// ─── process ─────────────────────────────────────────────────────────────────

void ClassicReverbPlugin::run(const float** inputs, float** outputs, uint32_t frames)
{
    const float* inL  = inputs[0];
    const float* inR  = inputs[1];
    float*       outL = outputs[0];
    float*       outR = outputs[1];

    for (uint32_t n = 0; n < frames; ++n)
    {
        const float srcL = inL[n];
        const float srcR = inR[n];

        // ── STEP 1: noise modulation (original: _RandExt + _DAT_00485f5c) ────
        const float noise  = nextNoise();
        const float inL_d  = srcL + noise;
        const float inR_d  = srcR + noise;

        // Write to early-reflection circular buffer (ushort wrap = 65536)
        erBufL[erPos] = inL_d;
        erBufR[erPos] = inR_d;

        // ── STEP 2: early reflection FIR (7 taps, cross-mixed L↔R) ───────────
        // Original stores L[i] = tap_i * gain in 0x800F4+i*8, R[i] in 0x800F8+i*8
        // L_early = L[0]+L[1]+R[2]+R[3]+R[4]+L[6]   (cross-mix for spread)
        // R_early = R[0]+R[1]+L[2]+L[3]+L[4]+R[6]
        float tapL[7], tapR[7];
        for (int i = 0; i < 7; i++)
        {
            uint16_t readPos = erPos - (uint16_t)erDelays[i]; // ushort wrap
            tapL[i] = erBufL[readPos] * ER_GAINS[i];
            tapR[i] = erBufR[readPos] * ER_GAINS[i];
        }
        erPos++;   // ushort: natural wrap at 65536

        float erSumL = tapL[0] + tapL[1] + tapR[2] + tapR[3] + tapR[4] + tapL[6];
        float erSumR = tapR[0] + tapR[1] + tapL[2] + tapL[3] + tapL[4] + tapR[6];

        // ── STEP 3: 1st-order LP on early reflections (0x127B7C/7B80 filter) ─
        // Original Direct-Form II allpass/LP (coefficients lp1A/B/C):
        //   v   = input − state × lp1C
        //   out = state × lp1B  + v × lp1A
        //   state = v
        {
            float vL = erSumL - lp1StateL * lp1C;
            erSumL   = lp1StateL * lp1B + vL * lp1A;
            lp1StateL = vL;

            float vR = erSumR - lp1StateR * lp1C;
            erSumR   = lp1StateR * lp1B + vR * lp1A;
            lp1StateR = vR;
        }

        // ── STEP 4: 2nd-order presence section ───────────────────────────────
        // Original input: (L[5] + R[5] + R[6]) for L; (L[5] + R[5] + L[6]) for R
        // (taps indices from memory-layout analysis)
        //   0x8011C = tapL[5],  0x80120 = tapR[5],
        //   0x80124 = tapL[6],  0x80128 = tapR[6]
        float eq2inL = tapL[5] + tapR[5] + tapR[6];
        float eq2inR = tapL[5] + tapR[5] + tapL[6];

        float eqOutL, eqOutR;
        {
            float vL = eq2inL - eq_xL[0]*eqD - eq_xL[1]*eqE;
            eqOutL   = eq_xL[1]*eqC + eq_xL[0]*eqB + vL*eqA;
            eq_xL[1] = eq_xL[0]; eq_xL[0] = vL;

            float vR = eq2inR - eq_xR[0]*eqD - eq_xR[1]*eqE;
            eqOutR   = eq_xR[1]*eqC + eq_xR[0]*eqB + vR*eqA;
            eq_xR[1] = eq_xR[0]; eq_xR[0] = vR;
        }

        // ── STEP 5: 3 allpass diffusion stages (on inL_d + inR_d) ───────────
        // Original order (FUN_0048490c lines 93091-93100):
        //   apIn = inL_d + inR_d  → 3 Schroeder allpass stages → apOut
        // The allpass OUTPUT is then used as the comb feedback input (line 93108).
        // Must happen BEFORE the comb read/write so allpass output is ready.
        float apIn = inL_d + inR_d;
        apIn = schroederAP(apBuf[0], apPos[0], apLen[0], apIn, apCoeff);
        apIn = schroederAP(apBuf[1], apPos[1], apLen[1], apIn, apCoeff);
        apIn = schroederAP(apBuf[2], apPos[2], apLen[2], apIn, apCoeff);
        // apIn now holds apOut — the diffused mono signal

        // ── STEP 6: 16 comb-filter delay lines (FDN core) ────────────────────
        // (a) Read current positions (oldest samples in each circular buffer)
        float combOut[16];
        float combSum = 0.0f;
        for (int i = 0; i < 16; i++)
        {
            combOut[i] = combBuf[i][combPos[i]];
            combSum   += combOut[i];
        }

        // (b) Stereo output sums (mixing matrix × decayNorm)
        float combSumL = 0.0f, combSumR = 0.0f;
        for (int i = 0; i < 16; i++)
        {
            combSumL += combOut[i] * COMB_MIX_L[i];
            combSumR += combOut[i] * COMB_MIX_R[i];
        }
        combSumL *= decayNorm;
        combSumR *= decayNorm;

        // (c) Feedback signal: apOut − 0.125 × combSum × decay
        // Original line 93108: 0x127b64 (= apOut) -= combSum * 0.125 * decay
        float fbSignal = apIn - combSum * 0.125f * decay;

        // (d) Damped write-back: 1-pole LP inside each comb loop
        for (int i = 0; i < 16; i++)
        {
            // Damped write-back: 1-pole LP inside the comb loop.
            // This is unconditionally stable: total loop gain = decay * |H_lp| ≤ decay < 1.
            float newSample = combBuf[i][combPos[i]] * decay + fbSignal;
            float lp = (1.0f - dampC) * newSample + dampC * dampState[i];
            dampState[i] = lp;
            combBuf[i][combPos[i]] = lp;

            combPos[i]++;
            if (combPos[i] >= combLen[i]) combPos[i] = 0;
        }

        // ── STEP 7 & 8: output AP, sd buffer, ER add, mix ────────────────────
        // Original structure (FUN_0048490c lines 93449-93523):
        //   a) Apply output AP/HP to combSumL/R alone   (hpState × lp1*)
        //   b) sd buffer routing:
        //        sdLen = 0        → bypass entirely (pre-delay = 0 ms)
        //        fStereo  > 0.5   → delay LATE REVERB; dry = srcL/R direct
        //        fStereo <= 0.5   → delay DRY signal;  late reverb is direct
        //      Negative fPreDelay swaps L/R in the buffer for R-leads-L effect.
        //   c) Add early reflections AFTER sd buffer so ER is always dry-locked
        //   d) wet/dry mix + output gain

        // (a) Output AP on late reverb only
        {
            float vL  = combSumL - hpStateL * lp1C;
            combSumL  = hpStateL * lp1B + vL * lp1A;
            hpStateL  = vL;

            float vR  = combSumR - hpStateR * lp1C;
            combSumR  = hpStateR * lp1B + vR * lp1A;
            hpStateR  = vR;
        }

        // (b) sd buffer
        float dryL, dryR;
        if (sdLen <= 0)
        {
            // Pre-delay = 0 ms: bypass ring buffer completely.
            dryL = srcL;
            dryR = srcR;
        }
        else if (fStereo > 0.5f)
        {
            // Wide stereo: delay LATE REVERB so L/R tails diverge; dry is direct.
            float tmpL = combSumL, tmpR = combSumR;
            if (fPreDelay >= 0.0f) {
                combSumL = sdBufL[sdPos];
                combSumR = sdBufR[sdPos];
                sdBufL[sdPos] = tmpL;
                sdBufR[sdPos] = tmpR;
            } else {
                // Negative: R channel leads → swap L/R in buffer
                combSumL = sdBufR[sdPos];
                combSumR = sdBufL[sdPos];
                sdBufL[sdPos] = tmpR;
                sdBufR[sdPos] = tmpL;
            }
            sdPos = (sdPos + 1 >= sdLen) ? 0 : sdPos + 1;
            dryL = srcL;
            dryR = srcR;
        }
        else
        {
            // Narrow / mono: delay DRY signal; late reverb is taken direct.
            float sdOutL = sdBufL[sdPos];
            float sdOutR = sdBufR[sdPos];
            if (fPreDelay >= 0.0f) {
                sdBufL[sdPos] = srcL;
                sdBufR[sdPos] = srcR;
            } else {
                sdBufL[sdPos] = srcR;
                sdBufR[sdPos] = srcL;
            }
            sdPos = (sdPos + 1 >= sdLen) ? 0 : sdPos + 1;
            dryL = sdOutL;
            dryR = sdOutR;
        }

        // (c) Add early reflections after sd buffer (ER is always dry-locked)
        const float em = fEarlyMix;
        float reverbL = (erSumL + eqOutL) * 2.0f * em + combSumL;
        float reverbR = (erSumR + eqOutR) * 2.0f * em + combSumR;

        // (d) Wet/dry mix + output gain
        const float wet = fWetDry;
        outL[n] = ((1.0f - wet) * dryL + reverbL * wet) * outputGain;
        outR[n] = ((1.0f - wet) * dryR + reverbR * wet) * outputGain;
    }
}

// ─── DPF plugin factory ───────────────────────────────────────────────────────

Plugin* createPlugin()
{
    return new ClassicReverbPlugin();
}

END_NAMESPACE_DISTRHO
