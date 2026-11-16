// MidiInput.h - WinMM MIDI input; forwards short messages to a window.
#pragma once

#include "PlatformWin.h"
#include <mmsystem.h>

#include <string>
#include <vector>

// Posted for each incoming short MIDI message: wParam = status | data1 << 8 | data2 << 16.
constexpr UINT WM_MIDI_EVENT = WM_APP + 2;

class MidiInput
{
public:
    MidiInput() = default;
    ~MidiInput();
    MidiInput(const MidiInput&) = delete;
    MidiInput& operator=(const MidiInput&) = delete;

    static std::vector<std::wstring> ListDevices();

    bool Open(int device, HWND notify);
    void Close();
    int  Device() const { return m_device; }
    const std::wstring& Name() const { return m_name; }

private:
    static void CALLBACK Callback(HMIDIIN handle, UINT msg, DWORD_PTR instance, DWORD_PTR param1, DWORD_PTR param2);

    HMIDIIN m_handle = nullptr;
    HWND m_notify = nullptr;
    int m_device = -1;
    std::wstring m_name;
};
