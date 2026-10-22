// Gameplay features. Each feature reads its settings from MoviesMod.ini and
// installs its hooks here. Addresses come from reverse-engineering MoviesSE.exe
// v1.2 (image base 0x400000, no ASLR) in Ghidra.

#include <windows.h>
#include <share.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "MinHook.h"
#include "mod.h"

namespace {

wchar_t g_iniPath[MAX_PATH];

// ---------------------------------------------------------------------------
// Tuning overrides
//
// The game reads nearly all of its balance values through one getter on a
// config-file object (688 call sites):
//
//   float __thiscall TuningFile::GetFloat(const String* key, float def, int flag)  @ 0x00558610
//
// The object's file path ("data/mood", ...) is a String at this+0xA8, set by
// TuningFile::Load @ 0x0055BE10. Strings are { char* text; int len; int cap; char inline[20] }.
// Hooking the getter lets us log every value the game asks for and replace any
// of them from MoviesMod.ini, e.g.
//
//   [mood]
//   WORKINGONAMOVIESTRESS = *0.5   ; multiply the game's value
//   DRINKINGSTRESS        = 2.0    ; or replace it outright
// ---------------------------------------------------------------------------

struct GameString {
    const char* text;
    int         len;
    int         cap;
};

constexpr DWORD kTuningGetFloat   = 0x00558610;
constexpr DWORD kTuningPathOffset = 0xA8;

struct Override {
    bool  multiply;
    float value;
};

std::map<std::string, Override> g_overrides;   // "file:key" (lower-case) -> override
std::set<std::string>           g_seen;        // keys already written to the catalog
CRITICAL_SECTION                g_tuningLock;
FILE*                           g_catalog = nullptr;
bool                            g_dumpCatalog = true;

using GetFloat_t = float(__fastcall*)(void* self, void* edx, const GameString* key, float def, int flag);
GetFloat_t g_origGetFloat = nullptr;
// Text values (lot.ini's "300,224" sizes...) come from the getter both of the others use:
//   String* __thiscall TuningFile::GetString(String* out, const String* key, int flag)  @ 0x005584E0
// A quoted value in MoviesMod.ini ([lot] 2 = "360,260") replaces the text; unquoted
// values stay number overrides, so the number getters' own lookups are never changed.
constexpr DWORD kTuningGetString = 0x005584E0;
constexpr DWORD kStringAssign    = 0x004015D0;   // String::Assign(const char*, int)
using GetString_t = GameString*(__fastcall*)(void* self, void* edx, GameString* out, const GameString* key, int flag);
GetString_t g_origGetString = nullptr;
std::map<std::string, std::string> g_textOverrides;   // "file:key" -> text

// Whole-number values (maxlitterobjects, revenue_base...) come through a twin getter.
constexpr DWORD kTuningGetInt = 0x00558750;
using GetInt_t = int(__fastcall*)(void* self, void* edx, const GameString* key, int def, int flag);
GetInt_t g_origGetInt = nullptr;

std::string Lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(tolower(c)); });
    return s;
}

std::string Narrow(const wchar_t* w)
{
    char buf[512];
    WideCharToMultiByte(CP_ACP, 0, w, -1, buf, sizeof(buf), nullptr, nullptr);
    return buf;
}

// "data/mood.txt" -> "mood"
std::string TuningFileName(void* self)
{
    auto path = reinterpret_cast<const GameString*>(static_cast<char*>(self) + kTuningPathOffset);
    if (IsBadReadPtr(path, sizeof(*path)) || !path->text || IsBadStringPtrA(path->text, 260))
        return "?";
    std::string name = Lower(path->text);
    std::replace(name.begin(), name.end(), '\\', '/');
    if (name.rfind("data/", 0) == 0)
        name.erase(0, 5);
    if (auto dot = name.find_last_of('.'); dot != std::string::npos && name.find('/', dot) == std::string::npos)
        name.erase(dot);
    return name;
}

float __fastcall HookGetFloat(void* self, void* edx, const GameString* key, float def, int flag)
{
    float value = g_origGetFloat(self, edx, key, def, flag);
    if (!key || !key->text)
        return value;

    std::string file = TuningFileName(self);
    std::string id   = file + ":" + Lower(key->text);
    float result     = value;

    if (auto it = g_overrides.find(id); it != g_overrides.end())
        result = it->second.multiply ? value * it->second.value : it->second.value;

    EnterCriticalSection(&g_tuningLock);
    if (g_seen.insert(id).second) {
        if (g_catalog) {
            fprintf(g_catalog, "%-24s %-40s default=%-10g game=%-10g%s\n", file.c_str(), key->text, def, value,
                    result != value ? (" -> " + std::to_string(result)).c_str() : "");
            fflush(g_catalog);
        }
        if (result != value)
            mod::Log("override %s: %g -> %g", id.c_str(), value, result);
    }
    LeaveCriticalSection(&g_tuningLock);
    return result;
}

int __fastcall HookGetInt(void* self, void* edx, const GameString* key, int def, int flag)
{
    int value = g_origGetInt(self, edx, key, def, flag);
    if (!key || !key->text)
        return value;
    std::string file = TuningFileName(self);
    std::string id   = file + ":" + Lower(key->text);
    int result       = value;
    if (auto it = g_overrides.find(id); it != g_overrides.end())
        result = static_cast<int>(it->second.multiply ? value * it->second.value : it->second.value);

    EnterCriticalSection(&g_tuningLock);
    if (g_seen.insert(id).second) {
        if (g_catalog) {
            fprintf(g_catalog, "%-24s %-40s default=%-10d game=%-10d (whole number)%s\n", file.c_str(), key->text, def, value,
                    result != value ? (" -> " + std::to_string(result)).c_str() : "");
            fflush(g_catalog);
        }
        if (result != value)
            mod::Log("override %s: %d -> %d", id.c_str(), value, result);
    }
    LeaveCriticalSection(&g_tuningLock);
    return result;
}

GameString* __fastcall HookGetString(void* self, void* edx, GameString* out, const GameString* key, int flag)
{
    GameString* result = g_origGetString(self, edx, out, key, flag);
    if (!out || !key || !key->text || key->len <= 0 || key->len > 64)
        return result;
    std::string id = TuningFileName(self) + ":" + Lower(std::string(key->text, key->len));
    auto it = g_textOverrides.find(id);
    if (it == g_textOverrides.end())
        return result;
    std::string text = it->second;
    // Lot sizes ("width,depth"): never smaller than what the game's data (or another lot
    // mod's lot.ini) already says.
    if (id.rfind("lot:", 0) == 0 && out->text && out->len > 0 && out->len < 64) {
        float ow = 0, od = 0, nw = 0, nd = 0;
        std::string orig(out->text, out->len);
        if (sscanf_s(orig.c_str(), "%f,%f", &ow, &od) == 2 && sscanf_s(text.c_str(), "%f,%f", &nw, &nd) == 2)
            text = std::to_string(static_cast<int>(nw > ow ? nw : ow)) + "," + std::to_string(static_cast<int>(nd > od ? nd : od));
    }
    using Assign_t = void(__thiscall*)(GameString*, const char*, int);
    reinterpret_cast<Assign_t>(kStringAssign)(out, text.c_str(), static_cast<int>(text.size()));
    EnterCriticalSection(&g_tuningLock);
    if (g_seen.insert(id + "#text").second)
        mod::Log("override %s: \"%s\"", id.c_str(), text.c_str());
    LeaveCriticalSection(&g_tuningLock);
    return result;
}

void LoadTuningOverrides()
{
    std::vector<wchar_t> sections(32768);
    GetPrivateProfileSectionNamesW(sections.data(), static_cast<DWORD>(sections.size()), g_iniPath);
    for (const wchar_t* sec = sections.data(); *sec; sec += wcslen(sec) + 1) {
        // Sections that configure the mod itself rather than a game tuning file.
        if (_wcsicmp(sec, L"MoviesMod") == 0 || _wcsicmp(sec, L"text") == 0 || _wcsicmp(sec, L"tv") == 0 ||
            _wcsicmp(sec, L"Ratings") == 0 ||
            _wcsicmp(sec, L"Manager") == 0 ||
            _wcsicmp(sec, L"MovieMaker") == 0 ||
            _wcsicmp(sec, L"BuildMenu") == 0)
            continue;
        std::vector<wchar_t> body(32768);
        GetPrivateProfileSectionW(sec, body.data(), static_cast<DWORD>(body.size()), g_iniPath);
        for (const wchar_t* line = body.data(); *line; line += wcslen(line) + 1) {
            std::wstring entry = line;
            auto eq = entry.find(L'=');
            if (eq == std::wstring::npos || entry[0] == L';')
                continue;
            auto trim = [](std::wstring s) {
                s.erase(0, s.find_first_not_of(L" \t"));
                s.erase(s.find_last_not_of(L" \t") + 1);
                return s;
            };
            std::wstring k = trim(entry.substr(0, eq));
            std::wstring v = trim(entry.substr(eq + 1));
            if (auto semi = v.find(L';'); semi != std::wstring::npos)
                v = trim(v.substr(0, semi));
            if (v.size() >= 2 && v.front() == L'"' && v.back() == L'"') {   // a text value
                g_textOverrides[Lower(Narrow(sec)) + ":" + Lower(Narrow(k.c_str()))] = Narrow(v.substr(1, v.size() - 2).c_str());
                continue;
            }
            Override o{ !v.empty() && v[0] == L'*', 0.0f };
            o.value = static_cast<float>(_wtof(v.c_str() + (o.multiply ? 1 : 0)));
            g_overrides[Lower(Narrow(sec)) + ":" + Lower(Narrow(k.c_str()))] = o;
        }
    }
    mod::Log("tuning overrides loaded: %u numbers, %u texts", static_cast<unsigned>(g_overrides.size()),
             static_cast<unsigned>(g_textOverrides.size()));
}

bool InstallTuningHook()
{
    InitializeCriticalSection(&g_tuningLock);
    if (g_dumpCatalog) {
        wchar_t path[MAX_PATH];
        swprintf_s(path, L"%s\\MoviesMod_tuning.txt", mod::GameDir());
        g_catalog = _wfsopen(path, L"w", _SH_DENYWR);
        if (g_catalog)
            fprintf(g_catalog, "# Every tuning value the game read this session (file, key, built-in default, value from game data, override)\n");
    }
    auto target = reinterpret_cast<LPVOID>(kTuningGetFloat);
    if (MH_CreateHook(target, reinterpret_cast<LPVOID>(&HookGetFloat), reinterpret_cast<LPVOID*>(&g_origGetFloat)) != MH_OK ||
        MH_EnableHook(target) != MH_OK) {
        mod::Log("tuning hook FAILED");
        return false;
    }
    mod::Log("tuning hook installed at 0x%08X", kTuningGetFloat);
    if (!g_textOverrides.empty()) {
        target = reinterpret_cast<LPVOID>(kTuningGetString);
        if (MH_CreateHook(target, reinterpret_cast<LPVOID>(&HookGetString), reinterpret_cast<LPVOID*>(&g_origGetString)) !=
                MH_OK ||
            MH_EnableHook(target) != MH_OK)
            mod::Log("tuning hook (text) FAILED");
    }
    target = reinterpret_cast<LPVOID>(kTuningGetInt);
    if (MH_CreateHook(target, reinterpret_cast<LPVOID>(&HookGetInt), reinterpret_cast<LPVOID*>(&g_origGetInt)) != MH_OK ||
        MH_EnableHook(target) != MH_OK)
        mod::Log("tuning hook (whole numbers) FAILED");
    return true;
}

} // namespace

namespace mod {

void LoadConfig()
{
    swprintf_s(g_iniPath, L"%s\\MoviesMod.ini", GameDir());
    if (GetFileAttributesW(g_iniPath) == INVALID_FILE_ATTRIBUTES) {
        Log("MoviesMod.ini not found - using defaults");
        return;
    }
    Log("config: %ls", g_iniPath);
    g_dumpCatalog = GetPrivateProfileIntW(L"MoviesMod", L"DumpTuning", 1, g_iniPath) != 0;
    LoadTuningOverrides();
}

void InstallHooks()
{
    int installed = 0;
    if (GetPrivateProfileIntW(L"MoviesMod", L"TuningOverrides", 1, g_iniPath) && InstallTuningHook())
        installed++;
    ratings::Install();
    Log("hooks installed: %d", installed);
}

} // namespace mod
