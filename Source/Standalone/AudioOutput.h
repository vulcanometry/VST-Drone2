// AudioOutput.h - WASAPI shared-mode output to the default render device.
#pragma once

#include "PlatformWin.h"

#include <atomic>
#include <string>
#include <thread>

class SynthEngine;
class Recorder;

// Posted to the notify window when the audio stream fails (e.g. device removed).
constexpr UINT WM_AUDIO_ERROR = WM_APP + 1;

class AudioOutput
{
public:
    AudioOutput() = default;
    ~AudioOutput();
    AudioOutput(const AudioOutput&) = delete;
    AudioOutput& operator=(const AudioOutput&) = delete;

    void Start(SynthEngine* engine, Recorder* recorder, HWND notifyWnd);
    void Stop();   // fades out, then stops the stream and joins the thread
    bool IsRunning() const { return m_thread.joinable(); }
    std::wstring GetLastError() const { return m_error; }

private:
    void ThreadProc();
    void Fail(const wchar_t* what, HRESULT hr);

    SynthEngine* m_engine = nullptr;
    Recorder* m_recorder = nullptr;
    HWND m_notify = nullptr;
    std::thread m_thread;
    std::atomic<bool> m_stopRequested{ false };
    std::wstring m_error;
};
