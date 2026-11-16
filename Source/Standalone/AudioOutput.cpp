// AudioOutput.cpp - WASAPI shared-mode output to the default render device.
#include "AudioOutput.h"
#include "SynthEngine.h"
#include "Recorder.h"

#include <mmdeviceapi.h>
#include <audioclient.h>
#include <avrt.h>
#include <mmreg.h>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace
{
const GUID kSubtypeFloat = { 0x00000003, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };
const GUID kSubtypePcm   = { 0x00000001, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };

enum class SampleFormat { Unsupported, Float32, Int16, Int24, Int32 };

SampleFormat DetectFormat(const WAVEFORMATEX* wfx)
{
    bool isFloat = wfx->wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
    bool isPcm = wfx->wFormatTag == WAVE_FORMAT_PCM;
    if (wfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
    {
        const auto* ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(wfx);
        isFloat = IsEqualGUID(ext->SubFormat, kSubtypeFloat) != FALSE;
        isPcm = IsEqualGUID(ext->SubFormat, kSubtypePcm) != FALSE;
    }
    if (isFloat && wfx->wBitsPerSample == 32) return SampleFormat::Float32;
    if (isPcm)
    {
        switch (wfx->wBitsPerSample)
        {
        case 16: return SampleFormat::Int16;
        case 24: return SampleFormat::Int24;
        case 32: return SampleFormat::Int32;
        }
    }
    return SampleFormat::Unsupported;
}

void WriteSample(BYTE*& p, SampleFormat fmt, float v)
{
    v = v > 1.0f ? 1.0f : (v < -1.0f ? -1.0f : v);
    switch (fmt)
    {
    case SampleFormat::Float32:
        *reinterpret_cast<float*>(p) = v; p += 4; break;
    case SampleFormat::Int16:
        *reinterpret_cast<int16_t*>(p) = static_cast<int16_t>(v * 32767.0f); p += 2; break;
    case SampleFormat::Int24:
    {
        const int32_t s = static_cast<int32_t>(v * 8388607.0f);
        p[0] = static_cast<BYTE>(s); p[1] = static_cast<BYTE>(s >> 8); p[2] = static_cast<BYTE>(s >> 16);
        p += 3; break;
    }
    case SampleFormat::Int32:
        *reinterpret_cast<int32_t*>(p) = static_cast<int32_t>(static_cast<double>(v) * 2147483647.0); p += 4; break;
    default: break;
    }
}

template <class T> void SafeRelease(T*& p)
{
    if (p) { p->Release(); p = nullptr; }
}
} // namespace

AudioOutput::~AudioOutput()
{
    Stop();
}

void AudioOutput::Start(SynthEngine* engine, Recorder* recorder, HWND notifyWnd)
{
    Stop();
    m_engine = engine;
    m_recorder = recorder;
    m_notify = notifyWnd;
    m_error.clear();
    m_stopRequested = false;
    m_thread = std::thread(&AudioOutput::ThreadProc, this);
}

void AudioOutput::Stop()
{
    if (m_thread.joinable())
    {
        m_stopRequested = true;
        m_thread.join();
    }
}

void AudioOutput::Fail(const wchar_t* what, HRESULT hr)
{
    wchar_t buf[256];
    swprintf_s(buf, L"%s failed (HRESULT 0x%08X).", what, static_cast<unsigned>(hr));
    m_error = buf;
    if (m_notify)
        ::PostMessage(m_notify, WM_AUDIO_ERROR, 0, 0);
}

void AudioOutput::ThreadProc()
{
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool comInit = SUCCEEDED(hr);

    DWORD taskIndex = 0;
    HANDLE mmcss = AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIndex);

    IMMDeviceEnumerator* enumerator = nullptr;
    IMMDevice* device = nullptr;
    IAudioClient* client = nullptr;
    IAudioRenderClient* render = nullptr;
    WAVEFORMATEX* wfx = nullptr;
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    bool started = false;

    do
    {
        hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                              __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&enumerator));
        if (FAILED(hr)) { Fail(L"Creating the audio device enumerator", hr); break; }

        hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
        if (FAILED(hr)) { Fail(L"Opening the default audio device", hr); break; }

        hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&client));
        if (FAILED(hr)) { Fail(L"Activating the audio client", hr); break; }

        hr = client->GetMixFormat(&wfx);
        if (FAILED(hr)) { Fail(L"Querying the device mix format", hr); break; }

        const SampleFormat fmt = DetectFormat(wfx);
        if (fmt == SampleFormat::Unsupported) { Fail(L"Using the device's sample format", E_FAIL); break; }

        constexpr REFERENCE_TIME kBufferDuration = 200000;   // 20 ms
        hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                kBufferDuration, 0, wfx, nullptr);
        if (FAILED(hr)) { Fail(L"Initializing the audio stream", hr); break; }

        hr = client->SetEventHandle(event);
        if (FAILED(hr)) { Fail(L"Setting the audio event handle", hr); break; }

        UINT32 bufferFrames = 0;
        hr = client->GetBufferSize(&bufferFrames);
        if (FAILED(hr)) { Fail(L"Querying the audio buffer size", hr); break; }

        hr = client->GetService(__uuidof(IAudioRenderClient), reinterpret_cast<void**>(&render));
        if (FAILED(hr)) { Fail(L"Getting the audio render client", hr); break; }

        const int channels = wfx->nChannels;
        std::vector<float> stereo(static_cast<size_t>(bufferFrames) * 2);

        m_engine->Prepare(static_cast<double>(wfx->nSamplesPerSec));
        m_engine->StartFadeIn();
        if (m_recorder)
            m_recorder->SetSampleRate(wfx->nSamplesPerSec);

        auto fill = [&](UINT32 frames) -> HRESULT
        {
            BYTE* data = nullptr;
            HRESULT r = render->GetBuffer(frames, &data);
            if (FAILED(r)) return r;
            m_engine->Process(stereo.data(), static_cast<int>(frames));
            if (m_recorder)
                m_recorder->Push(stereo.data(), static_cast<int>(frames));
            BYTE* p = data;
            for (UINT32 i = 0; i < frames; ++i)
            {
                const float L = stereo[2 * i], R = stereo[2 * i + 1];
                if (channels == 1)
                    WriteSample(p, fmt, 0.5f * (L + R));
                else
                    for (int c = 0; c < channels; ++c)
                        WriteSample(p, fmt, (c & 1) ? R : L);
            }
            return render->ReleaseBuffer(frames, 0);
        };

        hr = fill(bufferFrames);
        if (FAILED(hr)) { Fail(L"Writing the audio buffer", hr); break; }

        hr = client->Start();
        if (FAILED(hr)) { Fail(L"Starting the audio stream", hr); break; }
        started = true;

        bool fading = false;
        ULONGLONG deadline = 0;
        for (;;)
        {
            const DWORD wait = WaitForSingleObject(event, 200);

            if (m_stopRequested && !fading)
            {
                m_engine->StartFadeOut();
                fading = true;
                deadline = GetTickCount64() + 300;
            }
            if (fading && (m_engine->IsFadedOut() || GetTickCount64() > deadline))
                break;
            if (wait != WAIT_OBJECT_0)
                continue;

            UINT32 padding = 0;
            hr = client->GetCurrentPadding(&padding);
            if (FAILED(hr)) { Fail(L"Audio device", hr); break; }
            if (padding < bufferFrames)
            {
                hr = fill(bufferFrames - padding);
                if (FAILED(hr)) { Fail(L"Audio device", hr); break; }
            }
        }

        // Let the faded-out tail that is still queued play out before stopping.
        if (SUCCEEDED(hr))
        {
            const ULONGLONG drainUntil = GetTickCount64() + 100;
            UINT32 padding = 0;
            while (GetTickCount64() < drainUntil && SUCCEEDED(client->GetCurrentPadding(&padding)) && padding > 0)
                WaitForSingleObject(event, 20);
        }
    } while (false);

    if (m_recorder)
        m_recorder->SetSampleRate(0);
    if (started)
        client->Stop();
    if (wfx)
        CoTaskMemFree(wfx);
    SafeRelease(render);
    SafeRelease(client);
    SafeRelease(device);
    SafeRelease(enumerator);
    if (event)
        CloseHandle(event);
    if (mmcss)
        AvRevertMmThreadCharacteristics(mmcss);
    if (comInit)
        CoUninitialize();
}
