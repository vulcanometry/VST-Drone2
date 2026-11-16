// Drone2Render.cpp - offline test tool.
//
//   Drone2Render check [wavDir]    render every factory patch and every algorithm, report levels and CPU
//   Drone2Render shots <dir>       save editor screenshots in every theme
//   Drone2Render patches <dir>     test patch save / load / rename and the recorder (uses a temp name)
//
// Exit code 0 means every check passed.
#include "PlatformWin.h"
#include "PanelTheme.h"
#include "PanelView.h"
#include "Recorder.h"
#include "SynthEngine.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>
#include <vector>

namespace
{
constexpr double kRate = 48000.0;

struct Stats
{
    float peak = 0.0f, rms = 0.0f, dc = 0.0f;
    double cpu = 0.0;   // fraction of real time
    bool bad = false;
};

void Apply(SynthEngine& engine, const ParamValues& values)
{
    for (int id = 0; id < P_COUNT; ++id)
        engine.SetParam(id, values[id]);
}

// Renders `seconds`, measuring the final part once the attack has settled.
Stats Render(SynthEngine& engine, double seconds, double settle, std::vector<float>* capture = nullptr, double rate = kRate)
{
    Stats s;
    engine.Prepare(rate);
    engine.StartFadeIn();
    constexpr int kBlock = 256;
    float buffer[kBlock * 2];
    const long long total = static_cast<long long>(seconds * rate);
    const long long measureFrom = static_cast<long long>(settle * rate);
    double sum = 0.0, dc = 0.0;
    long long count = 0;
    const auto t0 = std::chrono::steady_clock::now();
    for (long long done = 0; done < total; done += kBlock)
    {
        engine.Process(buffer, kBlock);
        for (int i = 0; i < kBlock * 2; ++i)
        {
            const float v = buffer[i];
            if (!std::isfinite(v))
                s.bad = true;
            if (done >= measureFrom)
            {
                s.peak = (std::max)(s.peak, fabsf(v));
                sum += static_cast<double>(v) * v;
                dc += v;
                ++count;
            }
        }
        if (capture && done >= measureFrom)
            capture->insert(capture->end(), buffer, buffer + kBlock * 2);
    }
    const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    s.cpu = elapsed / seconds;
    if (count)
    {
        s.rms = static_cast<float>(sqrt(sum / count));
        s.dc = static_cast<float>(dc / count);
    }
    return s;
}

// Fast attack, so that the measurement need not wait for a long fade-in.
ParamValues WithFastAttack(ParamValues v)
{
    v[P_ATTACK] = 0.05f;
    return v;
}

bool StatsOk(const Stats& s)
{
    return !s.bad && s.rms > 0.02f && s.peak <= 1.0f && fabsf(s.dc) < 0.02f;
}

void Report(const wchar_t* group, const std::wstring& name, const Stats& s, int& failures)
{
    const bool ok = StatsOk(s);
    if (!ok)
        ++failures;
    wprintf(L"%-4s %-10s %-22s rms %6.1f dB  peak %6.3f  dc %+.4f  cpu %5.1f%%\n", ok ? L"ok" : L"FAIL", group, name.c_str(),
            20.0 * log10(s.rms + 1e-9), s.peak, s.dc, s.cpu * 100.0);
}

// Fraction of spectral energy above fc, for the aliasing check on a high pitch.
double InharmonicRatio(const std::vector<float>& stereo, double fundamental, double rate)
{
    constexpr int kSize = 1 << 16;
    if (stereo.size() < static_cast<size_t>(kSize) * 2)
        return 1.0;
    std::vector<double> re(kSize), im(kSize, 0.0);
    for (int i = 0; i < kSize; ++i)
        re[i] = stereo[static_cast<size_t>(i) * 2] * (0.5 - 0.5 * cos(2.0 * 3.14159265358979 * i / (kSize - 1)));
    wavetables::Fft(re, im, false);
    const double binHz = rate / kSize;
    double harmonic = 0.0, other = 0.0;
    for (int b = 2; b < kSize / 2; ++b)
    {
        const double power = re[b] * re[b] + im[b] * im[b];
        const double hz = b * binHz;
        const double nearest = floor(hz / fundamental + 0.5) * fundamental;
        if (fabs(hz - nearest) < 40.0)
            harmonic += power;
        else
            other += power;
    }
    return other / (harmonic + other + 1e-20);
}

// Layer A alone with a plain dry signal path.
ParamValues DryLayer(int algo)
{
    ParamValues v = WithFastAttack(PresetBank::Defaults());
    v[LayerId(0, LP_ALGO)] = params::FromStep(ALG_COUNT, algo);
    v[LayerId(1, LP_ON)] = 0.0f;
    v[P_VEC_X] = 0.0f;
    v[P_VEC_Y] = 0.0f;
    v[P_ORBIT_DEPTH] = 0.0f;
    v[P_REV_MIX] = 0.0f;
    v[P_DLY_MIX] = 0.0f;
    v[P_ENS_MIX] = 0.0f;
    v[P_FLT_MODE] = 0.0f;
    return v;
}

void WriteWav(const std::wstring& path, const std::vector<float>& stereo, unsigned rate)
{
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"wb") != 0 || !f)
        return;
    const unsigned bytes = static_cast<unsigned>(stereo.size() * 4);
    const unsigned riff = 36 + bytes, sixteen = 16, byteRate = rate * 8;
    const unsigned short fmt = 3, channels = 2, align = 8, bits = 32;
    fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); fwrite(&sixteen, 4, 1, f);
    fwrite(&fmt, 2, 1, f); fwrite(&channels, 2, 1, f); fwrite(&rate, 4, 1, f); fwrite(&byteRate, 4, 1, f);
    fwrite(&align, 2, 1, f); fwrite(&bits, 2, 1, f); fwrite("data", 1, 4, f); fwrite(&bytes, 4, 1, f);
    fwrite(stereo.data(), 4, stereo.size(), f);
    fclose(f);
}

int RunCheck(const wchar_t* wavDir)
{
    int failures = 0;
    SynthEngine engine;

    wprintf(L"--- every algorithm, with every table it can use at 3 pitches\n");
    for (int a = 0; a < ALG_COUNT; ++a)
    {
        ParamValues v = DryLayer(a);
        Apply(engine, v);
        Report(L"algo", params::AlgorithmName(a), Render(engine, 3.0, 1.5), failures);

        // Corners of the three algorithm knobs, full stack, extreme pitches: must never blow up.
        for (int corner = 0; corner < 8; ++corner)
            for (int root : { 12, 48, 84 })
            {
                ParamValues c = v;
                c[LayerId(0, LP_MORPH)] = corner & 1 ? 1.0f : 0.0f;
                c[LayerId(0, LP_TIMBRE)] = corner & 2 ? 1.0f : 0.0f;
                c[LayerId(0, LP_COLOR)] = corner & 4 ? 1.0f : 0.0f;
                c[LayerId(0, LP_VOICES)] = 1.0f;
                c[P_ROOT] = params::FromStep(85, root - 12);
                Apply(engine, c);
                const Stats s = Render(engine, 0.6, 0.3);
                if (s.bad || s.peak > 1.0f)
                {
                    wprintf(L"FAIL corner %s corner %d root %d: peak %.3f bad %d\n", params::AlgorithmName(a), corner, root, s.peak, s.bad ? 1 : 0);
                    ++failures;
                }
            }
    }

    wprintf(L"--- wavetables\n");
    for (int t = 0; t < wavetables::kNumTables; ++t)
    {
        ParamValues v = DryLayer(ALG_WAVETABLE);
        v[LayerId(0, LP_TABLE)] = params::FromStep(wavetables::kNumTables, t);
        Apply(engine, v);
        Report(L"table", wavetables::TableName(t), Render(engine, 1.5, 0.8), failures);
    }

    wprintf(L"--- aliasing: wave crossfade with a saw at a high pitch, energy off the harmonic series\n");
    {
        ParamValues v = DryLayer(ALG_MORPH);
        v[LayerId(0, LP_TIMBRE)] = 2.0f / 7.0f;
        v[LayerId(0, LP_COLOR)] = 2.0f / 7.0f;
        v[LayerId(0, LP_VOICES)] = 0.0f;
        v[LayerId(0, LP_DRIFT)] = 0.0f;
        v[LayerId(0, LP_SCAN_DEPTH)] = 0.0f;
        v[P_TEMPERAMENT] = 0.0f;
        v[P_ROOT] = params::FromStep(85, 79 - 12);   // G5, about 784 Hz
        Apply(engine, v);
        std::vector<float> capture;
        Render(engine, 3.0, 1.0, &capture);
        const double hz = 440.0 * pow(2.0, (79 - 69) / 12.0);
        const double ratio = InharmonicRatio(capture, hz, kRate);
        const double db = 10.0 * log10(ratio + 1e-20);
        wprintf(L"%-4s inharmonic energy %.1f dB\n", db < -60.0 ? L"ok" : L"FAIL", db);
        if (db >= -60.0)
            ++failures;
    }

    wprintf(L"--- reverb and delay level against the dry signal\n");
    {
        ParamValues dry = DryLayer(ALG_SWARM);
        Apply(engine, dry);
        const float dryRms = Render(engine, 4.0, 2.0).rms;
        for (float decay : { 0.3f, 0.7f, 0.95f })
        {
            ParamValues wet = dry;
            wet[P_REV_MIX] = 1.0f;
            wet[P_REV_DECAY] = decay;
            Apply(engine, wet);
            const float wetRms = Render(engine, 12.0, 8.0).rms;
            const double db = 20.0 * log10((wetRms + 1e-9) / (dryRms + 1e-9));
            const bool ok = fabs(db) < 6.0;
            wprintf(L"%-4s reverb decay %.2f: wet is %+.1f dB against dry\n", ok ? L"ok" : L"FAIL", decay, db);
            if (!ok)
                ++failures;
        }
    }

    wprintf(L"--- factory patches\n");
    PresetBank bank;
    bank.Refresh();
    for (size_t i = 0; i < bank.FactoryCount(); ++i)
    {
        ParamValues v;
        bank.Load(i, v);
        Apply(engine, WithFastAttack(v));
        std::vector<float> capture;
        const Stats s = Render(engine, wavDir ? 14.0 : 8.0, 4.0, wavDir ? &capture : nullptr);
        Report(L"patch", bank.Get(i).name, s, failures);
        if (wavDir)
            WriteWav(std::wstring(wavDir) + L"\\" + bank.Get(i).name + L".wav", capture, static_cast<unsigned>(kRate));
    }

    wprintf(L"--- random patches\n");
    for (unsigned seed = 1; seed <= 12; ++seed)
    {
        Apply(engine, WithFastAttack(PresetBank::Randomize(seed)));
        Report(L"random", std::to_wstring(seed), Render(engine, 5.0, 2.5), failures);
    }

    wprintf(L"--- other sample rates, shimmer and freeze stability, worst-case CPU\n");
    {
        ParamValues v = PresetBank::Defaults();
        for (int l = 0; l < kNumLayers; ++l)
        {
            v[LayerId(l, LP_ON)] = 1.0f;
            v[LayerId(l, LP_ALGO)] = params::FromStep(ALG_COUNT, ALG_WAVETABLE);
            v[LayerId(l, LP_TABLE)] = params::FromStep(wavetables::kNumTables, 5);
            v[LayerId(l, LP_VOICES)] = 1.0f;
        }
        v[P_ORBIT_DEPTH] = 1.0f;
        v[P_ENS_MIX] = 0.5f;
        v[P_PHS_MIX] = 0.5f;
        v[P_DLY_MIX] = 0.4f;
        v[P_REV_MIX] = 0.6f;
        v[P_REV_SHIMMER] = 0.7f;
        Apply(engine, WithFastAttack(v));
        for (double rate : { 44100.0, 96000.0, 192000.0 })
            Report(L"rate", std::to_wstring(static_cast<int>(rate)) + L" Hz", Render(engine, 2.0, 1.0, nullptr, rate), failures);

        v[P_REV_SHIMMER] = 1.0f;
        v[P_REV_DECAY] = 1.0f;
        v[P_REV_SIZE] = 1.0f;
        v[P_REV_MIX] = 1.0f;
        v[P_DLY_FB] = 1.0f;
        Apply(engine, WithFastAttack(v));
        Report(L"stress", L"max shimmer, 8 voices x4", Render(engine, 60.0, 20.0), failures);

        // Freeze holds the tail: the level must stay audible and not grow after the input stops.
        ParamValues freeze = v;
        freeze[P_REV_SHIMMER] = 0.0f;
        freeze[P_DLY_MIX] = 0.0f;
        Apply(engine, freeze);
        Render(engine, 8.0, 4.0);
        engine.SetParam(P_REV_FREEZE, 1.0f);
        constexpr int kBlock = 256;
        float buffer[kBlock * 2];
        auto level = [&](double seconds)
        {
            double sum = 0.0;
            const int blocks = static_cast<int>(seconds * kRate / kBlock);
            for (int b = 0; b < blocks; ++b)
            {
                engine.Process(buffer, kBlock);
                for (float x : buffer)
                    sum += static_cast<double>(x) * x;
            }
            return sqrt(sum / (blocks * kBlock * 2.0));
        };
        level(1.0);
        const double a = level(4.0);
        level(20.0);
        const double b = level(4.0);
        const double change = 20.0 * log10((b + 1e-12) / (a + 1e-12));
        const bool ok = a > 0.003 && fabs(change) < 6.0;
        wprintf(L"%-4s freeze: rms %.1f dB, change over 24 s %+.1f dB\n", ok ? L"ok" : L"FAIL", 20.0 * log10(a + 1e-12), change);
        if (!ok)
            ++failures;
    }

    wprintf(L"\n%d failure(s)\n", failures);
    return failures ? 1 : 0;
}

class NullHost : public PanelHost
{
public:
    float GetParam(int id) override { return engine.GetParam(id); }
    void BeginEdit(int) override {}
    void SetParam(int id, float v) override { engine.SetParam(id, v); }
    void EndEdit(int) override {}
    bool IsPowered() override { return true; }
    void SetPowered(bool) override {}
    SynthEngine* Engine() override { return &engine; }
    Recorder* GetRecorder() override { return nullptr; }
    std::wstring MidiInputName() override { return L"none"; }
    std::wstring CycleMidiInput() override { return L""; }
    std::wstring PatchName() override { return L"Four Corners"; }
    void SetPatchName(const std::wstring&) override {}
    SynthEngine engine;
};

int RunShots(const wchar_t* dir)
{
    NullHost host;
    PresetBank bank;
    bank.Refresh();
    ParamValues v;
    const int index = bank.IndexOfName(L"Four Corners");
    if (index >= 0 && bank.Load(static_cast<size_t>(index), v))
        Apply(host.engine, WithFastAttack(v));
    float* buffer = new float[8192];
    for (int i = 0; i < 400; ++i)
        host.engine.Process(buffer, 4096);
    delete[] buffer;

    const int saved = theme::Current();
    PanelView panel(host);
    HWND frame = CreateWindowExW(0, L"STATIC", L"", WS_POPUP, 0, 0, 100, 100, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!panel.Create(frame, 0, 0, 1.0f))
        return 1;
    int result = 0;
    for (int t = 0; t < theme::Count(); ++t)
    {
        theme::Apply(t);
        for (int page = 0; page < 2; ++page)
        {
            const std::wstring path = std::wstring(dir) + L"\\" + theme::Name(t) + (page ? L"_mod.png" : L"_fx.png");
            if (!panel.SaveScreenshot(path, page))
                result = 1;
        }
    }
    theme::Apply(saved);
    panel.Destroy();
    DestroyWindow(frame);
    return result;
}

int RunPatchTest(const wchar_t* dir)
{
    int failures = 0;
    auto check = [&failures](bool ok, const wchar_t* what)
    {
        wprintf(L"%-4s %s\n", ok ? L"ok" : L"FAIL", what);
        if (!ok) ++failures;
    };

    // Round trip through the text format, in a temp file rather than the user's library.
    ParamValues in = PresetBank::Randomize(4242);
    const std::wstring path = std::wstring(dir) + L"\\roundtrip.d2patch";
    check(PresetBank::WriteTextFile(path, PresetBank::Serialize(in)), L"write patch file");
    std::string text;
    check(PresetBank::ReadTextFile(path, text), L"read patch file");
    ParamValues out;
    PresetBank::Parse(text, out);
    bool same = true;
    for (int id = 0; id < P_COUNT; ++id)
        if (fabsf(in[id] - out[id]) > 1e-4f)
        {
            same = false;
            wprintf(L"     differs: %S %f %f\n", params::Info(id).key, in[id], out[id]);
        }
    check(same, L"every parameter survives a save and load");

    ParamValues partial;
    PresetBank::Parse("; comment\nroot=0.5\nbogus.key=3\nnot a line\na.level=5\n", partial);
    check(fabsf(partial[LayerId(0, LP_LEVEL)] - 1.0f) < 1e-6f, L"out-of-range values are clamped");
    check(fabsf(partial[P_FLT_CUTOFF] - params::Info(P_FLT_CUTOFF).defaultValue) < 1e-6f, L"missing keys take defaults");

    check(PresetBank::IsValidName(L"My Drone (take 2)"), L"accepts a normal name");
    for (const wchar_t* bad : { L"", L"a/b", L"a:b", L"CON", L"com1", L"trailing.", L" lead", L"what?" })
        check(!PresetBank::IsValidName(bad), (std::wstring(L"rejects the name \"") + bad + L"\"").c_str());

    PresetBank bank;
    bank.Refresh();
    int unique = 0;
    for (size_t i = 0; i < bank.FactoryCount(); ++i)
    {
        ParamValues v;
        unique += bank.Load(i, v) ? 1 : 0;
    }
    check(unique == static_cast<int>(bank.FactoryCount()), L"every factory patch loads");

    // Recorder: run the engine into it, on the same threads the real hosts use.
    {
        SynthEngine engine;
        engine.Prepare(kRate);
        Recorder recorder;
        recorder.SetSampleRate(static_cast<unsigned>(kRate));
        const std::wstring wav = std::wstring(dir) + L"\\recording.wav";
        recorder.Start(wav);
        check(recorder.IsRecording(), L"recorder armed");
        constexpr int kBlock = 480, kBlocks = 200;   // two seconds
        float buffer[kBlock * 2];
        for (int b = 0; b < kBlocks; ++b)
        {
            engine.Process(buffer, kBlock);
            recorder.Push(buffer, kBlock);
            if (b % 20 == 0)
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        check(recorder.Stop(), L"recorder stops without error");
        check(recorder.DroppedFrames() == 0, L"no frames dropped");
        std::string data;
        const bool read = PresetBank::ReadTextFile(wav, data);
        const size_t expected = 44 + static_cast<size_t>(kBlock) * kBlocks * 6;
        check(read && data.size() == expected, L"WAV file has the expected size");
        unsigned dataBytes = 0, rate = 0;
        unsigned short bits = 0, channels = 0;
        if (data.size() >= 44)
        {
            memcpy(&channels, data.data() + 22, 2);
            memcpy(&rate, data.data() + 24, 4);
            memcpy(&bits, data.data() + 34, 2);
            memcpy(&dataBytes, data.data() + 40, 4);
        }
        check(channels == 2 && rate == 48000 && bits == 24 && dataBytes == expected - 44, L"WAV header is correct");
    }

    wprintf(L"\n%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
} // namespace

int wmain(int argc, wchar_t** argv)
{
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const std::wstring mode = argc > 1 ? argv[1] : L"check";
    if (mode == L"check")
        return RunCheck(argc > 2 ? argv[2] : nullptr);
    if (mode == L"shots" && argc > 2)
        return RunShots(argv[2]);
    if (mode == L"patches" && argc > 2)
        return RunPatchTest(argv[2]);
    wprintf(L"usage: Drone2Render check [wavDir] | shots <dir> | patches <dir>\n");
    return 2;
}
