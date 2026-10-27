// The Golden Antenna Awards on the game's timeline (the bar at the bottom of the screen): an
// awards icon for the next ceremony and for every one held, as the movie awards have, and the
// real-time countdown the movie ceremony gets.
//
// The timeline (WTimeline) keeps its own awards icons in four fixed slots and lays only those
// out, so the mod makes icons of the same class (WTimelineAwardsIcon, the TV trophy picture)
// and places them itself every frame, from a hook on the timeline's layout. A click runs the
// mod's own callback (the replay of that year's ceremony) in place of the game's, which would
// open the movie ceremony for the date. The hover tooltip is the game's, with the TV text.
// Icons aren't saved and die with the timeline: a hook on their destructor forgets them, and
// they're made again for the next timeline.
//
// The movie countdown is three spoken cues ("4 minutes", "2 minutes", "30 seconds" of real
// time before the ceremony); the TV date gets the same cues, and a pop-up saying what they
// count down to. See re/timeline_awards.md.

#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstring>

#include "MinHook.h"
#include "game.h"
#include "mod.h"
#include "text.h"
#include "tv_internal.h"

namespace tv {
namespace {

template <typename T> T Rd(uintptr_t a) { return *reinterpret_cast<const T*>(a); }

constexpr uintptr_t kHudTimeline   = 0x0104E7B8;   // WTimeline*: the bar at the bottom of the screen
constexpr uintptr_t kLayout        = 0x00798C10;   // void __fastcall(WTimeline*): places its awards icons, every frame
constexpr uintptr_t kDateToX       = 0x00793F50;   // float __thiscall(WTimeline*, float date, float* frac); -1 off the bar
constexpr uintptr_t kIconY         = 0x00790EA0;   // float __cdecl(): the awards icons' height on the bar
constexpr uintptr_t kIconsShown    = 0x00E59C85;   // byte: awards icons visible
constexpr uintptr_t kOperatorNew   = 0x00ACBC50;   // void* __cdecl(size)
constexpr uintptr_t kIconCtor      = 0x007A2030;   // icon* __thiscall(mem, const String* texture, float u0, v0, u1, v1)
constexpr uintptr_t kIconDtor      = 0x007A2190;   // void __fastcall(icon)
constexpr uintptr_t kIconKind      = 0x007A19E0;   // void __thiscall(icon, int kind)
constexpr uintptr_t kIconTooltip   = 0x007A2790;   // void __thiscall(icon, char autoShow)
constexpr uintptr_t kTooltipPanel  = 0x007C1780;   // window* __cdecl(float date, char stunt): which popup to show
constexpr uintptr_t kTextPopup     = 0x007BF6D0;   // window* __cdecl(float date): the text one
constexpr uintptr_t kSetCallback   = 0x00741630;   // void __thiscall(window, int slot, cb, listener, const char* soundPrefix)
constexpr uintptr_t kTutorialIcon  = 0x0104E824;   // CP: the awards icon the tutorial points at (the ctor sets it)
constexpr uintptr_t kSpecialMode   = 0x0104D8E8;   // non-zero: a mode where the timeline doesn't open things
constexpr int kIconSize = 0x3A0, kIconRefs = 0x48, kIconNoSwap = 0x362, kIconDate = 0x370;
constexpr int kKindPast = 0;          // the game's "last ceremony" icon; the tooltip is ours anyway

// The countdown cues.
constexpr uintptr_t kModes         = 0x00F87B04;   // the game-mode stack
constexpr uintptr_t kModeTop       = 0x00423320;   // int __thiscall(modes): 0 = on the lot
constexpr uintptr_t kPaused        = 0x004201B0;   // bool __thiscall(modes)
constexpr uintptr_t kDate          = 0x00E4FA4C;   // float: today (days)
constexpr uintptr_t kMsPerDayTenth = 0x00E4FA60;   // int: real ms per game day / 100, by era
constexpr uintptr_t kClock         = 0x0104CDF4;   // the clock; +0x38 the game speed
constexpr uintptr_t kNextMovieAwards = 0x0085C530; // float* __cdecl(float* out)
constexpr uintptr_t kSoundRecord   = 0x0041C9C0;   // rec* __thiscall(rec[0x28], const char* sample)
constexpr uintptr_t kPlaySound     = 0x004F3270;   // int __thiscall(mgr, int 2, rec*, int -1, const char* "none")
constexpr uintptr_t kStopSound     = 0x009B11D0;   // void __cdecl(int handle)
constexpr uintptr_t kSoundManager  = 0x0104B0E4;
constexpr uintptr_t kNoneString    = 0x00D17518;

struct GameString {
    char* ptr;
    int   len;
    int   cap;
    char  buf[0x14];
};

struct Icon {
    uintptr_t window;
    int       year;
};
std::vector<Icon> g_icons;
uintptr_t g_timeline = 0;     // the timeline the icons hang from
int g_clicked = 0;            // a year whose icon was clicked (handled in the frame)
bool g_tvTooltip = false;     // a TV icon's tooltip is being made

using Fast_t = void(__fastcall*)(uintptr_t);
using Tooltip_t = void(__fastcall*)(uintptr_t, void*, char);
Fast_t g_origLayout = nullptr, g_origDtor = nullptr;
Tooltip_t g_origTooltip = nullptr;
using Panel_t = uintptr_t(__cdecl*)(float, char);
Panel_t g_origPanel = nullptr;

int IconYear(uintptr_t window)
{
    for (const Icon& i : g_icons)
        if (i.window == window)
            return i.year;
    return 0;
}

// --- Making, placing and forgetting the icons ----------------------------------------

char __stdcall OnClick(uintptr_t sender, uintptr_t listener)
{
    (void)sender;
    if (Rd<int>(kSpecialMode) == 0) {
        int year = IconYear(listener);
        if (year)
            g_clicked = year;
    }
    return 1;
}

uintptr_t MakeIcon(uintptr_t timeline)
{
    GameString tex{};
    const char* path = "ui\\awards_tv.dds";
    tex.ptr = tex.buf;
    tex.cap = 0x14;
    tex.len = static_cast<int>(strlen(path));
    memcpy(tex.buf, path, tex.len + 1);
    __try {
        uintptr_t tutorialCp = kTutorialIcon, before = Rd<uintptr_t>(tutorialCp + 0x14);
        uintptr_t mem = reinterpret_cast<uintptr_t(__cdecl*)(size_t)>(kOperatorNew)(kIconSize);
        if (!mem)
            return 0;
        uintptr_t icon = reinterpret_cast<uintptr_t(__thiscall*)(uintptr_t, const GameString*, float, float, float, float)>(
            kIconCtor)(mem, &tex, 0.0f, 0.0f, 1.0f, 1.0f);
        // The constructor makes it the tutorial's awards icon: give that back to the game's own.
        uintptr_t vt = Rd<uintptr_t>(tutorialCp);
        reinterpret_cast<void(__thiscall*)(uintptr_t)>(Rd<uintptr_t>(vt + 4))(tutorialCp);
        *reinterpret_cast<uintptr_t*>(tutorialCp + 0x14) = before;
        reinterpret_cast<void(__thiscall*)(uintptr_t)>(Rd<uintptr_t>(vt))(tutorialCp);
        uintptr_t ivt = Rd<uintptr_t>(icon);
        reinterpret_cast<void(__thiscall*)(uintptr_t, float, float)>(Rd<uintptr_t>(ivt + 0x74))(icon, 16.0f, 16.0f);
        reinterpret_cast<void(__thiscall*)(uintptr_t, int)>(kIconKind)(icon, kKindPast);
        *reinterpret_cast<uint8_t*>(icon + kIconNoSwap) = 1;   // no centennial picture
        reinterpret_cast<void(__thiscall*)(uintptr_t, int, void*, uintptr_t, const char*)>(kSetCallback)(
            icon, 0, reinterpret_cast<void*>(&OnClick), icon, "TIMELINEAWARDS");
        reinterpret_cast<void(__thiscall*)(uintptr_t, uintptr_t, int)>(Rd<uintptr_t>(Rd<uintptr_t>(timeline) + 0xC))(
            timeline, icon, 1);
        return icon;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

void DropIcon(uintptr_t icon)
{
    __try {
        if (--*reinterpret_cast<int*>(icon + kIconRefs) == 0)
            reinterpret_cast<void(__thiscall*)(uintptr_t, int)>(Rd<uintptr_t>(Rd<uintptr_t>(icon)))(icon, 1);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

void PlaceIcon(uintptr_t timeline, uintptr_t icon, float date)
{
    __try {
        *reinterpret_cast<float*>(icon + kIconDate) = date;
        float x = reinterpret_cast<float(__thiscall*)(uintptr_t, float, float*)>(kDateToX)(timeline, date, nullptr);
        uintptr_t vt = Rd<uintptr_t>(icon);
        auto show = reinterpret_cast<void(__thiscall*)(uintptr_t, int)>(Rd<uintptr_t>(vt + 0x20));
        if (x < 0) {
            show(icon, 0);
            return;
        }
        float y = reinterpret_cast<float(__cdecl*)()>(kIconY)();
        reinterpret_cast<void(__thiscall*)(uintptr_t, int, uintptr_t, float)>(Rd<uintptr_t>(vt + 0x5C))(icon, 1, timeline, x);
        reinterpret_cast<void(__thiscall*)(uintptr_t, int, uintptr_t, float)>(Rd<uintptr_t>(vt + 0x64))(icon, 1, timeline, y);
        show(icon, Rd<uint8_t>(kIconsShown));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

// The years with an icon: every ceremony held, and the next one.
std::vector<int> WantedYears()
{
    std::vector<int> years = AntennaYears();
    int next = NextAntennaYear();
    if (next && std::find(years.begin(), years.end(), next) == years.end())
        years.push_back(next);
    return years;
}

void __fastcall HookLayout(uintptr_t timeline)
{
    g_origLayout(timeline);
    if (!timeline || timeline != Rd<uintptr_t>(kHudTimeline) || !game::Ready() || !Unlocked())
        return;
    if (timeline != g_timeline) {   // a new timeline: the old one took its icons with it
        g_icons.clear();
        g_timeline = timeline;
    }
    std::vector<int> years = WantedYears();
    // Icons for years no longer wanted (another game was loaded) go; missing ones are made.
    for (size_t i = 0; i < g_icons.size();) {
        if (std::find(years.begin(), years.end(), g_icons[i].year) == years.end()) {
            uintptr_t w = g_icons[i].window;
            g_icons.erase(g_icons.begin() + i);
            DropIcon(w);
        } else {
            i++;
        }
    }
    for (int y : years) {
        bool have = false;
        for (const Icon& i : g_icons)
            have |= i.year == y;
        if (have)
            continue;
        uintptr_t w = MakeIcon(timeline);
        if (!w)
            return;
        g_icons.push_back({ w, y });
    }
    for (const Icon& i : g_icons)
        PlaceIcon(timeline, i.window, AntennaDay(i.year));
}

void __fastcall HookDtor(uintptr_t icon)
{
    for (size_t i = 0; i < g_icons.size(); i++)
        if (g_icons[i].window == icon) {
            g_icons.erase(g_icons.begin() + i);
            break;
        }
    g_origDtor(icon);
}

// The game's tooltip, with the TV text in place of "An awards ceremony will take place on...".
const char* const kTooltipKeys[] = { "SITT_AWARDS_UPCOMING", "SITT_AWARDS_PAST", "SITT_AWARDS_CENTENNIALUPCOMING",
                                     "SITT_AWARDS_CENTENNIALPAST" };

std::wstring Wide(const std::string& s)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

void CallTooltip(uintptr_t icon, void* edx, char autoShow)
{
    __try {
        g_origTooltip(icon, edx, autoShow);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

void __fastcall HookTooltip(uintptr_t icon, void* edx, char autoShow)
{
    int year = IconYear(icon);
    if (!year)
        return g_origTooltip(icon, edx, autoShow);
    std::string line = AntennaYearHeld(year)
                           ? Format("The %d Golden Antenna Awards took place on %s. Click to watch them again!", year,
                                    DateText(AntennaDay(year)).c_str())
                           : Format("The %d Golden Antenna Awards, television's biggest night, take place on %s!", year,
                                    DateText(AntennaDay(year)).c_str());
    for (const char* k : kTooltipKeys)
        text::Set(k, Wide(line));
    g_tvTooltip = true;
    CallTooltip(icon, edx, autoShow);
    g_tvTooltip = false;
    for (const char* k : kTooltipKeys)
        text::Unset(k);
}

// In one game mode the game shows its awards panel (the movie prizes) instead of the text;
// a TV icon always gets the text.
uintptr_t __cdecl HookPanel(float date, char stunt)
{
    if (g_tvTooltip)
        return reinterpret_cast<uintptr_t(__cdecl*)(float)>(kTextPopup)(date);
    return g_origPanel(date, stunt);
}

// --- The countdown ---------------------------------------------------------------------

int g_cueYear = 0;
int g_cuesDone = 0;           // bits: 4 minutes, 2 minutes, 30 seconds
int g_cueHandle = -1;

bool OnTheLot()
{
    __try {
        uintptr_t modes = Rd<uintptr_t>(kModes);
        return modes && reinterpret_cast<int(__thiscall*)(uintptr_t)>(kModeTop)(modes) == 0 &&
               !reinterpret_cast<bool(__thiscall*)(uintptr_t)>(kPaused)(modes);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Real milliseconds until a game date, as the game counts down to its own ceremony.
float RealMsUntil(float date)
{
    __try {
        float speed = Rd<float>(Rd<uintptr_t>(kClock) + 0x38);
        if (speed <= 0)
            return -1;
        return (date - Rd<float>(kDate)) * Rd<int>(kMsPerDayTenth) * 100.0f / speed;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

float NextMovieCeremony()
{
    __try {
        float out = 0;
        reinterpret_cast<float*(__cdecl*)(float*)>(kNextMovieAwards)(&out);
        return out;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

void PlayCue(const char* sample)
{
    uint32_t rec[10];
    __try {
        reinterpret_cast<void(__thiscall*)(uint32_t*, const char*)>(kSoundRecord)(rec, sample);
        rec[0] &= ~1u;
        if (g_cueHandle >= 0)
            reinterpret_cast<void(__cdecl*)(int)>(kStopSound)(g_cueHandle);
        uintptr_t mgr = Rd<uintptr_t>(kSoundManager);
        g_cueHandle = mgr ? reinterpret_cast<int(__thiscall*)(uintptr_t, int, uint32_t*, int, const char*)>(kPlaySound)(
                                mgr, 2, rec, -1, reinterpret_cast<const char*>(kNoneString))
                          : -1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_cueHandle = -1;
    }
}

void Countdown()
{
    int year = NextAntennaYear();
    if (!year || AntennaYearHeld(year))
        return;
    if (year != g_cueYear) {
        g_cueYear = year;
        g_cuesDone = 0;
    }
    float date = AntennaDay(year);
    if (std::abs(NextMovieCeremony() - date) < 2)
        return;   // the movie ceremony's own countdown is talking
    float ms = RealMsUntil(date);
    if (ms <= 0 || ms > 240000 || !OnTheLot())
        return;
    // Only the closest mark passed (a game loaded at 1 minute doesn't hear "4 minutes").
    struct Cue { float ms; int bit; const char* sample; const char* when; };
    const Cue cues[] = { { 30000, 4, "HUD_TIMELINE_AWARDS_NOTIFICATION_30SECS", "30 seconds" },
                         { 120000, 2, "HUD_TIMELINE_AWARDS_NOTIFICATION_2MINS", "2 minutes" },
                         { 240000, 1, "HUD_TIMELINE_AWARDS_NOTIFICATION_4MINS", "4 minutes" } };
    for (const Cue& c : cues) {
        if (ms > c.ms)
            continue;
        if (!(g_cuesDone & c.bit)) {
            g_cuesDone |= c.bit | (c.bit - 1);   // and the longer ones with it
            PlayCue(c.sample);
            Notify("The Golden Antenna Awards", Format("Television's biggest night begins in about %s.", c.when), kGold);
        }
        return;
    }
}

bool Hook(uintptr_t addr, void* detour, void** orig, const char* what)
{
    if (MH_CreateHook(reinterpret_cast<void*>(addr), detour, orig) == MH_OK && MH_EnableHook(reinterpret_cast<void*>(addr)) == MH_OK)
        return true;
    mod::Log("tv: timeline - FAILED to hook %s", what);
    return false;
}

} // namespace

void AwardsTimelineFrame()
{
    if (!game::Ready() || !Unlocked() || !CeremonyEnabled())
        return;
    if (g_clicked) {
        int year = g_clicked;
        g_clicked = 0;
        if (!WatchAntennas(year))
            Notify(Format("The %d Golden Antenna Awards", year),
                   Format("Television's biggest night is on %s.", DateText(AntennaDay(year)).c_str()), kGold);
    }
    Countdown();
}

void InstallAwardsTimeline()
{
    // The destructor hook first: nothing is made without it.
    bool ok = Hook(kIconDtor, reinterpret_cast<void*>(&HookDtor), reinterpret_cast<void**>(&g_origDtor), "awards icon") &&
              Hook(kIconTooltip, reinterpret_cast<void*>(&HookTooltip), reinterpret_cast<void**>(&g_origTooltip),
                   "awards tooltip") &&
              Hook(kTooltipPanel, reinterpret_cast<void*>(&HookPanel), reinterpret_cast<void**>(&g_origPanel),
                   "awards tooltip panel") &&
              Hook(kLayout, reinterpret_cast<void*>(&HookLayout), reinterpret_cast<void**>(&g_origLayout), "timeline layout");
    mod::Log("tv: Golden Antennas on the timeline %s", ok ? "installed" : "NOT installed");
}

} // namespace tv
