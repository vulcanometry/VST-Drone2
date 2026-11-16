// Wavetables.cpp - generates the band-limited wavetables and the PADsynth loops.
#include "Wavetables.h"

#include <cmath>
#include <functional>
#include <mutex>
#include <random>

namespace wavetables
{
float g_sine[4097];

namespace
{
constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;
constexpr int kFrameSize = 2048;
constexpr int kBins = kFrameSize / 2;

int FrameStride()
{
    int total = 0;
    for (int m = 0; m < kNumMips; ++m)
        total += MipLength(m) + 1;
    return total;
}

constexpr int MipOffset(int mip)
{
    int total = 0;
    for (int m = 0; m < mip; ++m)
        total += MipLength(m) + 1;
    return total;
}

// Frame contents in the time domain, one cycle of kFrameSize samples.
using TimeFn = std::function<double(double phase, double frame)>;
// Harmonic amplitude for harmonic h (1-based) at frame position 0..1.
using HarmFn = std::function<double(int h, double frame)>;

// Spectrum of one frame -> one band-limited copy per mip, all at the same gain.
void StoreFrame(Table& t, int frame, std::vector<double>& re, std::vector<double>& im)
{
    re[0] = im[0] = 0.0;
    double scale = 1.0;
    for (int mip = 0; mip < kNumMips; ++mip)
    {
        const int len = MipLength(mip);
        const int harmonics = MipHarmonics(mip);
        std::vector<double> r(len, 0.0), i(len, 0.0);
        for (int h = 1; h <= harmonics && h < len / 2; ++h)
        {
            r[h] = re[h];        i[h] = im[h];
            r[len - h] = re[h];  i[len - h] = -im[h];
        }
        Fft(r, i, true);
        if (mip == 0)
        {
            double peak = 1e-9;
            for (double v : r)
                peak = (std::max)(peak, fabs(v));
            scale = 0.95 / peak;
        }
        float* out = t.data.data() + frame * t.frameStride + MipOffset(mip);
        for (int s = 0; s < len; ++s)
            out[s] = static_cast<float>(r[s] * scale);
        out[len] = out[0];
    }
}

void InitTable(Table& t, const wchar_t* name, int frames)
{
    t.name = name;
    t.frames = frames;
    t.frameStride = FrameStride();
    t.data.assign(static_cast<size_t>(frames) * t.frameStride, 0.0f);
}

void BuildFromTime(Table& t, const wchar_t* name, int frames, const TimeFn& fn)
{
    InitTable(t, name, frames);
    std::vector<double> re(kFrameSize), im(kFrameSize);
    for (int f = 0; f < frames; ++f)
    {
        const double pos = frames > 1 ? static_cast<double>(f) / (frames - 1) : 0.0;
        for (int s = 0; s < kFrameSize; ++s)
        {
            re[s] = fn(static_cast<double>(s) / kFrameSize, pos);
            im[s] = 0.0;
        }
        Fft(re, im, false);
        StoreFrame(t, f, re, im);
    }
}

// fixedPhases: each harmonic keeps a random phase, which gives less peaky,
// smoother-sounding frames than all-sine phases.
void BuildFromHarmonics(Table& t, const wchar_t* name, int frames, const HarmFn& fn, unsigned seed = 0)
{
    InitTable(t, name, frames);
    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> dist(0.0, kTwoPi);
    std::vector<double> phases(kBins + 1, 0.0);
    if (seed)
        for (double& p : phases)
            p = dist(rng);
    std::vector<double> re(kFrameSize), im(kFrameSize);
    for (int f = 0; f < frames; ++f)
    {
        const double pos = frames > 1 ? static_cast<double>(f) / (frames - 1) : 0.0;
        std::fill(re.begin(), re.end(), 0.0);
        std::fill(im.begin(), im.end(), 0.0);
        for (int h = 1; h <= MipHarmonics(0); ++h)
        {
            const double a = fn(h, pos);
            // Unit-amplitude sine in the inverse transform: X[h] = -i a e^(i phase).
            re[h] = a * sin(phases[h]);
            im[h] = -a * cos(phases[h]);
        }
        StoreFrame(t, f, re, im);
    }
}

inline double Gauss(double x, double center, double width)
{
    const double d = (x - center) / width;
    return exp(-0.5 * d * d);
}

// A vowel-like spectral envelope: harmonics near the formants stand out.
double FormantEnv(double hz, double f1, double f2, double f3)
{
    return Gauss(hz, f1, 90.0) + 0.7 * Gauss(hz, f2, 140.0) + 0.4 * Gauss(hz, f3, 200.0) + 0.02;
}

void BuildAllTables(std::vector<Table>& tables)
{
    tables.resize(kNumTables);
    int n = 0;

    // Table 0: the waves Wave Crossfade blends between. Order matches ClassicWaveName().
    {
        InitTable(tables[n], L"Classic", kClassicWaves);
        const HarmFn waves[kClassicWaves] = {
            [](int h, double) { return h == 1 ? 1.0 : 0.0; },
            [](int h, double) { return h % 2 ? 1.0 / (h * h) * ((h % 4 == 1) ? 1.0 : -1.0) : 0.0; },
            [](int h, double) { return 1.0 / h; },
            [](int h, double) { return h % 2 ? 1.0 / h : 0.0; },
            [](int h, double) { return sin(kPi * h * 0.25) / h; },
            [](int h, double) { return sin(kPi * h * 0.10) / h; },
            [](int h, double) { return h % 3 ? 1.0 / pow(h, 1.5) : 0.0; },
            [](int h, double) { return (h == 1 || h == 2 || h == 3 || h == 4 || h == 6 || h == 8) ? 1.0 / sqrt(h) : 0.0; },
        };
        std::vector<double> re(kFrameSize), im(kFrameSize);
        for (int f = 0; f < kClassicWaves; ++f)
        {
            std::fill(re.begin(), re.end(), 0.0);
            std::fill(im.begin(), im.end(), 0.0);
            for (int h = 1; h <= MipHarmonics(0); ++h)
                im[h] = -waves[f](h, 0.0);
            StoreFrame(tables[n], f, re, im);
        }
        ++n;
    }

    static constexpr int kFrames = 16;
    BuildFromHarmonics(tables[n++], L"Harmonic Sweep", kFrames, [](int h, double p)
        { return exp(-static_cast<double>(h - 1) / (0.4 + 40.0 * p * p)) / sqrt(h); });
    BuildFromHarmonics(tables[n++], L"Odd Sweep", kFrames, [](int h, double p)
        { return (h % 2 ? 1.0 : 0.05) * exp(-static_cast<double>(h - 1) / (0.5 + 30.0 * p * p)) / sqrt(h); });
    BuildFromTime(tables[n++], L"PWM", kFrames, [](double ph, double p)
        { return ph < 0.5 - 0.46 * p ? 1.0 : -1.0; });
    BuildFromHarmonics(tables[n++], L"Vowels", kFrames, [](int h, double p)
        {
            // Sweeps ah -> eh -> ee -> oh -> oo at a nominal 110 Hz fundamental.
            static const double f[5][3] = { {800,1150,2900}, {400,1600,2700}, {280,2250,2890}, {450,800,2830}, {325,700,2700} };
            const double x = p * 3.999;
            const int i = static_cast<int>(x);
            const double w = x - i;
            const double hz = h * 110.0;
            return FormantEnv(hz, f[i][0] * (1 - w) + f[i + 1][0] * w, f[i][1] * (1 - w) + f[i + 1][1] * w, f[i][2] * (1 - w) + f[i + 1][2] * w) / pow(h, 0.3);
        }, 11);
    BuildFromHarmonics(tables[n++], L"Choir", kFrames, [](int h, double p)
        {
            const double hz = h * 110.0;
            return (FormantEnv(hz, 500 + 200 * p, 1050 + 500 * p, 2600) + 0.15 * Gauss(hz, 3400, 400)) / pow(h, 0.25);
        }, 12);
    BuildFromHarmonics(tables[n++], L"Organ", kFrames, [](int h, double p)
        {
            static const int bars[] = { 1, 2, 3, 4, 6, 8, 10, 12, 16 };
            double a = 0.0;
            for (int i = 0; i < 9; ++i)
                if (h == bars[i])
                {
                    const double on = (std::min)(1.0, (std::max)(0.0, p * 9.0 - i + 1.0));
                    a = on / sqrt(h);
                }
            return a;
        });
    BuildFromHarmonics(tables[n++], L"Bell", kFrames, [](int h, double p)
        {
            static const int partials[] = { 1, 3, 5, 7, 11, 13, 19, 23, 29, 37, 41 };
            double a = 0.0;
            for (int i = 0; i < 11; ++i)
                if (h == partials[i])
                    a = exp(-i * (1.2 - p)) * (1.0 + 0.5 * sin(p * 9.0 + i));
            return a;
        }, 13);
    BuildFromHarmonics(tables[n++], L"Glass", kFrames, [](int h, double p)
        {
            const double center = 4.0 + 60.0 * p;
            return Gauss(h, center, 3.0 + 6.0 * p) + (h == 1 ? 0.6 : 0.0);
        }, 14);
    BuildFromHarmonics(tables[n++], L"Spectral Comb", kFrames, [](int h, double p)
        {
            const double comb = 0.5 + 0.5 * cos(kTwoPi * h * (0.02 + 0.2 * p));
            return comb * comb / pow(h, 0.7);
        }, 15);
    BuildFromTime(tables[n++], L"FM Sweep", kFrames, [](double ph, double p)
        { return sin(kTwoPi * ph + (0.2 + 7.0 * p * p) * sin(kTwoPi * 2.0 * ph)); });
    BuildFromTime(tables[n++], L"Sync Sweep", kFrames, [](double ph, double p)
        {
            const double r = 1.0 + 7.0 * p;
            return (1.0 - ph) * sin(kTwoPi * fmod(ph * r, 1.0));
        });
    BuildFromTime(tables[n++], L"Fold Sweep", kFrames, [](double ph, double p)
        { return sin(kPi * (0.5 + 6.0 * p) * sin(kTwoPi * ph)); });
    BuildFromTime(tables[n++], L"Phase Bend", kFrames, [](double ph, double p)
        {
            const double k = 0.5 - 0.47 * p;
            const double warped = ph < k ? 0.5 * ph / k : 0.5 + 0.5 * (ph - k) / (1.0 - k);
            return cos(kTwoPi * warped);
        });
    BuildFromHarmonics(tables[n++], L"Tanpura", kFrames, [](int h, double p)
        {
            // A formant travelling up the harmonics, like the buzz of a jawari bridge.
            const double center = 6.0 + 40.0 * p;
            return 1.0 / pow(h, 0.9) + 0.5 * Gauss(h, center, 5.0);
        }, 16);
    BuildFromHarmonics(tables[n++], L"Reed", kFrames, [](int h, double p)
        { return (h % 2 ? 1.0 : 0.25 + 0.6 * p) * (1.0 + 2.0 * Gauss(h, 9.0 + 12.0 * p, 3.0)) / h; });
    BuildFromHarmonics(tables[n++], L"Brass", kFrames, [](int h, double p)
        { return exp(-static_cast<double>(h) / (2.0 + 16.0 * p)) * pow(h, 0.5); });
    {
        // Each frame draws its own harmonic levels from a smooth random walk.
        std::mt19937 rng(17);
        std::uniform_real_distribution<double> d(0.0, 1.0);
        std::vector<std::vector<double>> levels(kFrames, std::vector<double>(kBins + 1));
        for (auto& frame : levels)
            for (double& v : frame)
                v = d(rng);
        BuildFromHarmonics(tables[n++], L"Noise Spectrum", kFrames, [levels](int h, double p)
            {
                const double x = p * (kFrames - 1);
                const int i = static_cast<int>(x);
                const int j = i + 1 < kFrames ? i + 1 : i;
                const double v = levels[i][h] + (levels[j][h] - levels[i][h]) * (x - i);
                return v / pow(h, 0.8);
            }, 18);
    }
    BuildFromTime(tables[n++], L"Stepped", kFrames, [](double ph, double p)
        {
            const double steps = 32.0 - 28.0 * p;
            const double q = floor(ph * steps) / steps;
            return sin(kTwoPi * q) + 0.4 * sin(kTwoPi * 3.0 * q);
        });
    BuildFromHarmonics(tables[n++], L"Warm Saw", kFrames, [](int h, double p)
        { return exp(-static_cast<double>(h) / (6.0 + 90.0 * p * p)) / h; });
}

std::vector<float> BuildPad(double bandwidthCents, unsigned seed)
{
    const int n = kPadSize;
    std::vector<double> amp(n / 2 + 1, 0.0);
    const double binHz = kPadBaseRate / n;
    for (int h = 1; h <= 96; ++h)
    {
        const double hz = kPadBaseHz * h;
        const double a = (1.0 / pow(h, 0.85)) * (0.45 + 0.55 * pow(sin(h * 0.61), 2.0));
        const double widthHz = (pow(2.0, bandwidthCents / 1200.0) - 1.0) * hz;
        const double widthBins = (std::max)(widthHz / binHz, 0.5);
        const int center = static_cast<int>(hz / binHz);
        const int span = static_cast<int>(widthBins * 4.0) + 1;
        const double norm = a / sqrt(widthBins);
        for (int b = (std::max)(1, center - span); b <= center + span && b < n / 2; ++b)
        {
            const double d = (b - hz / binHz) / widthBins;
            amp[b] += norm * exp(-0.5 * d * d);
        }
    }
    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> phase(0.0, kTwoPi);
    std::vector<double> re(n, 0.0), im(n, 0.0);
    for (int b = 1; b < n / 2; ++b)
    {
        const double p = phase(rng);
        re[b] = amp[b] * cos(p);       im[b] = amp[b] * sin(p);
        re[n - b] = re[b];             im[n - b] = -im[b];
    }
    Fft(re, im, true);
    double energy = 0.0;
    for (double v : re)
        energy += v * v;
    const double scale = 0.3 / sqrt(energy / n + 1e-12);
    std::vector<float> out(static_cast<size_t>(n) + 1);
    for (int s = 0; s < n; ++s)
        out[s] = static_cast<float>(re[s] * scale);
    out[n] = out[0];
    return out;
}

struct Bank
{
    std::vector<Table> tables;
    std::vector<float> pads[kPadTables];

    Bank()
    {
        for (int i = 0; i <= 4096; ++i)
            g_sine[i] = static_cast<float>(sin(kTwoPi * i / 4096.0));
        BuildAllTables(tables);
        static const double widths[kPadTables] = { 6.0, 22.0, 70.0, 200.0 };
        for (int i = 0; i < kPadTables; ++i)
            pads[i] = BuildPad(widths[i], 100u + i);
    }
};

const Bank& TheBank()
{
    static const Bank bank;
    return bank;
}
} // namespace

const float* Table::Mip(int frame, int mip) const
{
    static constexpr int offsets[kNumMips] = { MipOffset(0), MipOffset(1), MipOffset(2), MipOffset(3), MipOffset(4),
                                           MipOffset(5), MipOffset(6), MipOffset(7), MipOffset(8), MipOffset(9) };
    return data.data() + frame * frameStride + offsets[mip];
}

const Table& Get(int index) { return TheBank().tables[index]; }
const wchar_t* TableName(int index) { return TheBank().tables[index].name; }
const float* Pad(int index) { return TheBank().pads[index].data(); }
void EnsureBuilt() { TheBank(); }

const wchar_t* ClassicWaveName(int index)
{
    static const wchar_t* const names[kClassicWaves] = { L"Sine", L"Triangle", L"Saw", L"Square", L"Pulse", L"Thin Pulse", L"Hollow", L"Organ" };
    return names[index];
}

MipBlend ChooseMips(float hz, float sampleRate)
{
    MipBlend b;
    const float nyquist = sampleRate * 0.5f;
    const float exact = log2f((std::max)(hz, 1.0f) * MipHarmonics(0) / nyquist);
    if (exact <= -1.0f)
        return b;   // every harmonic of mip 0 is below Nyquist, even after an octave of margin
    const int m = static_cast<int>(floorf(exact)) + 1;
    b.m0 = (std::min)(m, kNumMips - 1);
    b.m1 = (std::min)(m + 1, kNumMips - 1);
    b.w = b.m1 != b.m0 ? exact - floorf(exact) : 0.0f;
    return b;
}

void Fft(std::vector<double>& re, std::vector<double>& im, bool inverse)
{
    const int n = static_cast<int>(re.size());
    for (int i = 1, j = 0; i < n; ++i)
    {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
        {
            std::swap(re[i], re[j]);
            std::swap(im[i], im[j]);
        }
    }
    for (int len = 2; len <= n; len <<= 1)
    {
        const double angle = (inverse ? 1.0 : -1.0) * kTwoPi / len;
        const double wr = cos(angle), wi = sin(angle);
        const int half = len / 2;
        for (int start = 0; start < n; start += len)
        {
            double cr = 1.0, ci = 0.0;
            for (int k = 0; k < half; ++k)
            {
                const int a = start + k, b = a + half;
                const double tr = re[b] * cr - im[b] * ci;
                const double ti = re[b] * ci + im[b] * cr;
                re[b] = re[a] - tr;  im[b] = im[a] - ti;
                re[a] += tr;         im[a] += ti;
                const double next = cr * wr - ci * wi;
                ci = cr * wi + ci * wr;
                cr = next;
            }
        }
    }
}
} // namespace wavetables
