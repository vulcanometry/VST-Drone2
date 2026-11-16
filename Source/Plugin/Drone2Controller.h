// Drone2Controller.h - VST3 edit controller. Publishes the parameters to the
// host and is the PanelHost of the plugin editor, so panel edits become host
// automation.
#pragma once

#include "PluginShared.h"
#include "PanelHost.h"

#include "public.sdk/source/vst/vsteditcontroller.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"

namespace Drone2Plugin
{
class Drone2EditorView;

class Drone2Controller : public Steinberg::Vst::EditController, public Steinberg::Vst::IMidiMapping, public PanelHost
{
public:
    static Steinberg::FUnknown* createInstance(void*)
    {
        return static_cast<Steinberg::Vst::IEditController*>(new Drone2Controller());
    }

    // EditController
    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API terminate() SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setComponentState(Steinberg::IBStream* state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setParamNormalized(Steinberg::Vst::ParamID tag, Steinberg::Vst::ParamValue value) SMTG_OVERRIDE;
    Steinberg::IPlugView* PLUGIN_API createView(Steinberg::FIDString name) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API notify(Steinberg::Vst::IMessage* message) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API disconnect(Steinberg::Vst::IConnectionPoint* other) SMTG_OVERRIDE;

    // IMidiMapping: the mod wheel is a modulation source, as in the standalone app.
    Steinberg::tresult PLUGIN_API getMidiControllerAssignment(Steinberg::int32 busIndex, Steinberg::int16 channel,
                                                              Steinberg::Vst::CtrlNumber midiControllerNumber,
                                                              Steinberg::Vst::ParamID& id) SMTG_OVERRIDE;

    // PanelHost
    float GetParam(int id) override;
    void  BeginEdit(int id) override;
    void  SetParam(int id, float value) override;
    void  EndEdit(int id) override;
    bool  IsPowered() override;
    void  SetPowered(bool on) override;
    SynthEngine* Engine() override;
    Recorder* GetRecorder() override;
    std::wstring MidiInputName() override;
    std::wstring CycleMidiInput() override;
    std::wstring PatchName() override { return m_patchName; }
    void SetPatchName(const std::wstring& name) override { m_patchName = name; }

    void SetEditor(Drone2EditorView* editor) { m_editor = editor; }

    OBJ_METHODS(Drone2Controller, EditController)
    DEFINE_INTERFACES
        DEF_INTERFACE(IMidiMapping)
    END_DEFINE_INTERFACES(EditController)
    REFCOUNT_METHODS(EditController)

private:
    SharedState* m_shared = nullptr;   // owned by the processor; valid while connected
    Drone2EditorView* m_editor = nullptr;
    std::wstring m_patchName;          // saved in the controller's half of the host's state
};
} // namespace Drone2Plugin
