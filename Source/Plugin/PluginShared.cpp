// PluginShared.cpp - state stream helpers shared by the processor and controller.
#include "PluginShared.h"

#include <cstdlib>
#include <cstring>

namespace Drone2Plugin
{
bool ReadStream(Steinberg::IBStream* stream, std::string& text)
{
    if (!stream)
        return false;
    text.clear();
    char buf[4096];
    for (;;)
    {
        Steinberg::int32 read = 0;
        if (stream->read(buf, sizeof(buf), &read) != Steinberg::kResultOk || read <= 0)
            break;
        text.append(buf, static_cast<size_t>(read));
        if (text.size() > (1u << 20))   // a state is a few hundred bytes; refuse garbage
            return false;
    }
    return !text.empty();
}

bool WriteStream(Steinberg::IBStream* stream, const std::string& text)
{
    if (!stream)
        return false;
    Steinberg::int32 written = 0;
    return stream->write(const_cast<char*>(text.data()), static_cast<Steinberg::int32>(text.size()), &written) == Steinberg::kResultOk
        && written == static_cast<Steinberg::int32>(text.size());
}

bool ParsePower(const std::string& text, bool defaultValue)
{
    const std::string key = std::string(kPowerKey) + "=";
    const size_t pos = text.find(key);
    if (pos == std::string::npos || (pos > 0 && text[pos - 1] != '\n'))
        return defaultValue;
    return atof(text.c_str() + pos + key.size()) >= 0.5;
}
} // namespace Drone2Plugin
