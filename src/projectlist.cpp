// The HUD's right-hand column: the player's movie projects, plus a group of the mod's own.
//
// The column is one WMovieIconManager (0x3A4 bytes, singleton 0x0104EA94): every update it
// walks the global project list and keeps the player's projects that aren't "filed away"
// (FUN_005B3C00), one WMovieIcon card each, stacked down from the people card (re/hud_projects.md).
// The mod's share objects are CProjects too (draggable script objects), so they showed up as
// finished scripts. Now:
//   * the game's two "filed away?" calls (enumeration and each card's update) go through the
//     mod, which files a mod project away for the movie list and every movie away for ours;
//   * a second manager, built and anchored in the movie list's old place, lists only the mod's
//     projects, and the movie list hangs below it, shortened by its height; the three places
//     that use the singleton where they mean "this list" are pointed at the right one;
//   * their cards show the mod's picture (ui/shares_*.dds), make no film notifications or
//     chart sounds, and a click flies the camera to the object on the lot (the bag or the
//     briefcase) instead of opening a film's details.

#include "projectlist.h"

#include <windows.h>
#include <cstdint>
#include <cstring>

#include "MinHook.h"
#include "careers.h"
#include "mod.h"

namespace projectlist {
namespace {

template <typename T> T Rd(uintptr_t a) { return *reinterpret_cast<const T*>(a); }

constexpr uintptr_t kOperatorNew  = 0x00ACBC50;   // void* __cdecl(size)
constexpr uintptr_t kMgrCtor      = 0x007F2A60;   // WMovieIconManager* __fastcall(this), 0x3A4 bytes
constexpr uintptr_t kMgrClear     = 0x007F2830;   // void __fastcall(mgr)
constexpr uintptr_t kMgrUpdate    = 0x007F3240;   // void __fastcall(mgr)
constexpr uintptr_t kMgrEnumerate = 0x007F2FD0;   // void __fastcall(mgr)
constexpr uintptr_t kMgrDtor      = 0x007F2340;   // void __fastcall(mgr): the destructor body
constexpr uintptr_t kHasStunts    = 0x005B60B0;   // bool __fastcall(project): a scene with a stunt -> the card's skull
constexpr uintptr_t kSingletonRef = 0x0104EA80;   // CP<WMovieIconManager>: vtbl[1] unlink, value +0x14, vtbl[0] link
constexpr uintptr_t kSingleton    = 0x0104EA94;
constexpr uintptr_t kFiled        = 0x005B3C00;   // bool __fastcall(CProject*): filed away, not listed
constexpr uintptr_t kFiledCall1   = 0x007F305B;   // CALL kFiled in the enumeration
constexpr uintptr_t kFiledCall2   = 0x007EECAB;   // CALL kFiled in a card's update
constexpr uintptr_t kGetterCall1  = 0x007EECC5;   // CALL singleton getter: a card removing itself
constexpr uintptr_t kGetterCall2  = 0x007EF2A1;
constexpr uintptr_t kGetter       = 0x007EF840;
constexpr uintptr_t kSingletonUse = 0x007F04B6;   // MOV EAX,[singleton] where EBX is the manager
constexpr uintptr_t kIconUpdate   = 0x007EEC30;   // void __fastcall(icon)
constexpr uintptr_t kIconPicture  = 0x007EE480;   // void __fastcall(icon): the card's picture by stage
constexpr uintptr_t kIconNotify   = 0x007EC9B0;   // void __thiscall(icon, int stage, Obj by value): HUD_PROJECT_* messages, RET 0x40
constexpr uintptr_t kNotifyArgDtor = 0x00526BB0;  // void __fastcall(Obj*): what the callee runs on its by-value argument
constexpr uintptr_t kIconRating   = 0x007EC650;   // void __thiscall(icon, a, b): rating up/down sounds
constexpr uintptr_t kIconLeague   = 0x007EC8C0;   // void __fastcall(icon): chart position up/down
constexpr uintptr_t kIconInfo     = 0x007EC510;   // void __fastcall(icon): a click (slot 75, message 0x4B2)
constexpr uintptr_t kIconFlyTo    = 0x007EC540;   // void __fastcall(icon): camera to the card's object (slot 76)
constexpr uintptr_t kHudLayer     = 0x0071B2B0;   // Window* __cdecl()
constexpr uintptr_t kScreenRight  = 0x0071AFE0;   // float __cdecl()
constexpr uintptr_t kRightMargin  = 0x00D16B78;   // float
constexpr uintptr_t kIconWidth    = 0x00E5BD38;   // float
constexpr uintptr_t kIconHeight   = 0x00E5BD34;   // float
constexpr int kMgrRefs = 0x48, kMgrIcons = 0x358, kMgrCount = 0x364, kMgrSpacing = 0x398;
constexpr int kIconProject = 0x4C8, kIconPictureWidget = 0x4E0;
constexpr int kIconGenreWidget = 0x4F8;   // the genre badge, "ui/genre_<name>.dds" (0x007ED3B0)
// The card is the Flash movie filmpanel2.mfl. Its "star_info" sprite keeps the rating stars
// off the card's right edge until "showstars" slides them in (the card's builder plays it, and
// the update cycles "showstars"/"showmood"); the scene's play-by-name call is 0x00881B40.
constexpr uintptr_t kScenePlay = 0x00881B40;   // void __thiscall(scene, const char* node, int frame)
constexpr uintptr_t kStarInfoName = 0x00D4A888;   // "star_info", the string the card passes
constexpr uintptr_t kIconGenre = 0x007ED3B0;   // void __fastcall(icon)
constexpr int kVtSetVisible = 0x20;            // WWindow 0x005E5580 (int)
constexpr int kVtSetSize = 0x74, kVtHeight = 0x14, kVtAnchorX = 0x60, kVtAnchorY = 0x64, kVtAddChild = 0xC;

ProjectTest    g_isOurs = nullptr;
ProjectPicture g_picture = nullptr;
ProjectInfo    g_info = nullptr;

uintptr_t g_mgr = 0;        // our list
uintptr_t g_ctx = 0;        // the list whose code is running (0: neither)
uintptr_t g_movies = 0;     // the movie list we re-anchored
float g_fullHeight = 0;     // the movie list's height before we shortened it
float g_setHeight = -1;     // what we last gave it
int   g_shownCount = -1;

using Fast_t = void(__fastcall*)(uintptr_t);
using Rating_t = void(__fastcall*)(uintptr_t, void*, uintptr_t, uintptr_t);
Fast_t g_origEnumerate = nullptr, g_origIconUpdate = nullptr, g_origPicture = nullptr, g_origMgrUpdate = nullptr;
Fast_t g_origLeague = nullptr, g_origInfo = nullptr, g_origMgrDtor = nullptr;
using Test_t = bool(__fastcall*)(uintptr_t);
Test_t g_origHasStunts = nullptr;
Fast_t g_origGenre = nullptr;
using ScenePlay_t = void(__fastcall*)(uintptr_t scene, void* edx, const char* node, int frame);
ScenePlay_t g_origScenePlay = nullptr;
void* g_origNotify = nullptr;
Rating_t g_origRating = nullptr;

bool Write(uintptr_t at, const void* bytes, size_t len)
{
    DWORD old;
    auto target = reinterpret_cast<void*>(at);
    if (!VirtualProtect(target, len, PAGE_EXECUTE_READWRITE, &old))
        return false;
    memcpy(target, bytes, len);
    VirtualProtect(target, len, old, &old);
    FlushInstructionCache(GetCurrentProcess(), target, len);
    return true;
}

// A CALL rel32 at 'at' that must currently call 'expected' is pointed at 'to'.
bool Retarget(uintptr_t at, uintptr_t expected, const void* to, const char* what)
{
    if (Rd<uint8_t>(at) != 0xE8 || at + 5 + Rd<int32_t>(at + 1) != expected) {
        mod::Log("projectlist: %s - code at %08X is not what we expect, left alone", what, static_cast<unsigned>(at));
        return false;
    }
    int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(to) - (at + 5));
    return Write(at + 1, &rel, 4);
}

void* Hook(uintptr_t addr, const void* detour, const char* what)
{
    void* orig = nullptr;
    void* target = reinterpret_cast<void*>(addr);
    if (MH_CreateHook(target, const_cast<void*>(detour), &orig) != MH_OK || MH_EnableHook(target) != MH_OK) {
        mod::Log("projectlist: FAILED to hook %s", what);
        return nullptr;
    }
    return orig;
}

bool IsOurs(uintptr_t project)
{
    __try {
        return project && g_isOurs && g_isOurs(project);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

uintptr_t ProjectOf(uintptr_t icon)
{
    __try {
        return icon ? Rd<uintptr_t>(icon + kIconProject) : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

// --- The two lists' filter ---------------------------------------------------------

bool __fastcall Filed(uintptr_t project)
{
    bool filed = reinterpret_cast<bool(__fastcall*)(uintptr_t)>(kFiled)(project);
    if (!filed && g_ctx && g_mgr)
        filed = g_ctx == g_mgr ? !IsOurs(project) : IsOurs(project);
    return filed;
}

// The game's callers expect EDX kept (the original never touches it).
__declspec(naked) void FiledStub()
{
    __asm {
        push edx
        call Filed
        pop edx
        ret
    }
}

// "The list this card belongs to": ours while our code runs, otherwise the singleton.
__declspec(naked) void ListStub()
{
    __asm {
        mov eax, g_ctx
        test eax, eax
        jnz done
        mov eax, dword ptr ds:[0x0104EA94]
    done:
        ret
    }
}

bool OwnsIcon(uintptr_t icon)
{
    if (!g_mgr || !icon)
        return false;
    __try {
        uintptr_t begin = Rd<uintptr_t>(g_mgr + kMgrIcons), end = Rd<uintptr_t>(g_mgr + kMgrIcons + 4);
        if (!begin || end < begin || (end - begin) / 0x18 > 512)
            return false;
        for (uintptr_t e = begin; e < end; e += 0x18)
            if (Rd<uintptr_t>(e + 0x14) == icon)
                return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return false;
}

void __fastcall HookEnumerate(uintptr_t mgr)
{
    uintptr_t saved = g_ctx;
    g_ctx = mgr;
    g_origEnumerate(mgr);
    g_ctx = saved;
}

void Hide(uintptr_t icon, int widget)
{
    __try {
        if (uintptr_t w = Rd<uintptr_t>(icon + widget))
            reinterpret_cast<void(__thiscall*)(uintptr_t, int)>(Rd<uintptr_t>(Rd<uintptr_t>(w) + kVtSetVisible))(w, 0);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

void __fastcall HookIconUpdate(uintptr_t icon)
{
    uintptr_t saved = g_ctx;
    bool ours = OwnsIcon(icon);
    g_ctx = ours ? g_mgr : Rd<uintptr_t>(kSingleton);
    g_origIconUpdate(icon);
    if (ours)
        Hide(icon, kIconGenreWidget);   // a film's genre badge
    g_ctx = saved;
}

// --- Our cards ---------------------------------------------------------------------

struct GameString {
    char* ptr;
    int   len;
    int   cap;
    char  buf[0x14];
};

void SetPicture(uintptr_t icon, const char* tex)
{
    struct Rect {
        float x0, y0, x1, y1;
    };
    __try {
        uintptr_t widget = Rd<uintptr_t>(icon + kIconPictureWidget);
        if (!widget)
            return;
        GameString s;
        s.ptr = s.buf;
        s.cap = 0x14;
        s.len = static_cast<int>(strlen(tex));
        if (s.len >= 0x14)
            return;
        memcpy(s.buf, tex, s.len + 1);
        reinterpret_cast<void(__thiscall*)(uintptr_t, GameString*, Rect)>(Rd<uintptr_t>(Rd<uintptr_t>(widget) + 0x100))(
            widget, &s, Rect{ 0, 0, 1, 1 });
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

void __fastcall HookPicture(uintptr_t icon)
{
    uintptr_t p = ProjectOf(icon);
    if (!IsOurs(p)) {
        g_origPicture(icon);
        return;
    }
    // The game's picture logic would also post "script finished" notifications for it.
    if (const char* tex = g_picture ? g_picture(p) : nullptr)
        SetPicture(icon, tex);
}

// The notices take the stage and a 0x3C-byte object by value, which the callee destroys
// (RET 0x40). Films go straight on with the stack as it is; for the mod's own cards the stub
// destroys the object itself and returns as the original would.
bool __cdecl NotifyIsOurs(uintptr_t icon) { return IsOurs(ProjectOf(icon)); }

__declspec(naked) void HookNotify()
{
    __asm {
        push ecx
        push ecx
        call NotifyIsOurs
        add esp, 4
        pop ecx
        test al, al
        jnz skip
        jmp dword ptr [g_origNotify]
    skip:
        push ecx
        lea ecx, [esp + 0xC]   // the by-value object: past our push, the return address and the stage
        mov eax, 0x00526BB0   // kNotifyArgDtor
        call eax
        pop ecx
        ret 0x40
    }
}

void __fastcall HookRating(uintptr_t icon, void* edx, uintptr_t a, uintptr_t b)
{
    if (!IsOurs(ProjectOf(icon)))
        g_origRating(icon, edx, a, b);
}

// Our cards (built and updated while our list's code runs) never slide their rating stars in.
void __fastcall HookScenePlay(uintptr_t scene, void* edx, const char* node, int frame)
{
    if (g_ctx && g_ctx == g_mgr && reinterpret_cast<uintptr_t>(node) == kStarInfoName)
        return;
    g_origScenePlay(scene, edx, node, frame);
}

void __fastcall HookGenre(uintptr_t icon)
{
    if (!IsOurs(ProjectOf(icon)))
        g_origGenre(icon);
}

void __fastcall HookLeague(uintptr_t icon)
{
    if (!IsOurs(ProjectOf(icon)))
        g_origLeague(icon);
}

// The shares are made from a script whose scenes include a stunt: no stunt skull on our cards.
bool __fastcall HookHasStunts(uintptr_t project)
{
    if (g_ctx && g_ctx == g_mgr && IsOurs(project))
        return false;
    return g_origHasStunts(project);
}

bool FlyTo(uintptr_t icon)
{
    __try {
        reinterpret_cast<void(__fastcall*)(uintptr_t)>(kIconFlyTo)(icon);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// A click on a card (the input handler 0x007E8820 calls slot 75 when the button comes up
// without a drag): one of ours takes the camera to its object on the lot, as slot 76 does,
// rather than opening a film's details. The mod's screen is the fallback.
void __fastcall HookInfo(uintptr_t icon)
{
    uintptr_t p = ProjectOf(icon);
    if (!IsOurs(p)) {
        g_origInfo(icon);
        return;
    }
    if (!FlyTo(icon) && g_info)
        g_info(p);
}

// --- Our list, above the movies ----------------------------------------------------

template <typename R, typename... A> R VCall(uintptr_t obj, int slot, A... args)
{
    return reinterpret_cast<R(__thiscall*)(uintptr_t, A...)>(Rd<uintptr_t>(Rd<uintptr_t>(obj) + slot))(obj, args...);
}

// Our list is as tall as its cards; the movie list gets the rest of the column.
void Fit()
{
    if (!g_mgr || !g_movies)
        return;
    __try {
        if (Rd<uintptr_t>(kSingleton) != g_movies)
            return;   // the movie list was replaced; we only resize the one we hung below ours
        int n = Rd<int>(g_mgr + kMgrCount);
        float width = Rd<float>(kIconWidth);
        float movieHeight = VCall<float>(g_movies, kVtHeight);
        if (movieHeight != g_setHeight)
            g_fullHeight = movieHeight;   // first time, or the game resized it (resolution)
        else if (n == g_shownCount)
            return;
        float step = Rd<float>(kIconHeight) + Rd<float>(g_mgr + kMgrSpacing) + Rd<float>(kRightMargin);
        float ours = n > 0 ? 10.0f + n * step : 0.0f;
        if (ours > g_fullHeight * 0.5f)
            ours = g_fullHeight * 0.5f;
        VCall<void>(g_mgr, kVtSetSize, width, ours);
        g_setHeight = g_fullHeight - ours;
        VCall<void>(g_movies, kVtSetSize, width, g_setHeight);
        g_shownCount = n;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

void __fastcall HookMgrUpdate(uintptr_t mgr)
{
    g_origMgrUpdate(mgr);
    if (mgr && mgr == g_mgr)
        Fit();
}

uintptr_t Build(uintptr_t card)
{
    __try {
        uintptr_t real = Rd<uintptr_t>(kSingleton);
        if (!real)
            return 0;
        ++*reinterpret_cast<int*>(real + kMgrRefs);   // the ctor drops the singleton's reference (0x007F2B26)
        uintptr_t m = reinterpret_cast<uintptr_t(__cdecl*)(unsigned)>(kOperatorNew)(0x3A4);
        if (!m)
            return 0;
        reinterpret_cast<uintptr_t(__fastcall*)(uintptr_t)>(kMgrCtor)(m);   // takes the singleton
        ++*reinterpret_cast<int*>(m + kMgrRefs);                               // our own reference
        // Give the singleton back to the movie list.
        using Ref_t = void(__thiscall*)(uintptr_t);
        uintptr_t* refVt = Rd<uintptr_t*>(kSingletonRef);
        reinterpret_cast<Ref_t>(refVt[1])(kSingletonRef);
        *reinterpret_cast<uintptr_t*>(kSingleton) = real;
        reinterpret_cast<Ref_t>(refVt[0])(kSingletonRef);
        // The link and unlink only register the pointer; the ++ above already made up for the
        // ctor's release, so the movie list's count is as the HUD left it.
        // Where the movie list was: the screen's right edge, under the people card's row.
        uintptr_t layer = reinterpret_cast<uintptr_t(__cdecl*)()>(kHudLayer)();
        float right = reinterpret_cast<float(__cdecl*)()>(kScreenRight)() + Rd<float>(kRightMargin);
        VCall<void>(m, kVtAnchorX, 2, layer, right);
        VCall<void>(m, kVtAnchorY, 2, card, -16.0f);
        VCall<void>(layer, kVtAddChild, m, 2);
        VCall<void>(real, kVtAnchorY, 2, m, 0.0f);   // the movies hang below ours
        g_movies = real;
        g_setHeight = -1;
        g_shownCount = -1;
        return m;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

void OnBuilt(uintptr_t card)
{
    if (!g_isOurs)
        return;
    uintptr_t movies = Rd<uintptr_t>(kSingleton);
    int before = movies ? Rd<int>(movies + kMgrRefs) : -1;
    g_mgr = Build(card);
    mod::Log("projectlist: the mod's group on the project list %s (%08X; movie list refs %d -> %d)",
             g_mgr ? "added" : "FAILED", static_cast<unsigned>(g_mgr), before,
             movies ? Rd<int>(movies + kMgrRefs) : -1);
}

void OnCleared()
{
    uintptr_t m = g_mgr;
    if (!m)
        return;
    g_mgr = 0;
    g_movies = 0;
    __try {
        reinterpret_cast<Fast_t>(kMgrClear)(m);
        if (--*reinterpret_cast<int*>(m + kMgrRefs) == 0)
            VCall<void>(m, 0, 1);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        mod::Log("projectlist: releasing the mod's group faulted");
    }
}

// Loading a save frees the movie list without the HUD teardown we hook: forget it, and
// let ours go with it (a new HUD builds a new one).
void __fastcall HookMgrDtor(uintptr_t mgr)
{
    if (mgr && mgr == g_movies) {
        mod::Log("projectlist: the movie list went away - the mod's group goes with it");
        OnCleared();
    } else if (mgr && mgr == g_mgr) {
        g_mgr = 0;
        g_movies = 0;
    }
    g_origMgrDtor(mgr);
}

} // namespace

void SetCategory(ProjectTest isOurs, ProjectPicture picture, ProjectInfo info)
{
    g_isOurs = isOurs;
    g_picture = picture;
    g_info = info;
}

void Install()
{
    // MOV EAX,[singleton] -> MOV EAX,EBX (the manager running): the same for the movie list.
    if (memcmp(reinterpret_cast<void*>(kSingletonUse), "\xA1\x94\xEA\x04\x01", 5) != 0) {
        mod::Log("projectlist: code at %08X is not what we expect - the mod's group is OFF", static_cast<unsigned>(kSingletonUse));
        return;
    }
    bool ok = Retarget(kFiledCall1, kFiled, reinterpret_cast<const void*>(&FiledStub), "list filter") &&
              Retarget(kFiledCall2, kFiled, reinterpret_cast<const void*>(&FiledStub), "card filter") &&
              Retarget(kGetterCall1, kGetter, reinterpret_cast<const void*>(&ListStub), "card removal") &&
              Retarget(kGetterCall2, kGetter, reinterpret_cast<const void*>(&ListStub), "card removal 2") &&
              Write(kSingletonUse, "\x8B\xC3\x90\x90\x90", 5);
    g_origEnumerate = reinterpret_cast<Fast_t>(Hook(kMgrEnumerate, reinterpret_cast<const void*>(&HookEnumerate), "list"));
    g_origIconUpdate = reinterpret_cast<Fast_t>(Hook(kIconUpdate, reinterpret_cast<const void*>(&HookIconUpdate), "card"));
    g_origMgrUpdate = reinterpret_cast<Fast_t>(Hook(kMgrUpdate, reinterpret_cast<const void*>(&HookMgrUpdate), "list update"));
    g_origPicture = reinterpret_cast<Fast_t>(Hook(kIconPicture, reinterpret_cast<const void*>(&HookPicture), "card picture"));
    g_origNotify = Hook(kIconNotify, reinterpret_cast<const void*>(&HookNotify), "card notices");
    g_origRating = reinterpret_cast<Rating_t>(Hook(kIconRating, reinterpret_cast<const void*>(&HookRating), "card rating"));
    g_origLeague = reinterpret_cast<Fast_t>(Hook(kIconLeague, reinterpret_cast<const void*>(&HookLeague), "card chart"));
    g_origInfo = reinterpret_cast<Fast_t>(Hook(kIconInfo, reinterpret_cast<const void*>(&HookInfo), "card info"));
    g_origScenePlay = reinterpret_cast<ScenePlay_t>(Hook(kScenePlay, reinterpret_cast<const void*>(&HookScenePlay), "card stars"));
    g_origGenre = reinterpret_cast<Fast_t>(Hook(kIconGenre, reinterpret_cast<const void*>(&HookGenre), "card genre"));
    g_origHasStunts = reinterpret_cast<Test_t>(Hook(kHasStunts, reinterpret_cast<const void*>(&HookHasStunts), "card stunt skull"));
    g_origMgrDtor = reinterpret_cast<Fast_t>(Hook(kMgrDtor, reinterpret_cast<const void*>(&HookMgrDtor), "list teardown"));
    ok = ok && g_origEnumerate && g_origIconUpdate && g_origMgrUpdate && g_origMgrDtor;
    careers::OnHud(&OnBuilt, &OnCleared);
    mod::Log("projectlist: %s", ok ? "the mod's projects get their own group on the right of the HUD"
                                   : "some patches FAILED - the mod's projects may show as movies");
}

} // namespace projectlist
