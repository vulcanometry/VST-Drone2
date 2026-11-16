// PanelTheme.cpp - colour themes for the editor.
#include "PanelTheme.h"
#include "Presets.h"

#include <cstdlib>
#include <string>

namespace theme
{
namespace
{
Color Rgb(unsigned hex) { return Color(255, BYTE(hex >> 16), BYTE(hex >> 8), BYTE(hex)); }

const Palette kThemes[] =
{
    { L"Midnight", Rgb(0x0B0E17), Rgb(0x141927), Rgb(0x171C2B), Rgb(0x262D42), Rgb(0x0C0F19), Rgb(0x2B3248),
      Rgb(0xEDF1FA), Rgb(0xA3ADC6), Rgb(0x657091), Rgb(0x5CC8FF), Rgb(0xA78BFA),
      { Rgb(0x5CC8FF), Rgb(0xA78BFA), Rgb(0x4ADEB0), Rgb(0xFBBF5C) },
      Rgb(0x232A3E), Rgb(0x303955), Rgb(0x081018), Rgb(0xFF4D5E) },
    { L"Aurora", Rgb(0x07130F), Rgb(0x0E201A), Rgb(0x10251E), Rgb(0x1E3B31), Rgb(0x06110D), Rgb(0x234237),
      Rgb(0xE9FBF3), Rgb(0x9CC7B5), Rgb(0x5D8A78), Rgb(0x4CF0A8), Rgb(0x56C7F2),
      { Rgb(0x4CF0A8), Rgb(0x56C7F2), Rgb(0xC084FC), Rgb(0xE6F06A) },
      Rgb(0x183229), Rgb(0x25493C), Rgb(0x04130C), Rgb(0xFF5C70) },
    { L"Ember", Rgb(0x140D0A), Rgb(0x201510), Rgb(0x241813), Rgb(0x3D2A20), Rgb(0x110A07), Rgb(0x452F24),
      Rgb(0xFBF0E8), Rgb(0xCFB19D), Rgb(0x8F7361), Rgb(0xFF8A3D), Rgb(0xFFC857),
      { Rgb(0xFF8A3D), Rgb(0xFFC857), Rgb(0xE85D5D), Rgb(0xD9A7FF) },
      Rgb(0x35241B), Rgb(0x4A3327), Rgb(0x190D05), Rgb(0xFF3B47) },
    { L"Synthwave", Rgb(0x120A20), Rgb(0x1D1033), Rgb(0x21133A), Rgb(0x3B2360), Rgb(0x0E0719), Rgb(0x402868),
      Rgb(0xF8EEFF), Rgb(0xC2A8E6), Rgb(0x8267A8), Rgb(0xFF4FB4), Rgb(0x35E0F0),
      { Rgb(0xFF4FB4), Rgb(0x35E0F0), Rgb(0xFFD23F), Rgb(0x9B7BFF) },
      Rgb(0x2F1C52), Rgb(0x452A73), Rgb(0x1A0612), Rgb(0xFF3355) },
    { L"Moss", Rgb(0x10130B), Rgb(0x1A1E12), Rgb(0x1E2315), Rgb(0x343B25), Rgb(0x0D0F08), Rgb(0x3A422A),
      Rgb(0xF2F4E6), Rgb(0xB8BE9E), Rgb(0x7D8467), Rgb(0xB5D34C), Rgb(0xE3B85A),
      { Rgb(0xB5D34C), Rgb(0xE3B85A), Rgb(0x7CC6B2), Rgb(0xD98E73) },
      Rgb(0x2B3120), Rgb(0x3D452D), Rgb(0x121505), Rgb(0xEF5A4C) },
    { L"Graphite", Rgb(0x111213), Rgb(0x1B1C1E), Rgb(0x1F2022), Rgb(0x35373A), Rgb(0x0D0E0F), Rgb(0x3B3D41),
      Rgb(0xF2F2F3), Rgb(0xB0B2B6), Rgb(0x75787D), Rgb(0xE8E8EA), Rgb(0x9EA2A8),
      { Rgb(0xF0F0F2), Rgb(0xC4C7CC), Rgb(0x9EA2A8), Rgb(0x7F848B) },
      Rgb(0x2C2E31), Rgb(0x404246), Rgb(0x101112), Rgb(0xFF4A4A) },
    { L"Daylight", Rgb(0xE6E9EF), Rgb(0xF2F4F8), Rgb(0xFBFCFE), Rgb(0xCDD3DF), Rgb(0xE9ECF2), Rgb(0xD3D9E4),
      Rgb(0x1B2233), Rgb(0x4D586F), Rgb(0x838DA3), Rgb(0x2563EB), Rgb(0x8B3FE0),
      { Rgb(0x2563EB), Rgb(0x8B3FE0), Rgb(0x0E9F77), Rgb(0xD97706) },
      Rgb(0xE4E8F0), Rgb(0xD5DBE7), Rgb(0xFFFFFF), Rgb(0xDC2636) },
};

int g_current = 0;

std::wstring SettingsPath() { return PresetBank::DataFolder() + L"\\Settings.txt"; }
} // namespace

int Count() { return static_cast<int>(sizeof(kThemes) / sizeof(kThemes[0])); }
const wchar_t* Name(int index) { return kThemes[index].name; }
int Current() { return g_current; }
const Palette& P() { return kThemes[g_current]; }

void Apply(int index)
{
    if (index < 0 || index >= Count())
        return;
    g_current = index;
    PresetBank::WriteTextFile(SettingsPath(), "theme=" + std::to_string(index) + "\r\n");
}

void LoadSaved()
{
    std::string text;
    if (PresetBank::ReadTextFile(SettingsPath(), text) && text.rfind("theme=", 0) == 0)
    {
        const int index = atoi(text.c_str() + 6);
        if (index >= 0 && index < Count())
            g_current = index;
    }
}
} // namespace theme
