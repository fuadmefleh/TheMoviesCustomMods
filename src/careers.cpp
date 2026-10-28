// New employee types: the game's careers (CStaff +0x814) extended past its fixed 17.
//
// The game knows 17 careers (0 unemployed ... 14 writer, 15 scientist, 16 stuntman; see
// re/employees.md). Career 17 is the accountant (src/tv_staff.cpp), 18 the animator
// (src/tv_animation.cpp). Three things in the
// game are sized for exactly 17 and indexed by career without a check:
//   * the career names (0x00E54078, 17 game Strings; "SITT_DETAILS_CAREER_" + name is the job
//     title, and data/tasks.ini sections are looked up by it) and the costume lists read
//     from tasks.ini (0x0104D0C8 "costume", 0x0104D2E8 "vip"). There's nothing free after
//     any of them, so the mod makes 19-entry copies and points every instruction that uses
//     them at the copies, and raises the two loop bounds that count to 17;
//   * the film-credit lists (0x00F87C80), filled only for film crew: accountants are kept out
//     of the credits instead (FUN_0043B220 skips them).
// Everything else that reads the career compares it with constants or checks a range, so 17
// and 18 get the defaults: no task desires, no home building, no credits.
//
// The HUD's people card (top left: stars, extras, writers, builders, crew, janitors,
// scientists, one page each, flicked through with its arrows) gets a tenth page, the
// accountants. The pages are hard-coded, so the page is a second copy of the janitors' icon
// manager (WJanitorIconManager) whose "career == 6" tests read a variable that is 17 while
// that copy runs; the card's page functions and counts learn about page 9.
//
// Old saves don't matter: a game saved with accountants needs the mod to load them.

#include <windows.h>
#include <cstdint>
#include <cstring>

#include "MinHook.h"
#include "careers.h"
#include "game.h"
#include "mod.h"

namespace careers {
namespace {

template <typename T> T Rd(uintptr_t a) { return *reinterpret_cast<const T*>(a); }

// --- Patching ---------------------------------------------------------------------

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

// Replaces exactly 'len' bytes that must currently be 'expect'.
bool Patch(uintptr_t at, const char* expect, const char* bytes, size_t len, const char* what)
{
    if (memcmp(reinterpret_cast<void*>(at), expect, len) != 0) {
        mod::Log("careers: %s - code at %08X is not what we expect, left alone", what, static_cast<unsigned>(at));
        return false;
    }
    return Write(at, bytes, len);
}

// Points the 32-bit address at 'at + field' from a table at 'oldBase' to the same place in a
// copy at 'newBase'. The address may point inside the table (an entry's length, capacity...).
bool Relocate(uintptr_t at, int field, uintptr_t oldBase, uintptr_t size, uintptr_t newBase)
{
    uintptr_t v = Rd<uintptr_t>(at + field);
    if (v < oldBase || v >= oldBase + size) {
        mod::Log("careers: %08X refers to %08X, not the table at %08X - left alone", static_cast<unsigned>(at),
                 static_cast<unsigned>(v), static_cast<unsigned>(oldBase));
        return false;
    }
    uintptr_t nv = newBase + (v - oldBase);
    return Write(at + field, &nv, 4);
}

void* Hook(uintptr_t addr, const void* detour, const char* what)
{
    void* orig = nullptr;
    void* target = reinterpret_cast<void*>(addr);
    if (MH_CreateHook(target, const_cast<void*>(detour), &orig) != MH_OK || MH_EnableHook(target) != MH_OK) {
        mod::Log("careers: FAILED to hook %s", what);
        return nullptr;
    }
    return orig;
}

// --- The career tables --------------------------------------------------------------

struct GameString {   // the game's String: inline buffer for up to 19 characters
    char* ptr;
    int   len;
    int   cap;
    char  buf[0x14];
};
static_assert(sizeof(GameString) == 0x20, "game String is 0x20 bytes");

constexpr uintptr_t kNames = 0x00E54078, kCostumes = 0x0104D0C8, kVips = 0x0104D2E8;
constexpr uintptr_t kOldTableSize = kGameCareers * sizeof(GameString);
constexpr uintptr_t kReadTasks = 0x00597F70;   // void __cdecl(): fills the costume tables from data/tasks.ini

const char* const kCareerNames[kCareers] = {
    "unemployed", "staff",   "actor", "director",   "extra",     "builder",   "janitor",
    "cameraman",  "soundman", "directorsassistant", "dollyop",   "setop",     "clapperman",
    "assistant",  "writer",  "scientist", "stuntman", "accountant", "animator",
};

GameString g_names[kCareers], g_costumes[kCareers], g_vips[kCareers];

void SetInline(GameString& s, const char* text)
{
    s.ptr = s.buf;
    s.cap = 0x14;
    s.len = static_cast<int>(strlen(text));
    memcpy(s.buf, text, s.len + 1);
}

int RelocateTables()
{
    for (int i = 0; i < kCareers; i++) {
        SetInline(g_names[i], kCareerNames[i]);
        SetInline(g_costumes[i], "");   // filled by the game's tasks.ini reader
        SetInline(g_vips[i], "");
    }
    auto names = reinterpret_cast<uintptr_t>(g_names);
    auto costumes = reinterpret_cast<uintptr_t>(g_costumes);
    auto vips = reinterpret_cast<uintptr_t>(g_vips);
    int failed = 0;
    // Names: the tasks.ini reader's loop, the name -> career lookup, the two job-title builders.
    failed += !Relocate(0x00598022, 2, kNames, kOldTableSize, names);
    failed += !Relocate(0x005984B9, 1, kNames, kOldTableSize, names);
    failed += !Relocate(0x00907F05, 1, kNames, kOldTableSize, names);
    failed += !Relocate(0x00908725, 1, kNames, kOldTableSize, names);
    // Costumes and vip costumes: the tasks.ini reader's assignments, and the dresser.
    for (uintptr_t at : { 0x00598097u, 0x005980A6u, 0x005980BFu, 0x005980CDu, 0x005980D3u, 0x005980E1u, 0x005980E7u })
        failed += !Relocate(at, 2, kCostumes, kOldTableSize, costumes);
    failed += !Relocate(0x0059859F, 1, kCostumes, kOldTableSize, costumes);
    for (uintptr_t at : { 0x0059817Au, 0x00598189u, 0x005981A2u, 0x005981B0u, 0x005981B6u, 0x005981C4u, 0x005981CAu })
        failed += !Relocate(at, 2, kVips, kOldTableSize, vips);
    failed += !Relocate(0x00598598, 1, kVips, kOldTableSize, vips);
    // The reader's loop (CMP ESI, 17 * 0x20) and the name lookup's (CMP EBP, 17).
    static_assert(kCareers == 19, "the two bounds below are for 19 careers");
    failed += !Patch(0x00598210, "\x81\xFE\x20\x02\x00\x00", "\x81\xFE\x60\x02\x00\x00", 6, "tasks.ini loop");
    failed += !Patch(0x0059851E, "\x83\xFD\x11", "\x83\xFD\x13", 3, "career name lookup");
    return failed;
}

// Film credits: only film crew is listed (credits table 0x00F87C80, 17 entries).
using Credits_t = void(__cdecl*)(uintptr_t credits, uintptr_t staff);
Credits_t g_origCredits = nullptr;

int CareerOf(uintptr_t staff)
{
    __try {
        return staff ? Rd<int>(staff + 0x814) : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

void __cdecl HookCredits(uintptr_t credits, uintptr_t staff)
{
    if (CareerOf(staff) >= kGameCareers)
        return;
    g_origCredits(credits, staff);
}

// The cursor while a person is carried (FUN_0091EE70): the game's switch has no case for
// careers past 15, so accountants and animators keep whatever cursor was showing. They get the
// builders' and janitors' "staff" one.
constexpr uintptr_t kPickCursor = 0x0091EE70;   // void __thiscall(cursors, person)
constexpr uintptr_t kCursorByName = 0x0091E660; // cursor __thiscall(cursors, const char* name)
constexpr int kCursorCurrent = 0x88;
using PickCursor_t = void(__fastcall*)(uintptr_t, void*, uintptr_t);
PickCursor_t g_origPickCursor = nullptr;

void SetCursor(uintptr_t cursors, const char* name)
{
    __try {
        uintptr_t c = reinterpret_cast<uintptr_t(__thiscall*)(uintptr_t, const char*)>(kCursorByName)(cursors, name);
        *reinterpret_cast<uintptr_t*>(cursors + kCursorCurrent) = c;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

void __fastcall HookPickCursor(uintptr_t cursors, void* edx, uintptr_t person)
{
    g_origPickCursor(cursors, edx, person);
    if (person && game::ClassName(person) == "CStaff" && CareerOf(person) >= kAccountant)
        SetCursor(cursors, "staff");
}

// --- The HUD's people card ---------------------------------------------------------

constexpr uintptr_t kOperatorNew   = 0x00ACBC50;   // void* __cdecl(size)
constexpr uintptr_t kHudCtor       = 0x006A39C0;   // HUD* __fastcall(HUD*)
constexpr uintptr_t kHudRebuild    = 0x006A44F0;   // void __fastcall(HUD*): clears the card's managers first
constexpr uintptr_t kHudClear      = 0x0071C440;   // void __fastcall(?)
constexpr uintptr_t kCardCtor      = 0x007E6E30;   // WHudCardButtons* __thiscall(this, float slide)
constexpr uintptr_t kHudLayer      = 0x0071B2B0;   // Window* __cdecl(): what the managers hang from
constexpr uintptr_t kMgrCtor       = 0x007EBC90;   // WJanitorIconManager* __thiscall(this, char singleton), 0x37C
constexpr uintptr_t kMgrEnumerate  = 0x007EC130;   // void __fastcall(mgr): finds the people, makes icons
constexpr uintptr_t kMgrClear      = 0x007EBB40;   // void __fastcall(mgr)
constexpr uintptr_t kMgrRelayout   = 0x007EB040;   // void __fastcall(mgr)
constexpr uintptr_t kIconUpdate    = 0x007EA340;   // void __fastcall(icon): removes itself if no longer a janitor
constexpr uintptr_t kIconHit       = 0x007E9650;   // void __fastcall(icon)
constexpr uintptr_t kJanitorMgr    = 0x007EA7B0;   // mgr* __cdecl(): the janitor singleton (0x0104EA7C)
constexpr uintptr_t kJanitorSingle = 0x0104EA7C;
constexpr uintptr_t kPageMgr       = 0x007E4F70;   // mgr* __cdecl(int page)
constexpr uintptr_t kPageSkip      = 0x007E5290;   // bool __cdecl(int page)
constexpr uintptr_t kAllEmpty      = 0x007E5190;   // bool __cdecl()
constexpr uintptr_t kFirstPage     = 0x007E53C0;   // int __cdecl()
constexpr uintptr_t kNextPage      = 0x007E6180;   // void __fastcall(card): next arrow
constexpr uintptr_t kPrevPage      = 0x007E6630;   // void __fastcall(card): previous arrow
constexpr uintptr_t kLayoutA       = 0x007E5530;   // void __cdecl(): lays the managers out (resolution)
constexpr uintptr_t kLayoutB       = 0x007E5710;
constexpr uintptr_t kStarMgr       = 0x007FD5D0;   // mgr* __cdecl(): page 0's manager
constexpr uintptr_t kSpecialMode   = 0x0104D8E8;   // non-zero: a mode with its own pages (only 0-2 and 8)
constexpr uintptr_t kLayoutScaleA  = 0x00D1BA7C, kLayoutScaleB = 0x00D17098;
constexpr int kMgrCount = 0x344, kMgrIcons = 0x35C, kMgrRefs = 0x48;
constexpr int kCardPage = 0x38C, kCardJobIcon = 0x388;
constexpr int kOurPage = 9;

using Fast_t = void(__fastcall*)(uintptr_t);
using HudCtor_t = uintptr_t(__fastcall*)(uintptr_t);
using CardCtor_t = uintptr_t(__fastcall*)(uintptr_t, void*, float);
using PageMgr_t = uintptr_t(__cdecl*)(int);
using PageSkip_t = bool(__cdecl*)(int);
using NoArgBool_t = bool(__cdecl*)();
using NoArgInt_t = int(__cdecl*)();
using NoArg_t = void(__cdecl*)();
using Getter_t = uintptr_t(__cdecl*)();

HudCtor_t   g_origHudCtor = nullptr;
Fast_t      g_origHudRebuild = nullptr, g_origHudClear = nullptr;
CardCtor_t  g_origCardCtor = nullptr;
Fast_t      g_origEnumerate = nullptr, g_origIconUpdate = nullptr, g_origIconHit = nullptr;
Fast_t      g_origNextPage = nullptr, g_origPrevPage = nullptr;
PageMgr_t   g_origPageMgr = nullptr;
PageSkip_t  g_origPageSkip = nullptr;
NoArgBool_t g_origAllEmpty = nullptr;
NoArgInt_t  g_origFirstPage = nullptr;
NoArg_t     g_origLayoutA = nullptr, g_origLayoutB = nullptr;

uintptr_t g_card = 0;        // the card built inside the HUD's constructor
HudBuilt   g_onBuilt = nullptr;
HudCleared g_onCleared = nullptr;
uintptr_t g_mgr = 0;         // the accountants' page
// Read by the janitor code's two career tests (patched): 6, or 17 while our copy runs.
int g_hudCareer = 6;
uintptr_t g_activeMgr = 0;   // what the janitor singleton getter returns while our copy runs

// Runs 'fn' as our page: its career tests see 17, its singleton is our manager.
template <typename F> void AsOurs(F fn)
{
    int career = g_hudCareer;
    uintptr_t active = g_activeMgr;
    g_hudCareer = kAccountant;
    g_activeMgr = g_mgr;
    fn();
    g_hudCareer = career;
    g_activeMgr = active;
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

uintptr_t __cdecl HookJanitorMgr() { return g_activeMgr ? g_activeMgr : Rd<uintptr_t>(kJanitorSingle); }

void __fastcall HookEnumerate(uintptr_t mgr)
{
    if (mgr && mgr == g_mgr)
        AsOurs([&] { g_origEnumerate(mgr); });
    else
        g_origEnumerate(mgr);
}

void __fastcall HookIconUpdate(uintptr_t icon)
{
    if (OwnsIcon(icon))
        AsOurs([&] { g_origIconUpdate(icon); });
    else
        g_origIconUpdate(icon);
}

void __fastcall HookIconHit(uintptr_t icon)
{
    if (OwnsIcon(icon))
        AsOurs([&] { g_origIconHit(icon); });
    else
        g_origIconHit(icon);
}

uintptr_t __fastcall HookCardCtor(uintptr_t card, void* edx, float slide)
{
    uintptr_t r = g_origCardCtor(card, edx, slide);
    g_card = card;
    return r;
}

// The accountants' manager, hung from the HUD layer and the card like the janitors'.
uintptr_t BuildManager(uintptr_t card)
{
    __try {
        uintptr_t m = reinterpret_cast<uintptr_t(__cdecl*)(unsigned)>(kOperatorNew)(0x37C);
        if (!m)
            return 0;
        reinterpret_cast<uintptr_t(__thiscall*)(uintptr_t, char)>(kMgrCtor)(m, 0);   // 0: not the singleton
        uintptr_t layer = reinterpret_cast<Getter_t>(kHudLayer)();
        uintptr_t vt = Rd<uintptr_t>(m);
        reinterpret_cast<void(__thiscall*)(uintptr_t, int, uintptr_t, float)>(Rd<uintptr_t>(vt + 0x5C))(m, 1, layer, -100.0f);
        reinterpret_cast<void(__thiscall*)(uintptr_t, int, uintptr_t, float)>(Rd<uintptr_t>(vt + 0x64))(m, 2, card, -16.0f);
        reinterpret_cast<void(__thiscall*)(uintptr_t, uintptr_t, int)>(Rd<uintptr_t>(Rd<uintptr_t>(layer) + 0xC))(layer, m, 2);
        ++*reinterpret_cast<int*>(m + kMgrRefs);
        return m;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

uintptr_t __fastcall HookHudCtor(uintptr_t hud)
{
    g_card = 0;
    uintptr_t r = g_origHudCtor(hud);
    if (g_card) {
        g_mgr = BuildManager(g_card);
        mod::Log("careers: accountants' page on the people card %s (card %08X, page %08X)", g_mgr ? "added" : "FAILED",
                 static_cast<unsigned>(g_card), static_cast<unsigned>(g_mgr));
        if (g_onBuilt)
            g_onBuilt(g_card);
    }
    return r;
}

void ReleaseManager()
{
    uintptr_t m = g_mgr;
    if (!m)
        return;
    g_mgr = 0;
    __try {
        reinterpret_cast<Fast_t>(kMgrClear)(m);
        if (--*reinterpret_cast<int*>(m + kMgrRefs) == 0)
            reinterpret_cast<void(__thiscall*)(uintptr_t, int)>(Rd<uintptr_t>(Rd<uintptr_t>(m)))(m, 1);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        mod::Log("careers: releasing the accountants' page faulted");
    }
}

void __fastcall HookHudRebuild(uintptr_t hud)
{
    if (g_onCleared)
        g_onCleared();
    ReleaseManager();
    g_origHudRebuild(hud);
}

void __fastcall HookHudClear(uintptr_t self)
{
    if (g_onCleared)
        g_onCleared();
    ReleaseManager();
    g_origHudClear(self);
}

int MgrCount()
{
    __try {
        return g_mgr ? Rd<int>(g_mgr + kMgrCount) : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

bool OurPageSkipped() { return !(g_mgr && Rd<int>(kSpecialMode) == 0 && MgrCount() > 0); }

uintptr_t __cdecl HookPageMgr(int page) { return page == kOurPage ? g_mgr : g_origPageMgr(page); }

bool __cdecl HookPageSkip(int page) { return page == kOurPage ? OurPageSkipped() : g_origPageSkip(page); }

bool __cdecl HookAllEmpty() { return g_origAllEmpty() && OurPageSkipped(); }

int __cdecl HookFirstPage()
{
    int page = g_origFirstPage();
    if (page == 0 && g_origPageSkip(0) && !OurPageSkipped())
        return kOurPage;
    return page;
}

// The card's job picture for our page (the game's switch only knows pages 0-8).
void ShowPageIcon(uintptr_t card)
{
    struct Rect {
        float x0, y0, x1, y1;
    };
    __try {
        if (Rd<int>(card + kCardPage) != kOurPage)
            return;
        uintptr_t icon = Rd<uintptr_t>(card + kCardJobIcon);
        if (!icon)
            return;
        GameString tex;
        SetInline(tex, "ui/job_acct.dds");
        reinterpret_cast<void(__thiscall*)(uintptr_t, GameString*, Rect)>(Rd<uintptr_t>(Rd<uintptr_t>(icon) + 0x100))(
            icon, &tex, Rect{ 0, 0, 1, 1 });
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

void __fastcall HookNextPage(uintptr_t card)
{
    g_origNextPage(card);
    ShowPageIcon(card);
}

void __fastcall HookPrevPage(uintptr_t card)
{
    g_origPrevPage(card);
    ShowPageIcon(card);
}

// Lays our page out where the others go (same as the game does for each of its managers).
void LayOut(uintptr_t scaleAt)
{
    if (!g_mgr || Rd<int>(kSpecialMode) != 0)
        return;
    __try {
        uintptr_t star = reinterpret_cast<Getter_t>(kStarMgr)();
        uintptr_t layer = reinterpret_cast<Getter_t>(kHudLayer)();
        if (!star || !layer)
            return;
        float x = reinterpret_cast<float(__thiscall*)(uintptr_t)>(Rd<uintptr_t>(Rd<uintptr_t>(star) + 0x10))(star);
        float y = reinterpret_cast<float(__thiscall*)(uintptr_t)>(Rd<uintptr_t>(Rd<uintptr_t>(layer) + 0x14))(layer) *
                  Rd<float>(scaleAt);
        reinterpret_cast<void(__thiscall*)(uintptr_t, float, float)>(Rd<uintptr_t>(Rd<uintptr_t>(g_mgr) + 0x74))(g_mgr, x, y);
        reinterpret_cast<Fast_t>(kMgrRelayout)(g_mgr);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

void __cdecl HookLayoutA()
{
    g_origLayoutA();
    LayOut(kLayoutScaleA);
}

void __cdecl HookLayoutB()
{
    g_origLayoutB();
    LayOut(kLayoutScaleB);
}

// The janitor code's two "career == 6" tests become "career == g_hudCareer": each is
// replaced by a jump to a stub that compares with the variable and jumps back.
bool PatchCareerTests()
{
    auto stubs = static_cast<uint8_t*>(VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!stubs)
        return false;
    auto addr = reinterpret_cast<uintptr_t>(&g_hudCareer);
    auto rel = [](uintptr_t from, uintptr_t to) { return static_cast<int32_t>(to - (from + 5)); };
    // Enumerate, 0x007EC1AA: CMP dword [ESP+0x10],6 (83 7C 24 10 06)
    uint8_t* s1 = stubs;
    uint8_t code1[] = { 0x50,                                    // push eax
                        0xA1, 0, 0, 0, 0,                        // mov eax,[g_hudCareer]
                        0x39, 0x44, 0x24, 0x14,                  // cmp [esp+0x14],eax (+4: the push)
                        0x58,                                    // pop eax
                        0xE9, 0, 0, 0, 0 };                      // jmp 0x007EC1AF
    memcpy(code1 + 2, &addr, 4);
    int32_t back1 = rel(reinterpret_cast<uintptr_t>(s1 + 11), 0x007EC1AF);
    memcpy(code1 + 12, &back1, 4);
    memcpy(s1, code1, sizeof(code1));
    // Icon update, 0x007EA351: CMP dword [EAX+0x814],6 (83 B8 14 08 00 00 06)
    uint8_t* s2 = stubs + 32;
    uint8_t code2[] = { 0x51,                                    // push ecx
                        0x8B, 0x0D, 0, 0, 0, 0,                  // mov ecx,[g_hudCareer]
                        0x39, 0x88, 0x14, 0x08, 0x00, 0x00,      // cmp [eax+0x814],ecx
                        0x59,                                    // pop ecx
                        0xE9, 0, 0, 0, 0 };                      // jmp 0x007EA358
    memcpy(code2 + 3, &addr, 4);
    int32_t back2 = rel(reinterpret_cast<uintptr_t>(s2 + 14), 0x007EA358);
    memcpy(code2 + 15, &back2, 4);
    memcpy(s2, code2, sizeof(code2));
    FlushInstructionCache(GetCurrentProcess(), stubs, 64);

    char jmp1[5] = { '\xE9' };
    int32_t to1 = rel(0x007EC1AA, reinterpret_cast<uintptr_t>(s1));
    memcpy(jmp1 + 1, &to1, 4);
    char jmp2[7] = { '\xE9', 0, 0, 0, 0, '\x90', '\x90' };
    int32_t to2 = rel(0x007EA351, reinterpret_cast<uintptr_t>(s2));
    memcpy(jmp2 + 1, &to2, 4);
    return Patch(0x007EC1AA, "\x83\x7C\x24\x10\x06", jmp1, 5, "janitor page career test") &&
           Patch(0x007EA351, "\x83\xB8\x14\x08\x00\x00\x06", jmp2, 7, "janitor icon career test");
}

bool InstallHudPage()
{
    // The page count: next/previous wrap at 10 pages instead of 9. The jump tables that set
    // each page's picture stay at 9 (ours takes their default, then ShowPageIcon).
    bool ok = Patch(0x007E5FB9, "\x83\xFE\x09", "\x83\xFE\x0A", 3, "next page wrap") &&
              Patch(0x007E5FD2, "\x83\xFE\x09", "\x83\xFE\x0A", 3, "next page wrap 2") &&
              Patch(0x007E61F0, "\x83\xFF\x08", "\x83\xFF\x09", 3, "next arrow clamp") &&
              Patch(0x007E66B0, "\xBF\x08\x00\x00\x00", "\xBF\x09\x00\x00\x00", 5, "previous arrow wrap") &&
              PatchCareerTests();
    if (!ok)
        return false;
    g_origHudCtor = reinterpret_cast<HudCtor_t>(Hook(kHudCtor, reinterpret_cast<const void*>(&HookHudCtor), "HUD"));
    g_origHudRebuild = reinterpret_cast<Fast_t>(Hook(kHudRebuild, reinterpret_cast<const void*>(&HookHudRebuild), "HUD rebuild"));
    g_origHudClear = reinterpret_cast<Fast_t>(Hook(kHudClear, reinterpret_cast<const void*>(&HookHudClear), "HUD clear"));
    g_origCardCtor = reinterpret_cast<CardCtor_t>(Hook(kCardCtor, reinterpret_cast<const void*>(&HookCardCtor), "people card"));
    g_origEnumerate = reinterpret_cast<Fast_t>(Hook(kMgrEnumerate, reinterpret_cast<const void*>(&HookEnumerate), "janitor page people"));
    g_origIconUpdate = reinterpret_cast<Fast_t>(Hook(kIconUpdate, reinterpret_cast<const void*>(&HookIconUpdate), "janitor icon"));
    g_origIconHit = reinterpret_cast<Fast_t>(Hook(kIconHit, reinterpret_cast<const void*>(&HookIconHit), "janitor icon hit"));
    Hook(kJanitorMgr, reinterpret_cast<const void*>(&HookJanitorMgr), "janitor page getter");
    g_origPageMgr = reinterpret_cast<PageMgr_t>(Hook(kPageMgr, reinterpret_cast<const void*>(&HookPageMgr), "card page"));
    g_origPageSkip = reinterpret_cast<PageSkip_t>(Hook(kPageSkip, reinterpret_cast<const void*>(&HookPageSkip), "card page skip"));
    g_origAllEmpty = reinterpret_cast<NoArgBool_t>(Hook(kAllEmpty, reinterpret_cast<const void*>(&HookAllEmpty), "card empty"));
    g_origFirstPage = reinterpret_cast<NoArgInt_t>(Hook(kFirstPage, reinterpret_cast<const void*>(&HookFirstPage), "card first page"));
    g_origNextPage = reinterpret_cast<Fast_t>(Hook(kNextPage, reinterpret_cast<const void*>(&HookNextPage), "card next"));
    g_origPrevPage = reinterpret_cast<Fast_t>(Hook(kPrevPage, reinterpret_cast<const void*>(&HookPrevPage), "card previous"));
    g_origLayoutA = reinterpret_cast<NoArg_t>(Hook(kLayoutA, reinterpret_cast<const void*>(&HookLayoutA), "card layout"));
    g_origLayoutB = reinterpret_cast<NoArg_t>(Hook(kLayoutB, reinterpret_cast<const void*>(&HookLayoutB), "card layout 2"));
    return g_origHudCtor && g_origCardCtor && g_origEnumerate && g_origIconUpdate && g_origIconHit && g_origPageMgr &&
           g_origPageSkip && g_origAllEmpty && g_origFirstPage && g_origNextPage && g_origPrevPage;
}

bool TasksAlreadyRead()
{
    // The janitor's costume list name, read from tasks.ini at game start.
    __try {
        return Rd<int>(kCostumes + 6 * sizeof(GameString) + 4) > 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool ReadTasks()
{
    __try {
        reinterpret_cast<NoArg_t>(kReadTasks)();
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

} // namespace

void OnHud(HudBuilt built, HudCleared cleared)
{
    g_onBuilt = built;
    g_onCleared = cleared;
}

const char* Name(int career) { return career >= 0 && career < kCareers ? kCareerNames[career] : "?"; }

void Install()
{
    bool early = !TasksAlreadyRead();
    int failed = RelocateTables();
    if (failed) {
        // Half-moved tables would be worse than none: there are no accountants then.
        mod::Log("careers: %d career table patch(es) failed - new careers are OFF", failed);
        return;
    }
    // The mod is loaded after the game has started (dinput8 is delay-loaded), so the costume
    // lists have usually been read already - into the old tables. Read them again into ours.
    bool read = early || ReadTasks();
    g_origCredits = reinterpret_cast<Credits_t>(Hook(0x0043B220, reinterpret_cast<const void*>(&HookCredits), "film credits"));
    g_origPickCursor = reinterpret_cast<PickCursor_t>(Hook(kPickCursor, reinterpret_cast<const void*>(&HookPickCursor), "carried cursor"));
    mod::Log("careers: %d careers (accountant = %d, animator = %d); costume lists %s", kCareers, kAccountant, kAnimator,
             early ? "will be read at game start" : read ? "read again" : "FAILED to read again");
    bool hud = InstallHudPage();
    mod::Log("careers: accountants' page on the people card %s", hud ? "installed" : "NOT installed");
}

} // namespace careers
