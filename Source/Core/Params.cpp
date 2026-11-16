// Params.cpp - the parameter list: IDs, names, ranges and value text.
#include "Params.h"
#include "Wavetables.h"

#include <cmath>
#include <cstdio>
#include <list>
#include <vector>

namespace params
{
namespace
{
inline float LogMap(float n, float lo, float hi) { return lo * powf(hi / lo, n); }
inline float CurveMap(float n, float hi, float curve) { return hi * powf(n, curve); }

std::wstring Fmt(const wchar_t* format, double value)
{
    wchar_t buffer[64];
    swprintf_s(buffer, format, value);
    return buffer;
}

std::wstring Seconds(float s)
{
    if (s < 1.0f) return Fmt(L"%.0f ms", s * 1000.0);
    if (s < 100.0f) return Fmt(L"%.1f s", s);
    return Fmt(L"%.0f s", s);
}

std::wstring Hz(float hz)
{
    if (hz < 0.1f) return Fmt(L"%.0f s", 1.0 / hz);   // very slow rates read better as a period
    if (hz < 10.0f) return Fmt(L"%.2f Hz", hz);
    if (hz < 1000.0f) return Fmt(L"%.0f Hz", hz);
    return Fmt(L"%.2f kHz", hz / 1000.0);
}

std::wstring Percent(float n) { return Fmt(L"%.0f%%", n * 100.0); }
std::wstring Bipolar(float n) { return Fmt(L"%+.0f%%", (n - 0.5) * 200.0); }

const wchar_t* const kLayerNames[kNumLayers] = { L"A", L"B", L"C", L"D" };

// Names built at start-up stay here for the life of the program.
std::list<std::wstring>& NameStore()
{
    static std::list<std::wstring> store;
    return store;
}
std::list<std::string>& KeyStore()
{
    static std::list<std::string> store;
    return store;
}
const wchar_t* Keep(std::wstring s) { NameStore().push_back(std::move(s)); return NameStore().back().c_str(); }
const char* KeepKey(std::string s) { KeyStore().push_back(std::move(s)); return KeyStore().back().c_str(); }

std::vector<ParamInfo> BuildTable()
{
    std::vector<ParamInfo> t(P_COUNT);
    auto set = [&t](int id, const char* key, const wchar_t* name, const wchar_t* label, float def, int steps = 0, bool bipolar = false)
    {
        t[id] = { key, name, label, def, steps, bipolar };
    };

    set(P_ROOT, "root", L"Root Note", L"ROOT", FromStep(85, 36 - 12), 85);   // C0..C7, default C2
    set(P_TUNE, "tune", L"Tune", L"TUNE", 0.5f, 0, true);
    set(P_TEMPERAMENT, "temperament", L"Temperament", L"TUNING", FromStep(TMP_COUNT, TMP_JUST), TMP_COUNT);
    set(P_GLIDE, "glide", L"Glide", L"GLIDE", 0.35f);
    set(P_ATTACK, "attack", L"Attack", L"ATTACK", 0.5f);
    set(P_RELEASE, "release", L"Release", L"RELEASE", 0.55f);
    set(P_HOLD, "hold", L"Hold", L"HOLD", 1.0f, 2);

    set(P_VEC_X, "vec.x", L"Vector X", L"X", 0.5f);
    set(P_VEC_Y, "vec.y", L"Vector Y", L"Y", 0.5f);
    set(P_ORBIT_MODE, "orbit.mode", L"Orbit Path", L"PATH", 0.0f, ORB_COUNT);
    set(P_ORBIT_RATE, "orbit.rate", L"Orbit Rate", L"RATE", 0.35f);
    set(P_ORBIT_DEPTH, "orbit.depth", L"Orbit Depth", L"ORBIT", 0.25f);

    for (int l = 0; l < kNumLayers; ++l)
    {
        const std::wstring ln = std::wstring(L"Layer ") + kLayerNames[l] + L" ";
        const std::string lk = std::string("abcd").substr(l, 1) + ".";
        auto layer = [&](LayerParam p, const char* key, const wchar_t* name, const wchar_t* label, float def, int steps = 0, bool bipolar = false)
        {
            set(LayerId(l, p), KeepKey(lk + key), Keep(ln + name), label, def, steps, bipolar);
        };
        layer(LP_ON, "on", L"On", L"ON", l < 2 ? 1.0f : 0.0f, 2);
        layer(LP_ALGO, "algo", L"Algorithm", L"ALGORITHM", FromStep(ALG_COUNT, l == 0 ? ALG_MORPH : (l == 1 ? ALG_PAD : (l == 2 ? ALG_WAVETABLE : ALG_RESONATOR))), ALG_COUNT);
        layer(LP_TABLE, "table", L"Wavetable", L"TABLE", 0.0f, wavetables::kNumTables);
        layer(LP_MORPH, "morph", L"Morph", L"MORPH", 0.5f);
        layer(LP_SCAN_DEPTH, "scan.depth", L"Scan Depth", L"SCAN", 0.25f);
        layer(LP_SCAN_RATE, "scan.rate", L"Scan Rate", L"SCAN RT", 0.4f);
        layer(LP_TIMBRE, "timbre", L"Timbre", L"TIMBRE", 0.3f);
        layer(LP_COLOR, "color", L"Color", L"COLOR", 0.3f);
        layer(LP_OCT, "oct", L"Octave", L"OCT", 0.5f, 9, true);
        layer(LP_SEMI, "semi", L"Interval", L"SEMI", 0.5f, 49, true);
        layer(LP_FINE, "fine", L"Fine", L"FINE", 0.5f, 0, true);
        layer(LP_VOICES, "voices", L"Voices", L"VOICES", FromStep(8, 3), 8);
        layer(LP_DETUNE, "detune", L"Detune", L"DETUNE", 0.25f);
        layer(LP_WIDTH, "width", L"Stereo Width", L"WIDTH", 0.7f);
        layer(LP_DRIFT, "drift", L"Drift", L"DRIFT", 0.3f);
        layer(LP_LEVEL, "level", L"Level", L"LEVEL", 0.75f);
        layer(LP_PAN, "pan", L"Pan", L"PAN", 0.5f, 0, true);
    }

    set(P_FLT_MODE, "flt.mode", L"Filter Mode", L"MODE", FromStep(FLT_COUNT, FLT_LP24), FLT_COUNT);
    set(P_FLT_CUTOFF, "flt.cutoff", L"Filter Cutoff", L"CUTOFF", 0.72f);
    set(P_FLT_RES, "flt.res", L"Filter Resonance", L"RESO", 0.15f);
    set(P_FLT_DRIVE, "flt.drive", L"Filter Drive", L"DRIVE", 0.1f);
    set(P_FLT_KEY, "flt.key", L"Filter Key Track", L"KEY", 0.0f);

    static const float lfoRates[kNumLfos] = { 0.35f, 0.25f, 0.45f, 0.15f };
    static const int lfoShapes[kNumLfos] = { LFO_SINE, LFO_SMOOTH, LFO_TRI, LFO_LORENZ };
    for (int i = 0; i < kNumLfos; ++i)
    {
        const std::wstring n = L"LFO " + std::to_wstring(i + 1);
        const std::string k = "lfo" + std::to_string(i + 1) + ".";
        set(LfoId(i, LFOP_SHAPE), KeepKey(k + "shape"), Keep(n + L" Shape"), L"SHAPE", FromStep(LFO_COUNT, lfoShapes[i]), LFO_COUNT);
        set(LfoId(i, LFOP_RATE), KeepKey(k + "rate"), Keep(n + L" Rate"), L"RATE", lfoRates[i]);
    }
    for (int i = 0; i < kNumModSlots; ++i)
    {
        const std::wstring n = L"Mod " + std::to_wstring(i + 1);
        const std::string k = "mod" + std::to_string(i + 1) + ".";
        set(ModId(i, MODP_SRC), KeepKey(k + "src"), Keep(n + L" Source"), L"SOURCE", 0.0f, SRC_COUNT);
        set(ModId(i, MODP_DST), KeepKey(k + "dst"), Keep(n + L" Destination"), L"DEST", 0.0f, DST_COUNT);
        set(ModId(i, MODP_AMT), KeepKey(k + "amt"), Keep(n + L" Amount"), L"AMT", 0.5f, 0, true);
    }

    set(P_ENS_RATE, "ens.rate", L"Ensemble Rate", L"RATE", 0.35f);
    set(P_ENS_DEPTH, "ens.depth", L"Ensemble Depth", L"DEPTH", 0.5f);
    set(P_ENS_MIX, "ens.mix", L"Ensemble Mix", L"MIX", 0.3f);

    set(P_PHS_RATE, "phs.rate", L"Phaser Rate", L"RATE", 0.3f);
    set(P_PHS_DEPTH, "phs.depth", L"Phaser Depth", L"DEPTH", 0.6f);
    set(P_PHS_FB, "phs.fb", L"Phaser Feedback", L"FDBK", 0.4f);
    set(P_PHS_MIX, "phs.mix", L"Phaser Mix", L"MIX", 0.0f);

    set(P_DLY_TIME, "dly.time", L"Delay Time", L"TIME", 0.7f);
    set(P_DLY_FB, "dly.fb", L"Delay Feedback", L"FDBK", 0.5f);
    set(P_DLY_TONE, "dly.tone", L"Delay Tone", L"TONE", 0.5f);
    set(P_DLY_PING, "dly.ping", L"Delay Ping-Pong", L"PING", 0.6f);
    set(P_DLY_MOD, "dly.mod", L"Delay Modulation", L"WOW", 0.2f);
    set(P_DLY_MIX, "dly.mix", L"Delay Mix", L"MIX", 0.2f);

    set(P_REV_SIZE, "rev.size", L"Reverb Size", L"SIZE", 0.7f);
    set(P_REV_DECAY, "rev.decay", L"Reverb Decay", L"DECAY", 0.7f);
    set(P_REV_DAMP, "rev.damp", L"Reverb Damping", L"DAMP", 0.4f);
    set(P_REV_DIFF, "rev.diff", L"Reverb Diffusion", L"DIFFUSE", 0.75f);
    set(P_REV_MOD, "rev.mod", L"Reverb Modulation", L"MOD", 0.3f);
    set(P_REV_SHIMMER, "rev.shimmer", L"Reverb Shimmer", L"SHIMMER", 0.0f);
    set(P_REV_MIX, "rev.mix", L"Reverb Mix", L"MIX", 0.4f);
    set(P_REV_FREEZE, "rev.freeze", L"Reverb Freeze", L"FREEZE", 0.0f, 2);

    set(P_MST_LOWCUT, "mst.lowcut", L"Low Cut", L"LOW CUT", 0.0f);
    set(P_MST_TILT, "mst.tilt", L"Tilt EQ", L"TILT", 0.5f, 0, true);
    set(P_MST_WIDTH, "mst.width", L"Stereo Width", L"WIDTH", 0.5f);
    set(P_MST_VOLUME, "mst.volume", L"Volume", L"VOLUME", 0.75f);
    return t;
}

const std::vector<ParamInfo>& Table()
{
    static const std::vector<ParamInfo> table = BuildTable();
    return table;
}
} // namespace

const ParamInfo& Info(int id) { return Table()[id]; }

float Quantize(int id, float n)
{
    n = n < 0.0f ? 0.0f : (n > 1.0f ? 1.0f : n);
    const int steps = Info(id).steps;
    return steps > 1 ? FromStep(steps, ToStep(steps, n)) : n;
}

float RootNote(float n) { return 12.0f + n * 84.0f; }
float TuneCents(float n) { return (n - 0.5f) * 200.0f; }
float GlideSeconds(float n) { return CurveMap(n, 30.0f, 3.0f); }
float EnvSeconds(float n) { return 0.005f + CurveMap(n, 60.0f, 3.0f); }
float OrbitHz(float n) { return LogMap(n, 0.002f, 2.0f); }
float ScanHz(float n) { return LogMap(n, 0.002f, 4.0f); }
float LfoHz(float n) { return LogMap(n, 0.002f, 20.0f); }
float DetuneCents(float n) { return CurveMap(n, 60.0f, 2.0f); }
float DriftCents(float n) { return CurveMap(n, 40.0f, 2.0f); }
float CutoffHz(float n) { return LogMap(n, 20.0f, 20000.0f); }
float ResValue(float n) { return n; }
float DriveGain(float n) { return LogMap(n, 1.0f, 16.0f); }
float EnsembleHz(float n) { return LogMap(n, 0.05f, 6.0f); }
float PhaserHz(float n) { return LogMap(n, 0.005f, 5.0f); }
float DelaySeconds(float n) { return LogMap(n, 0.02f, 4.0f); }
float ReverbDecaySeconds(float n) { return LogMap(n, 0.5f, 120.0f); }
float ReverbSize(float n) { return LogMap(n, 0.3f, 2.5f); }
float LowCutHz(float n) { return LogMap(n, 5.0f, 400.0f); }
float VolumeGain(float n) { return n <= 0.0f ? 0.0f : powf(10.0f, (n - 0.75f) * 80.0f / 20.0f); }

std::wstring NoteName(int note)
{
    static const wchar_t* const names[12] = { L"C", L"C#", L"D", L"D#", L"E", L"F", L"F#", L"G", L"G#", L"A", L"A#", L"B" };
    return names[note % 12] + std::to_wstring(note / 12 - 1);
}

const wchar_t* AlgorithmName(int a)
{
    static const wchar_t* const names[ALG_COUNT] = {
        L"Wave Crossfade", L"Wavetable", L"Swarm", L"Additive", L"FM", L"Wavefold",
        L"Formant Choir", L"PAD Spectrum", L"Bowed String", L"Resonator Bank", L"Grain Cloud", L"Chaos Pair" };
    return names[a];
}

const wchar_t* AlgorithmBlurb(int a)
{
    static const wchar_t* const names[ALG_COUNT] = {
        L"Crossfades between two classic waveforms",
        L"Scans a band-limited wavetable",
        L"Detuned stack with independent drift",
        L"Slowly shifting harmonic partials",
        L"Two-operator FM with feedback",
        L"West-coast sine wavefolding",
        L"Vowel formants over a saw source",
        L"PADsynth smeared-harmonic spectra",
        L"Noise-bowed Karplus-Strong strings",
        L"Noise through tuned resonators",
        L"Granular cloud of wavetable grains",
        L"Cross-modulated oscillator pair" };
    return names[a];
}

void AlgorithmLabels(int a, const wchar_t*& m, const wchar_t*& t, const wchar_t*& c)
{
    switch (a)
    {
    case ALG_MORPH:     m = L"X-FADE";  t = L"WAVE A";  c = L"WAVE B";  break;
    case ALG_WAVETABLE: m = L"POSITION"; t = L"WARP";   c = L"SUB";     break;
    case ALG_SWARM:     m = L"SHAPE";   t = L"BLOOM";   c = L"OCTAVES"; break;
    case ALG_ADDITIVE:  m = L"BRIGHT";  t = L"ODD/EVEN"; c = L"STRETCH"; break;
    case ALG_FM:        m = L"INDEX";   t = L"RATIO";   c = L"FEEDBACK"; break;
    case ALG_FOLD:      m = L"FOLD";    t = L"SYMMETRY"; c = L"BODY";   break;
    case ALG_FORMANT:   m = L"VOWEL";   t = L"SHIFT";   c = L"BREATH";  break;
    case ALG_PAD:       m = L"SMEAR";   t = L"OCTAVE";  c = L"TONE";    break;
    case ALG_STRINGS:   m = L"BOW";     t = L"DAMPING"; c = L"PRESSURE"; break;
    case ALG_RESONATOR: m = L"FOCUS";   t = L"SPACING"; c = L"NOISE";   break;
    case ALG_CLOUD:     m = L"DENSITY"; t = L"SCATTER"; c = L"LENGTH";  break;
    default:            m = L"CROSS";   t = L"RATIO";   c = L"DRIVE";   break;
    }
}

bool AlgorithmUsesTable(int a) { return a == ALG_WAVETABLE || a == ALG_CLOUD; }

int AlgorithmMaxVoices(int a)
{
    switch (a)
    {
    case ALG_ADDITIVE: return 3;
    case ALG_FORMANT:  return 4;
    case ALG_STRINGS:  return 4;
    case ALG_RESONATOR: return 2;
    case ALG_CHAOS:    return 3;
    case ALG_CLOUD:    return 1;   // the grains are the voices
    default:           return 8;
    }
}

const wchar_t* FilterModeName(int m)
{
    static const wchar_t* const names[FLT_COUNT] = { L"Off", L"Low 12", L"Low 24", L"Band", L"High", L"Notch", L"Ladder" };
    return names[m];
}

const wchar_t* TemperamentName(int t)
{
    static const wchar_t* const names[TMP_COUNT] = { L"Equal", L"Just", L"Pythagorean", L"Septimal", L"Meantone" };
    return names[t];
}

const wchar_t* LfoShapeName(int s)
{
    static const wchar_t* const names[LFO_COUNT] = { L"Sine", L"Triangle", L"Saw Up", L"Saw Down", L"Square", L"Stepped", L"Wander", L"Lorenz" };
    return names[s];
}

const wchar_t* OrbitModeName(int m)
{
    static const wchar_t* const names[ORB_COUNT] = { L"Circle", L"Figure 8", L"Drift" };
    return names[m];
}

const wchar_t* ModSourceName(int s)
{
    static const wchar_t* const names[SRC_COUNT] = { L"None", L"LFO 1", L"LFO 2", L"LFO 3", L"LFO 4", L"Envelope",
        L"Orbit X", L"Orbit Y", L"Wander", L"Mod Wheel", L"Velocity", L"Key" };
    return names[s];
}

std::wstring ModDestName(int d)
{
    if (d == DST_NONE)
        return L"None";
    if (d < DST_VEC_X)
    {
        static const wchar_t* const names[LD_COUNT] = { L"Morph", L"Timbre", L"Color", L"Level", L"Pitch" };
        const int i = d - DST_LAYER_BASE;
        return std::wstring(kLayerNames[i / LD_COUNT]) + L" " + names[i % LD_COUNT];
    }
    static const wchar_t* const names[] = { L"Vector X", L"Vector Y", L"Cutoff", L"Resonance", L"Drive", L"Ensemble Mix", L"Phaser Mix",
        L"Delay Time", L"Delay Mix", L"Reverb Mix", L"Shimmer", L"Width" };
    return names[d - DST_VEC_X];
}

float IntervalCents(int temperament, float semitones)
{
    // Cents above the root for each scale degree; octaves stay pure in every system.
    static const float tables[TMP_COUNT][12] = {
        { 0, 100, 200, 300, 400, 500, 600, 700, 800, 900, 1000, 1100 },
        { 0, 111.73f, 203.91f, 315.64f, 386.31f, 498.04f, 590.22f, 701.96f, 813.69f, 884.36f, 1017.60f, 1088.27f },
        { 0, 90.22f, 203.91f, 294.13f, 407.82f, 498.04f, 611.73f, 701.96f, 792.18f, 905.87f, 996.09f, 1109.78f },
        { 0, 119.44f, 203.91f, 266.87f, 386.31f, 498.04f, 582.51f, 701.96f, 813.69f, 884.36f, 968.83f, 1088.27f },
        { 0, 76.05f, 193.16f, 310.26f, 386.31f, 503.42f, 579.47f, 696.58f, 772.63f, 889.74f, 1006.84f, 1082.89f },
    };
    const float* table = tables[temperament];
    const float floorSemi = floorf(semitones);
    const float frac = semitones - floorSemi;
    auto at = [table](int semi)
    {
        int octave = semi >= 0 ? semi / 12 : -((11 - semi) / 12);
        return octave * 1200.0f + table[semi - octave * 12];
    };
    const int s = static_cast<int>(floorSemi);
    return frac <= 0.0f ? at(s) : at(s) + (at(s + 1) - at(s)) * frac;
}

std::wstring Format(int id, float n)
{
    if (id >= P_LAYER_BASE && id < P_FLT_MODE)
    {
        const LayerParam p = static_cast<LayerParam>((id - P_LAYER_BASE) % LP_COUNT);
        switch (p)
        {
        case LP_ON:        return n >= 0.5f ? L"On" : L"Off";
        case LP_ALGO:      return AlgorithmName(ToStep(ALG_COUNT, n));
        case LP_TABLE:     return wavetables::TableName(ToStep(wavetables::kNumTables, n));
        case LP_SCAN_DEPTH: return Percent(n);
        case LP_SCAN_RATE: return Hz(ScanHz(n));
        case LP_OCT:       return Fmt(L"%+.0f", ToStep(9, n) - 4.0);
        case LP_SEMI:      return Fmt(L"%+.0f st", ToStep(49, n) - 24.0);
        case LP_FINE:      return Fmt(L"%+.1f ct", (n - 0.5) * 100.0);
        case LP_VOICES:    return std::to_wstring(ToStep(8, n) + 1);
        case LP_DETUNE:    return Fmt(L"%.1f ct", DetuneCents(n));
        case LP_DRIFT:     return Fmt(L"%.1f ct", DriftCents(n));
        case LP_PAN:       return Bipolar(n);
        default:           return Percent(n);
        }
    }
    if (id >= P_LFO_BASE && id < P_MOD_BASE)
    {
        const int p = (id - P_LFO_BASE) % LFOP_COUNT;
        return p == LFOP_SHAPE ? std::wstring(LfoShapeName(ToStep(LFO_COUNT, n))) : Hz(LfoHz(n));
    }
    if (id >= P_MOD_BASE && id < P_ENS_RATE)
    {
        const int p = (id - P_MOD_BASE) % MODP_COUNT;
        if (p == MODP_SRC) return ModSourceName(ToStep(SRC_COUNT, n));
        if (p == MODP_DST) return ModDestName(ToStep(DST_COUNT, n));
        return Bipolar(n);
    }
    switch (id)
    {
    case P_ROOT:        return NoteName(static_cast<int>(RootNote(n) + 0.5f));
    case P_TUNE:        return Fmt(L"%+.0f ct", TuneCents(n));
    case P_TEMPERAMENT: return TemperamentName(ToStep(TMP_COUNT, n));
    case P_GLIDE:       return Seconds(GlideSeconds(n));
    case P_ATTACK:
    case P_RELEASE:     return Seconds(EnvSeconds(n));
    case P_HOLD:
    case P_REV_FREEZE:  return n >= 0.5f ? L"On" : L"Off";
    case P_ORBIT_MODE:  return OrbitModeName(ToStep(ORB_COUNT, n));
    case P_ORBIT_RATE:  return Hz(OrbitHz(n));
    case P_FLT_MODE:    return FilterModeName(ToStep(FLT_COUNT, n));
    case P_FLT_CUTOFF:  return Hz(CutoffHz(n));
    case P_FLT_DRIVE:   return Fmt(L"%.1f dB", 20.0 * log10(DriveGain(n)));
    case P_ENS_RATE:    return Hz(EnsembleHz(n));
    case P_PHS_RATE:    return Hz(PhaserHz(n));
    case P_DLY_TIME:    return Seconds(DelaySeconds(n));
    case P_REV_SIZE:    return Fmt(L"%.2fx", ReverbSize(n));
    case P_REV_DECAY:   return Seconds(ReverbDecaySeconds(n));
    case P_MST_LOWCUT:  return n <= 0.0f ? L"Off" : Hz(LowCutHz(n));
    case P_MST_TILT:    return Fmt(L"%+.1f dB", (n - 0.5) * 12.0);
    case P_MST_WIDTH:   return Fmt(L"%.0f%%", n * 200.0);
    case P_MST_VOLUME:
        if (n <= 0.0f) return L"-inf dB";
        return Fmt(L"%+.1f dB", (n - 0.75) * 80.0);
    default:            return Percent(n);
    }
}
} // namespace params
