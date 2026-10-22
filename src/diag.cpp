// Diagnostics for new content: logs the data files the game looks for and can't find,
// which shows what a new building, set or prop is still missing.
//
// The game checks for files through one function (FUN_009d3660):
//     bool __cdecl FileExists(const String* path, int* sizeOut)
// It looks in the pak archives first, then (when allowed) in loose files under Data\.

#include "diag.h"

#include <windows.h>
#include <set>
#include <string>

#include "MinHook.h"
#include "mod.h"

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

} // namespace diag
