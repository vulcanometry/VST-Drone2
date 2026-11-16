// PluginShared.h - IDs and state shared by the VST3 processor and controller.
#pragma once

#include "SynthEngine.h"
#include "Recorder.h"

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/vsttypes.h"

#include <atomic>
#include <string>

namespace Drone2Plugin
{
static const Steinberg::FUID kProcessorUID(0x953EDE75, 0x15C23F9A, 0xCBF0415D, 0xAB5552C2);
static const Steinberg::FUID kControllerUID(0x69C7D42E, 0xA729FD54, 0x37764D4D, 0xF60827EA);

// Engine parameters use their ParamId as the VST3 ParamID; POWER is plugin-only.
constexpr Steinberg::Vst::ParamID kPowerParam = P_COUNT;
// Receives the mod wheel through IMidiMapping; hidden from the host's parameter list.
constexpr Steinberg::Vst::ParamID kWheelParam = P_COUNT + 1;

// Sent from the processor to the controller after they are connected. It
// carries the address of the processor's SharedState so the editor can show
// the live displays, play notes from the computer keyboard and record.
// It is only used when both sides live in the same process.
constexpr const char* kSharedStateMessage = "Drone2SharedState";

struct SharedState
{
    SynthEngine engine;
    Recorder recorder;
};

// State (processor and controller) is the preset text format plus the power switch.
constexpr const char* kPowerKey = "plugin.power";

bool ReadStream(Steinberg::IBStream* stream, std::string& text);
bool WriteStream(Steinberg::IBStream* stream, const std::string& text);
bool ParsePower(const std::string& text, bool defaultValue);
} // namespace Drone2Plugin
