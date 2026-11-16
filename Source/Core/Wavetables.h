// Wavetables.h - the generated, band-limited wave data shared by every engine.
//
// Each table is a set of single-cycle frames, and each frame is stored as a
// mipmap: one copy per octave with the harmonics that would alias removed by
// FFT. Mips near the top are 4x oversampled, so linear interpolation stays clean.
// The PAD tables are long periodic loops made with the PADsynth algorithm:
// harmonics smeared into Gaussian bands with random phases.
//
// The bank is built once, on first use, and is read-only afterwards.
#pragma once

#include <cstdint>
#include <vector>

namespace wavetables
{
constexpr int kNumTables = 20;
constexpr int kNumMips = 10;
constexpr int kClassicWaves = 8;             // frames of table 0, the crossfade waveforms
constexpr int kPadTables = 4;
constexpr int kPadSize = 1 << 18;
constexpr double kPadBaseHz = 55.0;          // pitch of a PAD table played back at 44.1 kHz
constexpr double kPadBaseRate = 44100.0;

constexpr int MipLength(int mip) { return mip == 0 ? 2048 : (mip == 1 ? 1024 : (mip == 2 ? 512 : 256)); }
constexpr int MipHarmonics(int mip) { return 512 >> mip; }

struct Table
{
    const wchar_t* name = L"";
    int frames = 0;
    std::vector<float> data;
    const float* Mip(int frame, int mip) const;   // MipLength(mip) + 1 samples (the last repeats the first)
    int frameStride = 0;
};

const Table& Get(int index);
const wchar_t* TableName(int index);
const wchar_t* ClassicWaveName(int index);
const float* Pad(int index);                     // kPadSize + 1 samples
void EnsureBuilt();                              // call off the audio thread

// Which two mips to blend for a given pitch. Mip m0 fades out as its top
// harmonic reaches Nyquist, so nothing audibly aliases and no pitch steps.
struct MipBlend
{
    int m0 = 0, m1 = 0;
    float w = 0.0f;
};
MipBlend ChooseMips(float hz, float sampleRate);

inline float ReadMip(const float* mip, int length, double phase)
{
    const double pos = phase * length;
    const int i = static_cast<int>(pos);
    const float f = static_cast<float>(pos - i);
    return mip[i] + (mip[i + 1] - mip[i]) * f;
}

// Reads one frame position (fractional frames crossfade) at phase 0..1.
inline float Read(const Table& t, float framePos, const MipBlend& b, double phase)
{
    const int f0 = static_cast<int>(framePos);
    const int f1 = f0 + 1 < t.frames ? f0 + 1 : f0;
    const float fw = framePos - f0;
    const int len0 = MipLength(b.m0);
    float v = ReadMip(t.Mip(f0, b.m0), len0, phase);
    if (fw > 0.0f && f1 != f0)
        v += (ReadMip(t.Mip(f1, b.m0), len0, phase) - v) * fw;
    if (b.w > 0.0f)
    {
        const int len1 = MipLength(b.m1);
        float u = ReadMip(t.Mip(f0, b.m1), len1, phase);
        if (fw > 0.0f && f1 != f0)
            u += (ReadMip(t.Mip(f1, b.m1), len1, phase) - u) * fw;
        v += (u - v) * b.w;
    }
    return v;
}

// Fast sine for the additive and FM engines; phase is a full 32-bit turn.
extern float g_sine[4097];
inline float FastSin(uint32_t phase)
{
    const uint32_t i = phase >> 20;
    const float f = static_cast<float>(phase & 0xFFFFF) * (1.0f / 1048576.0f);
    return g_sine[i] + (g_sine[i + 1] - g_sine[i]) * f;
}
inline float FastSinTurn(float turns)
{
    turns -= static_cast<float>(static_cast<int>(turns)) - (turns < 0.0f ? 1.0f : 0.0f);
    return FastSin(static_cast<uint32_t>(turns * 4294967040.0f));
}

void Fft(std::vector<double>& re, std::vector<double>& im, bool inverse);   // radix-2, unnormalized
}
