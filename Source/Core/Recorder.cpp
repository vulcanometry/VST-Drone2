// Recorder.cpp - records the synth output to 24-bit stereo WAV files.
#include "PlatformWin.h"
#include "Recorder.h"

#include <ShlObj.h>
#include <chrono>
#include <cmath>
#include <cstring>

namespace
{
constexpr size_t kRingFloats = 1u << 21;   // ~20 s of stereo audio at 48 kHz
constexpr unsigned kChannels = 2, kBytesPerSample = 3;
constexpr unsigned long long kMaxDataBytes = 0xFF000000ull;   // just under the WAV limit

void Put32(unsigned char* p, uint32_t v) { p[0] = BYTE(v); p[1] = BYTE(v >> 8); p[2] = BYTE(v >> 16); p[3] = BYTE(v >> 24); }
void Put16(unsigned char* p, uint16_t v) { p[0] = BYTE(v); p[1] = BYTE(v >> 8); }
}

Recorder::Recorder() : m_ring(kRingFloats, 0.0f)
{
}

Recorder::~Recorder()
{
    Stop();
}

std::wstring Recorder::DefaultFolder()
{
    std::wstring folder;
    PWSTR docs = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &docs)))
    {
        folder = docs;
        CoTaskMemFree(docs);
    }
    folder += L"\\Drone 2 Recordings";
    CreateDirectoryW(folder.c_str(), nullptr);
    return folder;
}

std::wstring Recorder::MakeFileName(const std::wstring& patchName)
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t stamp[32];
    swprintf_s(stamp, L"%04u-%02u-%02u_%02u-%02u-%02u", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    std::wstring safe;
    for (wchar_t c : patchName)
        if (!wcschr(L"<>:\"/\\|?*", c) && c >= 32)
            safe += c;
    return (safe.empty() ? std::wstring(L"Drone 2") : safe) + L"_" + stamp + L".wav";
}

void Recorder::Push(const float* stereo, int frames)
{
    if (!m_armed.load(std::memory_order_acquire))
        return;
    const size_t w = m_write.load(std::memory_order_relaxed);
    const size_t r = m_read.load(std::memory_order_acquire);
    const size_t wanted = static_cast<size_t>(frames) * kChannels;
    const size_t count = (std::min)(wanted, kRingFloats - (w - r)) & ~size_t(1);
    for (size_t i = 0; i < count; ++i)
        m_ring[(w + i) & (kRingFloats - 1)] = stereo[i];
    m_write.store(w + count, std::memory_order_release);
    if (count < wanted)
        m_dropped.fetch_add((wanted - count) / kChannels, std::memory_order_relaxed);
}

void Recorder::Start(const std::wstring& path)
{
    Stop();
    m_path = path;
    m_part = 1;
    m_fileFrames = 0;
    m_totalFrames = 0;
    m_dropped = 0;
    m_fileRate = 0;
    m_error = false;
    m_read.store(m_write.load());   // discard anything stale
    m_stopWriter = false;
    m_armed.store(true, std::memory_order_release);
    m_writer = std::thread([this] { WriterLoop(); });
}

double Recorder::Seconds() const
{
    const unsigned rate = m_fileRate.load();
    return rate ? static_cast<double>(m_totalFrames.load()) / rate : 0.0;
}

void Recorder::WriteHeader()
{
    const uint64_t dataBytes = m_fileFrames * kChannels * kBytesPerSample;
    const uint32_t data32 = static_cast<uint32_t>(dataBytes);
    const unsigned rate = m_fileRate.load();
    unsigned char h[44];
    memcpy(h, "RIFF", 4);  Put32(h + 4, 36 + data32);
    memcpy(h + 8, "WAVEfmt ", 8);
    Put32(h + 16, 16);
    Put16(h + 20, 1);                                          // PCM
    Put16(h + 22, kChannels);
    Put32(h + 24, rate);
    Put32(h + 28, rate * kChannels * kBytesPerSample);
    Put16(h + 32, kChannels * kBytesPerSample);
    Put16(h + 34, kBytesPerSample * 8);
    memcpy(h + 36, "data", 4); Put32(h + 40, data32);
    _fseeki64(m_file, 0, SEEK_SET);
    if (fwrite(h, 1, sizeof(h), m_file) != sizeof(h)) m_error = true;
    _fseeki64(m_file, 0, SEEK_END);
}

bool Recorder::OpenFile()
{
    std::wstring path = m_path;
    if (m_part > 1)
    {
        const size_t dot = path.find_last_of(L'.');
        const std::wstring suffix = L"_part" + std::to_wstring(m_part);
        if (dot == std::wstring::npos)
            path += suffix;
        else
            path.insert(dot, suffix);
    }
    if (_wfopen_s(&m_file, path.c_str(), L"wb") != 0 || !m_file)
    {
        m_file = nullptr;
        m_error = true;
        return false;
    }
    m_fileFrames = 0;
    WriteHeader();
    return true;
}

void Recorder::CloseFile()
{
    if (!m_file)
        return;
    WriteHeader();
    if (fclose(m_file) != 0) m_error = true;
    m_file = nullptr;
}

void Recorder::Drain()
{
    if (!m_file)
    {
        const unsigned rate = m_sampleRate.load();
        if (rate == 0 || m_error.load())
            return;   // audio not started yet; samples wait in the ring
        m_fileRate = rate;
        if (!OpenFile())
            return;
    }

    const size_t r = m_read.load(std::memory_order_relaxed);
    const size_t w = m_write.load(std::memory_order_acquire);
    size_t count = w - r;
    if (count == 0)
        return;

    // Stop at the file's size limit; the rest goes to the next part on the next pass.
    const unsigned long long room = (kMaxDataBytes - m_fileFrames * kChannels * kBytesPerSample) / kBytesPerSample;
    count = (std::min)(count, static_cast<size_t>(room)) & ~size_t(1);

    m_scratch.resize(count * kBytesPerSample);
    unsigned char* p = m_scratch.data();
    for (size_t i = 0; i < count; ++i)
    {
        float v = m_ring[(r + i) & (kRingFloats - 1)];
        v = v > 1.0f ? 1.0f : (v < -1.0f ? -1.0f : v);
        const int32_t s = static_cast<int32_t>(lrintf(v * 8388607.0f));
        p[0] = BYTE(s); p[1] = BYTE(s >> 8); p[2] = BYTE(s >> 16);
        p += 3;
    }
    m_read.store(r + count, std::memory_order_release);

    if (fwrite(m_scratch.data(), 1, m_scratch.size(), m_file) != m_scratch.size())
        m_error = true;
    m_fileFrames += count / kChannels;
    m_totalFrames.fetch_add(count / kChannels);

    if ((m_fileFrames + 1024) * kChannels * kBytesPerSample >= kMaxDataBytes)
    {
        CloseFile();
        ++m_part;
        OpenFile();
    }
}

void Recorder::WriterLoop()
{
    while (!m_stopWriter.load())
    {
        Drain();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    Drain();
    CloseFile();
}

bool Recorder::Stop()
{
    const bool wasRecording = m_armed.exchange(false);
    m_stopWriter = true;
    if (m_writer.joinable())
        m_writer.join();
    return !wasRecording || !m_error.load();
}
