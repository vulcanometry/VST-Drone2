// Layer.cpp - the twelve drone algorithms.
#include "Layer.h"

#include <algorithm>
#include <cmath>

using namespace wavetables;

namespace
{
constexpr float kPi = 3.14159265358979f;
constexpr float kTwoPi = 2.0f * kPi;

inline double Wrap(double p) { return p >= 1.0 ? p - 1.0 : p; }
inline float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
inline float SoftClip(float x)
{
    if (x > 3.0f) return 1.0f;
    if (x < -3.0f) return -1.0f;
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}
inline float CentsRatio(float c) { return powf(2.0f, c / 1200.0f); }
inline uint32_t PhaseInc(float hz, float sr) { return static_cast<uint32_t>(hz / sr * 4294967040.0f); }
} // namespace

void Svf::Set(float hz, float q, float sampleRate)
{
    const float g = tanf(kPi * (std::min)(hz, sampleRate * 0.45f) / sampleRate);
    k = 1.0f / (std::max)(q, 0.05f);
    a1 = 1.0f / (1.0f + g * (g + k));
    a2 = g * a1;
    a3 = g * a2;
}

void Layer::Prepare(double sampleRate, unsigned seed)
{
    wavetables::EnsureBuilt();
    m_sr = sampleRate;
    m_srf = static_cast<float>(sampleRate);
    m_rng.state = 0x9E3779B9u * (seed + 1u) | 1u;
    if (m_stringLines.empty())
        m_stringLines.assign(static_cast<size_t>(4) * (kStringLen + 1), 0.0f);
    for (Voice& v : m_voices)
    {
        v = Voice{};
        v.phase = m_rng.Uni();
        v.phase2 = m_rng.Uni();
        v.phase3 = m_rng.Uni();
        v.wanderHz = 0.05f + 0.3f * m_rng.Uni();
    }
    m_algo = -1;
    m_switchGain = 0.0f;
    m_gainL = m_gainR = 0.0f;
    ResetAlgorithm();
}

void Layer::ResetAlgorithm()
{
    for (Voice& v : m_voices)
    {
        v.z1 = v.z2 = v.fb = 0.0f;
        v.chaos = m_rng.Next();
    }
    for (auto& bank : m_formants)
        for (Svf& f : bank)
            f.Reset();
    for (auto& bank : m_res)
        for (Svf& f : bank)
            f.Reset();
    std::fill(m_stringLines.begin(), m_stringLines.end(), 0.0f);
    for (int& w : m_stringWrite) w = 0;
    for (float& z : m_exciteLp) z = 0.0f;
    for (Grain& g : m_grains) g.active = false;
    m_grainClock = 0.0f;
    m_toneL = m_toneR = 0.0f;
    m_noiseLp[0] = m_noiseLp[1] = 0.0f;
    m_rms = 0.0f;
    m_autoGain = 1.0f;
    for (auto& partials : m_partialPhase)
        for (uint32_t& p : partials)
            p = m_rng.Next();
    for (int i = 0; i < kPartials; ++i)
        m_partialWander[i] = m_partialTarget[i] = m_rng.Uni();
    m_lastMorph = m_lastTimbre = m_lastColor = -1.0f;
}

// Pitch spread, slow random drift and stereo position of the voice stack.
void Layer::UpdateVoices(const LayerControl& c)
{
    const int count = (std::min)((std::min)(c.voices, kMaxVoices), params::AlgorithmMaxVoices(c.algo));
    m_activeVoices = count;
    const float dt = kControlBlock / m_srf;
    for (int i = 0; i < count; ++i)
    {
        Voice& v = m_voices[i];
        const float p = count > 1 ? (2.0f * i / (count - 1) - 1.0f) : 0.0f;

        // Each voice walks toward a new random target at its own slow rate.
        if (m_rng.Uni() < v.wanderHz * dt)
        {
            v.wanderTarget = m_rng.Bi();
            v.bloomTarget = m_rng.Uni();
        }
        const float k = 1.0f - expf(-v.wanderHz * 2.0f * dt);
        v.wander += (v.wanderTarget - v.wander) * k;
        v.bloom += (v.bloomTarget - v.bloom) * k;

        v.hz = c.hz * CentsRatio(c.detuneCents * p + c.driftCents * v.wander);

        // Alternate sides so neighbouring pitches do not bunch up on one side.
        const float side = (i & 1 ? -1.0f : 1.0f) * fabsf(p) * c.width;
        const float angle = (side + 1.0f) * 0.25f * kPi;
        v.panL = cosf(angle);
        v.panR = sinf(angle);
    }
}

void Layer::Render(const LayerControl& c, float* left, float* right, int frames)
{
    // Changing algorithm: fade out, swap, fade in. Keeps switching silent.
    const bool switching = c.algo != m_algo;
    const float targetSwitch = switching ? 0.0f : 1.0f;
    const float targetL = c.on ? c.gainL : 0.0f;
    const float targetR = c.on ? c.gainR : 0.0f;

    if (switching && m_switchGain <= 0.001f)
    {
        m_algo = c.algo;
        ResetAlgorithm();
    }
    if (!c.on && m_gainL < 1e-4f && m_gainR < 1e-4f)
    {
        m_gainL = m_gainR = 0.0f;
        return;   // nothing to hear: save the CPU
    }

    const float nextSwitch = m_switchGain + std::clamp(targetSwitch - m_switchGain, -0.08f, 0.02f);
    UpdateVoices(c);

    std::fill(m_bufL, m_bufL + frames, 0.0f);
    std::fill(m_bufR, m_bufR + frames, 0.0f);

    switch (m_algo)
    {
    case ALG_MORPH:     RenderMorph(c, frames); break;
    case ALG_WAVETABLE: RenderWavetable(c, frames); break;
    case ALG_SWARM:     RenderSwarm(c, frames); break;
    case ALG_ADDITIVE:  RenderAdditive(c, frames); break;
    case ALG_FM:        RenderFm(c, frames); break;
    case ALG_FOLD:      RenderFold(c, frames); break;
    case ALG_FORMANT:   RenderFormant(c, frames); break;
    case ALG_PAD:       RenderPad(c, frames); break;
    case ALG_STRINGS:   RenderStrings(c, frames); break;
    case ALG_RESONATOR: RenderResonator(c, frames); break;
    case ALG_CLOUD:     RenderCloud(c, frames); break;
    case ALG_CHAOS:     RenderChaos(c, frames); break;
    default: break;
    }

    // Slow leveller for the algorithms whose output level swings with their settings.
    if (m_algo == ALG_STRINGS || m_algo == ALG_RESONATOR || m_algo == ALG_FORMANT)
    {
        float energy = 0.0f;
        for (int i = 0; i < frames; ++i)
            energy += m_bufL[i] * m_bufL[i] + m_bufR[i] * m_bufR[i];
        const float block = energy / (2.0f * frames);
        const float coeff = 1.0f - expf(-frames / (m_srf * 0.3f));
        m_rms += (block - m_rms) * coeff;
        const float wanted = (std::clamp)(0.3f / sqrtf(m_rms + 1e-9f), 0.05f, 40.0f);
        const float start = m_autoGain;
        m_autoGain += (wanted - m_autoGain) * coeff;
        const float step = (m_autoGain - start) / frames;
        float g = start;
        for (int i = 0; i < frames; ++i, g += step)
        {
            m_bufL[i] = SoftClip(m_bufL[i] * g);
            m_bufR[i] = SoftClip(m_bufR[i] * g);
        }
    }

    const float invN = 1.0f / frames;
    const float dL = (targetL * nextSwitch - m_gainL * m_switchGain) * invN;
    const float dR = (targetR * nextSwitch - m_gainR * m_switchGain) * invN;
    float gL = m_gainL * m_switchGain, gR = m_gainR * m_switchGain;
    for (int i = 0; i < frames; ++i)
    {
        gL += dL;
        gR += dR;
        left[i] += m_bufL[i] * gL;
        right[i] += m_bufR[i] * gR;
    }
    m_gainL = targetL;
    m_gainR = targetR;
    m_switchGain = nextSwitch;
}

// Crossfades two points of the classic wave series. Waves share their phase, so
// the blend is coherent; the -4 dB crossfade law keeps the level steady.
void Layer::RenderMorph(const LayerControl& c, int n)
{
    const Table& t = Get(0);
    const float posA = c.timbre * (kClassicWaves - 1);
    const float posB = c.color * (kClassicWaves - 1);
    const float ga = powf(1.0f - c.morph, 0.7f), gb = powf(c.morph, 0.7f);
    const float prevA = m_lastTimbre < 0.0f ? ga : m_lastTimbre, prevB = m_lastColor < 0.0f ? gb : m_lastColor;
    const float da = (ga - prevA) / n, db = (gb - prevB) / n;
    const float norm = 1.0f / sqrtf(static_cast<float>(m_activeVoices));
    for (int v = 0; v < m_activeVoices; ++v)
    {
        Voice& vo = m_voices[v];
        const MipBlend mips = ChooseMips(vo.hz, m_srf);
        const double inc = vo.hz / m_sr;
        float a = prevA, b = prevB;
        for (int i = 0; i < n; ++i)
        {
            a += da;
            b += db;
            const float s = (Read(t, posA, mips, vo.phase) * a + Read(t, posB, mips, vo.phase) * b) * norm * 0.6f;
            m_bufL[i] += s * vo.panL;
            m_bufR[i] += s * vo.panR;
            vo.phase = Wrap(vo.phase + inc);
        }
    }
    m_lastTimbre = ga;
    m_lastColor = gb;
}

void Layer::RenderWavetable(const LayerControl& c, int n)
{
    const Table& t = Get(c.table);
    const float pos = c.morph * (t.frames - 1);
    const float prev = m_lastMorph < 0.0f ? pos : m_lastMorph;
    const float dp = (pos - prev) / n;
    const float warp = c.timbre * 0.9f;
    const float sub = c.color;
    const float norm = 0.6f / sqrtf(static_cast<float>(m_activeVoices));
    const double kWarpScale = warp / (2.0 * 3.14159265358979);
    for (int v = 0; v < m_activeVoices; ++v)
    {
        Voice& vo = m_voices[v];
        const MipBlend mips = ChooseMips(vo.hz * (1.0f + warp), m_srf);
        const double inc = vo.hz / m_sr;
        float p = prev;
        for (int i = 0; i < n; ++i)
        {
            p += dp;
            // Phase warp: a smooth, periodic bend that keeps the mipmap's band limit roughly intact.
            double ph = vo.phase + kWarpScale * FastSinTurn(static_cast<float>(vo.phase));
            if (ph < 0.0) ph += 1.0; else if (ph >= 1.0) ph -= 1.0;
            float s = Read(t, p, mips, ph);
            s += sub * FastSinTurn(static_cast<float>(vo.phase2)) * 0.7f;
            s *= norm;
            m_bufL[i] += s * vo.panL;
            m_bufR[i] += s * vo.panR;
            const double next = vo.phase + inc;
            vo.phase = Wrap(next);
            vo.phase2 = Wrap(vo.phase2 + inc * 0.5);
        }
    }
    m_lastMorph = pos;
}

// Detuned saw stack, the classic thick drone. BLOOM makes each voice swell and
// recede on its own; OCTAVES adds a copy an octave up on every voice.
void Layer::RenderSwarm(const LayerControl& c, int n)
{
    const Table& t = Get(0);
    const float pos = c.morph * 3.0f;   // sine, triangle, saw, square
    const float bloomDepth = c.timbre;
    const float oct = c.color * 0.7f;
    const float norm = 0.95f / sqrtf(static_cast<float>(m_activeVoices));
    for (int v = 0; v < m_activeVoices; ++v)
    {
        Voice& vo = m_voices[v];
        const MipBlend mips = ChooseMips(vo.hz, m_srf);
        const MipBlend mips2 = ChooseMips(vo.hz * 2.0f, m_srf);
        const double inc = vo.hz / m_sr;
        const float amp = norm * (1.0f - bloomDepth * vo.bloom);
        for (int i = 0; i < n; ++i)
        {
            float s = Read(t, pos, mips, vo.phase);
            if (oct > 0.0f)
                s += oct * Read(t, pos, mips2, Wrap(vo.phase2));
            s *= amp;
            m_bufL[i] += s * vo.panL;
            m_bufR[i] += s * vo.panR;
            vo.phase = Wrap(vo.phase + inc);
            vo.phase2 = Wrap(vo.phase2 + 2.0 * inc);
        }
    }
}

void Layer::RenderAdditive(const LayerControl& c, int n)
{
    const float rolloff = 3.0f * (1.0f - c.morph) + 0.2f;
    const float evenGain = (std::min)(1.0f, 2.0f * (1.0f - c.timbre));
    const float oddGain = (std::min)(1.0f, 2.0f * c.timbre);
    const float stretch = c.color * c.color * 0.015f;
    const float dt = kControlBlock / m_srf;

    float amps[kPartials];
    float energy = 1e-6f;
    for (int k = 0; k < kPartials; ++k)
    {
        const int h = k + 1;
        if (m_rng.Uni() < 0.08f * dt * (1.0f + k * 0.1f))
            m_partialTarget[k] = m_rng.Uni();
        m_partialWander[k] += (m_partialTarget[k] - m_partialWander[k]) * (1.0f - expf(-0.4f * dt));
        float a = powf(static_cast<float>(h), -rolloff) * (0.25f + 0.75f * m_partialWander[k]);
        if (h > 1)
            a *= (h & 1) ? oddGain : evenGain;
        amps[k] = a;
        energy += a * a;
    }
    const float norm = 0.6f / sqrtf(energy * m_activeVoices);

    for (int v = 0; v < m_activeVoices; ++v)
    {
        const Voice& vo = m_voices[v];
        for (int k = 0; k < kPartials; ++k)
        {
            const float h = static_cast<float>(k + 1);
            const float hz = vo.hz * h * (1.0f + stretch * (h - 1.0f));
            if (hz >= m_srf * 0.45f)
                continue;
            const uint32_t inc = PhaseInc(hz, m_srf);
            const float a = amps[k] * norm;
            uint32_t ph = m_partialPhase[v][k];
            for (int i = 0; i < n; ++i)
            {
                const float s = FastSin(ph) * a;
                m_bufL[i] += s * vo.panL;
                m_bufR[i] += s * vo.panR;
                ph += inc;
            }
            m_partialPhase[v][k] = ph;
        }
    }
}

void Layer::RenderFm(const LayerControl& c, int n)
{
    static const float ratios[] = { 0.5f, 1.0f, 1.5f, 2.0f, 2.5f, 3.0f, 3.5f, 4.0f, 5.0f, 6.0f, 7.0f, 9.0f };
    const float ratio = ratios[static_cast<int>(c.timbre * 11.0f + 0.5f)];
    const float wantIndex = c.morph * c.morph * 8.0f;
    const float feedback = c.color * 0.9f;
    const float norm = 0.55f / sqrtf(static_cast<float>(m_activeVoices));
    for (int v = 0; v < m_activeVoices; ++v)
    {
        Voice& vo = m_voices[v];
        // Carson's rule: keep the sidebands below Nyquist, which keeps FM alias-free.
        const float mod = vo.hz * ratio;
        const float maxIndex = (std::max)(0.0f, (m_srf * 0.45f - vo.hz) / (mod + 1.0f) - 1.0f);
        const float index = (std::min)(wantIndex, maxIndex) / kTwoPi;
        const double inc = vo.hz / m_sr, modInc = mod / m_sr;
        for (int i = 0; i < n; ++i)
        {
            const float m = FastSinTurn(static_cast<float>(vo.phase2) + feedback * vo.fb / kTwoPi);
            vo.fb = 0.5f * (vo.fb + m);
            const float s = FastSinTurn(static_cast<float>(vo.phase) + index * m) * norm;
            m_bufL[i] += s * vo.panL;
            m_bufR[i] += s * vo.panR;
            vo.phase = Wrap(vo.phase + inc);
            vo.phase2 = Wrap(vo.phase2 + modInc);
        }
    }
}

void Layer::RenderFold(const LayerControl& c, int n)
{
    const float wantGain = 0.3f + c.morph * c.morph * 7.0f;
    const float offset = (c.timbre - 0.5f) * 1.2f;
    const float body = c.color;
    const float norm = 0.5f / sqrtf(static_cast<float>(m_activeVoices));
    for (int v = 0; v < m_activeVoices; ++v)
    {
        Voice& vo = m_voices[v];
        // sin(g sin x) has g+1 significant harmonics: bound g so they stay below Nyquist.
        const float gain = (std::min)(wantGain, (std::max)(0.2f, m_srf * 0.4f / vo.hz / kPi));
        const double inc = vo.hz / m_sr;
        float dc = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const float sine = FastSinTurn(static_cast<float>(vo.phase));
            const float folded = FastSinTurn(0.5f * (gain * sine + offset));
            // One-pole DC blocker: asymmetric folding has an offset.
            dc = folded - vo.z1 + 0.998f * vo.z2;
            vo.z1 = folded;
            vo.z2 = dc;
            const float s = (dc + body * sine) * norm;
            m_bufL[i] += s * vo.panL;
            m_bufR[i] += s * vo.panR;
            vo.phase = Wrap(vo.phase + inc);
        }
    }
}

void Layer::RenderFormant(const LayerControl& c, int n)
{
    static const float vowels[5][3] = { {800,1150,2900}, {400,1600,2700}, {280,2250,2890}, {450,800,2830}, {325,700,2700} };
    static const float gains[3] = { 1.0f, 0.7f, 0.35f };
    const Table& t = Get(0);
    const float x = c.morph * 3.999f;
    const int vi = static_cast<int>(x);
    const float vw = x - vi;
    const float shift = powf(2.0f, (c.timbre - 0.5f) * 2.0f);
    const float breath = c.color * c.color * 0.5f;
    for (int v = 0; v < m_activeVoices; ++v)
    {
        Voice& vo = m_voices[v];
        for (int f = 0; f < 3; ++f)
        {
            const float hz = (vowels[vi][f] + (vowels[vi + 1][f] - vowels[vi][f]) * vw) * shift;
            m_formants[v][f].Set(hz, 9.0f, m_srf);
        }
        const MipBlend mips = ChooseMips(vo.hz, m_srf);
        const double inc = vo.hz / m_sr;
        for (int i = 0; i < n; ++i)
        {
            const float src = Read(t, 2.0f, mips, vo.phase) + breath * m_rng.Bi();
            float s = 0.0f;
            for (int f = 0; f < 3; ++f)
            {
                float low, band;
                m_formants[v][f].Tick(src, low, band);
                s += band * gains[f];
            }
            s *= 0.5f;
            m_bufL[i] += s * vo.panL;
            m_bufR[i] += s * vo.panR;
            vo.phase = Wrap(vo.phase + inc);
        }
    }
}

// PADsynth loops. The spectra were built in advance, so this is just long-table
// playback, which is what makes it cheap for such a rich sound.
void Layer::RenderPad(const LayerControl& c, int n)
{
    const float x = c.morph * (kPadTables - 1);
    const int t0 = (std::min)(static_cast<int>(x), kPadTables - 2);
    const float tw = x - t0;
    const float* a = Pad(t0);
    const float* b = Pad(t0 + 1);
    const float octave = c.timbre;
    const float toneHz = 200.0f * powf(90.0f, c.color);
    const float toneK = 1.0f - expf(-kTwoPi * toneHz / m_srf);
    const float norm = 1.5f / sqrtf(static_cast<float>(m_activeVoices));
    constexpr double kSize = kPadSize;
    for (int v = 0; v < m_activeVoices; ++v)
    {
        Voice& vo = m_voices[v];
        const double speed = vo.hz / kPadBaseHz * (kPadBaseRate / m_sr) / kSize;
        for (int i = 0; i < n; ++i)
        {
            auto read = [&](double phase)
            {
                const double pos = phase * kSize;
                const int idx = static_cast<int>(pos);
                const float f = static_cast<float>(pos - idx);
                const float va = a[idx] + (a[idx + 1] - a[idx]) * f;
                const float vb = b[idx] + (b[idx + 1] - b[idx]) * f;
                return va + (vb - va) * tw;
            };
            float s = read(vo.phase);
            if (octave > 0.001f)
                s += octave * read(vo.phase2);
            s *= norm;
            m_bufL[i] += s * vo.panL;
            m_bufR[i] += s * vo.panR;
            vo.phase = Wrap(vo.phase + speed);
            vo.phase2 = Wrap(vo.phase2 + speed * 2.0);
        }
    }
    for (int i = 0; i < n; ++i)
    {
        m_toneL += (m_bufL[i] - m_toneL) * toneK;
        m_toneR += (m_bufR[i] - m_toneR) * toneK;
        m_bufL[i] = m_toneL;
        m_bufR[i] = m_toneR;
    }
}

// Karplus-Strong loop fed with continuous noise instead of a pluck, so the string
// is bowed and never dies away.
void Layer::RenderStrings(const LayerControl& c, int n)
{
    const float bowK = 0.02f + 0.9f * c.morph * c.morph;
    const float damp = 0.05f + 0.85f * c.timbre;     // loop one-pole coefficient
    const float excite = 0.002f + 0.06f * c.color * c.color;
    const float feedback = 0.9985f;
    for (int v = 0; v < m_activeVoices; ++v)
    {
        Voice& vo = m_voices[v];
        float* line = m_stringLines.data() + static_cast<size_t>(v) * (kStringLen + 1);
        int w = m_stringWrite[v];
        // Subtract the low-frequency delay of the loop filter so the pitch stays true.
        const float period = m_srf / (std::max)(vo.hz, 8.0f);
        const float delay = std::clamp(period - damp / (1.0f - damp) - 1.0f, 2.0f, static_cast<float>(kStringLen - 4));
        const int di = static_cast<int>(delay);
        const float df = delay - di;
        for (int i = 0; i < n; ++i)
        {
            m_exciteLp[v] += (m_rng.Bi() - m_exciteLp[v]) * bowK;
            const float a = line[(w - di) & (kStringLen - 1)];
            const float b = line[(w - di - 1) & (kStringLen - 1)];
            const float out = a + (b - a) * df;
            vo.z1 += (out - vo.z1) * (1.0f - damp);
            const float in = m_exciteLp[v] * excite + vo.z1 * feedback;
            line[w] = SoftClip(in);
            w = (w + 1) & (kStringLen - 1);
            m_bufL[i] += out * vo.panL;
            m_bufR[i] += out * vo.panR;
        }
        m_stringWrite[v] = w;
    }
}

// Tuned bandpass filters on noise: airy, choral, wind-like harmonics.
void Layer::RenderResonator(const LayerControl& c, int n)
{
    const float q = 8.0f * powf(60.0f, c.morph);
    const float exponent = 1.0f + (c.timbre - 0.5f) * 0.5f;
    const float noiseK = 0.03f + 0.97f * c.color * c.color;
    const int banks = m_activeVoices;
    for (int b = 0; b < banks; ++b)
    {
        const Voice& vo = m_voices[b];
        for (int k = 0; k < kResonators; ++k)
        {
            const float hz = vo.hz * powf(static_cast<float>(k + 1), exponent);
            m_resOn[b][k] = hz < m_srf * 0.45f;
            if (m_resOn[b][k])
                m_res[b][k].Set(hz, q, m_srf);
        }
    }
    const float gain = sqrtf(q) * 0.35f / sqrtf(static_cast<float>(banks));
    for (int b = 0; b < banks; ++b)
    {
        const Voice& vo = m_voices[b];
        for (int i = 0; i < n; ++i)
        {
            m_noiseLp[b] += (m_rng.Bi() - m_noiseLp[b]) * noiseK;
            float s = 0.0f;
            for (int k = 0; k < kResonators; ++k)
            {
                if (!m_resOn[b][k])
                    continue;
                float low, band;
                m_res[b][k].Tick(m_noiseLp[b], low, band);
                s += band / (1.0f + 0.25f * k);
            }
            s *= gain;
            // With one bank, width still comes from the bank's stereo position (centre).
            m_bufL[i] += s * (banks > 1 ? vo.panL : 0.707f);
            m_bufR[i] += s * (banks > 1 ? vo.panR : 0.707f);
        }
    }
}

void Layer::RenderCloud(const LayerControl& c, int n)
{
    static const float intervals[] = { 0.0f, 0.0f, 12.0f, 7.0f, -12.0f, 19.0f, 24.0f, 5.0f, -5.0f, 31.0f };
    const Table& t = Get(c.table);
    const float rate = 0.8f * powf(50.0f, c.morph);                       // grains per second
    const float length = 0.04f * powf(50.0f, c.color);                    // seconds
    const float norm = 1.3f / sqrtf(1.0f + rate * length * 0.5f);
    const float hz = m_voices[0].hz;

    for (int i = 0; i < n; ++i)
    {
        m_grainClock += rate / m_srf;
        if (m_grainClock >= 1.0f)
        {
            m_grainClock -= 1.0f;
            for (Grain& g : m_grains)
            {
                if (g.active)
                    continue;
                const int choices = 2 + static_cast<int>(c.timbre * 8.0f);
                const float semis = intervals[(std::min)(static_cast<int>(m_rng.Uni() * choices), 9)];
                const float scatterCents = c.detuneCents * m_rng.Bi() + c.driftCents * m_rng.Bi();
                const float grainHz = hz * powf(2.0f, semis / 12.0f) * CentsRatio(scatterCents);
                g.active = true;
                g.phase = m_rng.Uni();
                g.inc = grainHz / m_sr;
                g.pos = 0.0f;
                g.posInc = 1.0f / (length * m_srf * (0.6f + 0.8f * m_rng.Uni()));
                g.frame = c.morph * 0.5f * (t.frames - 1) + 0.5f * m_rng.Uni() * (t.frames - 1);
                g.mips = ChooseMips(grainHz, m_srf);
                const float angle = (c.width * m_rng.Bi() + 1.0f) * 0.25f * kPi;
                g.panL = cosf(angle);
                g.panR = sinf(angle);
                break;
            }
        }
    }

    for (Grain& g : m_grains)
    {
        if (!g.active)
            continue;
        for (int i = 0; i < n; ++i)
        {
            const float w = FastSinTurn(g.pos * 0.5f);
            const float s = Read(t, g.frame, g.mips, g.phase) * w * w * norm;
            m_bufL[i] += s * g.panL;
            m_bufR[i] += s * g.panR;
            g.phase = Wrap(g.phase + g.inc);
            g.pos += g.posInc;
            if (g.pos >= 1.0f)
            {
                g.active = false;
                break;
            }
        }
    }
}

// Two sines phase-modulating each other (the Lyra-8 idea), with the coupling
// pushed around by a Lorenz attractor so the pattern never quite repeats.
void Layer::RenderChaos(const LayerControl& c, int n)
{
    const float dt = 0.0006f * kControlBlock / 32.0f;
    const float dx = 10.0f * (m_ly - m_lx);
    const float dy = m_lx * (28.0f - m_lz) - m_ly;
    const float dz = m_lx * m_ly - 2.667f * m_lz;
    m_lx += dx * dt;
    m_ly += dy * dt;
    m_lz += dz * dt;
    const float lorenz = m_lx / 20.0f;

    const float ratio = 1.0f + c.timbre * 2.0f;
    const float cross = c.morph * c.morph * (0.8f + 0.2f * lorenz) * 2.5f;
    const float drive = 1.0f + c.color * c.color * 7.0f;
    const float makeup = 1.0f / sqrtf(drive);
    const float norm = 0.7f / sqrtf(static_cast<float>(m_activeVoices));
    for (int v = 0; v < m_activeVoices; ++v)
    {
        Voice& vo = m_voices[v];
        const float limit = (std::max)(0.0f, (m_srf * 0.4f / (vo.hz * ratio) - 1.0f) / kTwoPi);
        const float k = (std::min)(cross, limit);
        const double inc1 = vo.hz / m_sr, inc2 = vo.hz * ratio / m_sr;
        for (int i = 0; i < n; ++i)
        {
            const float a = FastSinTurn(static_cast<float>(vo.phase) + k * vo.z2);
            const float b = FastSinTurn(static_cast<float>(vo.phase2) + k * vo.z1);
            vo.z1 += (a - vo.z1) * 0.5f;
            vo.z2 += (b - vo.z2) * 0.5f;
            const float s = SoftClip((a + b * 0.6f) * drive) * makeup * norm;
            m_bufL[i] += s * vo.panL;
            m_bufR[i] += s * vo.panR;
            vo.phase = Wrap(vo.phase + inc1);
            vo.phase2 = Wrap(vo.phase2 + inc2);
        }
    }
}
