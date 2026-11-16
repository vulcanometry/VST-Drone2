// Params.h - the parameter list: IDs, names, ranges and value text.
//
// Every parameter is stored normalized (0..1). IDs double as the VST3 ParamIDs,
// so the order here must never change once projects exist: add new ones at the end.
#pragma once

#include <string>

constexpr int kNumLayers = 4;
constexpr int kNumLfos = 4;
constexpr int kNumModSlots = 8;

// Per-layer parameters, repeated for layers A..D.
enum LayerParam
{
    LP_ON, LP_ALGO, LP_TABLE, LP_MORPH, LP_SCAN_DEPTH, LP_SCAN_RATE, LP_TIMBRE, LP_COLOR,
    LP_OCT, LP_SEMI, LP_FINE, LP_VOICES, LP_DETUNE, LP_WIDTH, LP_DRIFT, LP_LEVEL, LP_PAN,
    LP_COUNT
};

enum LfoParam { LFOP_SHAPE, LFOP_RATE, LFOP_COUNT };
enum ModParam { MODP_SRC, MODP_DST, MODP_AMT, MODP_COUNT };

enum ParamId
{
    P_ROOT, P_TUNE, P_TEMPERAMENT, P_GLIDE, P_ATTACK, P_RELEASE, P_HOLD,
    P_VEC_X, P_VEC_Y, P_ORBIT_MODE, P_ORBIT_RATE, P_ORBIT_DEPTH,
    P_LAYER_BASE,
    P_FLT_MODE = P_LAYER_BASE + kNumLayers * LP_COUNT,
    P_FLT_CUTOFF, P_FLT_RES, P_FLT_DRIVE, P_FLT_KEY,
    P_LFO_BASE,
    P_MOD_BASE = P_LFO_BASE + kNumLfos * LFOP_COUNT,
    P_ENS_RATE = P_MOD_BASE + kNumModSlots * MODP_COUNT,
    P_ENS_DEPTH, P_ENS_MIX,
    P_PHS_RATE, P_PHS_DEPTH, P_PHS_FB, P_PHS_MIX,
    P_DLY_TIME, P_DLY_FB, P_DLY_TONE, P_DLY_PING, P_DLY_MOD, P_DLY_MIX,
    P_REV_SIZE, P_REV_DECAY, P_REV_DAMP, P_REV_DIFF, P_REV_MOD, P_REV_SHIMMER, P_REV_MIX, P_REV_FREEZE,
    P_MST_LOWCUT, P_MST_TILT, P_MST_WIDTH, P_MST_VOLUME,
    P_COUNT
};

constexpr int LayerId(int layer, LayerParam p) { return P_LAYER_BASE + layer * LP_COUNT + p; }
constexpr int LfoId(int lfo, LfoParam p) { return P_LFO_BASE + lfo * LFOP_COUNT + p; }
constexpr int ModId(int slot, ModParam p) { return P_MOD_BASE + slot * MODP_COUNT + p; }

enum Algorithm
{
    ALG_MORPH, ALG_WAVETABLE, ALG_SWARM, ALG_ADDITIVE, ALG_FM, ALG_FOLD,
    ALG_FORMANT, ALG_PAD, ALG_STRINGS, ALG_RESONATOR, ALG_CLOUD, ALG_CHAOS,
    ALG_COUNT
};

enum FilterMode { FLT_OFF, FLT_LP12, FLT_LP24, FLT_BP, FLT_HP, FLT_NOTCH, FLT_LADDER, FLT_COUNT };
enum Temperament { TMP_EQUAL, TMP_JUST, TMP_PYTHAGOREAN, TMP_SEPTIMAL, TMP_MEANTONE, TMP_COUNT };
enum LfoShape { LFO_SINE, LFO_TRI, LFO_SAW_UP, LFO_SAW_DOWN, LFO_SQUARE, LFO_SH, LFO_SMOOTH, LFO_LORENZ, LFO_COUNT };
enum OrbitMode { ORB_CIRCLE, ORB_FIGURE8, ORB_DRIFT, ORB_COUNT };

enum ModSource
{
    SRC_NONE, SRC_LFO1, SRC_LFO2, SRC_LFO3, SRC_LFO4, SRC_ENV, SRC_ORBIT_X, SRC_ORBIT_Y,
    SRC_WANDER, SRC_WHEEL, SRC_VELOCITY, SRC_KEY, SRC_COUNT
};

// Destinations: layer destinations come first, in LayerDest order per layer.
enum LayerDest { LD_MORPH, LD_TIMBRE, LD_COLOR, LD_LEVEL, LD_PITCH, LD_COUNT };
enum ModDest
{
    DST_NONE,
    DST_LAYER_BASE,
    DST_VEC_X = DST_LAYER_BASE + kNumLayers * LD_COUNT,
    DST_VEC_Y, DST_CUTOFF, DST_RES, DST_DRIVE, DST_ENS_MIX, DST_PHS_MIX,
    DST_DLY_TIME, DST_DLY_MIX, DST_REV_MIX, DST_SHIMMER, DST_WIDTH, DST_COUNT
};

struct ParamInfo
{
    const char* key;         // stable key used in patch files
    const wchar_t* name;     // full name, shown by the host
    const wchar_t* label;    // short caption on the panel
    float defaultValue;      // normalized
    int steps;               // 0 = continuous, otherwise the number of positions
    bool bipolar;            // the knob's arc starts at 12 o'clock
};

namespace params
{
const ParamInfo& Info(int id);
std::wstring Format(int id, float normalized);   // value text, as the panel and the host show it
float Quantize(int id, float normalized);        // snaps stepped parameters to their positions

inline int ToStep(int steps, float n) { return static_cast<int>(n * (steps - 1) + 0.5f); }
inline float FromStep(int steps, int step) { return static_cast<float>(step) / static_cast<float>(steps - 1); }

// Value mappings used by both the engine and Format().
float RootNote(float n);          // MIDI note
float TuneCents(float n);
float GlideSeconds(float n);
float EnvSeconds(float n);
float OrbitHz(float n);
float ScanHz(float n);
float LfoHz(float n);
float DetuneCents(float n);
float DriftCents(float n);
float CutoffHz(float n);
float ResValue(float n);
float DriveGain(float n);
float EnsembleHz(float n);
float PhaserHz(float n);
float DelaySeconds(float n);
float ReverbDecaySeconds(float n);
float ReverbSize(float n);
float LowCutHz(float n);
float VolumeGain(float n);

const wchar_t* AlgorithmName(int algo);
const wchar_t* AlgorithmBlurb(int algo);
// Panel captions for the three algorithm-dependent knobs (MORPH, TIMBRE, COLOR).
void AlgorithmLabels(int algo, const wchar_t*& morph, const wchar_t*& timbre, const wchar_t*& color);
bool AlgorithmUsesTable(int algo);
int  AlgorithmMaxVoices(int algo);   // VOICES beyond this behaves as this many, to bound the CPU cost

const wchar_t* FilterModeName(int mode);
const wchar_t* TemperamentName(int t);
const wchar_t* LfoShapeName(int s);
const wchar_t* OrbitModeName(int m);
const wchar_t* ModSourceName(int s);
std::wstring ModDestName(int d);
std::wstring NoteName(int note);

// Cents above the root for an interval in (possibly fractional) semitones.
float IntervalCents(int temperament, float semitones);
}
