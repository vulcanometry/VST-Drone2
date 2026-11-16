// Drone2Processor.cpp - VST3 audio processor: runs the SynthEngine for the host.
#include "Drone2Processor.h"
#include "Presets.h"

#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include <windows.h>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace Drone2Plugin
{
Drone2Processor::Drone2Processor() : m_shared(std::make_unique<SharedState>())
{
    setControllerClass(kControllerUID);
}

tresult PLUGIN_API Drone2Processor::initialize(FUnknown* context)
{
    const tresult result = AudioEffect::initialize(context);
    if (result != kResultOk)
        return result;
    addEventInput(STR16("MIDI In"), 1);
    addAudioOutput(STR16("Stereo Out"), SpeakerArr::kStereo);
    return kResultOk;
}

tresult PLUGIN_API Drone2Processor::setBusArrangements(SpeakerArrangement* inputs, int32 numIns,
                                                       SpeakerArrangement* outputs, int32 numOuts)
{
    if (numIns == 0 && numOuts == 1 && outputs[0] == SpeakerArr::kStereo)
        return AudioEffect::setBusArrangements(inputs, numIns, outputs, numOuts);
    return kResultFalse;
}

tresult PLUGIN_API Drone2Processor::canProcessSampleSize(int32 symbolicSampleSize)
{
    return symbolicSampleSize == kSample32 ? kResultTrue : kResultFalse;
}

tresult PLUGIN_API Drone2Processor::setupProcessing(ProcessSetup& setup)
{
    m_buffer.assign(static_cast<size_t>((std::max)(setup.maxSamplesPerBlock, 64)) * 2, 0.0f);
    return AudioEffect::setupProcessing(setup);
}

tresult PLUGIN_API Drone2Processor::setActive(TBool state)
{
    m_active = state != 0;
    if (m_active)
    {
        m_shared->engine.Prepare(processSetup.sampleRate);
        if (m_powered)
            m_shared->engine.StartFadeIn();
        m_shared->recorder.SetSampleRate(static_cast<unsigned>(processSetup.sampleRate));
    }
    else
    {
        m_shared->recorder.SetSampleRate(0);
    }
    return AudioEffect::setActive(state);
}

void Drone2Processor::SetPowered(bool on)
{
    if (on == m_powered)
        return;
    m_powered = on;
    if (on)
        m_shared->engine.StartFadeIn();
    else
        m_shared->engine.StartFadeOut();
}

tresult PLUGIN_API Drone2Processor::process(ProcessData& data)
{
    SynthEngine& engine = m_shared->engine;

    // Parameter changes: the last value of each queue applies to the block
    // (the engine smooths every continuous parameter anyway).
    if (IParameterChanges* changes = data.inputParameterChanges)
    {
        const int32 count = changes->getParameterCount();
        for (int32 i = 0; i < count; ++i)
        {
            IParamValueQueue* queue = changes->getParameterData(i);
            if (!queue || queue->getPointCount() <= 0)
                continue;
            int32 offset = 0;
            ParamValue value = 0.0;
            if (queue->getPoint(queue->getPointCount() - 1, offset, value) != kResultTrue)
                continue;
            const ParamID id = queue->getParameterId();
            if (id < static_cast<ParamID>(P_COUNT))
                engine.SetParam(static_cast<int>(id), static_cast<float>(value));
            else if (id == kPowerParam)
                SetPowered(value >= 0.5);
            else if (id == kWheelParam)
                engine.SetModWheel(static_cast<float>(value));
        }
    }

    if (IEventList* events = data.inputEvents)
    {
        const int32 count = events->getEventCount();
        for (int32 i = 0; i < count; ++i)
        {
            Event e = {};
            if (events->getEvent(i, e) != kResultOk)
                continue;
            if (e.type == Event::kNoteOnEvent)
            {
                if (e.noteOn.velocity > 0.0f)
                    engine.NoteOn(e.noteOn.pitch, e.noteOn.velocity);
                else
                    engine.NoteOff(e.noteOn.pitch);
            }
            else if (e.type == Event::kNoteOffEvent)
            {
                engine.NoteOff(e.noteOff.pitch);
            }
        }
    }

    if (data.numSamples <= 0 || data.numOutputs < 1 || data.outputs[0].numChannels < 2)
        return kResultOk;

    float* left = data.outputs[0].channelBuffers32[0];
    float* right = data.outputs[0].channelBuffers32[1];
    const int maxChunk = static_cast<int>(m_buffer.size() / 2);
    for (int done = 0; done < data.numSamples;)
    {
        const int frames = (std::min)(data.numSamples - done, maxChunk);
        engine.Process(m_buffer.data(), frames);
        m_shared->recorder.Push(m_buffer.data(), frames);
        for (int i = 0; i < frames; ++i)
        {
            left[done + i] = m_buffer[2 * i];
            right[done + i] = m_buffer[2 * i + 1];
        }
        done += frames;
    }
    data.outputs[0].silenceFlags = 0;
    return kResultOk;
}

tresult PLUGIN_API Drone2Processor::setState(IBStream* state)
{
    std::string text;
    if (!ReadStream(state, text))
        return kResultFalse;
    ParamValues values;
    PresetBank::Parse(text, values);
    for (int id = 0; id < P_COUNT; ++id)
        m_shared->engine.SetParam(id, values[id]);
    SetPowered(ParsePower(text, true));
    return kResultOk;
}

tresult PLUGIN_API Drone2Processor::getState(IBStream* state)
{
    ParamValues values{};
    for (int id = 0; id < P_COUNT; ++id)
        values[id] = m_shared->engine.GetParam(id);
    std::string text = PresetBank::Serialize(values);
    text += std::string(kPowerKey) + (m_powered ? "=1\r\n" : "=0\r\n");
    return WriteStream(state, text) ? kResultOk : kResultFalse;
}

tresult PLUGIN_API Drone2Processor::connect(IConnectionPoint* other)
{
    const tresult result = AudioEffect::connect(other);
    if (result == kResultTrue)
    {
        if (IPtr<IMessage> message = owned(allocateMessage()))
        {
            message->setMessageID(kSharedStateMessage);
            message->getAttributes()->setInt("ptr", static_cast<int64>(reinterpret_cast<intptr_t>(m_shared.get())));
            message->getAttributes()->setInt("pid", static_cast<int64>(GetCurrentProcessId()));
            sendMessage(message);
        }
    }
    return result;
}
} // namespace Drone2Plugin
