// PluginFactory.cpp - VST3 class factory: exports the Drone 2 instrument.
#include "Drone2Controller.h"
#include "Drone2Processor.h"

#include "public.sdk/source/main/pluginfactory.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"

#define DRONE_VERSION "1.0.0"

using namespace Drone2Plugin;

BEGIN_FACTORY_DEF("Drone 2", "", "")

    DEF_CLASS2(INLINE_UID_FROM_FUID(kProcessorUID),
               PClassInfo::kManyInstances,
               kVstAudioEffectClass,
               "Drone 2",
               Vst::kDistributable,
               Vst::PlugType::kInstrumentSynth,
               DRONE_VERSION,
               kVstVersionString,
               Drone2Processor::createInstance)

    DEF_CLASS2(INLINE_UID_FROM_FUID(kControllerUID),
               PClassInfo::kManyInstances,
               kVstComponentControllerClass,
               "Drone 2 Controller",
               0,
               "",
               DRONE_VERSION,
               kVstVersionString,
               Drone2Controller::createInstance)

END_FACTORY
