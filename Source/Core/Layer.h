// Layer.h - one sound layer: twelve drone algorithms behind a common voice stack.
#pragma once

#include "Params.h"
#include "Wavetables.h"

#include <cstdint>
#include <vector>

constexpr int kControlBlock = 32;   // parameters and modulation update every 32 samples

struct XorShift
{
    uint32_t state = 0x12345678u;
    inline uint32_t Next()
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }
    inline float Uni() { return static_cast<float>(Next()) * (1.0f / 4294967296.0f); }      // 0..1
    inline float Bi() { return Uni() * 2.0f - 1.0f; }                                       // -1..1
};

// Topology-preserving state variable filter (Zavalishin), stable under modulation.
struct Svf
{
    float ic1 = 0.0f, ic2 = 0.0f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f, k = 1.0f;
    void Set(float hz, float q, float sampleRate);
    inline void Tick(float in, float& low, float& band)
    {
        const float v3 = in - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        low = v2;
        band = v1;
    }
    void Reset() { ic1 = ic2 = 0.0f; }
};

struct LayerControl
{
    bool on = false;
    int algo = 0;
    int table = 0;
    float hz = 110.0f;
    float morph = 0.5f, timbre = 0.5f, color = 0.5f;   // after modulation, 0..1
    int voices = 1;
    float detuneCents = 0.0f, width = 0.0f, driftCents = 0.0f;
    float gainL = 0.0f, gainR = 0.0f;                  // level, vector mix and pan
};

class Layer
{
public:
    static constexpr int kMaxVoices = 8;

    void Prepare(double sampleRate, unsigned seed);

    // Adds up to kControlBlock frames of this layer into left/right.
    void Render(const LayerControl& c, float* left, float* right, int frames);

private:
    struct Voice
    {
        double phase = 0.0, phase2 = 0.0, phase3 = 0.0;
        float wander = 0.0f, wanderTarget = 0.0f, wanderHz = 0.2f;
        float bloom = 1.0f, bloomTarget = 1.0f;
        float hz = 110.0f, panL = 0.7f, panR = 0.7f;
        float z1 = 0.0f, z2 = 0.0f, fb = 0.0f;
        uint32_t chaos = 0;
    };

    struct Grain
    {
        bool active = false;
        double phase = 0.0, inc = 0.0;
        float pos = 0.0f, posInc = 0.0f, frame = 0.0f, panL = 0.7f, panR = 0.7f;
        wavetables::MipBlend mips;
    };

    void UpdateVoices(const LayerControl& c);
    void ResetAlgorithm();

    // Each renders into m_bufL/m_bufR. morph etc. are ramped across the block
    // where it matters for clicks, and stepped where the change is inaudible.
    void RenderMorph(const LayerControl& c, int n);
    void RenderWavetable(const LayerControl& c, int n);
    void RenderSwarm(const LayerControl& c, int n);
    void RenderAdditive(const LayerControl& c, int n);
    void RenderFm(const LayerControl& c, int n);
    void RenderFold(const LayerControl& c, int n);
    void RenderFormant(const LayerControl& c, int n);
    void RenderPad(const LayerControl& c, int n);
    void RenderStrings(const LayerControl& c, int n);
    void RenderResonator(const LayerControl& c, int n);
    void RenderCloud(const LayerControl& c, int n);
    void RenderChaos(const LayerControl& c, int n);

    double m_sr = 48000.0;
    float m_srf = 48000.0f;
    XorShift m_rng;

    Voice m_voices[kMaxVoices];
    int m_activeVoices = 1;

    int m_algo = -1;
    float m_switchGain = 0.0f;   // fades to 0 before the algorithm changes, then back to 1
    float m_gainL = 0.0f, m_gainR = 0.0f;
    float m_lastMorph = -1.0f, m_lastTimbre = -1.0f, m_lastColor = -1.0f;

    float m_bufL[kControlBlock] = {}, m_bufR[kControlBlock] = {};

    // Additive
    static constexpr int kPartials = 24;
    uint32_t m_partialPhase[3][kPartials] = {};
    float m_partialWander[kPartials] = {}, m_partialTarget[kPartials] = {};

    // Formant
    Svf m_formants[4][3];

    // Strings
    std::vector<float> m_stringLines;
    int m_stringWrite[4] = {};
    float m_exciteLp[4] = {};
    static constexpr int kStringLen = 1 << 14;

    // Resonator
    static constexpr int kResonators = 8;
    Svf m_res[2][kResonators];
    bool m_resOn[2][kResonators] = {};
    float m_resGain = 1.0f;
    float m_noiseLp[2] = {};

    // Cloud
    static constexpr int kGrains = 24;
    Grain m_grains[kGrains];
    float m_grainClock = 0.0f;

    // PAD tone filter
    float m_toneL = 0.0f, m_toneR = 0.0f;

    // Levelling for the self-exciting algorithms, whose loudness depends strongly
    // on their settings.
    float m_rms = 0.0f, m_autoGain = 1.0f;

    // Lorenz attractor for the chaos pair.
    float m_lx = 0.1f, m_ly = 0.0f, m_lz = 20.0f;
};
