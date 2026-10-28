// MoviesMod - proxy dinput8.dll for The Movies: Stunts & Effects (MoviesSE.exe v1.2).
//
// The game delay-loads DINPUT8.dll and only imports DirectInput8Create, so a
// dinput8.dll placed next to MoviesSE.exe is loaded instead of the system one.
// We forward that call to the real DLL and use the chance to run our own code
// inside the game process.

#include <windows.h>
#include <share.h>
#include <cstdarg>
#include <cstdio>

#include "MinHook.h"
#include "careers.h"
#include "mod.h"
#include "debugshots.h"
#include "nativeui.h"
#include "news.h"
#include "overlay.h"
#include "diag.h"
#include "persist.h"
#include "projectlist.h"
#include "rooms.h"
#include "text.h"
#include "tv.h"

namespace {

HMODULE g_self = nullptr;
FILE*   g_log  = nullptr;
wchar_t g_gameDir[MAX_PATH] = {};
bool    g_hooksEnabled = false;

// The build we reverse-engineered. Hooks use hard-coded addresses, so refuse
// to install them on any other executable.
constexpr DWORD kExpectedTimestamp = 0x44500000;   // Wed Apr 26 2006
constexpr DWORD kExpectedImageBase = 0x00400000;

bool IsExpectedGameBuild()
{
    auto base = reinterpret_cast<BYTE*>(GetModuleHandleW(nullptr));
    auto dos  = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto nt   = reinterpret_cast<IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
    mod::Log("exe image base 0x%08X, timestamp 0x%08X",
             reinterpret_cast<DWORD>(base), nt->FileHeader.TimeDateStamp);
    return reinterpret_cast<DWORD>(base) == kExpectedImageBase &&
           nt->FileHeader.TimeDateStamp == kExpectedTimestamp;
}

using DirectInput8Create_t = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);

DirectInput8Create_t RealDirectInput8Create()
{
    static DirectInput8Create_t real = [] {
        wchar_t path[MAX_PATH];
        GetSystemDirectoryW(path, MAX_PATH);   // SysWOW64 for a 32-bit process
        wcscat_s(path, L"\\dinput8.dll");
        HMODULE dll = LoadLibraryW(path);
        auto fn = dll ? reinterpret_cast<DirectInput8Create_t>(GetProcAddress(dll, "DirectInput8Create")) : nullptr;
        mod::Log("real dinput8.dll %s", fn ? "loaded" : "FAILED to load");
        return fn;
    }();
    return real;
}

void OnAttach()
{
    GetModuleFileNameW(g_self, g_gameDir, MAX_PATH);
    if (wchar_t* slash = wcsrchr(g_gameDir, L'\\'))
        *slash = L'\0';

    wchar_t logPath[MAX_PATH];
    swprintf_s(logPath, L"%s\\MoviesMod.log", g_gameDir);
    g_log = _wfsopen(logPath, L"w", _SH_DENYWR);   // fresh log every launch; readable while the game runs

    mod::Log("MoviesMod loaded (built " __DATE__ " " __TIME__ ")");

    if (!IsExpectedGameBuild()) {
        mod::Log("unexpected game build - hooks disabled, running as plain passthrough");
        return;
    }
    if (MH_Initialize() != MH_OK) {
        mod::Log("MH_Initialize failed - hooks disabled");
        return;
    }
    mod::LoadConfig();
    lists::MergeAll();
    mod::InstallHooks();
    text::Install();
    diag::Install();
    diag::InstallCrashLog();
    g_hooksEnabled = true;
}

} // namespace

namespace mod {

void Log(const char* fmt, ...)
{
    if (!g_log)
        return;
    SYSTEMTIME t;
    GetLocalTime(&t);
    fprintf(g_log, "[%02d:%02d:%02d.%03d] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list args;
    va_start(args, fmt);
    vfprintf(g_log, fmt, args);
    va_end(args);
    fputc('\n', g_log);
    fflush(g_log);
}

const wchar_t* GameDir() { return g_gameDir; }

} // namespace mod

extern "C" HRESULT WINAPI Proxy_DirectInput8Create(HINSTANCE inst, DWORD version, REFIID riid,
                                                   LPVOID* out, LPUNKNOWN outer)
{
    static bool first = true;
    if (first) {
        first = false;
        mod::Log("DirectInput8Create called by game (version 0x%04X)", version);
        // First call runs on the game thread, outside the loader lock: safe to set up the overlay.
        if (g_hooksEnabled) {
            overlay::Install();
            nativeui::Install();
            debugshots::Install();
            buildmenu::Install();
            careers::Install();
            projectlist::Install();
            persist::Install();
            overlay::AddFrameCallback(&persist::Tick);
            rooms::Install();
            tv::Install();
            news::Install();
            manager::Install();
            selfcheck::Install();
            moviemaker::Install();
            production::Install();
            toon::Install();
            mod::RegisterInspector();
        }
    }
    auto real = RealDirectInput8Create();
    HRESULT hr = real ? real(inst, version, riid, out, outer) : E_FAIL;
    if (SUCCEEDED(hr) && g_hooksEnabled && out)
        overlay::OnDirectInputCreated(*out);
    return hr;
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        g_self = module;
        DisableThreadLibraryCalls(module);
        OnAttach();
        break;
    case DLL_PROCESS_DETACH:
        mod::Log("MoviesMod unloading");
        MH_Uninitialize();
        if (g_log)
            fclose(g_log);
        break;
    }
    return TRUE;
}
