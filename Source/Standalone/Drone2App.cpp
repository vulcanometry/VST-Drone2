// Drone2App.cpp - standalone application: a window hosting the editor, with WASAPI
// audio output, WinMM MIDI input and WAV recording. The last session is restored.
#include "PlatformWin.h"
#include "AudioOutput.h"
#include "MidiInput.h"
#include "PanelView.h"
#include "Recorder.h"
#include "SynthEngine.h"

namespace
{
const wchar_t kFrameClass[] = L"Drone2StandaloneFrame";

class StandaloneHost : public PanelHost
{
public:
    float GetParam(int id) override { return engine.GetParam(id); }
    void  BeginEdit(int) override {}
    void  SetParam(int id, float value) override { engine.SetParam(id, value); }
    void  EndEdit(int) override {}

    bool IsPowered() override { return powered; }
    void SetPowered(bool on) override
    {
        if (on == powered)
            return;
        powered = on;
        if (on)
            audio.Start(&engine, &recorder, frame);
        else
            audio.Stop();
    }

    SynthEngine* Engine() override { return &engine; }
    Recorder* GetRecorder() override { return &recorder; }

    std::wstring MidiInputName() override { return midi.Device() >= 0 ? midi.Name() : L"none"; }
    std::wstring CycleMidiInput() override
    {
        const auto devices = MidiInput::ListDevices();
        const int next = midi.Device() + 1;
        if (next >= static_cast<int>(devices.size()))
        {
            midi.Close();
            return devices.empty() ? L"No MIDI input devices found" : L"MIDI input off";
        }
        if (midi.Open(next, frame))
            return L"MIDI input: " + midi.Name();
        return L"Could not open MIDI input " + devices[next] + L" (in use by another app?)";
    }

    std::wstring PatchName() override { return patchName; }
    void SetPatchName(const std::wstring& name) override { patchName = name; }

    void RestoreSession()
    {
        std::string text;
        if (!PresetBank::ReadTextFile(SessionPath(), text))
            return;
        ParamValues values;
        PresetBank::Parse(text, values);
        for (int id = 0; id < P_COUNT; ++id)
            engine.SetParam(id, values[id]);
        std::wstring name;
        if (PresetBank::ReadTextFile(SessionPath() + L".name", text))
        {
            const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
            name.assign(static_cast<size_t>(length), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), name.data(), length);
        }
        patchName = name;
    }

    void SaveSession()
    {
        ParamValues values{};
        for (int id = 0; id < P_COUNT; ++id)
            values[id] = engine.GetParam(id);
        PresetBank::WriteTextFile(SessionPath(), PresetBank::Serialize(values));
        const int length = WideCharToMultiByte(CP_UTF8, 0, patchName.data(), static_cast<int>(patchName.size()), nullptr, 0, nullptr, nullptr);
        std::string utf8(static_cast<size_t>(length), '\0');
        WideCharToMultiByte(CP_UTF8, 0, patchName.data(), static_cast<int>(patchName.size()), utf8.data(), length, nullptr, nullptr);
        PresetBank::WriteTextFile(SessionPath() + L".name", utf8);
    }

    SynthEngine engine;
    AudioOutput audio;
    Recorder recorder;
    MidiInput midi;
    HWND frame = nullptr;
    bool powered = false;
    std::wstring patchName;

private:
    static std::wstring SessionPath() { return PresetBank::DataFolder() + L"\\LastSession.txt"; }
};

StandaloneHost* g_host = nullptr;
PanelView* g_panel = nullptr;

LRESULT CALLBACK FrameProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_SETFOCUS:
        if (g_panel && g_panel->Hwnd())
            SetFocus(g_panel->Hwnd());
        return 0;
    case WM_AUDIO_ERROR:
    {
        const std::wstring err = g_host->audio.GetLastError();
        g_host->SetPowered(false);
        g_panel->OnPowerLost(err);
        return 0;
    }
    case WM_MIDI_EVENT:
        g_panel->HandleMidiMessage(static_cast<unsigned>(wp));
        return 0;
    case WM_DESTROY:
        g_host->midi.Close();
        g_host->recorder.Stop();
        g_host->SetPowered(false);
        g_host->SaveSession();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// Editor scale from the system DPI, shrunk if the window would not fit on screen.
float ChooseScale(DWORD style, RECT& frame, const RECT& work)
{
    HDC screen = GetDC(nullptr);
    float scale = static_cast<float>(GetDeviceCaps(screen, LOGPIXELSX)) / 96.0f;
    ReleaseDC(nullptr, screen);
    for (;;)
    {
        const SIZE size = PanelView::PixelSize(scale);
        frame = { 0, 0, size.cx, size.cy };
        AdjustWindowRectEx(&frame, style, FALSE, 0);
        if ((frame.right - frame.left <= work.right - work.left && frame.bottom - frame.top <= work.bottom - work.top) || scale <= 0.5f)
            return scale;
        scale -= 0.05f;
    }
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCmd)
{
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);   // file dialogs

    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = FrameProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    wc.lpszClassName = kFrameClass;
    RegisterClassExW(&wc);

    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
    RECT work, frame;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const float scale = ChooseScale(style, frame, work);
    const int width = frame.right - frame.left, height = frame.bottom - frame.top;
    const int x = work.left + ((work.right - work.left) - width) / 2;
    const int y = (std::max)(static_cast<int>(work.top), static_cast<int>(work.top + ((work.bottom - work.top) - height) / 2));

    StandaloneHost host;
    host.RestoreSession();
    PanelView panel(host);
    g_host = &host;
    g_panel = &panel;

    host.frame = CreateWindowExW(0, kFrameClass, L"Drone 2", style, x, y, width, height, nullptr, nullptr, instance, nullptr);
    if (!host.frame || !panel.Create(host.frame, 0, 0, scale))
        return 1;

    if (!MidiInput::ListDevices().empty())
        host.midi.Open(0, host.frame);

    ShowWindow(host.frame, showCmd);
    UpdateWindow(host.frame);
    SetFocus(panel.Hwnd());

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    g_panel = nullptr;
    g_host = nullptr;
    CoUninitialize();
    return static_cast<int>(msg.wParam);
}
