// Sandbox options: "Instant Script-writing", a toggle in the sandbox game's options window
// next to the game's own "Instant Movie-making".
//
// The window (WSandboxOptions, ctor 0x0070D020, the open one at [0x0104DE3C]) lays its
// options out in a grid (+0x628): it gives the grid one row height per option
// (0x0063F830(height); then 0x0063F890(1)), then each option's builder adds a row to it.
// The instant-shooting builder (0x0070AB20) makes a row window, a label (WHTML), a toggle
// button with an on/off picture (button_toggleoff. / ui/button_toggle{on,off}.dds), and
// its click handler (0x00704F00) flips a byte in the window and shows the option's
// description in the window's description panel (+0x658). We redirect two calls in the
// constructor: the grid's 0x0063F890 (one more row height first) and the instant-shooting
// builder after it (ours is built first, the same way). Each row is placed under the one
// before it: the next builder (0x0070AFD0) finds that row through the instant-shooting
// button (+0x3B8, the row is the button's parent), so it is shown ours for that call.
//
// Starting the game (0x006E1220) copies the window's toggles into the sandbox options
// ([0x0104A974] + 0x60 + i*4); option 6 is the kind of game, 2 = sandbox. Ours is kept by
// the mod: a new sandbox game takes the choice, and it is saved with the game (.tvs).
// 0x006E1220 only starts new games (sandbox and story), so it also tells persist that the
// game that follows is new: the menus read saves (the autosave) before it.
// Writing is instant through the script's AddWork (0x004BDBE0, thiscall(script, float),
// work done +0x124 clamped to the target +0x128): when it is on, each bit of work the
// writers do finishes the script.

#include <windows.h>
#include <cstdint>
#include <cstring>

#include "MinHook.h"
#include "mod.h"
#include "nativeui.h"
#include "persist.h"

namespace sandbox {
namespace {

constexpr uintptr_t kOptionsWindow  = 0x0104DE3C;
constexpr uintptr_t kSandboxOptions = 0x0104A974;
constexpr uintptr_t kGridRowsCall   = 0x0070D8FC;   // CALL 0x0063F890
constexpr uintptr_t kGridRowsDone   = 0x0063F890;   // thiscall(grid, int), RET 4
constexpr uintptr_t kGridAddRow     = 0x0063F830;   // thiscall(grid, float height), RET 4
constexpr uintptr_t kNextRowCall    = 0x0070D918;   // CALL 0x0070AFD0
constexpr uintptr_t kNextRow        = 0x0070AFD0;   // thiscall(window), the row after instant shooting
constexpr uintptr_t kScreenWindow   = 0x0071B2A0;   // the window the description width is judged by
constexpr uintptr_t kAddWork        = 0x004BDBE0;   // thiscall(script, float), RET 4
constexpr uintptr_t kStartNewGame   = 0x006E1220;   // thiscall(options window)
constexpr int kGrid = 0x628, kLabelColumn = 0x370, kButtonColumn = 0x358, kDescription = 0x658;
constexpr int kShootButton = 0x3B8, kParent = 0x118;
constexpr int kWorkDone = 0x124, kWorkTarget = 0x128;
constexpr int kOptionGameKind = 6;
constexpr float kSandboxGame = 2.0f, kRowHeight = 50.0f;

const wchar_t* const kLabel =
    L"<t2><table><tr><td align=left width=292><translate>SANDBOXOPTIONS_INSTANTWRITELABEL</translate></td></tr></table></t2>";

bool  g_choice = false;     // the toggle in the options window
bool  g_on = false;         // this game writes scripts instantly
void* g_picture = nullptr;  // the toggle's on/off picture, while the window is open
int   g_written = 0;        // scripts finished instantly this session (for the log)

using AddWork_t = void(__thiscall*)(uintptr_t, float);
AddWork_t g_origAddWork = nullptr;
using StartNewGame_t = void(__thiscall*)(void*);
StartNewGame_t g_origStartNewGame = nullptr;

template <typename R = void, typename... A> R VCall(void* obj, int offset, A... args)
{
    auto fn = *reinterpret_cast<R(__thiscall**)(void*, A...)>(*reinterpret_cast<char**>(obj) + offset);
    return fn(obj, args...);
}

void* Ptr(void* base, int offset) { return *reinterpret_cast<void**>(static_cast<char*>(base) + offset); }

int Fault(const char* what)
{
    mod::Log("sandbox: %s faulted", what);
    return EXCEPTION_EXECUTE_HANDLER;
}

// --- The options window --------------------------------------------------------------

void __fastcall GridRowsDone(void* grid, void*, int arg)
{
    __try {
        reinterpret_cast<void(__thiscall*)(void*, float)>(kGridAddRow)(grid, kRowHeight);
    } __except (Fault("the extra row")) {
    }
    reinterpret_cast<void(__thiscall*)(void*, int)>(kGridRowsDone)(grid, arg);
}

void ShowDescription()
{
    void* window = *reinterpret_cast<void**>(kOptionsWindow);
    void* panel = window ? Ptr(window, kDescription) : nullptr;
    if (!panel)
        return;
    // The game's handler: 150 wide when the screen has room right of the grid, else 125.
    int width = 125;
    __try {
        void* screen = reinterpret_cast<void*(__cdecl*)()>(kScreenWindow)();
        float right = *reinterpret_cast<float*>(static_cast<char*>(Ptr(window, kGrid)) + 0x108);
        if (VCall<float>(screen, 0x10) - right > 200.0f)
            width = 150;
    } __except (Fault("the description width")) {
    }
    wchar_t text[256];
    swprintf_s(text, L"<t2><table><tr><td align=center width=%d><translate>SANDBOXOPTIONS_INSTANTWRITEBUTTON_%s</translate></td></tr></table></t2>",
               width, g_choice ? L"ON" : L"OFF");
    nativeui::SetText(panel, text);
}

void Toggle()
{
    g_choice = !g_choice;
    if (g_picture)
        nativeui::SetPicture(g_picture, g_choice ? "ui/button_toggleon.dds" : "ui/button_toggleoff.dds");
    ShowDescription();
}

// Our row, built like the instant-shooting one (0x0070AB20).
bool LayOut(void* window, void* row, void* label, void* button, void* picture)
{
    __try {
        void* grid = Ptr(window, kGrid);
        VCall(row, 0x64, 2, Ptr(Ptr(window, kShootButton), kParent), -2.0f);   // under instant shooting
        VCall(row, 0x5C, 1, grid, 10.0f);
        VCall(row, 0x74, VCall<float>(grid, 0x10) - 18.0f, kRowHeight);
        VCall(grid, 0x0C, row, 1);

        VCall(label, 0x64, 1, row, (kRowHeight - VCall<float>(label, 0x14)) * 0.5f);
        VCall(label, 0x5C, 1, Ptr(window, kLabelColumn), 0.0f);
        VCall(row, 0x0C, label, 1);

        VCall(button, 0x5C, 1, Ptr(window, kButtonColumn), 0.0f);
        VCall(button, 0x64, 1, row, 1.0f);
        VCall(row, 0x0C, button, 1);

        VCall(picture, 0x70, button, 0);
        VCall(button, 0x0C, picture, 1);
        return true;
    } __except (Fault("laying out the row")) {
        return false;
    }
}

// Returns our toggle button, or null.
void* AddRow(void* window)
{
    g_choice = false;   // like the game's own toggles, off each time the window opens
    g_picture = nullptr;
    void* row = nativeui::NewContainer();
    void* label = row ? nativeui::NewText(kLabel) : nullptr;
    void* button = label ? nativeui::NewIconButton("button_toggleoff.", L"", 48.0f) : nullptr;
    void* picture = button ? nativeui::NewPicture("ui/button_toggleoff.dds") : nullptr;
    if (!picture || !nativeui::OnClick(button, "SANDBOXOPTIONS_INSTANTWRITECLICKED", &Toggle) ||
        !LayOut(window, row, label, button, picture)) {
        mod::Log("sandbox: FAILED to add Instant Script-writing to the options");
        return nullptr;
    }
    g_picture = picture;
    static bool logged = false;
    if (!logged) {
        logged = true;
        mod::Log("sandbox: Instant Script-writing added to the options");
    }
    return button;
}

void __fastcall NextRow(void* window)
{
    void* button = AddRow(window);
    auto shoot = reinterpret_cast<void**>(static_cast<char*>(window) + kShootButton);
    void* saved = *shoot;
    if (button)
        *shoot = button;   // the next row goes under ours
    reinterpret_cast<void(__thiscall*)(void*)>(kNextRow)(window);
    *shoot = saved;
}

// Points the CALL at 'at' (which must call 'expect') to 'to'.
bool RedirectCall(uintptr_t at, uintptr_t expect, const void* to, const char* what)
{
    auto code = reinterpret_cast<uint8_t*>(at);
    int32_t rel;
    memcpy(&rel, code + 1, 4);
    if (code[0] != 0xE8 || at + 5 + rel != expect) {
        mod::Log("sandbox: %s - code at %08X is not what we expect, left alone", what, static_cast<unsigned>(at));
        return false;
    }
    rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(to) - (at + 5));
    DWORD old;
    if (!VirtualProtect(code, 5, PAGE_EXECUTE_READWRITE, &old))
        return false;
    memcpy(code + 1, &rel, 4);
    VirtualProtect(code, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), code, 5);
    return true;
}

// --- Instant writing -----------------------------------------------------------------

bool IsSandboxGame()
{
    __try {
        auto options = *reinterpret_cast<uintptr_t*>(kSandboxOptions);
        return options && *reinterpret_cast<float*>(options + 0x60 + kOptionGameKind * 4) == kSandboxGame;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void __fastcall HookAddWork(uintptr_t script, void*, float work)
{
    if (g_on) {
        float target = *reinterpret_cast<float*>(script + kWorkTarget);
        if (work < target) {
            work = target;   // AddWork clamps the total to the target
            if (*reinterpret_cast<float*>(script + kWorkDone) < target && g_written++ < 20)
                mod::Log("sandbox: a script was written instantly");
        }
    }
    g_origAddWork(script, work);
}

void __fastcall HookStartNewGame(void* window)
{
    persist::NewGameStarting();
    g_origStartNewGame(window);
}

bool Hook(uintptr_t at, void* detour, void** original)
{
    void* target = reinterpret_cast<void*>(at);
    return MH_CreateHook(target, detour, original) == MH_OK && MH_EnableHook(target) == MH_OK;
}

} // namespace

void Install()
{
    bool rows = RedirectCall(kGridRowsCall, kGridRowsDone, reinterpret_cast<const void*>(&GridRowsDone), "grid rows");
    if (rows && !RedirectCall(kNextRowCall, kNextRow, reinterpret_cast<const void*>(&NextRow), "options row")) {
        // Without our row the extra height would leave a gap: put the grid's call back.
        uint8_t* code = reinterpret_cast<uint8_t*>(kGridRowsCall);
        int32_t rel = static_cast<int32_t>(kGridRowsDone - (kGridRowsCall + 5));
        DWORD old;
        if (VirtualProtect(code, 5, PAGE_EXECUTE_READWRITE, &old)) {
            memcpy(code + 1, &rel, 4);
            VirtualProtect(code, 5, old, &old);
        }
        rows = false;
    }
    bool work = Hook(kAddWork, reinterpret_cast<void*>(&HookAddWork), reinterpret_cast<void**>(&g_origAddWork));
    bool start = Hook(kStartNewGame, reinterpret_cast<void*>(&HookStartNewGame), reinterpret_cast<void**>(&g_origStartNewGame));
    mod::Log("sandbox: Instant Script-writing %s", rows && work && start ? "ready" : "FAILED to install");

    persist::Register(
        "sandbox",
        [](persist::Section& out) {
            if (g_on)
                out["instant_writing"] = "1";
        },
        [](const persist::Section& in) {
            auto it = in.find("instant_writing");
            g_on = it != in.end() && it->second == "1";
            if (g_on)
                mod::Log("sandbox: this game writes scripts instantly");
        },
        [] {
            // A new game, or a save without mod data: only a new sandbox game takes the choice.
            g_on = g_choice && persist::CurrentSave().empty() && IsSandboxGame();
            g_choice = false;
            if (g_on)
                mod::Log("sandbox: new sandbox game - scripts are written instantly");
        });
}

} // namespace sandbox
