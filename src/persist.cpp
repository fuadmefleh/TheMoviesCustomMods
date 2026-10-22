// Save-game integration.
//
// Saving: when the game opens "...\Saved Games\X.jad" for writing (or renames a temp
// file onto it), we write "X.tvs" next to it from the registered sections.
//
// Loading: the load menu also opens every save to read its header, so opening a .jad
// for reading is not proof of a load. We remember the last .jad opened for reading
// (including Data\Scenarios\*.jad, which a new game starts from) and apply it when the
// game world actually changes: a different player studio, or the date jumping.

#include "persist.h"

#include <windows.h>
#include <cstdio>
#include <vector>

#include "MinHook.h"
#include "game.h"
#include "mod.h"

namespace persist {
namespace {

struct Registration {
    std::string name;
    SaveFn      save;
    LoadFn      load;
    ResetFn     reset;
};

std::vector<Registration> g_systems;
CRITICAL_SECTION          g_lock;
std::wstring              g_lastReadJad;     // most recent .jad the game opened for reading
std::wstring              g_currentSave;
uintptr_t                 g_lastStudio = 0;
float                     g_lastDays = -1;

using CreateFileW_t = HANDLE(WINAPI*)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
using CreateFileA_t = HANDLE(WINAPI*)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
using MoveFileW_t   = BOOL(WINAPI*)(LPCWSTR, LPCWSTR);
using MoveFileA_t   = BOOL(WINAPI*)(LPCSTR, LPCSTR);
using DeleteFileW_t = BOOL(WINAPI*)(LPCWSTR);
using DeleteFileA_t = BOOL(WINAPI*)(LPCSTR);

CreateFileW_t g_origCreateFileW = nullptr;
CreateFileA_t g_origCreateFileA = nullptr;
MoveFileW_t   g_origMoveFileW = nullptr;
MoveFileA_t   g_origMoveFileA = nullptr;
DeleteFileW_t g_origDeleteFileW = nullptr;
DeleteFileA_t g_origDeleteFileA = nullptr;

std::wstring Widen(const char* s)
{
    if (!s)
        return L"";
    int n = MultiByteToWideChar(CP_ACP, 0, s, -1, nullptr, 0);
    std::wstring w(n > 0 ? n - 1 : 0, L'\0');
    if (n > 1)
        MultiByteToWideChar(CP_ACP, 0, s, -1, w.data(), n);
    return w;
}

std::wstring Lower(std::wstring s)
{
    for (wchar_t& c : s)
        c = towlower(c);
    return s;
}

bool EndsWith(const std::wstring& s, const wchar_t* suffix)
{
    size_t n = wcslen(suffix);
    return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

bool IsJad(const std::wstring& path) { return EndsWith(Lower(path), L".jad"); }
bool IsPlayerSave(const std::wstring& path) { return IsJad(path) && Lower(path).find(L"\\saved games\\") != std::wstring::npos; }

std::wstring SidecarPath(const std::wstring& jad) { return jad.substr(0, jad.size() - 4) + L".tvs"; }

std::string Escape(const std::string& v)
{
    std::string out;
    for (char c : v) {
        if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else out += c;
    }
    return out;
}

std::string Unescape(const std::string& v)
{
    std::string out;
    for (size_t i = 0; i < v.size(); i++) {
        if (v[i] == '\\' && i + 1 < v.size()) {
            char n = v[++i];
            out += n == 'n' ? '\n' : n == 'r' ? '\r' : n;
        } else {
            out += v[i];
        }
    }
    return out;
}

void WriteSidecar(const std::wstring& jad)
{
    std::wstring path = SidecarPath(jad);
    std::wstring temp = path + L".tmp";
    FILE* f = _wfopen(temp.c_str(), L"wb");
    if (!f) {
        mod::Log("persist: could not write %ls", path.c_str());
        return;
    }
    fprintf(f, "# MoviesMod - The Movies: Television & Streaming save data\n");
    for (const Registration& r : g_systems) {
        Section sec;
        r.save(sec);
        fprintf(f, "[%s]\n", r.name.c_str());
        for (const auto& [k, v] : sec)
            fprintf(f, "%s=%s\n", k.c_str(), Escape(v).c_str());
    }
    fclose(f);
    MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
    g_currentSave = jad;
    mod::Log("persist: saved %zu sections to %ls", g_systems.size(), path.c_str());
}

void ResetAll()
{
    for (const Registration& r : g_systems)
        r.reset();
}

void LoadSidecar(const std::wstring& jad)
{
    ResetAll();
    std::wstring path = SidecarPath(jad);
    FILE* f = _wfopen(path.c_str(), L"rb");
    if (!f) {
        mod::Log("persist: %ls has no mod data - starting fresh", jad.c_str());
        return;
    }
    std::map<std::string, Section> sections;
    std::string current;
    char line[8192];
    while (fgets(line, sizeof(line), f)) {
        std::string s = line;
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r'))
            s.pop_back();
        if (s.empty() || s[0] == '#')
            continue;
        if (s[0] == '[' && s.back() == ']') {
            current = s.substr(1, s.size() - 2);
            sections[current];
        } else if (auto eq = s.find('='); eq != std::string::npos) {
            sections[current][s.substr(0, eq)] = Unescape(s.substr(eq + 1));
        }
    }
    fclose(f);
    for (const Registration& r : g_systems)
        if (auto it = sections.find(r.name); it != sections.end())
            r.load(it->second);
    mod::Log("persist: loaded %zu sections from %ls", sections.size(), path.c_str());
}

void OnOpen(const std::wstring& path, DWORD access)
{
    if (!IsJad(path))
        return;
    EnterCriticalSection(&g_lock);
    if (access & GENERIC_WRITE) {
        if (IsPlayerSave(path))
            WriteSidecar(path);
    } else {
        g_lastReadJad = path;
    }
    LeaveCriticalSection(&g_lock);
}

void OnMove(const std::wstring& to)
{
    if (!IsPlayerSave(to))
        return;
    EnterCriticalSection(&g_lock);
    WriteSidecar(to);
    LeaveCriticalSection(&g_lock);
}

void OnDelete(const std::wstring& path)
{
    if (IsPlayerSave(path) && g_origDeleteFileW)
        g_origDeleteFileW(SidecarPath(path).c_str());
}

HANDLE WINAPI HookCreateFileW(LPCWSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD disp, DWORD flags, HANDLE tmpl)
{
    if (name)
        OnOpen(name, access);
    return g_origCreateFileW(name, access, share, sa, disp, flags, tmpl);
}

HANDLE WINAPI HookCreateFileA(LPCSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD disp, DWORD flags, HANDLE tmpl)
{
    if (name)
        OnOpen(Widen(name), access);
    return g_origCreateFileA(name, access, share, sa, disp, flags, tmpl);
}

BOOL WINAPI HookMoveFileW(LPCWSTR from, LPCWSTR to)
{
    BOOL ok = g_origMoveFileW(from, to);
    if (ok && to)
        OnMove(to);
    return ok;
}

BOOL WINAPI HookMoveFileA(LPCSTR from, LPCSTR to)
{
    BOOL ok = g_origMoveFileA(from, to);
    if (ok && to)
        OnMove(Widen(to));
    return ok;
}

BOOL WINAPI HookDeleteFileW(LPCWSTR name)
{
    BOOL ok = g_origDeleteFileW(name);
    if (ok && name)
        OnDelete(name);
    return ok;
}

BOOL WINAPI HookDeleteFileA(LPCSTR name)
{
    BOOL ok = g_origDeleteFileA(name);
    if (ok && name)
        OnDelete(Widen(name));
    return ok;
}

template <typename T>
void HookApi(const char* fn, T detour, T* original)
{
    if (MH_CreateHookApi(L"kernel32", fn, reinterpret_cast<LPVOID>(detour), reinterpret_cast<LPVOID*>(original)) == MH_OK &&
        MH_EnableHook(MH_ALL_HOOKS) == MH_OK)
        mod::Log("persist: hooked %s", fn);
    else
        mod::Log("persist: FAILED to hook %s", fn);
}

} // namespace

void Register(const char* name, SaveFn save, LoadFn load, ResetFn reset)
{
    g_systems.push_back({ name, save, load, reset });
}

void Install()
{
    InitializeCriticalSection(&g_lock);
    HookApi("CreateFileW", &HookCreateFileW, &g_origCreateFileW);
    HookApi("CreateFileA", &HookCreateFileA, &g_origCreateFileA);
    HookApi("MoveFileW", &HookMoveFileW, &g_origMoveFileW);
    HookApi("MoveFileA", &HookMoveFileA, &g_origMoveFileA);
    HookApi("DeleteFileW", &HookDeleteFileW, &g_origDeleteFileW);
    HookApi("DeleteFileA", &HookDeleteFileA, &g_origDeleteFileA);
}

void Tick()
{
    if (!game::Ready()) {
        g_lastStudio = 0;
        return;
    }
    // Detect a different game: a new studio object, or the clock moving backwards or
    // leaping forward (loading a later save). Normal play advances well under a day per frame.
    uintptr_t studio = game::ReadPlayerStudio().address;
    float days = game::DateDays();
    bool switched = studio != g_lastStudio || (g_lastDays >= 0 && (days < g_lastDays - 0.01f || days > g_lastDays + 30.0f));
    g_lastStudio = studio;
    g_lastDays = days;
    if (!switched)
        return;

    EnterCriticalSection(&g_lock);
    std::wstring source = g_lastReadJad;
    LeaveCriticalSection(&g_lock);
    if (IsPlayerSave(source)) {
        g_currentSave = source;
        LoadSidecar(source);
    } else {
        g_currentSave.clear();
        ResetAll();
        mod::Log("persist: new game (from %ls) - mod data reset", source.empty() ? L"?" : source.c_str());
    }
}

const std::wstring& CurrentSave() { return g_currentSave; }

} // namespace persist
