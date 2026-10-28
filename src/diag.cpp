// Diagnostics for new content: logs the data files the game looks for and can't find,
// which shows what a new building, set or prop is still missing.
//
// The game checks for files through one function (FUN_009d3660):
//     bool __cdecl FileExists(const String* path, int* sizeOut)
// It looks in the pak archives first, then (when allowed) in loose files under Data\.

#include "diag.h"

#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>

#include "MinHook.h"
#include "mod.h"
#include "overlay.h"

namespace diag {
namespace {

constexpr uintptr_t kFileExists = 0x009D3660;
constexpr size_t    kMaxLogged  = 500;

struct GameString { const char* text; int len; int cap; };

using FileExists_t = bool(__cdecl*)(const GameString*, int*);
FileExists_t g_origFileExists = nullptr;

std::set<std::string> g_seen;
std::string g_watch;   // also log successful lookups containing this text (e.g. "tv_")

bool __cdecl HookFileExists(const GameString* path, int* size)
{
    bool found = g_origFileExists(path, size);
    if (path && path->text && path->len > 0 && path->len < 260 && g_seen.size() < kMaxLogged) {
        std::string p(path->text, path->len);
        bool watched = !g_watch.empty() && p.find(g_watch) != std::string::npos;
        if ((!found || watched) && g_seen.insert(p).second)
            mod::Log("file: %s '%s'", found ? "found" : "MISSING", p.c_str());
    }
    return found;
}

} // namespace

void Install()
{

    wchar_t path[MAX_PATH], watch[64];
    swprintf_s(path, L"%s\\MoviesMod.ini", mod::GameDir());
    if (!GetPrivateProfileIntW(L"MoviesMod", L"LogMissingFiles", 1, path))
        return;
    GetPrivateProfileStringW(L"MoviesMod", L"WatchFiles", L"facility_tv_", watch, 64, path);
    for (wchar_t* c = watch; *c; c++)
        g_watch += static_cast<char>(*c);

    void* target = reinterpret_cast<void*>(kFileExists);
    if (MH_CreateHook(target, &HookFileExists, reinterpret_cast<void**>(&g_origFileExists)) == MH_OK &&
        MH_EnableHook(target) == MH_OK)
        mod::Log("file check hook installed (logging missing files%s%s)", g_watch.empty() ? "" : " and any matching ",
                 g_watch.c_str());
    else
        mod::Log("FAILED to hook file check");
}

// ---------------------------------------------------------------------------------
// Crash reports. An access violation inside the game's own code is logged with its
// registers and the return addresses found on the stack (game addresses as they are,
// mod ones as dinput8+offset, which build\dinput8.map resolves). A crash nothing
// handles also writes MoviesMod_crash.dmp next to the game.

namespace {

uintptr_t g_exeBase, g_exeEnd, g_dllBase, g_dllEnd;
LONG g_reports = 0;

void ModuleRange(HMODULE m, uintptr_t* base, uintptr_t* end)
{
    auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(m);
    auto nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(reinterpret_cast<const char*>(m) + dos->e_lfanew);
    *base = reinterpret_cast<uintptr_t>(m);
    *end = *base + nt->OptionalHeader.SizeOfImage;
}

// A return address: in the game or the mod, right after a call instruction.
bool LooksLikeReturn(uintptr_t a)
{
    bool exe = a > g_exeBase + 0x1000 && a < g_exeEnd, dll = a > g_dllBase + 0x1000 && a < g_dllEnd;
    if (!exe && !dll)
        return false;
    const uint8_t* p = reinterpret_cast<const uint8_t*>(a);
    __try {
        return p[-5] == 0xE8 || (p[-6] == 0xFF && (p[-5] & 0x38) == 0x10) || (p[-2] == 0xFF && (p[-1] & 0x38) == 0x10) ||
               (p[-3] == 0xFF && (p[-2] & 0x38) == 0x10) || (p[-7] == 0xFF && (p[-6] & 0x38) == 0x10);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void Describe(char* out, size_t size, uintptr_t a)
{
    if (a >= g_dllBase && a < g_dllEnd)
        sprintf_s(out, size, "dinput8+%06X", static_cast<unsigned>(a - g_dllBase));
    else
        sprintf_s(out, size, "%08X", static_cast<unsigned>(a));
}

// Up to 24 return addresses from the stack, as text.
void StackText(uintptr_t esp, char* out, size_t size)
{
    out[0] = 0;
    int found = 0;
    for (int i = 0; i < 2048 && found < 24; i++) {
        uintptr_t v = 0;
        __try {
            v = reinterpret_cast<const uintptr_t*>(esp)[i];
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            break;
        }
        if (!LooksLikeReturn(v))
            continue;
        char s[32];
        Describe(s, sizeof s, v);
        if (found++)
            strcat_s(out, size, " ");
        strcat_s(out, size, s);
    }
}

void Report(const char* what, EXCEPTION_POINTERS* ep)
{
    const CONTEXT* c = ep->ContextRecord;
    const EXCEPTION_RECORD* r = ep->ExceptionRecord;
    char at[32], stack[24 * 20];
    Describe(at, sizeof at, c->Eip);
    mod::Log("CRASH (%s): code %08X at %s, %s address %08X", what, r->ExceptionCode, at,
             r->NumberParameters > 0 && r->ExceptionInformation[0] ? "writing" : "reading",
             r->NumberParameters > 1 ? static_cast<unsigned>(r->ExceptionInformation[1]) : 0u);
    mod::Log("  eax %08X ebx %08X ecx %08X edx %08X esi %08X edi %08X ebp %08X esp %08X", c->Eax, c->Ebx, c->Ecx,
             c->Edx, c->Esi, c->Edi, c->Ebp, c->Esp);
    StackText(c->Esp, stack, sizeof stack);
    mod::Log("  stack: %s", stack);
}

LONG CALLBACK FirstChance(EXCEPTION_POINTERS* ep)
{
    uintptr_t eip = ep->ContextRecord->Eip;
    // Only the game's own faults: the mod's guarded reads raise and handle these on purpose.
    if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && eip > g_exeBase && eip < g_exeEnd &&
        InterlockedIncrement(&g_reports) <= 3)
        Report("game code", ep);
    return EXCEPTION_CONTINUE_SEARCH;
}

LONG WINAPI Unhandled(EXCEPTION_POINTERS* ep)
{
    uintptr_t eip = ep->ContextRecord->Eip;
    if (ep->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || !(eip > g_exeBase && eip < g_exeEnd))
        Report("unhandled", ep);
    mod::Log("CRASH: the game is closing; writing MoviesMod_crash.dmp");
    wchar_t path[MAX_PATH];
    swprintf_s(path, L"%s\\MoviesMod_crash.dmp", mod::GameDir());
    HMODULE dbghelp = LoadLibraryW(L"dbghelp.dll");
    using Write_t = BOOL(WINAPI*)(HANDLE, DWORD, HANDLE, int, void*, void*, void*);
    auto write = dbghelp ? reinterpret_cast<Write_t>(GetProcAddress(dbghelp, "MiniDumpWriteDump")) : nullptr;
    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (write && file != INVALID_HANDLE_VALUE) {
        struct { DWORD thread; EXCEPTION_POINTERS* ep; BOOL client; } info = { GetCurrentThreadId(), ep, FALSE };
        write(GetCurrentProcess(), GetCurrentProcessId(), file, 0x1 | 0x4 /* data segments, handles */, &info,
              nullptr, nullptr);
    }
    if (file != INVALID_HANDLE_VALUE)
        CloseHandle(file);
    return EXCEPTION_CONTINUE_SEARCH;
}

} // namespace

// ---------------------------------------------------------------------------------
// Hang watchdog. Every frame the game presents beats a heartbeat (from the Present hook). If
// none comes for a while after the first, a watcher thread logs where the game's thread is:
// its instruction address and the return addresses on its stack (as in a crash report), at
// 30, 60 and 120 seconds, so a busy loop (the address moves) can be told from a wait. The
// first report also writes MoviesMod_hang.dmp. It never stops the game; if frames come back
// it logs how long the stall was.

namespace {

volatile LONG g_beat = 0;          // GetTickCount() of the last frame, 0 before the first
volatile DWORD g_mainThread = 0;   // the thread that presents frames
DWORD g_hangSeconds = 30;

void Heartbeat()
{
    g_mainThread = GetCurrentThreadId();
    InterlockedExchange(&g_beat, static_cast<LONG>(GetTickCount()));
}

void ReportHang(DWORD seconds, bool dump)
{
    HANDLE t = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, g_mainThread);
    if (!t) {
        mod::Log("HANG: no frame for %u s, and the game's thread can't be opened", static_cast<unsigned>(seconds));
        return;
    }
    CONTEXT c{};
    c.ContextFlags = CONTEXT_FULL;
    bool got = SuspendThread(t) != static_cast<DWORD>(-1) && GetThreadContext(t, &c);
    char at[32] = "?", stack[24 * 20] = "";
    if (got) {
        Describe(at, sizeof at, c.Eip);
        StackText(c.Esp, stack, sizeof stack);
    }
    ResumeThread(t);
    mod::Log("HANG: no frame for %u s - the game's thread is at %s", static_cast<unsigned>(seconds), at);
    if (got) {
        mod::Log("  eax %08X ebx %08X ecx %08X edx %08X esi %08X edi %08X ebp %08X esp %08X", c.Eax, c.Ebx, c.Ecx,
                 c.Edx, c.Esi, c.Edi, c.Ebp, c.Esp);
        mod::Log("  stack: %s", stack);
    }
    if (dump) {
        wchar_t path[MAX_PATH];
        swprintf_s(path, L"%s\\MoviesMod_hang.dmp", mod::GameDir());
        HMODULE dbghelp = LoadLibraryW(L"dbghelp.dll");
        using Write_t = BOOL(WINAPI*)(HANDLE, DWORD, HANDLE, int, void*, void*, void*);
        auto write = dbghelp ? reinterpret_cast<Write_t>(GetProcAddress(dbghelp, "MiniDumpWriteDump")) : nullptr;
        HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        bool ok = write && file != INVALID_HANDLE_VALUE &&
                  write(GetCurrentProcess(), GetCurrentProcessId(), file, 0x1 | 0x4 /* data segments, handles */,
                        nullptr, nullptr, nullptr);
        if (file != INVALID_HANDLE_VALUE)
            CloseHandle(file);
        mod::Log("HANG: %s MoviesMod_hang.dmp", ok ? "wrote" : "FAILED to write");
    }
    CloseHandle(t);
}

DWORD WINAPI WatchThread(void*)
{
    const DWORD marks[] = { g_hangSeconds, g_hangSeconds * 2, g_hangSeconds * 4 };
    int reported = 0;
    for (;;) {
        Sleep(1000);
        LONG beat = g_beat;
        if (!beat)
            continue;   // no frame yet: still starting up
        DWORD quiet = (GetTickCount() - static_cast<DWORD>(beat)) / 1000;
        if (quiet < 2) {
            if (reported)
                mod::Log("HANG: frames are back (the game was stuck for at least %u s)",
                         static_cast<unsigned>(marks[reported - 1]));
            reported = 0;
            continue;
        }
        if (reported < 3 && quiet >= marks[reported]) {
            ReportHang(quiet, reported == 0);
            reported++;
        }
    }
}

} // namespace

void InstallHangWatch()
{
    wchar_t path[MAX_PATH];
    swprintf_s(path, L"%s\\MoviesMod.ini", mod::GameDir());
    g_hangSeconds = GetPrivateProfileIntW(L"MoviesMod", L"HangWatchSeconds", 30, path);
    if (!g_hangSeconds)
        return;
    if (!g_exeBase) {   // the crash log is off: the stack reader still needs the module ranges
        HMODULE self = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&InstallHangWatch), &self);
        ModuleRange(GetModuleHandleW(nullptr), &g_exeBase, &g_exeEnd);
        ModuleRange(self, &g_dllBase, &g_dllEnd);
    }
    overlay::AddFrameCallback(&Heartbeat);
    HANDLE t = CreateThread(nullptr, 0, &WatchThread, nullptr, 0, nullptr);
    if (t)
        CloseHandle(t);
    mod::Log("hang watch on (%u s without a frame is logged, with MoviesMod_hang.dmp)", static_cast<unsigned>(g_hangSeconds));
}

void InstallCrashLog()
{
    wchar_t path[MAX_PATH];
    swprintf_s(path, L"%s\\MoviesMod.ini", mod::GameDir());
    if (!GetPrivateProfileIntW(L"MoviesMod", L"LogCrashes", 1, path))
        return;
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&InstallCrashLog), &self);
    ModuleRange(GetModuleHandleW(nullptr), &g_exeBase, &g_exeEnd);
    ModuleRange(self, &g_dllBase, &g_dllEnd);
    AddVectoredExceptionHandler(1, &FirstChance);
    SetUnhandledExceptionFilter(&Unhandled);
    mod::Log("crash log on (faults in game code are logged; a fatal one also writes MoviesMod_crash.dmp)");
}

} // namespace diag
