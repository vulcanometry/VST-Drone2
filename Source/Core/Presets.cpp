// Presets.cpp - factory patches plus user patches stored as .d2patch text files.
#include "PlatformWin.h"
#include "Presets.h"
#include "Wavetables.h"

#include <ShlObj.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <random>

using namespace params;

namespace
{
struct FactoryPatch { const wchar_t* category; const wchar_t* name; const char* text; };

// Values are normalized knob positions. Stepped parameters use index/(positions-1):
//   root: (note-12)/84      algo: index/11     table: index/19    voices: (n-1)/7
//   oct: (octaves+4)/8      semi: (st+24)/48   temperament: index/4
//   filter mode: index/6    lfo shape: index/7 mod src: index/11  mod dst: index/32
const FactoryPatch kFactory[] =
{
    { L"Init", L"Init Drone", "" },

    { L"Cathedral", L"Stone Choir",
      "root=.2857\na.algo=.5455\na.morph=.4\na.timbre=.6\na.color=.15\na.voices=.4286\na.detune=.2\n"
      "b.on=1\nb.algo=.6364\nb.morph=.55\nb.timbre=.2\nb.color=.55\nb.oct=.625\nb.level=.6\n"
      "flt.cutoff=.75\nrev.size=.95\nrev.decay=.85\nrev.mix=.65\nrev.shimmer=.12\ndly.mix=.1\nens.mix=.35\n"
      "mod1.src=.0909\nmod1.dst=.0313\nmod1.amt=.7\n" },
    { L"Cathedral", L"Organ Loft",
      "root=.2857\na.algo=.0909\na.table=.3158\na.morph=.7\na.scan.depth=.1\na.color=.2\na.voices=.2857\na.detune=.08\na.drift=.15\n"
      "b.on=1\nb.algo=.0909\nb.table=.3158\nb.morph=.45\nb.semi=.6458\nb.voices=.2857\nb.detune=.09\nb.level=.55\n"
      "temperament=.25\nflt.cutoff=.7\nrev.size=.9\nrev.decay=.8\nrev.mix=.55\nens.mix=.2\n" },
    { L"Cathedral", L"Shimmer Vault",
      "root=.3571\na.algo=.6364\na.morph=.8\na.timbre=.35\na.color=.7\na.voices=.4286\n"
      "b.on=1\nb.algo=.0\nb.morph=.5\nb.timbre=0\nb.color=.14\nb.oct=.625\nb.level=.45\nb.voices=.2857\n"
      "flt.mode=0\nrev.size=1\nrev.decay=.9\nrev.damp=.25\nrev.shimmer=.6\nrev.mix=.75\nens.mix=.2\ndly.mix=.15\n" },

    { L"Deep", L"Sub Ocean",
      "mst.volume=.825\nroot=.1429\na.algo=.0\na.morph=.3\na.timbre=0\na.color=.14\na.voices=.2857\na.detune=.1\n"
      "b.on=1\nb.algo=.6364\nb.morph=.3\nb.color=.3\nb.level=.5\nb.oct=.625\n"
      "flt.cutoff=.55\nflt.res=.2\nmod1.src=.1818\nmod1.dst=.71875\nmod1.amt=.62\nlfo2.rate=.3\nrev.mix=.35\nrev.size=.85\nmst.lowcut=.08\n" },
    { L"Deep", L"Tectonic",
      "root=.0714\na.algo=.1818\na.morph=.66\na.timbre=.6\na.color=.3\na.voices=1\na.detune=.35\na.drift=.5\n"
      "flt.mode=1\nflt.cutoff=.42\nflt.res=.35\nflt.drive=.45\nmod1.src=.0909\nmod1.dst=.71875\nmod1.amt=.68\nlfo1.rate=.2\n"
      "rev.mix=.3\ndly.mix=.1\n" },
    { L"Deep", L"Abyssal Fifths",
      "root=.1429\na.algo=.0\na.morph=.5\na.timbre=.28\na.color=.42\na.voices=.4286\na.detune=.2\n"
      "b.on=1\nb.algo=.0\nb.morph=.4\nb.timbre=.14\nb.color=.28\nb.semi=.6458\nb.voices=.4286\nb.detune=.22\nb.level=.65\n"
      "temperament=.25\nflt.cutoff=.58\nflt.res=.3\nflt.mode=.3333\nvec.y=.25\norbit.depth=.15\nrev.mix=.5\nrev.size=.85\n" },

    { L"Swarm", L"Hive Mind",
      "root=.2857\na.algo=.1818\na.morph=.66\na.timbre=.4\na.color=.15\na.voices=1\na.detune=.45\na.drift=.45\n"
      "b.on=1\nb.algo=.1818\nb.morph=.33\nb.timbre=.7\nb.oct=.625\nb.voices=.7143\nb.detune=.5\nb.level=.45\n"
      "flt.cutoff=.68\nmod1.src=.1818\nmod1.dst=.71875\nmod1.amt=.6\nens.mix=.5\nrev.mix=.4\n" },
    { L"Swarm", L"Solar Wind",
      "root=.3571\na.algo=.1818\na.morph=.66\na.timbre=.6\na.color=.5\na.voices=.8571\na.detune=.65\na.drift=.6\n"
      "flt.mode=.5\nflt.cutoff=.3\nflt.res=.1\nphs.mix=.6\nphs.rate=.25\nphs.fb=.55\nrev.mix=.55\nrev.shimmer=.25\nvec.x=0\nvec.y=0\norbit.depth=0\n" },

    { L"Spectral", L"Slow Spectra",
      "mst.volume=.825\nroot=.2857\na.algo=.0909\na.table=.0526\na.morph=.5\na.scan.depth=1\na.scan.rate=.2\na.voices=.4286\na.detune=.2\n"
      "b.on=1\nb.algo=.0909\nb.table=.8947\nb.morph=.3\nb.scan.depth=.9\nb.scan.rate=.15\nb.oct=.625\nb.level=.5\nb.voices=.2857\n"
      "flt.cutoff=.8\nrev.mix=.5\nrev.size=.85\ndly.mix=.2\n" },
    { L"Spectral", L"Overtone Singer",
      "root=.2143\na.algo=.2727\na.morph=.72\na.timbre=.7\na.color=0\na.voices=.2857\na.drift=.15\n"
      "b.on=1\nb.algo=.5455\nb.morph=.1\nb.scan.depth=.7\nb.scan.rate=.15\nb.oct=.625\nb.level=.3\n"
      "temperament=.25\nflt.mode=0\nrev.mix=.55\nrev.size=.9\nrev.decay=.8\n" },
    { L"Spectral", L"Glass Harmonica",
      "root=.4286\na.algo=.0909\na.table=.4211\na.morph=.5\na.scan.depth=.6\na.scan.rate=.25\na.voices=.4286\na.detune=.1\n"
      "b.on=1\nb.algo=.2727\nb.morph=.55\nb.timbre=.8\nb.color=.3\nb.level=.45\n"
      "flt.mode=0\nrev.mix=.6\nrev.shimmer=.3\nrev.size=.8\ndly.mix=.25\ndly.time=.72\n" },
    { L"Spectral", L"Tanpura",
      "root=.2857\na.algo=.0909\na.table=.7368\na.morph=.5\na.scan.depth=.9\na.scan.rate=.5\na.voices=.1429\na.detune=.1\na.drift=.1\n"
      "b.on=1\nb.algo=.0909\nb.table=.7368\nb.semi=.6458\nb.morph=.2\nb.scan.depth=.9\nb.scan.rate=.45\nb.level=.6\nb.voices=.1429\n"
      "c.on=1\nc.algo=.0909\nc.table=.7368\nc.oct=.625\nc.morph=.7\nc.scan.depth=.9\nc.scan.rate=.48\nc.level=.5\nc.voices=.1429\n"
      "temperament=.25\nflt.mode=0\nrev.mix=.3\nrev.size=.6\norbit.depth=.2\n" },

    { L"Strings", L"Bowed Metal",
      "root=.2143\na.algo=.7273\na.morph=.5\na.timbre=.2\na.color=.4\na.voices=.4286\na.detune=.1\n"
      "b.on=1\nb.algo=.7273\nb.semi=.6458\nb.morph=.35\nb.timbre=.35\nb.color=.3\nb.voices=.2857\nb.level=.6\n"
      "flt.cutoff=.75\nrev.mix=.5\nrev.size=.85\nens.mix=.2\n" },
    { L"Strings", L"Cello Section",
      "root=.1429\na.algo=.7273\na.morph=.3\na.timbre=.5\na.color=.5\na.voices=.4286\na.detune=.12\na.drift=.25\n"
      "flt.mode=.1667\nflt.cutoff=.68\nens.mix=.45\nrev.mix=.45\nrev.size=.75\nvec.x=0\nvec.y=0\norbit.depth=0\n" },

    { L"Air", L"Wind Choir",
      "root=.2857\na.algo=.8182\na.morph=.75\na.timbre=.5\na.color=.8\na.voices=.1429\n"
      "b.on=1\nb.algo=.5455\nb.morph=.8\nb.scan.depth=.5\nb.color=.5\nb.level=.5\n"
      "flt.mode=0\nmod1.src=.1818\nmod1.dst=.0\nmod1.amt=.5\nrev.mix=.6\nrev.size=.9\n" },
    { L"Air", L"Night Grains",
      "root=.3571\na.algo=.9091\na.table=.0526\na.morph=.5\na.timbre=.6\na.color=.65\na.detune=.1\na.drift=.2\n"
      "b.on=1\nb.algo=.6364\nb.morph=.8\nb.oct=.375\nb.color=.3\nb.level=.4\n"
      "flt.mode=0\ndly.mix=.3\ndly.fb=.6\nrev.mix=.6\nrev.size=.85\nrev.shimmer=.15\n" },
    { L"Air", L"Glacier Breath",
      "mst.volume=.825\nroot=.2143\na.algo=.8182\na.morph=.9\na.timbre=.65\na.color=.45\na.voices=.1429\n"
      "b.on=1\nb.algo=.8182\nb.morph=.85\nb.timbre=.35\nb.oct=.625\nb.color=.7\nb.voices=.1429\nb.level=.5\n"
      "flt.mode=.5\nflt.cutoff=.65\nflt.res=.25\nmod1.src=.2727\nmod1.dst=.71875\nmod1.amt=.7\nrev.mix=.7\nrev.size=1\nrev.decay=.9\n" },

    { L"Machine", L"Rust Engine",
      "root=.2143\na.algo=1\na.morph=.45\na.timbre=.4\na.color=.5\na.voices=.2857\na.detune=.1\n"
      "b.on=1\nb.algo=.4545\nb.morph=.5\nb.timbre=.35\nb.color=.3\nb.oct=.375\nb.level=.5\n"
      "flt.mode=1\nflt.cutoff=.6\nflt.res=.45\nflt.drive=.5\nmod1.src=.3636\nmod1.dst=.71875\nmod1.amt=.65\nlfo4.rate=.4\nrev.mix=.25\ndly.mix=.2\n" },
    { L"Machine", L"Lyra Storm",
      "mst.volume=.825\nroot=.2857\na.algo=1\na.morph=.65\na.timbre=.5\na.color=.25\na.voices=.2857\na.detune=.2\n"
      "b.on=1\nb.algo=1\nb.morph=.5\nb.timbre=.26\nb.color=.3\nb.semi=.6458\nb.voices=.2857\nb.level=.6\n"
      "flt.cutoff=.7\ndly.mix=.35\ndly.fb=.65\ndly.time=.65\nrev.mix=.4\nmod1.src=.3636\nmod1.dst=.0\nmod1.amt=.5\n" },
    { L"Machine", L"FM Monolith",
      "root=.1429\na.algo=.3636\na.morph=.45\na.timbre=.1\na.color=.3\na.voices=.4286\na.detune=.12\n"
      "b.on=1\nb.algo=.3636\nb.morph=.6\nb.timbre=.27\nb.color=.1\nb.oct=.625\nb.level=.45\nb.voices=.2857\n"
      "mod1.src=.0909\nmod1.dst=.0313\nmod1.amt=.65\nmod2.src=.1818\nmod2.dst=.1875\nmod2.amt=.65\nflt.cutoff=.72\nrev.mix=.35\nens.mix=.25\n" },
    { L"Machine", L"Folded Sun",
      "root=.2857\na.algo=.4545\na.morph=.5\na.scan.depth=.7\na.scan.rate=.25\na.timbre=.6\na.color=.3\na.voices=.4286\na.detune=.18\n"
      "flt.mode=.3333\nflt.cutoff=.7\nens.mix=.3\nrev.mix=.45\nvec.x=0\nvec.y=0\norbit.depth=0\n" },

    { L"Crossfade", L"Four Corners",
      "root=.2857\na.algo=0\na.timbre=0\na.color=.14\nb.on=1\nb.algo=.1818\nb.morph=.66\nb.voices=.7143\nb.detune=.4\n"
      "c.on=1\nc.algo=.0909\nc.table=.2105\nc.morph=.5\nc.scan.depth=.6\nc.oct=.625\nd.on=1\nd.algo=.6364\nd.morph=.7\nd.semi=.6458\n"
      "orbit.depth=1\norbit.rate=.42\nrev.mix=.5\nrev.size=.8\nflt.cutoff=.75\n" },
    { L"Crossfade", L"Wave Tide",
      "root=.2857\na.algo=0\na.morph=.5\na.scan.depth=1\na.scan.rate=.28\na.timbre=0\na.color=.29\na.voices=.4286\na.detune=.15\n"
      "b.on=1\nb.algo=0\nb.morph=.5\nb.scan.depth=1\nb.scan.rate=.22\nb.timbre=.14\nb.color=.86\nb.semi=.6458\nb.voices=.4286\nb.detune=.15\nb.level=.6\n"
      "temperament=.25\nflt.cutoff=.65\nflt.res=.2\nrev.mix=.45\nens.mix=.3\n" },
    { L"Crossfade", L"Vowel Drift",
      "root=.2143\na.algo=.5455\na.morph=.5\na.scan.depth=1\na.scan.rate=.25\na.timbre=.5\na.color=.1\na.voices=.4286\na.detune=.12\n"
      "b.on=1\nb.algo=.5455\nb.morph=.3\nb.scan.depth=1\nb.scan.rate=.2\nb.timbre=.6\nb.oct=.625\nb.voices=.4286\nb.level=.5\n"
      "c.on=1\nc.algo=.6364\nc.morph=.6\nc.color=.4\nc.oct=.375\nc.level=.5\norbit.mode=1\norbit.depth=.6\norbit.rate=.3\nrev.mix=.55\nrev.size=.85\n" },
};
} // namespace

std::wstring PresetBank::DataFolder()
{
    std::wstring folder;
    PWSTR appData = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appData)))
    {
        folder = appData;
        CoTaskMemFree(appData);
    }
    folder += L"\\Drone 2";
    CreateDirectoryW(folder.c_str(), nullptr);
    return folder;
}

std::wstring PresetBank::UserFolder()
{
    const std::wstring folder = DataFolder() + L"\\Patches";
    CreateDirectoryW(folder.c_str(), nullptr);
    return folder;
}

std::wstring PresetBank::NameFromPath(const std::wstring& path)
{
    const size_t slash = path.find_last_of(L"\\/");
    std::wstring name = slash == std::wstring::npos ? path : path.substr(slash + 1);
    const size_t dot = name.find_last_of(L'.');
    if (dot != std::wstring::npos)
        name.erase(dot);
    return name;
}

std::wstring PresetBank::PathForName(const std::wstring& name)
{
    return UserFolder() + L"\\" + name + L".d2patch";
}

bool PresetBank::IsValidName(const std::wstring& name)
{
    if (name.empty() || name.size() > 100 || name.back() == L'.' || name.back() == L' ' || name[0] == L' ')
        return false;
    for (wchar_t c : name)
        if (c < 32 || wcschr(L"<>:\"/\\|?*", c))
            return false;
    // Device names cannot be file names on Windows.
    std::wstring upper = name;
    for (wchar_t& c : upper)
        c = towupper(c);
    static const wchar_t* const reserved[] = { L"CON", L"PRN", L"AUX", L"NUL" };
    for (const wchar_t* r : reserved)
        if (upper == r)
            return false;
    if (upper.size() == 4 && (upper.rfind(L"COM", 0) == 0 || upper.rfind(L"LPT", 0) == 0) && iswdigit(upper[3]))
        return false;
    return true;
}

void PresetBank::Refresh()
{
    m_presets.clear();
    for (const FactoryPatch& f : kFactory)
        m_presets.push_back({ f.name, f.category, L"", f.text });

    std::vector<Preset> user;
    WIN32_FIND_DATAW fd;
    const std::wstring folder = UserFolder();
    HANDLE find = FindFirstFileW((folder + L"\\*.d2patch").c_str(), &fd);
    if (find != INVALID_HANDLE_VALUE)
    {
        do
        {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            {
                const std::wstring path = folder + L"\\" + fd.cFileName;
                user.push_back({ NameFromPath(path), L"", path, "" });
            }
        } while (FindNextFileW(find, &fd));
        FindClose(find);
    }
    std::sort(user.begin(), user.end(), [](const Preset& a, const Preset& b)
              { return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0; });
    m_presets.insert(m_presets.end(), user.begin(), user.end());
}

size_t PresetBank::FactoryCount() const
{
    return std::size(kFactory);
}

int PresetBank::IndexOfName(const std::wstring& name) const
{
    int found = -1;
    for (size_t i = 0; i < m_presets.size(); ++i)
        if (_wcsicmp(m_presets[i].name.c_str(), name.c_str()) == 0)
            found = static_cast<int>(i);
    return found;
}

ParamValues PresetBank::Defaults()
{
    ParamValues values{};
    for (int id = 0; id < P_COUNT; ++id)
        values[id] = Info(id).defaultValue;
    return values;
}

void PresetBank::Parse(const std::string& text, ParamValues& values)
{
    values = Defaults();
    size_t pos = 0;
    while (pos < text.size())
    {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(pos, end - pos);
        pos = end + 1;

        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
            line.pop_back();
        const size_t eq = line.find('=');
        if (line.empty() || line[0] == ';' || eq == std::string::npos)
            continue;
        const std::string key = line.substr(0, eq);
        for (int id = 0; id < P_COUNT; ++id)
        {
            if (key == Info(id).key)
            {
                values[id] = Quantize(id, static_cast<float>(atof(line.c_str() + eq + 1)));
                break;
            }
        }
    }
}

std::string PresetBank::Serialize(const ParamValues& values)
{
    std::string text = "; Drone 2 patch\r\n";
    char line[128];
    for (int id = 0; id < P_COUNT; ++id)
    {
        sprintf_s(line, "%s=%.5f\r\n", Info(id).key, values[id]);
        text += line;
    }
    return text;
}

bool PresetBank::Load(size_t index, ParamValues& values) const
{
    if (index >= m_presets.size())
        return false;
    const Preset& p = m_presets[index];
    std::string text = p.text;
    if (!p.path.empty() && !ReadTextFile(p.path, text))
        return false;
    Parse(text, values);
    return true;
}

bool PresetBank::ReadTextFile(const std::wstring& path, std::string& text)
{
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || !f)
        return false;
    text.clear();
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        text.append(buf, n);
    fclose(f);
    return true;
}

bool PresetBank::WriteTextFile(const std::wstring& path, const std::string& text)
{
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"wb") != 0 || !f)
        return false;
    const bool ok = fwrite(text.data(), 1, text.size(), f) == text.size();
    return fclose(f) == 0 && ok;
}

ParamValues PresetBank::Randomize(unsigned seed)
{
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> uni(0.0f, 1.0f);
    auto range = [&](float lo, float hi) { return lo + (hi - lo) * uni(rng); };

    ParamValues v = Defaults();
    v[P_ROOT] = FromStep(85, static_cast<int>(range(12.0f, 40.0f)));
    v[P_TEMPERAMENT] = FromStep(TMP_COUNT, uni(rng) < 0.7f ? TMP_JUST : static_cast<int>(range(0.0f, 4.99f)));
    v[P_ATTACK] = range(0.35f, 0.6f);
    v[P_ORBIT_MODE] = FromStep(ORB_COUNT, static_cast<int>(range(0.0f, 2.99f)));
    v[P_ORBIT_RATE] = range(0.15f, 0.5f);
    v[P_ORBIT_DEPTH] = range(0.1f, 0.8f);

    // Consonant intervals, so a random patch is still a usable drone.
    static const int intervals[] = { 0, 0, 7, 12, 5, 19, -5, 24, 4, 3 };
    const int layers = 2 + static_cast<int>(uni(rng) * 3.0f);
    for (int l = 0; l < kNumLayers; ++l)
    {
        v[LayerId(l, LP_ON)] = l < layers ? 1.0f : 0.0f;
        v[LayerId(l, LP_ALGO)] = FromStep(ALG_COUNT, static_cast<int>(range(0.0f, ALG_COUNT - 0.01f)));
        v[LayerId(l, LP_TABLE)] = FromStep(wavetables::kNumTables, static_cast<int>(range(1.0f, wavetables::kNumTables - 0.01f)));
        v[LayerId(l, LP_MORPH)] = uni(rng);
        v[LayerId(l, LP_SCAN_DEPTH)] = range(0.0f, 0.9f);
        v[LayerId(l, LP_SCAN_RATE)] = range(0.1f, 0.5f);
        v[LayerId(l, LP_TIMBRE)] = uni(rng);
        v[LayerId(l, LP_COLOR)] = range(0.0f, 0.7f);
        v[LayerId(l, LP_SEMI)] = FromStep(49, 24 + intervals[static_cast<int>(uni(rng) * 9.99f)]);
        v[LayerId(l, LP_VOICES)] = FromStep(8, static_cast<int>(range(1.0f, 7.99f)));
        v[LayerId(l, LP_DETUNE)] = range(0.05f, 0.45f);
        v[LayerId(l, LP_DRIFT)] = range(0.1f, 0.5f);
        v[LayerId(l, LP_LEVEL)] = range(0.5f, 0.8f);
        v[LayerId(l, LP_PAN)] = range(0.35f, 0.65f);
    }

    v[P_FLT_CUTOFF] = range(0.55f, 0.9f);
    v[P_FLT_RES] = range(0.0f, 0.4f);
    v[P_ENS_MIX] = range(0.0f, 0.5f);
    v[P_PHS_MIX] = uni(rng) < 0.25f ? range(0.2f, 0.6f) : 0.0f;
    v[P_DLY_MIX] = range(0.0f, 0.35f);
    v[P_DLY_TIME] = range(0.5f, 0.85f);
    v[P_REV_SIZE] = range(0.5f, 1.0f);
    v[P_REV_DECAY] = range(0.5f, 0.9f);
    v[P_REV_MIX] = range(0.3f, 0.7f);
    v[P_REV_SHIMMER] = uni(rng) < 0.3f ? range(0.1f, 0.5f) : 0.0f;

    // One slow sweep of the filter and a little movement on the first layer.
    v[ModId(0, MODP_SRC)] = FromStep(SRC_COUNT, SRC_LFO2);
    v[ModId(0, MODP_DST)] = FromStep(DST_COUNT, DST_CUTOFF);
    v[ModId(0, MODP_AMT)] = range(0.55f, 0.7f);
    v[ModId(1, MODP_SRC)] = FromStep(SRC_COUNT, SRC_LFO1);
    v[ModId(1, MODP_DST)] = FromStep(DST_COUNT, DST_LAYER_BASE + LD_TIMBRE);
    v[ModId(1, MODP_AMT)] = range(0.5f, 0.7f);
    return v;
}
