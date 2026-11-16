// Effects.cpp - the post-mix chain: filter, ensemble, phaser, delay, reverb and master.
#include "Effects.h"

#include <algorithm>
#include <cmath>
#include <random>

using wavetables::FastSinTurn;

namespace
{
constexpr float kPi = 3.14159265358979f;
inline float Tanh(float x)
{
    // Pade approximation, exact enough here and much cheaper than tanhf.
    if (x > 3.0f) return 1.0f;
    if (x < -3.0f) return -1.0f;
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}
// Linear up to 0.7, so normal levels pick up no distortion at all; saturates toward 0.98 above.
inline float SafetyClip(float x)
{
    const float a = fabsf(x);
    if (a <= 0.7f)
        return x;
    const float y = 0.7f + 0.28f * Tanh((a - 0.7f) / 0.28f);
    return x < 0.0f ? -y : y;
}
inline float Wrapf(float p) { return p >= 1.0f ? p - 1.0f : p; }
inline float ReadFrac(const std::vector<float>& buf, int mask, int write, float delay)
{
    const int d = static_cast<int>(delay);
    const float f = delay - d;
    const float a = buf[(write - d) & mask];
    const float b = buf[(write - d - 1) & mask];
    return a + (b - a) * f;
}
} // namespace

// ---------------------------------------------------------------------------
// Filter
// ---------------------------------------------------------------------------
void StereoFilter::Prepare(double sampleRate)
{
    m_sr = static_cast<float>(sampleRate);
    for (int c = 0; c < 2; ++c)
    {
        m_a[c].Reset();
        m_b[c].Reset();
        for (float& s : m_ladder[c]) s = 0.0f;
    }
}

void StereoFilter::Set(int mode, float hz, float res, float drive)
{
    m_mode = mode;
    m_drive = drive;
    m_makeup = 1.0f / sqrtf(drive);
    const float q = 0.5f + res * res * 24.0f;
    for (int c = 0; c < 2; ++c)
    {
        m_a[c].Set(hz, q, m_sr);
        m_b[c].Set(hz, mode == FLT_LP24 ? 0.54f + (q - 0.5f) * 0.25f : q, m_sr);
    }
    const float g = tanf(kPi * (std::min)(hz, m_sr * 0.45f) / m_sr);
    m_ladderG = g / (1.0f + g);
    m_ladderK = res * 3.9f;
}

void StereoFilter::Process(float* left, float* right, int n)
{
    if (m_mode == FLT_OFF)
        return;
    float* channels[2] = { left, right };
    for (int c = 0; c < 2; ++c)
    {
        float* x = channels[c];
        for (int i = 0; i < n; ++i)
        {
            const float in = m_drive > 1.001f ? Tanh(x[i] * m_drive) * m_makeup : x[i];
            float low, band, out = in;
            switch (m_mode)
            {
            case FLT_LP12:
                m_a[c].Tick(in, low, band);
                out = low;
                break;
            case FLT_LP24:
                m_a[c].Tick(in, low, band);
                m_b[c].Tick(low, out, band);
                break;
            case FLT_BP:
                m_a[c].Tick(in, low, band);
                out = band * m_a[c].k;
                break;
            case FLT_HP:
                m_a[c].Tick(in, low, band);
                out = in - m_a[c].k * band - low;
                break;
            case FLT_NOTCH:
                m_a[c].Tick(in, low, band);
                out = in - m_a[c].k * band;
                break;
            case FLT_LADDER:
            {
                float* s = m_ladder[c];
                const float fb = Tanh(s[3]);
                float u = in * (1.0f + m_ladderK * 0.5f) - m_ladderK * fb;
                for (int p = 0; p < 4; ++p)
                {
                    const float v = (u - s[p]) * m_ladderG;
                    const float y = v + s[p];
                    s[p] = y + v;
                    u = y;
                }
                out = u;
                break;
            }
            default:
                break;
            }
            x[i] = out;
        }
    }
}

// ---------------------------------------------------------------------------
// Ensemble: three modulated delays per channel, 120 degrees apart.
// ---------------------------------------------------------------------------
void Ensemble::Prepare(double sampleRate)
{
    m_sr = static_cast<float>(sampleRate);
    for (auto& b : m_buf)
        b.assign(kSize, 0.0f);
    m_write = 0;
}

void Ensemble::Process(float* left, float* right, int n, float hz, float depth, float mix)
{
    if (mix <= 0.0005f && m_mix <= 0.0005f)
    {
        // Keep the delay lines running so that raising the mix is seamless.
        for (int i = 0; i < n; ++i)
        {
            m_buf[0][m_write] = left[i];
            m_buf[1][m_write] = right[i];
            m_write = (m_write + 1) & (kSize - 1);
        }
        m_mix = 0.0f;
        return;
    }
    const float inc = hz / m_sr;
    const float baseDelay = 0.012f * m_sr, sweep = (0.0005f + 0.006f * depth) * m_sr;
    const float dm = (mix - m_mix) / n;
    for (int i = 0; i < n; ++i)
    {
        m_mix += dm;
        m_buf[0][m_write] = left[i];
        m_buf[1][m_write] = right[i];
        float wetL = 0.0f, wetR = 0.0f;
        for (int t = 0; t < 3; ++t)
        {
            const float p = m_phase + t / 3.0f;
            wetL += ReadFrac(m_buf[0], kSize - 1, m_write, baseDelay + sweep * (1.0f + FastSinTurn(p > 1.0f ? p - 1.0f : p)));
            const float q = p + 0.25f;
            wetR += ReadFrac(m_buf[1], kSize - 1, m_write, baseDelay + sweep * (1.0f + FastSinTurn(q > 1.0f ? q - floorf(q) : q)));
        }
        wetL *= 0.45f;
        wetR *= 0.45f;
        left[i] += (wetL - left[i]) * m_mix;
        right[i] += (wetR - right[i]) * m_mix;
        m_write = (m_write + 1) & (kSize - 1);
        m_phase = Wrapf(m_phase + inc);
    }
}

// ---------------------------------------------------------------------------
// Phaser: six first-order allpasses with a sweep in quadrature between channels.
// ---------------------------------------------------------------------------
void Phaser::Prepare(double sampleRate)
{
    m_sr = static_cast<float>(sampleRate);
    for (auto& channel : m_state)
        for (float& s : channel) s = 0.0f;
    m_fb[0] = m_fb[1] = 0.0f;
}

void Phaser::Process(float* left, float* right, int n, float hz, float depth, float feedback, float mix)
{
    if (mix <= 0.0005f && m_mix <= 0.0005f)
    {
        m_mix = 0.0f;
        return;
    }
    const float dm = (mix - m_mix) / n;
    m_phase = fmodf(m_phase + hz * n / m_sr, 1.0f);
    float* channels[2] = { left, right };
    float startMix = m_mix;
    for (int c = 0; c < 2; ++c)
    {
        const float lfo = FastSinTurn(fmodf(m_phase + c * 0.25f, 1.0f));
        const float fc = 700.0f * powf(2.0f, lfo * depth * 2.5f);
        const float t = tanf(kPi * (std::min)(fc, m_sr * 0.4f) / m_sr);
        const float a = (t - 1.0f) / (t + 1.0f);
        float* x = channels[c];
        float wetMix = startMix;
        for (int i = 0; i < n; ++i)
        {
            wetMix += dm;
            float s = x[i] + m_fb[c] * feedback * 0.85f;
            for (int k = 0; k < kStages; ++k)
            {
                const float y = a * s + m_state[c][k];
                m_state[c][k] = s - a * y;
                s = y;
            }
            m_fb[c] = s;
            x[i] += (0.5f * (x[i] + s) - x[i]) * wetMix;
        }
    }
    m_mix = mix;
}

// ---------------------------------------------------------------------------
// Delay: tape-like; the time glides rather than jumps.
// ---------------------------------------------------------------------------
void StereoDelay::Prepare(double sampleRate)
{
    m_sr = static_cast<float>(sampleRate);
    int size = 1;
    while (size < static_cast<int>(m_sr * 4.2f))
        size <<= 1;
    m_size = size;
    for (auto& b : m_buf)
        b.assign(size, 0.0f);
    m_write = 0;
    m_time = -1.0f;
    m_lp[0] = m_lp[1] = m_hp[0] = m_hp[1] = 0.0f;
}

void StereoDelay::Process(float* left, float* right, int n, float seconds, float feedback, float tone, float ping, float wow, float mix)
{
    const int mask = m_size - 1;
    const float target = seconds * m_sr;
    if (m_time < 0.0f)
        m_time = target;
    const float glide = 1.0f - expf(-1.0f / (0.25f * m_sr));
    const float lpK = 1.0f - expf(-2.0f * kPi * (400.0f * powf(40.0f, tone)) / m_sr);
    const float hpK = 1.0f - expf(-2.0f * kPi * 50.0f / m_sr);
    const float wowDepth = wow * wow * 0.004f * m_sr;
    const float fb = feedback * 0.98f;
    const float dm = (mix - m_mix) / n;
    for (int i = 0; i < n; ++i)
    {
        m_mix += dm;
        m_time += (target - m_time) * glide;
        const float wobble = wowDepth * (1.0f + FastSinTurn(m_phase));
        const float tapL = ReadFrac(m_buf[0], mask, m_write, (std::min)(m_time + wobble + 2.0f, static_cast<float>(m_size - 4)));
        const float tapR = ReadFrac(m_buf[1], mask, m_write, (std::min)(m_time * (1.0f + 0.03f * (1.0f - ping)) + wobble + 2.0f, static_cast<float>(m_size - 4)));

        m_lp[0] += (tapL - m_lp[0]) * lpK;
        m_lp[1] += (tapR - m_lp[1]) * lpK;
        m_hp[0] += (m_lp[0] - m_hp[0]) * hpK;
        m_hp[1] += (m_lp[1] - m_hp[1]) * hpK;
        const float loopL = m_lp[0] - m_hp[0];
        const float loopR = m_lp[1] - m_hp[1];

        // PING moves the input and the returns to the opposite channel.
        const float inMono = 0.5f * (left[i] + right[i]);
        const float inL = left[i] + (inMono - left[i]) * ping;
        const float inR = right[i] * (1.0f - ping);
        m_buf[0][m_write] = Tanh(inL + fb * (loopL + (loopR - loopL) * ping));
        m_buf[1][m_write] = Tanh(inR + fb * (loopR + (loopL - loopR) * ping));
        m_write = (m_write + 1) & mask;
        m_phase = Wrapf(m_phase + 0.31f / m_sr);

        left[i] += (tapL - left[i]) * m_mix * 0.7f;
        right[i] += (tapR - right[i]) * m_mix * 0.7f;
    }
}

// ---------------------------------------------------------------------------
// Reverb
// ---------------------------------------------------------------------------
void Reverb::Prepare(double sampleRate)
{
    m_sr = static_cast<float>(sampleRate);
    // Exponentially spaced lengths with no common factors, in milliseconds at size 1.
    static const float baseMs[kCh] = { 37.0f, 43.0f, 53.0f, 61.0f, 73.0f, 89.0f, 103.0f, 127.0f };
    std::mt19937 rng(77);
    std::uniform_real_distribution<float> uni(0.0f, 1.0f);
    for (int c = 0; c < kCh; ++c)
    {
        m_line[c].assign(kLineSize, 0.0f);
        m_lineTime[c] = baseMs[c] * 0.001f * m_sr;
        m_damp[c] = 0.0f;
        m_modPhase[c] = uni(rng);
    }
    m_write = 0;

    float rangeMs = 12.0f;
    for (int s = 0; s < kSteps; ++s)
    {
        DiffStep& step = m_diff[s];
        for (int c = 0; c < kCh; ++c)
        {
            step.buf[c].assign(kDiffSize, 0.0f);
            const float lo = rangeMs * c / kCh, hi = rangeMs * (c + 1) / kCh;
            const int samples = static_cast<int>((lo + (hi - lo) * uni(rng)) * 0.001f * m_sr);
            step.delay[c] = std::clamp(samples, 1, kDiffSize - 1);
            step.flip[c] = uni(rng) < 0.5f;
        }
        step.write = 0;
        rangeMs *= 1.7f;
    }

    for (auto& b : m_pitch)
        b.assign(kPitchSize, 0.0f);
    m_pitchWrite = 0;
    m_pitchPhase = 0.0f;
    m_shimmerOut[0] = m_shimmerOut[1] = 0.0f;
    m_shimmerHp[0] = m_shimmerHp[1] = 0.0f;
    m_inputGain = 0.1f;
}

namespace
{
// Fast Hadamard mix, orthogonal, in place.
inline void Hadamard8(float* x)
{
    constexpr float scale = 0.35355339f;   // 1 / sqrt(8)
    for (int size = 1; size < 8; size *= 2)
        for (int i = 0; i < 8; i += 2 * size)
            for (int j = i; j < i + size; ++j)
            {
                const float a = x[j], b = x[j + size];
                x[j] = a + b;
                x[j + size] = a - b;
            }
    for (int i = 0; i < 8; ++i)
        x[i] *= scale;
}
} // namespace

void Reverb::Process(float* left, float* right, int n, float size, float decaySeconds, float damp,
                     float diffusion, float mod, float shimmer, float mix, bool freeze)
{
    static const float baseMs[kCh] = { 37.0f, 43.0f, 53.0f, 61.0f, 73.0f, 89.0f, 103.0f, 127.0f };
    float targetDelay[kCh], gains[kCh];
    for (int c = 0; c < kCh; ++c)
    {
        const float ms = baseMs[c] * size;
        targetDelay[c] = (std::min)(ms * 0.001f * m_sr, static_cast<float>(kLineSize - 8));
        // Per-line gain for the wanted RT60; a frozen tail neither decays nor darkens.
        gains[c] = freeze ? 1.0f : powf(10.0f, -3.0f * (ms * 0.001f) / decaySeconds);
    }
    const float dampK = freeze ? 1.0f : 1.0f - damp * 0.92f;
    const float modDepth = mod * mod * 0.004f * m_sr;
    const float modInc = 0.07f + 0.5f * mod;
    const float glide = 1.0f - expf(-1.0f / (0.2f * m_sr));
    // Scale the input with the loop loss, so a sustained drone stays at about the
    // same level however long the decay is, instead of piling up in the tank. The
    // exponent and gain are fitted to measurement; the render tool checks the result.
    const float meanGain = gains[kCh / 2];
    const float inTarget = freeze ? 0.0f : 2.37f * powf((std::max)(1.0f - meanGain * meanGain, 1e-5f), 0.264f);
    const float dm = (mix - m_mix) / n;
    // Shimmer read heads: octave up means reading at 2x, so each head sweeps its
    // whole range once per grain, and two heads crossfade to hide the wrap.
    const float grainSamples = (std::min)(0.12f * m_sr, static_cast<float>(kPitchSize - 8));
    const float pitchInc = 1.0f / grainSamples;

    for (int i = 0; i < n; ++i)
    {
        m_mix += dm;
        m_inputGain += (inTarget - m_inputGain) * 0.002f;

        // Split the stereo input across the channels, with alternating polarity.
        float x[kCh];
        const float inL = (left[i] + m_shimmerOut[0] * shimmer) * m_inputGain;
        const float inR = (right[i] + m_shimmerOut[1] * shimmer) * m_inputGain;
        for (int c = 0; c < kCh; c += 2)
        {
            x[c] = inL * (c & 2 ? -1.0f : 1.0f);
            x[c + 1] = inR * (c & 4 ? -1.0f : 1.0f);
        }

        // Diffuser: delay, flip, mix; several times with growing ranges.
        for (DiffStep& step : m_diff)
        {
            float d[kCh];
            for (int c = 0; c < kCh; ++c)
            {
                step.buf[c][step.write] = x[c];
                const float out = step.buf[c][(step.write - step.delay[c]) & (kDiffSize - 1)];
                d[c] = step.flip[c] ? -out : out;
            }
            step.write = (step.write + 1) & (kDiffSize - 1);
            Hadamard8(d);
            for (int c = 0; c < kCh; ++c)
                x[c] += (d[c] - x[c]) * diffusion;
        }

        // Delay outputs, damped.
        float y[kCh], sum = 0.0f;
        for (int c = 0; c < kCh; ++c)
        {
            m_lineTime[c] += (targetDelay[c] - m_lineTime[c]) * glide;
            const float wobble = modDepth * (1.0f + FastSinTurn(m_modPhase[c]));
            m_modPhase[c] = Wrapf(m_modPhase[c] + modInc * (1.0f + 0.1f * c) / m_sr);
            const float out = ReadFrac(m_line[c], kLineSize - 1, m_write, m_lineTime[c] + wobble + 1.0f);
            m_damp[c] += (out - m_damp[c]) * dampK;
            y[c] = m_damp[c];
            sum += y[c];
        }

        // Householder reflection: y - (2/N) * sum. Lossless and very cheap.
        const float reflect = sum * (2.0f / kCh);
        for (int c = 0; c < kCh; ++c)
            m_line[c][m_write] = (y[c] - reflect) * gains[c] + x[c];
        m_write = (m_write + 1) & (kLineSize - 1);

        const float wetL = (y[0] - y[2] + y[4] - y[6]) * 0.5f;
        const float wetR = (y[1] - y[3] + y[5] - y[7]) * 0.5f;

        if (shimmer > 0.0005f)
        {
            m_pitch[0][m_pitchWrite] = wetL;
            m_pitch[1][m_pitchWrite] = wetR;
            for (int ch = 0; ch < 2; ++ch)
            {
                float out = 0.0f;
                for (int head = 0; head < 2; ++head)
                {
                    float p = m_pitchPhase + head * 0.5f;
                    if (p >= 1.0f) p -= 1.0f;
                    // Delay shrinks from a full grain to zero: a read speed of 2x.
                    const float delay = (1.0f - p) * grainSamples + 1.0f;
                    const float window = FastSinTurn(p * 0.5f);
                    out += ReadFrac(m_pitch[ch], kPitchSize - 1, m_pitchWrite, delay) * window * window;
                }
                // High-pass and soft limit in the loop so the shimmer cannot run away.
                m_shimmerHp[ch] += (out - m_shimmerHp[ch]) * 0.01f;
                m_shimmerOut[ch] = Tanh((out - m_shimmerHp[ch]) * 0.7f);
            }
            m_pitchWrite = (m_pitchWrite + 1) & (kPitchSize - 1);
            m_pitchPhase = Wrapf(m_pitchPhase + pitchInc);
        }
        else
        {
            m_shimmerOut[0] = m_shimmerOut[1] = 0.0f;
        }

        left[i] += (wetL - left[i]) * m_mix;
        right[i] += (wetR - right[i]) * m_mix;
    }
}

// ---------------------------------------------------------------------------
// Master: DC / low cut, tilt EQ, stereo width, volume and a safety limiter.
// ---------------------------------------------------------------------------
void Master::Prepare(double sampleRate)
{
    m_sr = static_cast<float>(sampleRate);
    m_hp[0].Reset();
    m_hp[1].Reset();
    m_tilt[0] = m_tilt[1] = 0.0f;
    m_env = 0.0f;
    m_width = m_gain = -1.0f;
}

void Master::Process(float* left, float* right, int n, float lowCutHz, float tiltDb, float width, float gain)
{
    m_hp[0].Set(lowCutHz, 0.707f, m_sr);
    m_hp[1].Set(lowCutHz, 0.707f, m_sr);
    const float tiltK = 1.0f - expf(-2.0f * kPi * 700.0f / m_sr);
    const float tilt = powf(10.0f, tiltDb / 20.0f);
    const float lowGain = 1.0f / tilt, highGain = tilt;

    if (m_gain < 0.0f) { m_gain = gain; m_width = width; }
    const float dg = (gain - m_gain) / n, dw = (width - m_width) / n;
    const float attack = 1.0f - expf(-1.0f / (0.002f * m_sr));
    const float release = 1.0f - expf(-1.0f / (0.4f * m_sr));
    float peakL = 0.0f, peakR = 0.0f;

    for (int i = 0; i < n; ++i)
    {
        m_gain += dg;
        m_width += dw;
        float low, band;
        m_hp[0].Tick(left[i], low, band);
        float l = left[i] - m_hp[0].k * band - low;
        m_hp[1].Tick(right[i], low, band);
        float r = right[i] - m_hp[1].k * band - low;

        m_tilt[0] += (l - m_tilt[0]) * tiltK;
        m_tilt[1] += (r - m_tilt[1]) * tiltK;
        l = m_tilt[0] * lowGain + (l - m_tilt[0]) * highGain;
        r = m_tilt[1] * lowGain + (r - m_tilt[1]) * highGain;

        const float mid = 0.5f * (l + r), side = 0.5f * (l - r) * m_width;
        l = (mid + side) * m_gain;
        r = (mid - side) * m_gain;

        // Limiter: gain follows the peak above 0.8, and a soft clip catches anything faster.
        const float peak = (std::max)(fabsf(l), fabsf(r));
        m_env += (peak - m_env) * (peak > m_env ? attack : release);
        if (m_env > 0.8f)
        {
            const float g = 0.8f / m_env;
            l *= g;
            r *= g;
        }
        l = SafetyClip(l);
        r = SafetyClip(r);
        left[i] = l;
        right[i] = r;
        peakL = (std::max)(peakL, fabsf(l));
        peakR = (std::max)(peakR, fabsf(r));
    }
    m_peakL = peakL;
    m_peakR = peakR;
}
