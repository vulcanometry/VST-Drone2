// Drone2Controller.cpp - VST3 edit controller.
#include "Drone2Controller.h"
#include "Drone2EditorView.h"
#include "Presets.h"

#include "pluginterfaces/base/ustring.h"
#include "pluginterfaces/vst/ivstmessage.h"

#include <windows.h>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace Drone2Plugin
{
namespace
{
// Shows values in the host exactly as the panel does ("1.2 kHz", "Wavetable" ...).
class Drone2Parameter : public Parameter
{
public:
    explicit Drone2Parameter(int id)
        : Parameter(reinterpret_cast<const TChar*>(params::Info(id).name), static_cast<ParamID>(id), nullptr,
                    params::Info(id).defaultValue,
                    params::Info(id).steps > 1 ? params::Info(id).steps - 1 : 0,
                    ParameterInfo::kCanAutomate | (params::Info(id).steps > 1 ? ParameterInfo::kIsList : 0))
        , m_id(id)
    {
    }

    void toString(ParamValue valueNormalized, String128 string) const SMTG_OVERRIDE
    {
        const std::wstring text = params::Format(m_id, static_cast<float>(valueNormalized));
        UString(string, 128).assign(reinterpret_cast<const char16*>(text.c_str()));
    }

private:
    int m_id;
};
} // namespace

tresult PLUGIN_API Drone2Controller::initialize(FUnknown* context)
{
    const tresult result = EditController::initialize(context);
    if (result != kResultOk)
        return result;

    for (int id = 0; id < P_COUNT; ++id)
        parameters.addParameter(new Drone2Parameter(id));
    parameters.addParameter(STR16("Power"), nullptr, 1, 1.0, ParameterInfo::kCanAutomate | ParameterInfo::kIsList, kPowerParam);
    parameters.addParameter(STR16("Mod Wheel"), nullptr, 0, 0.0, ParameterInfo::kIsHidden, kWheelParam);
    return kResultOk;
}

tresult PLUGIN_API Drone2Controller::terminate()
{
    m_shared = nullptr;
    return EditController::terminate();
}

tresult PLUGIN_API Drone2Controller::setComponentState(IBStream* state)
{
    std::string text;
    if (!ReadStream(state, text))
        return kResultFalse;
    ParamValues values;
    PresetBank::Parse(text, values);
    for (int id = 0; id < P_COUNT; ++id)
        setParamNormalized(static_cast<ParamID>(id), values[id]);
    setParamNormalized(kPowerParam, ParsePower(text, true) ? 1.0 : 0.0);
    return kResultOk;
}

tresult PLUGIN_API Drone2Controller::setState(IBStream* state)
{
    std::string utf8;
    if (!ReadStream(state, utf8))
        return kResultOk;   // nothing saved: not an error
    const int length = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    m_patchName.assign(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), m_patchName.data(), length);
    return kResultOk;
}

tresult PLUGIN_API Drone2Controller::getState(IBStream* state)
{
    const int length = WideCharToMultiByte(CP_UTF8, 0, m_patchName.data(), static_cast<int>(m_patchName.size()), nullptr, 0, nullptr, nullptr);
    std::string utf8(static_cast<size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, m_patchName.data(), static_cast<int>(m_patchName.size()), utf8.data(), length, nullptr, nullptr);
    return utf8.empty() || WriteStream(state, utf8) ? kResultOk : kResultFalse;
}

tresult PLUGIN_API Drone2Controller::setParamNormalized(ParamID tag, ParamValue value)
{
    const tresult result = EditController::setParamNormalized(tag, value);
    if (m_editor)
        m_editor->Refresh();
    return result;
}

IPlugView* PLUGIN_API Drone2Controller::createView(FIDString name)
{
    if (FIDStringsEqual(name, ViewType::kEditor))
        return new Drone2EditorView(this);
    return nullptr;
}

tresult PLUGIN_API Drone2Controller::notify(IMessage* message)
{
    if (message && FIDStringsEqual(message->getMessageID(), kSharedStateMessage))
    {
        int64 ptr = 0, pid = 0;
        if (message->getAttributes()->getInt("ptr", ptr) == kResultOk &&
            message->getAttributes()->getInt("pid", pid) == kResultOk &&
            pid == static_cast<int64>(GetCurrentProcessId()))
        {
            m_shared = reinterpret_cast<SharedState*>(static_cast<intptr_t>(ptr));
            if (m_editor)
                m_editor->Refresh();
        }
        return kResultOk;
    }
    return EditController::notify(message);
}

tresult PLUGIN_API Drone2Controller::disconnect(IConnectionPoint* other)
{
    m_shared = nullptr;
    if (m_editor)
        m_editor->Refresh();
    return EditController::disconnect(other);
}

tresult PLUGIN_API Drone2Controller::getMidiControllerAssignment(int32 busIndex, int16, CtrlNumber midiControllerNumber, ParamID& id)
{
    if (busIndex == 0 && midiControllerNumber == kCtrlModWheel)
    {
        id = kWheelParam;
        return kResultTrue;
    }
    return kResultFalse;
}

// ---------------------------------------------------------------------------
// PanelHost
// ---------------------------------------------------------------------------
float Drone2Controller::GetParam(int id)
{
    return static_cast<float>(getParamNormalized(static_cast<ParamID>(id)));
}

void Drone2Controller::BeginEdit(int id)
{
    beginEdit(static_cast<ParamID>(id));
}

void Drone2Controller::SetParam(int id, float value)
{
    EditController::setParamNormalized(static_cast<ParamID>(id), value);
    performEdit(static_cast<ParamID>(id), value);
}

void Drone2Controller::EndEdit(int id)
{
    endEdit(static_cast<ParamID>(id));
}

bool Drone2Controller::IsPowered()
{
    return getParamNormalized(kPowerParam) >= 0.5;
}

void Drone2Controller::SetPowered(bool on)
{
    const ParamValue value = on ? 1.0 : 0.0;
    beginEdit(kPowerParam);
    EditController::setParamNormalized(kPowerParam, value);
    performEdit(kPowerParam, value);
    endEdit(kPowerParam);
}

SynthEngine* Drone2Controller::Engine()
{
    return m_shared ? &m_shared->engine : nullptr;
}

Recorder* Drone2Controller::GetRecorder()
{
    return m_shared ? &m_shared->recorder : nullptr;
}

std::wstring Drone2Controller::MidiInputName()
{
    return L"from host";
}

std::wstring Drone2Controller::CycleMidiInput()
{
    return L"MIDI comes from the host: route a MIDI track or keyboard to Drone 2";
}
} // namespace Drone2Plugin
