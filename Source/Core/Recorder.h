// Recorder.h - records the synth output to 24-bit stereo WAV files.
//
// The audio thread pushes samples into a lock-free single-producer /
// single-consumer ring; a writer thread drains it to disk, so a recording keeps
// going when a plugin's editor is closed. Long drones are split into parts just
// before a WAV file reaches its 4 GB limit.
#pragma once

#include <atomic>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

class Recorder
{
public:
    Recorder();
    ~Recorder();
    Recorder(const Recorder&) = delete;
    Recorder& operator=(const Recorder&) = delete;

    // Audio thread.
    void Push(const float* stereo, int frames);
    void SetSampleRate(unsigned rate) { m_sampleRate.store(rate); }

    // UI thread.
    void Start(const std::wstring& path);   // the file is created once the sample rate is known
    bool Stop();                            // drains, finalizes the header; false on write error
    bool IsRecording() const { return m_armed.load(); }
    double Seconds() const;
    const std::wstring& Path() const { return m_path; }
    bool HasError() const { return m_error.load(); }
    unsigned long long DroppedFrames() const { return m_dropped.load(); }

    static std::wstring DefaultFolder();
    static std::wstring MakeFileName(const std::wstring& patchName);

private:
    void WriterLoop();
    void Drain();
    bool OpenFile();
    void CloseFile();
    void WriteHeader();

    std::vector<float> m_ring;
    std::atomic<size_t> m_write{ 0 }, m_read{ 0 };
    std::atomic<bool> m_armed{ false };
    std::atomic<bool> m_stopWriter{ false };
    std::atomic<unsigned> m_sampleRate{ 0 };
    std::atomic<unsigned long long> m_dropped{ 0 };
    std::atomic<unsigned long long> m_totalFrames{ 0 };
    std::atomic<unsigned> m_fileRate{ 0 };
    std::atomic<bool> m_error{ false };

    std::thread m_writer;
    FILE* m_file = nullptr;
    std::wstring m_path;
    int m_part = 1;
    unsigned long long m_fileFrames = 0;
    std::vector<unsigned char> m_scratch;
};
