// Debug screenshots: a picture of every game window as it opens, while you play.
//
// Every screen of the game is built in code and added to the root window (WWindow virtual
// +0x0C AddChild, or +0xAC AddPopup for dialogs; see nativeui.cpp), so hooking the root's two
// functions sees each one open. The window's RTTI class names it; a moment later (once it
// has laid out and animated in) the overlay saves the frame to
// <game>\MoviesMod\shots\win_<time>_<class>.bmp. Each class is shot at most every
// DebugShotsCooldown seconds. Ctrl+F12 saves the screen at any time.
//
// [MoviesMod] DebugShots = 1 turns it on; DebugShotsIgnore lists class names to skip
// (comma-separated, matched as substrings).

#include "debugshots.h"

#include <windows.h>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "MinHook.h"
#include "game.h"
#include "mod.h"
#include "overlay.h"

namespace debugshots {
namespace {

constexpr uintptr_t kRootWindow = 0x0104DF40;   // the full-screen layer every screen is added to
constexpr int kAddChildSlot = 0x0C / 4;         // WWindow::AddChild(child, mode)
constexpr int kAddPopupSlot = 0xAC / 4;         // WWindow::AddPopup(child)
constexpr DWORD kSettleMs = 800;                // windows slide and fade in

using AddChild_t = uintptr_t(__thiscall*)(void* self, void* child, int mode);
using AddPopup_t = uintptr_t(__thiscall*)(void* self, void* child);
AddChild_t g_origAddChild = nullptr;
AddPopup_t g_origAddPopup = nullptr;
bool g_hooked = false;

struct Pending {
    std::string name;
    DWORD       at;
};
std::vector<Pending> g_pending;
std::map<std::string, DWORD> g_lastShot;   // class -> tick of its last picture
std::vector<std::string> g_ignore;
DWORD g_cooldownMs = 20000;
void* g_lastChild = nullptr;               // AddChild and AddPopup often come in a pair
bool g_keyDown = false;

void* Root() { return *reinterpret_cast<void**>(kRootWindow); }

std::string TimeStamp()
{
    SYSTEMTIME t;
    GetLocalTime(&t);
    char buf[32];
    sprintf_s(buf, "%02d%02d%02d_%03d", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    return buf;
}

void Opened(void* self, void* child)
{
    if (self != Root() || !child || child == g_lastChild)
        return;
    g_lastChild = child;
    std::string cls = game::ClassName(reinterpret_cast<uintptr_t>(child));
    if (cls.empty())
        cls = "unknown";
    for (const std::string& skip : g_ignore)
        if (cls.find(skip) != std::string::npos)
            return;
    DWORD now = GetTickCount();
    auto it = g_lastShot.find(cls);
    if (it != g_lastShot.end() && now - it->second < g_cooldownMs)
        return;
    g_lastShot[cls] = now;
    mod::Log("debugshots: %s opened", cls.c_str());
    g_pending.push_back({ "win_" + TimeStamp() + "_" + cls, now + kSettleMs });
}

uintptr_t __fastcall HookAddChild(void* self, void*, void* child, int mode)
{
    uintptr_t r = g_origAddChild(self, child, mode);
    Opened(self, child);
    return r;
}

uintptr_t __fastcall HookAddPopup(void* self, void*, void* child)
{
    uintptr_t r = g_origAddPopup(self, child);
    Opened(self, child);
    return r;
}

// The root exists once the game's interface does: hook its class's two functions then.
void TryHook()
{
    void* root = Root();
    if (!root)
        return;
    g_hooked = true;
    void** vtable = *reinterpret_cast<void***>(root);
    void* addChild = vtable[kAddChildSlot];
    void* addPopup = vtable[kAddPopupSlot];
    bool ok = MH_CreateHook(addChild, reinterpret_cast<LPVOID>(&HookAddChild), reinterpret_cast<LPVOID*>(&g_origAddChild)) == MH_OK &&
              MH_EnableHook(addChild) == MH_OK;
    if (ok && addPopup != addChild)
        ok = MH_CreateHook(addPopup, reinterpret_cast<LPVOID>(&HookAddPopup), reinterpret_cast<LPVOID*>(&g_origAddPopup)) == MH_OK &&
             MH_EnableHook(addPopup) == MH_OK;
    mod::Log("debugshots: %s root window %s (AddChild %p, AddPopup %p)", ok ? "hooked" : "FAILED to hook",
             game::ClassName(reinterpret_cast<uintptr_t>(root)).c_str(), addChild, addPopup);
}

void Frame()
{
    if (!g_hooked)
        TryHook();
    DWORD now = GetTickCount();
    for (size_t i = 0; i < g_pending.size();) {
        if (static_cast<int>(now - g_pending[i].at) >= 0) {
            overlay::RequestScreenshot(g_pending[i].name.c_str());
            g_pending.erase(g_pending.begin() + i);
        } else {
            i++;
        }
    }
    // Ctrl+F12, only while the game has the focus.
    bool down = (GetAsyncKeyState(VK_F12) & 0x8000) && (GetAsyncKeyState(VK_CONTROL) & 0x8000);
    if (down && !g_keyDown) {
        DWORD pid = 0;
        GetWindowThreadProcessId(GetForegroundWindow(), &pid);
        if (pid == GetCurrentProcessId()) {
            std::string name = "key_" + TimeStamp();
            mod::Log("debugshots: %s", name.c_str());
            overlay::RequestScreenshot(name.c_str());
        }
    }
    g_keyDown = down;
}

} // namespace

void Install()
{
    wchar_t ini[MAX_PATH];
    swprintf_s(ini, L"%s\\MoviesMod.ini", mod::GameDir());
    if (!GetPrivateProfileIntW(L"MoviesMod", L"DebugShots", 0, ini))
        return;
    g_cooldownMs = 1000u * GetPrivateProfileIntW(L"MoviesMod", L"DebugShotsCooldown", 20, ini);
    wchar_t buf[512];
    GetPrivateProfileStringW(L"MoviesMod", L"DebugShotsIgnore", L"", buf, 512, ini);
    std::wstring list = buf;
    for (size_t pos = 0; pos <= list.size();) {
        size_t comma = list.find(L',', pos);
        std::wstring item = list.substr(pos, comma == std::wstring::npos ? std::wstring::npos : comma - pos);
        while (!item.empty() && item.front() == L' ')
            item.erase(item.begin());
        while (!item.empty() && item.back() == L' ')
            item.pop_back();
        if (!item.empty()) {
            std::string narrow;   // class names are ASCII
            for (wchar_t c : item)
                narrow += static_cast<char>(c);
            g_ignore.push_back(narrow);
        }
        if (comma == std::wstring::npos)
            break;
        pos = comma + 1;
    }
    mod::Log("debugshots: on - every game window as it opens, Ctrl+F12 any time (MoviesMod\\shots)");
    overlay::AddFrameCallback(&Frame);
}

} // namespace debugshots
