// MidiInput.cpp - WinMM MIDI input; forwards short messages to a window.
#include "MidiInput.h"

MidiInput::~MidiInput()
{
    Close();
}

std::vector<std::wstring> MidiInput::ListDevices()
{
    std::vector<std::wstring> names;
    const UINT count = midiInGetNumDevs();
    for (UINT i = 0; i < count; ++i)
    {
        MIDIINCAPSW caps = {};
        names.push_back(midiInGetDevCapsW(i, &caps, sizeof(caps)) == MMSYSERR_NOERROR ? caps.szPname : L"MIDI device");
    }
    return names;
}

bool MidiInput::Open(int device, HWND notify)
{
    Close();
    const auto names = ListDevices();
    if (device < 0 || device >= static_cast<int>(names.size()))
        return false;

    m_notify = notify;
    if (midiInOpen(&m_handle, static_cast<UINT>(device), reinterpret_cast<DWORD_PTR>(&Callback),
                   reinterpret_cast<DWORD_PTR>(this), CALLBACK_FUNCTION) != MMSYSERR_NOERROR)
    {
        m_handle = nullptr;
        return false;
    }
    midiInStart(m_handle);
    m_device = device;
    m_name = names[device];
    return true;
}

void MidiInput::Close()
{
    if (m_handle)
    {
        midiInStop(m_handle);
        midiInReset(m_handle);
        midiInClose(m_handle);
        m_handle = nullptr;
    }
    m_device = -1;
    m_name.clear();
}

// Runs on a WinMM thread; PostMessage is one of the few calls allowed here.
void CALLBACK MidiInput::Callback(HMIDIIN, UINT msg, DWORD_PTR instance, DWORD_PTR param1, DWORD_PTR)
{
    if (msg != MIM_DATA)
        return;
    const auto* self = reinterpret_cast<const MidiInput*>(instance);
    if (self->m_notify)
        ::PostMessage(self->m_notify, WM_MIDI_EVENT, static_cast<WPARAM>(param1 & 0xFFFFFF), 0);
}
