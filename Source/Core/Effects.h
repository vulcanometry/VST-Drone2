// Effects.h - the post-mix chain: filter, ensemble, phaser, delay, reverb and master.
#pragma once

#include "Layer.h"

#include <vector>

class StereoFilter
{
public:
    void Prepare(double sampleRate);
    void Set(int mode, float hz, float res, float drive);
    void Process(float* left, float* right, int n);
private:
    float m_sr = 48000.0f;
    int m_mode = FLT_OFF;
    float m_drive = 1.0f, m_makeup = 1.0f, m_k = 0.0f, m_ladderG = 0.0f, m_ladderK = 0.0f;
    Svf m_a[2], m_b[2];
    float m_ladder[2][4] = {};
    float m_mixBlend = 0.0f;
};

class Ensemble
{
public:
    void Prepare(double sampleRate);
    void Process(float* left, float* right, int n, float hz, float depth, float mix);
private:
    static constexpr int kSize = 1 << 13;
    float m_sr = 48000.0f;
    std::vector<float> m_buf[2];
    int m_write = 0;
    float m_phase = 0.0f, m_mix = 0.0f;
};

class Phaser
{
public:
    void Prepare(double sampleRate);
    void Process(float* left, float* right, int n, float hz, float depth, float feedback, float mix);
private:
    static constexpr int kStages = 6;
    float m_sr = 48000.0f;
    float m_state[2][kStages] = {};
    float m_fb[2] = {};
    float m_phase = 0.0f, m_mix = 0.0f;
};

class StereoDelay
{
public:
    void Prepare(double sampleRate);
    void Process(float* left, float* right, int n, float seconds, float feedback, float tone, float ping, float wow, float mix);
private:
    float m_sr = 48000.0f;
    int m_size = 0;
    std::vector<float> m_buf[2];
    int m_write = 0;
    float m_time = -1.0f;
    float m_lp[2] = {}, m_hp[2] = {};
    float m_phase = 0.0f, m_mix = 0.0f;
};

// Eight-channel feedback delay network: a Householder matrix mixes the lines
// (every line feeds every other, so echo density builds at once), a Hadamard
// diffuser in front removes the initial spatter, and a pitch shifter in the loop
// adds the octave-up "shimmer".
class Reverb
{
public:
    void Prepare(double sampleRate);
    void Process(float* left, float* right, int n, float size, float decaySeconds, float damp,
                 float diffusion, float mod, float shimmer, float mix, bool freeze);
private:
    static constexpr int kCh = 8;
    static constexpr int kSteps = 4;
    static constexpr int kLineSize = 1 << 17;
    static constexpr int kDiffSize = 1 << 13;
    static constexpr int kPitchSize = 1 << 14;

    struct DiffStep
    {
        std::vector<float> buf[kCh];
        int delay[kCh] = {};
        bool flip[kCh] = {};
        int write = 0;
    };

    float m_sr = 48000.0f;
    std::vector<float> m_line[kCh];
    float m_lineTime[kCh] = {};
    int m_write = 0;
    float m_damp[kCh] = {};
    float m_modPhase[kCh] = {};
    DiffStep m_diff[kSteps];

    std::vector<float> m_pitch[2];
    int m_pitchWrite = 0;
    float m_pitchPhase = 0.0f;
    float m_shimmerOut[2] = {};
    float m_shimmerHp[2] = {};
    float m_mix = 0.0f, m_inputGain = 1.0f;
};

class Master
{
public:
    void Prepare(double sampleRate);
    void Process(float* left, float* right, int n, float lowCutHz, float tiltDb, float width, float gain);
    float PeakL() const { return m_peakL; }
    float PeakR() const { return m_peakR; }
private:
    float m_sr = 48000.0f;
    Svf m_hp[2];
    float m_tilt[2] = {};
    float m_env = 0.0f;
    float m_width = -1.0f, m_gain = -1.0f, m_tiltDb = 0.0f;
    float m_peakL = 0.0f, m_peakR = 0.0f;
};
