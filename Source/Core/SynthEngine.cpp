// SynthEngine.cpp - the Drone 2 engine: layers, vector mix, modulation and effects.
#include "SynthEngine.h"

#include <algorithm>
#include <cmath>
#include <xmmintrin.h>

using namespace params;

namespace
{
constexpr float kPi = 3.14159265358979f;
constexpr float kTwoPi = 2.0f * kPi;
inline float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

// Restores the caller's FPU mode when the audio callback returns.
class ScopedFlushDenormals
{
public:
    ScopedFlushDenormals() : m_saved(_mm_getcsr()) { _mm_setcsr(m_saved | 0x8040); }
    ~ScopedFlushDenormals() { _mm_setcsr(m_saved); }
private:
    unsigned m_saved;
};
} // namespace

SynthEngine::SynthEngine()
{
    wavetables::EnsureBuilt();
    ResetToDefaults();
    for (auto& s : m_scope)
        s.store(0.0f, std::memory_order_relaxed);
    Prepare(48000.0);
}

void SynthEngine::ResetToDefaults()
{
    for (int id = 0; id < P_COUNT; ++id)
        m_params[id].store(Info(id).defaultValue, std::memory_order_relaxed);
}

void SynthEngine::SetParam(int id, float normalized)
{
    if (id >= 0 && id < P_COUNT)
        m_params[id].store(Quantize(id, normalized), std::memory_order_relaxed);
}

void SynthEngine::Prepare(double sampleRate)
{
    m_sr = sampleRate;
    for (int l = 0; l < kNumLayers; ++l)
    {
        m_layers[l].Prepare(sampleRate, static_cast<unsigned>(l * 7919u + 17u));
        m_scanPhase[l] = l * 0.27f;
        m_scanWalk[l] = m_scanWalkTarget[l] = 0.0f;
        m_morphSmooth[l] = GetParam(LayerId(l, LP_MORPH));
        m_timbreSmooth[l] = GetParam(LayerId(l, LP_TIMBRE));
        m_colorSmooth[l] = GetParam(LayerId(l, LP_COLOR));
        m_hzSmooth[l] = 0.0f;
    }
    m_filter.Prepare(sampleRate);
    m_ensemble.Prepare(sampleRate);
    m_phaser.Prepare(sampleRate);
    m_delay.Prepare(sampleRate);
    m_reverb.Prepare(sampleRate);
    m_master.Prepare(sampleRate);
    m_env = 0.0f;
    m_dryGain = 0.0f;
    m_cutoffSmooth = -1.0f;
    m_rng.state = 0x2545F491u;
    for (int i = 0; i < kNumLfos; ++i)
    {
        m_lfos[i] = Lfo{};
        m_lfos[i].phase = i * 0.21f;
        m_lfos[i].x = 0.1f + i * 0.3f;
    }
}

// ---------------------------------------------------------------------------
// Notes
// ---------------------------------------------------------------------------
void SynthEngine::NoteOn(int note, float velocity)
{
    if (note < 0 || note > 127)
        return;
    m_held[note / 32].fetch_or(1u << (note % 32));
    m_velocity.store(velocity, std::memory_order_relaxed);
    m_lastNote.store(note, std::memory_order_relaxed);
}

void SynthEngine::NoteOff(int note)
{
    if (note < 0 || note > 127)
        return;
    m_held[note / 32].fetch_and(~(1u << (note % 32)));
}

void SynthEngine::AllNotesOff()
{
    for (auto& h : m_held)
        h.store(0);
}

void SynthEngine::ResetPitch()
{
    AllNotesOff();
    m_lastNote.store(-1, std::memory_order_relaxed);
}

void SynthEngine::UpdateNoteState()
{
    bool any = false;
    int highest = -1;
    bool lastHeld = false;
    const int last = m_lastNote.load(std::memory_order_relaxed);
    for (int w = 0; w < 4; ++w)
    {
        const uint32_t bits = m_held[w].load(std::memory_order_relaxed);
        if (!bits)
            continue;
        any = true;
        for (int b = 31; b >= 0; --b)
            if (bits & (1u << b))
            {
                highest = w * 32 + b;
                break;
            }
        if (last >= 0 && last / 32 == w && (bits & (1u << (last % 32))))
            lastHeld = true;
    }
    m_gate = any;
    // Last note has priority; on release, fall back to the highest key still down.
    if (any && !lastHeld)
        m_lastNote.store(highest, std::memory_order_relaxed);
}

void SynthEngine::StartFadeIn()
{
    m_fadeTarget.store(1.0f);
}

void SynthEngine::StartFadeOut()
{
    m_fadeTarget.store(0.0f);
}

// ---------------------------------------------------------------------------
// LFOs
// ---------------------------------------------------------------------------
void SynthEngine::UpdateLfo(int index, float dt)
{
    Lfo& lfo = m_lfos[index];
    const int shape = ToStep(LFO_COUNT, GetParam(LfoId(index, LFOP_SHAPE)));
    const float hz = LfoHz(GetParam(LfoId(index, LFOP_RATE)));
    float next = lfo.phase + hz * dt;
    const bool wrapped = next >= 1.0f;
    if (wrapped)
        next -= floorf(next);
    lfo.phase = next;
    float v = 0.0f;
    switch (shape)
    {
    case LFO_SINE:     v = sinf(lfo.phase * kTwoPi); break;
    case LFO_TRI:      v = 1.0f - 4.0f * fabsf(lfo.phase - 0.5f); break;
    case LFO_SAW_UP:   v = 2.0f * lfo.phase - 1.0f; break;
    case LFO_SAW_DOWN: v = 1.0f - 2.0f * lfo.phase; break;
    case LFO_SQUARE:   v = lfo.phase < 0.5f ? 1.0f : -1.0f; break;
    case LFO_SH:
        if (wrapped) lfo.held = m_rng.Bi();
        v = lfo.held;
        break;
    case LFO_SMOOTH:
        if (wrapped)
        {
            lfo.from = lfo.to;
            lfo.to = m_rng.Bi();
        }
        v = lfo.from + (lfo.to - lfo.from) * (0.5f - 0.5f * cosf(lfo.phase * kPi));
        break;
    case LFO_LORENZ:
    {
        // RATE sets how fast the system is integrated, in small enough steps to stay stable.
        const float step = (std::min)(hz * dt * 2.0f, 0.01f);
        const float dx = 10.0f * (lfo.y - lfo.x);
        const float dy = lfo.x * (28.0f - lfo.z) - lfo.y;
        const float dz = lfo.x * lfo.y - 2.667f * lfo.z;
        lfo.x += dx * step;
        lfo.y += dy * step;
        lfo.z += dz * step;
        v = std::clamp(lfo.x / 20.0f, -1.0f, 1.0f);
        break;
    }
    default: break;
    }
    // A touch of smoothing takes the click out of square and stepped shapes.
    lfo.value += (v - lfo.value) * 0.25f;
}

// ---------------------------------------------------------------------------
// Audio
// ---------------------------------------------------------------------------
void SynthEngine::RenderBlock(float* left, float* right, int n)
{
    const float dt = n / static_cast<float>(m_sr);

    UpdateNoteState();

    // Modulators.
    for (int i = 0; i < kNumLfos; ++i)
    {
        UpdateLfo(i, dt);
        m_lfoDisplay[i].store(m_lfos[i].value, std::memory_order_relaxed);
    }
    if (m_rng.Uni() < 0.2f * dt)
        m_wanderTarget = m_rng.Bi();
    m_wander += (m_wanderTarget - m_wander) * (1.0f - expf(-0.5f * dt));

    // Envelope: one exponential segment each way.
    const bool hold = GetParam(P_HOLD) >= 0.5f;
    const bool envOpen = hold || m_gate;
    const float envTime = envOpen ? EnvSeconds(GetParam(P_ATTACK)) : EnvSeconds(GetParam(P_RELEASE));
    m_env += ((envOpen ? 1.0f : 0.0f) - m_env) * (1.0f - expf(-dt * 3.0f / envTime));
    m_envDisplay.store(m_env, std::memory_order_relaxed);

    // Orbit around the vector position.
    const int orbitMode = ToStep(ORB_COUNT, GetParam(P_ORBIT_MODE));
    const float orbitHz = OrbitHz(GetParam(P_ORBIT_RATE));
    m_orbitPhase += orbitHz * dt;
    if (m_orbitPhase >= 1.0f)
        m_orbitPhase -= 1.0f;
    switch (orbitMode)
    {
    case ORB_FIGURE8:
        m_orbitTx = sinf(m_orbitPhase * kTwoPi);
        m_orbitTy = sinf(m_orbitPhase * 2.0f * kTwoPi);
        break;
    case ORB_DRIFT:
        if (m_rng.Uni() < orbitHz * dt * 2.0f)
        {
            m_orbitTx = m_rng.Bi();
            m_orbitTy = m_rng.Bi();
        }
        break;
    default:
        m_orbitTx = cosf(m_orbitPhase * kTwoPi);
        m_orbitTy = sinf(m_orbitPhase * kTwoPi);
        break;
    }
    const float orbitK = orbitMode == ORB_DRIFT ? 1.0f - expf(-orbitHz * 4.0f * dt) : 1.0f;
    m_orbitDx += (m_orbitTx - m_orbitDx) * orbitK;
    m_orbitDy += (m_orbitTy - m_orbitDy) * orbitK;

    // Modulation matrix.
    float sources[SRC_COUNT] = {};
    for (int i = 0; i < kNumLfos; ++i)
        sources[SRC_LFO1 + i] = m_lfos[i].value;
    sources[SRC_ENV] = m_env;
    sources[SRC_ORBIT_X] = m_orbitDx;
    sources[SRC_ORBIT_Y] = m_orbitDy;
    sources[SRC_WANDER] = m_wander;
    sources[SRC_WHEEL] = m_wheel.load(std::memory_order_relaxed);
    sources[SRC_VELOCITY] = m_velocity.load(std::memory_order_relaxed);
    const int note = m_lastNote.load(std::memory_order_relaxed);
    sources[SRC_KEY] = note >= 0 ? (note - 60) / 48.0f : 0.0f;

    float mods[DST_COUNT] = {};
    for (int s = 0; s < kNumModSlots; ++s)
    {
        const int src = ToStep(SRC_COUNT, GetParam(ModId(s, MODP_SRC)));
        const int dst = ToStep(DST_COUNT, GetParam(ModId(s, MODP_DST)));
        if (src != SRC_NONE && dst != DST_NONE)
            mods[dst] += sources[src] * (GetParam(ModId(s, MODP_AMT)) - 0.5f) * 2.0f;
    }

    // Vector mix: position in the square gives each corner an equal-power gain.
    const float orbitDepth = GetParam(P_ORBIT_DEPTH) * 0.5f;
    const float vx = Clamp01(GetParam(P_VEC_X) + m_orbitDx * orbitDepth + mods[DST_VEC_X] * 0.5f);
    const float vy = Clamp01(GetParam(P_VEC_Y) + m_orbitDy * orbitDepth + mods[DST_VEC_Y] * 0.5f);
    m_vecXDisplay.store(vx, std::memory_order_relaxed);
    m_vecYDisplay.store(vy, std::memory_order_relaxed);
    const float corners[kNumLayers] = { (1 - vx) * (1 - vy), vx * (1 - vy), (1 - vx) * vy, vx * vy };

    // Pitch: glide the key offset, then apply the tuning system to key + interval.
    const float glideSeconds = GlideSeconds(GetParam(P_GLIDE));
    const float glideK = glideSeconds < 0.005f ? 1.0f : 1.0f - expf(-dt * 3.0f / glideSeconds);
    const float wantedOffset = note >= 0 ? static_cast<float>(note - 60) : 0.0f;
    m_keyOffset += (wantedOffset - m_keyOffset) * glideK;

    const int temperament = ToStep(TMP_COUNT, GetParam(P_TEMPERAMENT));
    const float rootNote = RootNote(GetParam(P_ROOT));
    const float tuneCents = TuneCents(GetParam(P_TUNE));

    std::fill(left, left + n, 0.0f);
    std::fill(right, right + n, 0.0f);

    const float paramK = 1.0f - expf(-dt / 0.03f);

    for (int l = 0; l < kNumLayers; ++l)
    {
        LayerControl c;
        c.on = GetParam(LayerId(l, LP_ON)) >= 0.5f;
        c.algo = ToStep(ALG_COUNT, GetParam(LayerId(l, LP_ALGO)));
        c.table = ToStep(wavetables::kNumTables, GetParam(LayerId(l, LP_TABLE)));
        c.voices = ToStep(8, GetParam(LayerId(l, LP_VOICES))) + 1;
        c.detuneCents = DetuneCents(GetParam(LayerId(l, LP_DETUNE)));
        c.width = GetParam(LayerId(l, LP_WIDTH));
        c.driftCents = DriftCents(GetParam(LayerId(l, LP_DRIFT)));

        const int md = DST_LAYER_BASE + l * LD_COUNT;

        // SCAN sweeps MORPH on its own: a triangle blurred with a slow random walk.
        const float scanDepth = GetParam(LayerId(l, LP_SCAN_DEPTH));
        const float scanHz = ScanHz(GetParam(LayerId(l, LP_SCAN_RATE)));
        m_scanPhase[l] += scanHz * dt;
        if (m_scanPhase[l] >= 1.0f)
        {
            m_scanPhase[l] -= 1.0f;
            m_scanWalkTarget[l] = m_rng.Bi();
        }
        m_scanWalk[l] += (m_scanWalkTarget[l] - m_scanWalk[l]) * (1.0f - expf(-scanHz * 2.0f * dt));
        const float tri = 1.0f - 4.0f * fabsf(m_scanPhase[l] - 0.5f);
        const float scan = (0.75f * tri + 0.25f * m_scanWalk[l]) * scanDepth * 0.5f;

        const float wantMorph = Clamp01(GetParam(LayerId(l, LP_MORPH)) + scan + mods[md + LD_MORPH] * 0.5f);
        const float wantTimbre = Clamp01(GetParam(LayerId(l, LP_TIMBRE)) + mods[md + LD_TIMBRE] * 0.5f);
        const float wantColor = Clamp01(GetParam(LayerId(l, LP_COLOR)) + mods[md + LD_COLOR] * 0.5f);
        m_morphSmooth[l] += (wantMorph - m_morphSmooth[l]) * paramK;
        m_timbreSmooth[l] += (wantTimbre - m_timbreSmooth[l]) * paramK;
        m_colorSmooth[l] += (wantColor - m_colorSmooth[l]) * paramK;
        c.morph = m_morphSmooth[l];
        c.timbre = m_timbreSmooth[l];
        c.color = m_colorSmooth[l];
        m_morphDisplay[l].store(c.morph, std::memory_order_relaxed);

        const int octave = ToStep(9, GetParam(LayerId(l, LP_OCT))) - 4;
        const int semi = ToStep(49, GetParam(LayerId(l, LP_SEMI))) - 24;
        const float fine = (GetParam(LayerId(l, LP_FINE)) - 0.5f) * 100.0f;
        const float cents = IntervalCents(temperament, m_keyOffset + static_cast<float>(semi)) + octave * 1200.0f
                          + fine + tuneCents + mods[md + LD_PITCH] * 1200.0f;
        const float wantHz = 440.0f * powf(2.0f, ((rootNote - 69.0f) * 100.0f + cents) / 1200.0f);
        m_hzSmooth[l] = m_hzSmooth[l] <= 0.0f ? wantHz : m_hzSmooth[l] + (wantHz - m_hzSmooth[l]) * (paramK * 2.0f);
        c.hz = std::clamp(m_hzSmooth[l], 1.0f, static_cast<float>(m_sr) * 0.45f);

        const float level = GetParam(LayerId(l, LP_LEVEL)) * (1.0f + mods[md + LD_LEVEL]);
        const float gain = (std::max)(level, 0.0f) * sqrtf(corners[l]) * 1.41421356f;
        const float pan = GetParam(LayerId(l, LP_PAN)) * 0.5f * kPi;
        c.gainL = gain * cosf(pan) * 1.41421356f;
        c.gainR = gain * sinf(pan) * 1.41421356f;

        m_layers[l].Render(c, left, right, n);
    }

    // The envelope runs on the dry sum, so the effects tails ring on after release.
    const float dryGain = m_env;
    const float dryStep = (dryGain - m_dryGain) / n;
    for (int i = 0; i < n; ++i)
    {
        m_dryGain += dryStep;
        left[i] *= m_dryGain;
        right[i] *= m_dryGain;
    }

    // Filter.
    const int mode = ToStep(FLT_COUNT, GetParam(P_FLT_MODE));
    const float keyTrack = GetParam(P_FLT_KEY);
    float cutoffN = GetParam(P_FLT_CUTOFF) + mods[DST_CUTOFF] * 0.5f;
    if (m_cutoffSmooth < 0.0f)
        m_cutoffSmooth = cutoffN;
    m_cutoffSmooth += (cutoffN - m_cutoffSmooth) * paramK;
    const float hz = CutoffHz(Clamp01(m_cutoffSmooth)) * powf(2.0f, keyTrack * m_keyOffset / 12.0f);
    const float res = Clamp01(GetParam(P_FLT_RES) + mods[DST_RES] * 0.5f);
    const float drive = DriveGain(Clamp01(GetParam(P_FLT_DRIVE) + mods[DST_DRIVE] * 0.5f));
    m_filter.Set(mode, hz, res, drive);
    m_filter.Process(left, right, n);

    m_ensemble.Process(left, right, n, EnsembleHz(GetParam(P_ENS_RATE)), GetParam(P_ENS_DEPTH),
                       Clamp01(GetParam(P_ENS_MIX) + mods[DST_ENS_MIX] * 0.5f));
    m_phaser.Process(left, right, n, PhaserHz(GetParam(P_PHS_RATE)), GetParam(P_PHS_DEPTH), GetParam(P_PHS_FB),
                     Clamp01(GetParam(P_PHS_MIX) + mods[DST_PHS_MIX] * 0.5f));
    m_delay.Process(left, right, n, DelaySeconds(Clamp01(GetParam(P_DLY_TIME) + mods[DST_DLY_TIME] * 0.25f)), GetParam(P_DLY_FB),
                    GetParam(P_DLY_TONE), GetParam(P_DLY_PING), GetParam(P_DLY_MOD), Clamp01(GetParam(P_DLY_MIX) + mods[DST_DLY_MIX] * 0.5f));
    m_reverb.Process(left, right, n, ReverbSize(GetParam(P_REV_SIZE)), ReverbDecaySeconds(GetParam(P_REV_DECAY)), GetParam(P_REV_DAMP),
                     GetParam(P_REV_DIFF), GetParam(P_REV_MOD), Clamp01(GetParam(P_REV_SHIMMER) + mods[DST_SHIMMER] * 0.5f) * 0.7f,
                     Clamp01(GetParam(P_REV_MIX) + mods[DST_REV_MIX] * 0.5f), GetParam(P_REV_FREEZE) >= 0.5f);

    const float lowCutN = GetParam(P_MST_LOWCUT);
    m_master.Process(left, right, n, lowCutN <= 0.0f ? 10.0f : LowCutHz(lowCutN), (GetParam(P_MST_TILT) - 0.5f) * 12.0f,
                     Clamp01(GetParam(P_MST_WIDTH) + mods[DST_WIDTH] * 0.5f) * 2.0f, VolumeGain(GetParam(P_MST_VOLUME)));
    m_peakLDisplay.store(m_master.PeakL(), std::memory_order_relaxed);
    m_peakRDisplay.store(m_master.PeakR(), std::memory_order_relaxed);
}

void SynthEngine::Process(float* interleavedStereo, int frames)
{
    ScopedFlushDenormals noDenormals;
    float left[kControlBlock], right[kControlBlock];
    const float fadeTarget = m_fadeTarget.load();
    const float fadeStep = 1.0f / (0.04f * static_cast<float>(m_sr));
    int write = m_scopeWrite.load(std::memory_order_relaxed);

    for (int done = 0; done < frames;)
    {
        const int n = (std::min)(frames - done, kControlBlock);
        RenderBlock(left, right, n);
        float* out = interleavedStereo + static_cast<size_t>(done) * 2;
        for (int i = 0; i < n; ++i)
        {
            if (m_fade < fadeTarget)
                m_fade = (std::min)(m_fade + fadeStep, fadeTarget);
            else if (m_fade > fadeTarget)
                m_fade = (std::max)(m_fade - fadeStep, fadeTarget);
            const float l = left[i] * m_fade, r = right[i] * m_fade;
            out[2 * i] = l;
            out[2 * i + 1] = r;
            m_scope[write].store(0.5f * (l + r), std::memory_order_relaxed);
            write = (write + 1) & (kScopeSize - 1);
        }
        done += n;
    }
    m_scopeWrite.store(write, std::memory_order_relaxed);
}

void SynthEngine::CopyScope(float* mono, int count) const
{
    const int write = m_scopeWrite.load(std::memory_order_relaxed);
    for (int i = 0; i < count; ++i)
        mono[i] = m_scope[(write - count + i) & (kScopeSize - 1)].load(std::memory_order_relaxed);
}
