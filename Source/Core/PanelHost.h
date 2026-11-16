// PanelHost.h - what the editor needs from whoever hosts it.
//
// The standalone app implements this on top of the engine, the WASAPI output
// and WinMM MIDI; the VST3 plugin implements it on top of its edit controller,
// so knob changes become host automation.
#pragma once

#include <string>

class SynthEngine;
class Recorder;

class PanelHost
{
public:
    virtual ~PanelHost() = default;

    // Normalized parameter values. BeginEdit/EndEdit bracket a gesture (a knob
    // drag), which plugin hosts use to record automation.
    virtual float GetParam(int id) = 0;
    virtual void  BeginEdit(int id) = 0;
    virtual void  SetParam(int id, float value) = 0;
    virtual void  EndEdit(int id) = 0;

    // POWER switch: starts/stops the audio device (app) or fades the plugin output.
    virtual bool IsPowered() = 0;
    virtual void SetPowered(bool on) = 0;

    // Running engine for the displays and note input.
    // May be null (a plugin editor whose processor is not connected yet).
    virtual SynthEngine* Engine() = 0;
    virtual Recorder* GetRecorder() = 0;   // may be null

    // MIDI display.
    virtual std::wstring MidiInputName() = 0;
    virtual std::wstring CycleMidiInput() = 0;   // returns a status message

    // Name of the current patch; kept by the host so a reopened editor or a
    // reloaded project shows it again.
    virtual std::wstring PatchName() = 0;
    virtual void SetPatchName(const std::wstring& name) = 0;
};
