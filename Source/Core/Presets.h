// Presets.h - factory patches plus user patches stored as .d2patch text files.
//
// File format: one "key=value" per line, values normalized 0..1, keys from
// ParamInfo::key. Lines starting with ';' are comments. Missing keys take their
// default value, unknown keys are ignored. A patch's name is its file name. The
// same text format is used for the plugin's saved state.
#pragma once

#include "Params.h"

#include <array>
#include <string>
#include <vector>

using ParamValues = std::array<float, P_COUNT>;

struct Preset
{
    std::wstring name;
    std::wstring category;   // factory patches only
    std::wstring path;       // empty for factory patches
    std::string text;        // factory patch contents
};

class PresetBank
{
public:
    void Refresh();   // factory patches followed by the files in UserFolder()
    size_t Count() const { return m_presets.size(); }
    const Preset& Get(size_t i) const { return m_presets[i]; }
    int IndexOfName(const std::wstring& name) const;   // user patches win over factory ones
    size_t FactoryCount() const;

    bool Load(size_t index, ParamValues& values) const;

    static ParamValues Defaults();
    static void Parse(const std::string& text, ParamValues& values);
    static std::string Serialize(const ParamValues& values);

    static bool ReadTextFile(const std::wstring& path, std::string& text);
    static bool WriteTextFile(const std::wstring& path, const std::string& text);
    static std::wstring UserFolder();
    static std::wstring DataFolder();                       // %APPDATA%\Drone 2
    static std::wstring NameFromPath(const std::wstring& path);
    static std::wstring PathForName(const std::wstring& name);
    static bool IsValidName(const std::wstring& name);      // usable as a file name

    // A musically constrained random patch. `seed` makes it repeatable.
    static ParamValues Randomize(unsigned seed);

private:
    std::vector<Preset> m_presets;
};
