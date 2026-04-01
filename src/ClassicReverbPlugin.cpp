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

// ─── constructor ─────────────────────────────────────────────────────────────

ClassicReverbPlugin::ClassicReverbPlugin()
    : Plugin(PARAM_COUNT, 0, 0),
      fRoomSize(kParamRanges[PARAM_ROOM_SIZE].def),
      fStereo(kParamRanges[PARAM_STEREO].def),
      fDamping(kParamRanges[PARAM_DAMPING].def),
      fHFColour(kParamRanges[PARAM_HF_COLOUR].def),
      fEarlyMix(kParamRanges[PARAM_EARLY_MIX].def),
      fWetDry(kParamRanges[PARAM_WET_DRY].def),
      fOutputVolume(kParamRanges[PARAM_OUTPUT_VOLUME].def),
      fPreDelay(kParamRanges[PARAM_PREDELAY].def),
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
    p.ranges = kParamRanges[index];

    switch (index)
    {
    case PARAM_ROOM_SIZE:
        p.name    = "Room Size";
        p.symbol  = "room_size";
        p.unit    = "m2";
        p.hints |= kParameterIsLogarithmic;
        break;
    case PARAM_STEREO:
        p.name    = "Stereo Width";
        p.symbol  = "stereo_width";
        break;
    case PARAM_DAMPING:
        p.name    = "Damping";
        p.symbol  = "damping";
        break;
    case PARAM_HF_COLOUR:
        p.name    = "HF Colour";
        p.symbol  = "hf_colour";
        break;
    case PARAM_EARLY_MIX:
        p.name    = "Early Mix";
        p.symbol  = "early_mix";
        break;
    case PARAM_WET_DRY:
        p.name    = "Wet/Dry";
        p.symbol  = "wet_dry";
        break;
    case PARAM_OUTPUT_VOLUME:
        p.name    = "Output Volume";
        p.symbol  = "output_volume";
        // Note: parameter is stored as linear [0,1]; the dB mapping is done
        // internally via 2^((vol-0.5)*10). Do NOT set kParameterIsLogarithmic
        // here — min=0 breaks DPF's log normalization, and the host would
        // apply a redundant log curve on top of the DSP's exponential mapping.
        break;
    case PARAM_PREDELAY:
        p.name    = "Pre-Delay";
        p.symbol  = "pre_delay";
        p.unit    = "ms";
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
// ── State ──────────────────────────────────────────────────────────────
void ClassicReverbPlugin::initState(uint32_t index, State& state)
{
    state.hints = kStateIsHostWritable;

    switch (index)
    {
    case 0:
        state.key          = "preset_name";
        state.defaultValue = "";
        state.label        = "Current Preset Name";
        break;
    case 1:
        state.key          = "preset_modified";
        state.defaultValue = "false";
        state.label        = "Preset Modified";
        break;
    case 2:
        state.key          = "preset_type";
        state.defaultValue = "Factory";
        state.label        = "Preset Type";
        break;
    default:
        break;
    }
}

void ClassicReverbPlugin::setState(const char* /*key*/, const char* /*value*/)
{
    // Preset state is managed by the UI; the DSP side does not need to act on it.
    // DPF will forward state changes to the UI via stateChanged() automatically.
}
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

// ─── DPF plugin factory ───────────────────────────────────────────────────────

START_NAMESPACE_DISTRHO

Plugin* createPlugin()
{
    return new ClassicReverbPlugin();
}

END_NAMESPACE_DISTRHO
