// Drone2Processor.h - VST3 audio processor: runs the SynthEngine for the host.
#pragma once

#include "PluginShared.h"

#include "public.sdk/source/vst/vstaudioeffect.h"

#include <memory>
#include <vector>

namespace Drone2Plugin
{
class Drone2Processor : public Steinberg::Vst::AudioEffect
{
public:
    Drone2Processor();
    static Steinberg::FUnknown* createInstance(void*)
    {
        return static_cast<Steinberg::Vst::IAudioProcessor*>(new Drone2Processor());
    }

    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setBusArrangements(Steinberg::Vst::SpeakerArrangement* inputs, Steinberg::int32 numIns,
                                                     Steinberg::Vst::SpeakerArrangement* outputs, Steinberg::int32 numOuts) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API canProcessSampleSize(Steinberg::int32 symbolicSampleSize) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setupProcessing(Steinberg::Vst::ProcessSetup& setup) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setActive(Steinberg::TBool state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API process(Steinberg::Vst::ProcessData& data) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API connect(Steinberg::Vst::IConnectionPoint* other) SMTG_OVERRIDE;

private:
    void SetPowered(bool on);

    std::unique_ptr<SharedState> m_shared;
    std::vector<float> m_buffer;   // interleaved stereo scratch
    bool m_powered = true;
    bool m_active = false;
};
} // namespace Drone2Plugin
