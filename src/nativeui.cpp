// The game's interface is C++ widgets built in code (Flash only animates small ornaments).
// Every screen is a W* window whose constructor creates its children with operator new +
// constructor + parent->AddChild, so the mod can compose screens from the same widgets:
//
//   WDialog       0x3A8 bytes, ctor 0x006889C0 (this, char closeButton): the build menu's
//                 frame with a title (SetTitle 0x006888D0) and an X button (+0x384);
//                 children go in its content window (+0x36C) via AddContent 0x006884E0.
//   WHTML         0x3FC bytes, ctor 0x00833290: rich text (tables, fonts, images), wrap
//                 width at +0x354, text set with virtual +0x54.
//   WImageButton  0x420 bytes, ctor 0x0069FB10 (this, String* texture base, WString* tooltip,
//                 float w, float h).
//   ShowConfirm   0x00669B80: the game's yes/no dialog (tick and cross).
//
// WWindow virtuals (byte offsets): +0x0C AddChild(child, mode), +0x10/+0x14 width/height,
// +0x18 SetHandler(type, fn, ctx, name) (type 0 = click; fn is bool __stdcall(sender, ctx)),
// +0x50 Layout, +0x54 SetText, +0x5C/+0x64 anchor left/top (mode, relative, offset),
// +0x60 anchor right, +0x78/+0x7C SetWidth/SetHeight, +0x8C fit height, +0xAC AddPopup.
// Root window [0x0104DF40]. Close-and-release handler 0x0071B530.
// Details: docs/GAME_INTERNALS.md "Native interface".

#include "nativeui.h"

#include <windows.h>
#include <cstdint>
#include <cstring>

#include "game.h"
#include "mod.h"
#include "overlay.h"

namespace nativeui {
namespace {

constexpr uintptr_t kNew           = 0x00ACBC50;   // void* __cdecl operator new(size)
constexpr uintptr_t kFree          = 0x00ACD4A1;
constexpr uintptr_t kWAssign       = 0x004036D0;   // WString::Assign(const wchar_t*, int)
constexpr uintptr_t kNAssign       = 0x004015D0;   // String::Assign(const char*, int)
constexpr uintptr_t kRootWindow    = 0x0104DF40;
constexpr uintptr_t kGameLoaded    = 0x0104BCE4;
constexpr uintptr_t kDialogCtor    = 0x006889C0;
constexpr uintptr_t kDialogTitle   = 0x006888D0;
constexpr uintptr_t kDialogAdd     = 0x006884E0;
constexpr uintptr_t kHtmlCtor      = 0x00833290;
constexpr uintptr_t kImgButtonCtor = 0x0069FB10;
constexpr uintptr_t kCloseHandler  = 0x0071B530;   // bool __stdcall(sender, window): hide and release
constexpr uintptr_t kNoTutorialLock = 0x005F37F0;  // the game registers it as every button's type-5 handler
constexpr uintptr_t kShowConfirm   = 0x00669B80;
constexpr int kDialogSize = 0x3A8, kHtmlSize = 0x3FC, kImgButtonSize = 0x420;
constexpr int kDialogContent = 0x36C, kDialogClose = 0x384, kHtmlWrap = 0x354;

struct WStr { wchar_t* text; int len; int cap; wchar_t inline_[10]; };   // cap 10 = inline
struct NStr { char* text; int len; int cap; char inline_[20]; };         // cap 0x14 = inline
using Handler = bool(__stdcall*)(void* sender, void* ctx);
using ConfirmCb = void(__stdcall*)(void* sender, void* dialog);

template <typename R = void, typename... A> R VCall(void* obj, int offset, A... args)
{
    auto fn = *reinterpret_cast<R(__thiscall**)(void*, A...)>(*reinterpret_cast<char**>(obj) + offset);
    return fn(obj, args...);
}

void* Ptr(void* base, int offset) { return *reinterpret_cast<void**>(static_cast<char*>(base) + offset); }
void* Root() { return *reinterpret_cast<void**>(kRootWindow); }

void MakeW(WStr& s, const wchar_t* v)
{
    s.text = s.inline_;
    s.len = 0;
    s.cap = 10;
    s.inline_[0] = 0;
    reinterpret_cast<void(__thiscall*)(WStr*, const wchar_t*, int)>(kWAssign)(&s, v, static_cast<int>(wcslen(v)));
}
void FreeW(WStr& s)
{
    if (s.cap > 10 && s.text != s.inline_)
        reinterpret_cast<void(__cdecl*)(void*)>(kFree)(s.text);
}
void MakeN(NStr& s, const char* v)
{
    s.text = s.inline_;
    s.len = 0;
    s.cap = 0x14;
    s.inline_[0] = 0;
    reinterpret_cast<void(__thiscall*)(NStr*, const char*, int)>(kNAssign)(&s, v, static_cast<int>(strlen(v)));
}
void FreeN(NStr& s)
{
    if (s.cap > 0x14 && s.text != s.inline_)
        reinterpret_cast<void(__cdecl*)(void*)>(kFree)(s.text);
}
void* GameNew(size_t size) { return reinterpret_cast<void*(__cdecl*)(size_t)>(kNew)(size); }

void SetClick(void* w, Handler fn, void* ctx, const char* name)
{
    VCall(w, 0x18, 0, fn, ctx, name);
    VCall(w, 0x18, 5, reinterpret_cast<Handler>(kNoTutorialLock), static_cast<void*>(nullptr), name);
}

// ---------------------------------------------------------------------------------
// State

Screen g_screen;
int    g_tab = 0;
void*  g_dialog = nullptr;
void*  g_html = nullptr;
DWORD  g_refreshed = 0;
void*  g_session = nullptr;   // [kGameLoaded] when our widgets were made

struct Hud {
    const char*  texture;
    std::wstring tooltip;
    std::function<void()> onClick;
    void*        widget = nullptr;
};
std::vector<Hud> g_hud;

std::function<void()> g_yes, g_no;

// ---------------------------------------------------------------------------------
// Game calls, each behind an SEH guard (no C++ objects in these functions).

bool __stdcall OnButton(void*, void* ctx);
bool __stdcall OnClose(void* sender, void* ctx);
bool __stdcall OnHud(void*, void* ctx);

void* NewImageButton(const char* texture, const wchar_t* tooltip, float w, float h)
{
    __try {
        NStr base;
        WStr tip;
        MakeN(base, texture);
        MakeW(tip, tooltip);
        void* b = GameNew(kImgButtonSize);
        reinterpret_cast<void*(__thiscall*)(void*, NStr*, WStr*, float, float)>(kImgButtonCtor)(b, &base, &tip, w, h);
        FreeN(base);
        FreeW(tip);
        return b;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

bool SetHtml(void* html, const wchar_t* text)
{
    __try {
        WStr s;
        MakeW(s, text);
        VCall(html, 0x54, &s);
        VCall<float>(html, 0x8C, 0);
        FreeW(s);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Builds the dialog, its HTML panel and buttons; returns false if anything faults.
bool BuildDialog(const wchar_t* title, float width, float height, int buttons, const char* const* textures,
                 const wchar_t* const* tooltips)
{
    __try {
        void* d = GameNew(kDialogSize);
        reinterpret_cast<void*(__thiscall*)(void*, char)>(kDialogCtor)(d, 1);
        SetClick(Ptr(d, kDialogClose), &OnClose, d, "DIALOGBOX_CLOSE");
        WStr t;
        MakeW(t, title);
        reinterpret_cast<void(__thiscall*)(void*, WStr*)>(kDialogTitle)(d, &t);
        FreeW(t);
        void* content = Ptr(d, kDialogContent);

        for (int i = 0; i < buttons; i++) {
            void* b = NewImageButton(textures[i], tooltips[i], 48, 48);
            if (!b)
                continue;
            SetClick(b, &OnButton, reinterpret_cast<void*>(static_cast<intptr_t>(i)), "MOVIESMOD_BUTTON");
            VCall(b, 0x5C, 1, content, 8.0f + 54.0f * i);
            VCall(b, 0x64, 1, content, 4.0f);
            reinterpret_cast<void(__thiscall*)(void*, void*, int)>(kDialogAdd)(d, b, 1);
        }

        void* h = GameNew(kHtmlSize);
        reinterpret_cast<void*(__thiscall*)(void*)>(kHtmlCtor)(h);
        VCall(h, 0x78, width - 48);
        *reinterpret_cast<float*>(static_cast<char*>(h) + kHtmlWrap) = width - 48;
        VCall(h, 0x5C, 1, content, 8.0f);
        VCall(h, 0x64, 1, content, 62.0f);
        reinterpret_cast<void(__thiscall*)(void*, void*, int)>(kDialogAdd)(d, h, 1);

        VCall(d, 0x78, width);
        VCall(d, 0x7C, height);
        for (int i = 0; i < 50 && VCall<bool>(d, 0x50, 1); i++) {
        }
        void* r = Root();
        VCall(r, 0x0C, d, 1);
        VCall(r, 0xAC, d);
        VCall(d, 0x5C, 1, r, (VCall<float>(r, 0x10) - width) * 0.5f);
        VCall(d, 0x64, 1, r, (VCall<float>(r, 0x14) - height) * 0.5f);
        g_dialog = d;
        g_html = h;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool CloseDialog(void* d)
{
    __try {
        reinterpret_cast<Handler>(kCloseHandler)(nullptr, d);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void* AddHudWidget(const char* texture, const wchar_t* tooltip, int index)
{
    __try {
        void* r = Root();
        void* b = NewImageButton(texture, tooltip, 48, 48);
        if (!r || !b)
            return nullptr;
        SetClick(b, &OnHud, reinterpret_cast<void*>(static_cast<intptr_t>(index)), "MOVIESMOD_HUD");
        VCall(r, 0x0C, b, 1);
        VCall(b, 0x60, 2, r, 12.0f);
        VCall(b, 0x64, 1, r, 140.0f + 56.0f * index);
        return b;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

void* CallConfirm(const wchar_t* title, const wchar_t* body, ConfirmCb yes, ConfirmCb no)
{
    __try {
        WStr t, b;
        MakeW(t, title);
        MakeW(b, body);   // passed by value: the dialog takes it over
        void* d = reinterpret_cast<void*(__cdecl*)(const WStr*, ConfirmCb, ConfirmCb, WStr)>(kShowConfirm)(&t, yes, no, b);
        FreeW(t);
        return d;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

// ---------------------------------------------------------------------------------
// Handlers (called by the game on its thread)

void ShowPage()
{
    if (!g_html || !g_screen.html)
        return;
    std::wstring page = g_screen.html(g_tab);
    if (!SetHtml(g_html, page.c_str()))
        mod::Log("nativeui: FAILED to set the page text");
    g_refreshed = GetTickCount();
}

bool __stdcall OnButton(void*, void* ctx)
{
    int i = static_cast<int>(reinterpret_cast<intptr_t>(ctx));
    if (i < 0 || i >= static_cast<int>(g_screen.buttons.size()))
        return true;
    const Button& b = g_screen.buttons[i];
    if (b.tab >= 0) {
        g_tab = b.tab;
        ShowPage();
    } else if (b.action) {
        b.action();
    }
    return true;
}

bool __stdcall OnClose(void* sender, void* ctx)
{
    reinterpret_cast<Handler>(kCloseHandler)(sender, ctx);
    if (ctx == g_dialog)
        g_dialog = g_html = nullptr;
    return true;
}

bool __stdcall OnHud(void*, void* ctx)
{
    size_t i = static_cast<size_t>(reinterpret_cast<intptr_t>(ctx));
    if (i < g_hud.size() && g_hud[i].onClick)
        g_hud[i].onClick();
    return true;
}

void __stdcall OnYes(void*, void*)
{
    auto fn = std::move(g_yes);
    g_yes = g_no = nullptr;
    if (fn)
        fn();
}

void __stdcall OnNo(void*, void*)
{
    auto fn = std::move(g_no);
    g_yes = g_no = nullptr;
    if (fn)
        fn();
}

void Frame()
{
    void* session = *reinterpret_cast<void**>(kGameLoaded);
    if (session != g_session) {
        // A game was loaded or quit: the old widgets are gone with the old interface.
        g_session = session;
        g_dialog = g_html = nullptr;
        for (Hud& h : g_hud)
            h.widget = nullptr;
    }
    if (!Ready())
        return;
    for (size_t i = 0; i < g_hud.size(); i++) {
        if (g_hud[i].widget)
            continue;
        g_hud[i].widget = AddHudWidget(g_hud[i].texture, g_hud[i].tooltip.c_str(), static_cast<int>(i));
        mod::Log("nativeui: HUD button %s %s", g_hud[i].texture, g_hud[i].widget ? "added" : "FAILED");
        if (!g_hud[i].widget)
            g_hud[i].widget = reinterpret_cast<void*>(1);   // don't retry every frame
    }
    if (g_dialog && GetTickCount() - g_refreshed > 2000)
        ShowPage();
}

} // namespace

bool g_enabled = false;
bool Enabled() { return g_enabled; }

bool Ready() { return *reinterpret_cast<void**>(kGameLoaded) && Root() && game::Ready(); }

void Confirm(const std::wstring& title, const std::wstring& body, std::function<void()> onYes,
             std::function<void()> onNo)
{
    if (!Ready())
        return;
    g_yes = std::move(onYes);
    g_no = std::move(onNo);
    if (!CallConfirm(title.c_str(), body.c_str(), &OnYes, &OnNo))
        mod::Log("nativeui: FAILED to show a yes/no dialog");
}

void Open(const Screen& screen, int tab)
{
    if (!Ready())
        return;
    Close();
    g_screen = screen;
    g_tab = tab;
    std::vector<const char*> textures;
    std::vector<const wchar_t*> tips;
    for (const Button& b : g_screen.buttons) {
        textures.push_back(b.texture);
        tips.push_back(b.tooltip.c_str());
    }
    if (!BuildDialog(g_screen.title.c_str(), g_screen.width, g_screen.height, static_cast<int>(textures.size()),
                     textures.data(), tips.data())) {
        mod::Log("nativeui: FAILED to build the screen");
        g_dialog = g_html = nullptr;
        return;
    }
    static bool logged = false;
    if (!logged) {
        logged = true;
        mod::Log("nativeui: screen open (thread %lu)", GetCurrentThreadId());
    }
    ShowPage();
}

void Close()
{
    if (g_dialog)
        CloseDialog(g_dialog);
    g_dialog = g_html = nullptr;
}

bool IsOpen() { return g_dialog != nullptr; }

void Refresh()
{
    if (g_dialog)
        ShowPage();
}

void AddHudButton(const char* texture, const std::wstring& tooltip, std::function<void()> onClick)
{
    g_hud.push_back({ texture, tooltip, std::move(onClick), nullptr });
}

std::wstring Escape(const std::string& utf8)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), w.data(), n);
    for (wchar_t& c : w)
        if (c == L'<')
            c = L'(';
        else if (c == L'>')
            c = L')';
    return w;
}

void Install()
{
    wchar_t ini[MAX_PATH];
    swprintf_s(ini, L"%s\\MoviesMod.ini", mod::GameDir());
    if (!GetPrivateProfileIntW(L"MoviesMod", L"NativeUI", 1, ini)) {
        mod::Log("nativeui: NativeUI = 0 - off");
        return;
    }
    g_enabled = true;
    overlay::AddFrameCallback(&Frame);
}

} // namespace nativeui
