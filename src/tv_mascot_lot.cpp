// Toon Studio: the mascot on the lot.
//
// The Mascot Statue (facility_mascot_statue, tools/make_tv_models.py): a painted statue of the
// studio mascot on a stone plinth. Once one is built (not while the builders are still at it)
// and the mascot is born, the mascot's merchandise sells kStatueMerch more and audiences forget
// the mascot more slowly (its fade half-life is kStatueFade times as long). More statues add
// nothing more.
//
// The costumed walkabout: one of the player's extras dropped on a statue puts on the mascot
// suit (g_mm_mascot, tools/costume_pack.json) and walks the lot from spot to spot, standing a
// while at each, until they're dropped on a statue again or put to work in a building; then
// they change back into their own clothes. While the mascot walks, merchandise sells
// kWalkerMerch more. One walker at a time.
//
// The game's people act through a queue of activities, each an AI scene placed in the world
// (re/mascot.md). The walk copies the game's forced janitor tidy (0x00462BE0): a scene at a
// random walkable point (0x004EEBE0, as the wander behaviour picks one) playing ai_donothing,
// an activity on it that can't be interrupted, the current one cancelled and this one added.
// Dressing copies the game's dresser (0x00598540): the costume is built by name and put on, and
// also made the costume the person wants to wear, or the AI would change them back.

#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include "game.h"
#include "mod.h"
#include "overlay.h"
#include "persist.h"
#include "rooms.h"
#include "tv_internal.h"

namespace tv {
namespace {

constexpr const char* kStatueType = "facility_mascot_statue";
constexpr const char* kStatueZone = "room_tvm_";
constexpr float kStatueMerch = 0.20f;
constexpr float kStatueFade = 1.5f;

constexpr float kWalkerMerch = 0.15f;
constexpr const char* kSuit = "g_mm_mascot";
constexpr int kCareerExtra = 4;
constexpr DWORD kWalkerTickMs = 1500;
constexpr float kSpotDistance = 12.0f;   // how far the next spot is, at least (metres)

// TMCharacter / CStaff
constexpr int kStaffCareer = 0x814, kStaffEmployer = 0x934, kGender = 0x4A0;
constexpr int kWornCostume = 0x568;                // CCostume*
constexpr int kCostumeRefs = 0x48, kCostumeName = 0x78;
constexpr int kVSetCostume = 0x128;                // vfunc: void __thiscall(person, CCostume*, char rebuild)
constexpr uintptr_t kAllStaff = 0x0104CFC0;        // InList<CStaff>: first node +8, sentinel +0x14
// Functions (re/mascot.md)
constexpr uintptr_t kBuildCostume  = 0x004335F0;   // void __cdecl(CCostume** out, String* name, int gender, 0, 0, 0)
constexpr uintptr_t kSetDesired    = 0x0059BB60;   // void __thiscall(person, CCostume*)  (addrefs)
constexpr uintptr_t kOwnClothes    = 0x0059BBD0;   // CCostume* __fastcall(person)  (built if missing)
constexpr uintptr_t kNew           = 0x00ACBC50;   // operator new
constexpr uintptr_t kSceneCtor     = 0x00445F60;   // __thiscall(mem, float pos[3], float angle)
constexpr int       kVLoadScene    = 0xB0;         // scene vfunc: (String* "ai_x.flm")
constexpr uintptr_t kActivityCtor  = 0x00402380;   // __thiscall(mem, person, scene)
constexpr uintptr_t kSetDesire     = 0x00401A00;   // __thiscall(activity, desire)
constexpr uintptr_t kSetInterrupt  = 0x00401050;   // __thiscall(activity, char)
constexpr uintptr_t kFindDesire    = 0x0059C530;   // desire* __thiscall(person, const char*)
constexpr uintptr_t kCurrentAct    = 0x005998E0;   // activity* __fastcall(person)
constexpr uintptr_t kCancelAct     = 0x0059ADD0;   // __thiscall(person, activity)
constexpr uintptr_t kAddAction     = 0x0059C020;   // __thiscall(person, activity)
constexpr uintptr_t kRandomSpot    = 0x004EEBE0;   // float* __cdecl(float out[2], person, float dist)
constexpr int kActFlags = 0x210, kActForced = 0x292, kActMode = 0x240;
// The desire the walk's activities count as: the first of these the person has.
const char* const kDesires[] = { "wander", "roam", "urgent", "respondtodrop" };

bool  g_statueSeen = false;   // a built statue has been announced (saved)
float g_lastCheck = -1;

struct Walker {
    std::string name;            // "" = nobody in the suit (saved)
    uintptr_t   person = 0;      // found again by name after a load
    uintptr_t   activity = 0;    // the walk's current activity (compared only, never read)
    DWORD       nextTick = 0;
    int         spots = 0;
    bool        redressed = false;   // put back in the suit once (after a load, say)
};
Walker g_walker;

bool StatueBuilt() { return game::CountFacilities(kStatueType, true) > 0; }

// The game's narrow String, short enough to sit in its own buffer.
struct GameString {
    const char* text;
    int         len;
    int         cap;
    char        buf[20];
};
void MakeString(GameString* s, const char* text)
{
    s->len = static_cast<int>(strlen(text));
    if (s->len > 19)
        s->len = 19;
    memcpy(s->buf, text, s->len);
    s->buf[s->len] = 0;
    s->text = s->buf;
    s->cap = 0x14;
}

template <class T> T Rd(uintptr_t a) { return *reinterpret_cast<T*>(a); }

void Release(uintptr_t refCounted)
{
    if (refCounted && --*reinterpret_cast<int*>(refCounted + kCostumeRefs) == 0) {
        using Dtor = void(__thiscall*)(uintptr_t, int);
        reinterpret_cast<Dtor>(Rd<uintptr_t>(Rd<uintptr_t>(refCounted)))(refCounted, 1);
    }
}

// The name of what they're wearing ("" if unreadable).
bool ReadWorn(uintptr_t person, char* out, int size)
{
    __try {
        uintptr_t c = Rd<uintptr_t>(person + kWornCostume);
        if (!c)
            return false;
        uintptr_t s = c + kCostumeName;   // String: text, length, capacity, buffer
        int len = std::clamp(Rd<int>(s + 4), 0, size - 1);
        memcpy(out, Rd<const char*>(s), len);
        out[len] = 0;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

std::string WornName(uintptr_t person)
{
    char out[40] = {};
    return ReadWorn(person, out, sizeof(out)) ? std::string(out) : std::string();
}

bool Wearing(uintptr_t person) { return WornName(person).find("mascot") != std::string::npos; }

bool PutOn(uintptr_t person, uintptr_t costume)
{
    using SetCostume = void(__thiscall*)(uintptr_t, uintptr_t, char);
    reinterpret_cast<SetCostume>(Rd<uintptr_t>(Rd<uintptr_t>(person) + kVSetCostume))(person, costume, 1);
    reinterpret_cast<void(__thiscall*)(uintptr_t, uintptr_t)>(kSetDesired)(person, costume);
    return true;
}

bool DressAsMascot(uintptr_t person)
{
    __try {
        GameString name;
        MakeString(&name, kSuit);
        uintptr_t costume = 0;
        reinterpret_cast<void(__cdecl*)(uintptr_t*, GameString*, int, int, int, int)>(kBuildCostume)(
            &costume, &name, Rd<int>(person + kGender), 0, 0, 0);
        if (!costume)
            return false;
        PutOn(person, costume);
        Release(costume);   // both setters hold their own reference
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool DressInOwnClothes(uintptr_t person)
{
    __try {
        uintptr_t own = reinterpret_cast<uintptr_t(__fastcall*)(uintptr_t)>(kOwnClothes)(person);
        return own && PutOn(person, own);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

uintptr_t CurrentActivity(uintptr_t person)
{
    __try {
        return reinterpret_cast<uintptr_t(__fastcall*)(uintptr_t)>(kCurrentAct)(person);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

uintptr_t FindDesire(uintptr_t person, const char** which)
{
    for (const char* d : kDesires) {
        uintptr_t desire = 0;
        __try {
            desire = reinterpret_cast<uintptr_t(__thiscall*)(uintptr_t, const char*)>(kFindDesire)(person, d);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            desire = 0;
        }
        if (desire) {
            *which = d;
            return desire;
        }
    }
    return 0;
}

// Sends them to stand at a new spot on the lot: returns the activity, or 0.
uintptr_t WalkToNextSpot(uintptr_t person, float* where)
{
    const char* desireName = "";
    uintptr_t desire = FindDesire(person, &desireName);
    if (!desire) {
        mod::Log("tv: mascot walker has none of the desires to walk with");
        return 0;
    }
    __try {
        float spot[2] = {};
        reinterpret_cast<float*(__cdecl*)(float*, uintptr_t, float)>(kRandomSpot)(spot, person, kSpotDistance);
        float pos[3] = { spot[0], spot[1], 0.0f };
        float angle = static_cast<float>((GetTickCount() % 628) / 100.0);
        auto alloc = reinterpret_cast<uintptr_t(__cdecl*)(size_t)>(kNew);
        uintptr_t scene = alloc(0x2E0);
        if (!scene)
            return 0;
        scene = reinterpret_cast<uintptr_t(__thiscall*)(uintptr_t, float*, float)>(kSceneCtor)(scene, pos, angle);
        GameString file;
        MakeString(&file, "ai_donothing.flm");
        reinterpret_cast<void(__thiscall*)(uintptr_t, GameString*)>(Rd<uintptr_t>(Rd<uintptr_t>(scene) + kVLoadScene))(scene, &file);
        uintptr_t act = alloc(0x2B4);
        if (!act) {
            Release(scene);
            return 0;
        }
        act = reinterpret_cast<uintptr_t(__thiscall*)(uintptr_t, uintptr_t, uintptr_t)>(kActivityCtor)(act, person, scene);
        reinterpret_cast<void(__thiscall*)(uintptr_t, uintptr_t)>(kSetDesire)(act, desire);
        *reinterpret_cast<uint32_t*>(act + kActFlags) &= ~1u;
        reinterpret_cast<void(__thiscall*)(uintptr_t, char)>(kSetInterrupt)(act, 0);
        Release(scene);   // the activity holds it
        uintptr_t current = CurrentActivity(person);
        if (current)
            reinterpret_cast<void(__thiscall*)(uintptr_t, uintptr_t)>(kCancelAct)(person, current);
        reinterpret_cast<void(__thiscall*)(uintptr_t, uintptr_t)>(kAddAction)(person, act);
        *reinterpret_cast<uint8_t*>(act + kActForced) = 1;
        *reinterpret_cast<int*>(act + kActMode) = 2;
        where[0] = spot[0];
        where[1] = spot[1];
        static bool logged = false;
        if (!logged) {
            logged = true;
            mod::Log("tv: mascot walker walks with the '%s' desire", desireName);
        }
        return act;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        mod::Log("tv: mascot walker's walk failed");
        return 0;
    }
}

bool IsExtraOf(uintptr_t person, uintptr_t studio)
{
    __try {
        return studio && Rd<int>(person + kStaffCareer) == kCareerExtra && Rd<uintptr_t>(person + kStaffEmployer) == studio;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool IsPlayersExtra(uintptr_t person) { return IsExtraOf(person, game::ReadPlayerStudio().address); }

int ListStaff(uintptr_t* out, int max)
{
    int n = 0;
    __try {
        uintptr_t sentinel = kAllStaff + 0x14;
        uintptr_t node = Rd<uintptr_t>(kAllStaff + 8);
        for (int i = 0; node && node != sentinel && i < 4000 && n < max; i++, node = Rd<uintptr_t>(node + 4))
            if (uintptr_t p = Rd<uintptr_t>(node + 8))
                out[n++] = p;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return n;
}

// The player's extra called 'name' (after a load), or 0.
uintptr_t FindByName(const std::string& name)
{
    std::vector<uintptr_t> people(4000);
    int n = ListStaff(people.data(), static_cast<int>(people.size()));
    uintptr_t studio = game::ReadPlayerStudio().address;
    for (int i = 0; i < n; i++)
        if (IsExtraOf(people[i], studio) && rooms::NameOf(people[i]) == name)
            return people[i];
    return 0;
}

void CancelActivity(uintptr_t person, uintptr_t activity)
{
    __try {
        reinterpret_cast<void(__thiscall*)(uintptr_t, uintptr_t)>(kCancelAct)(person, activity);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

void StopWalker(const char* why)
{
    if (g_walker.name.empty())
        return;
    uintptr_t p = g_walker.person;
    if (p) {
        if (g_walker.activity && CurrentActivity(p) == g_walker.activity)
            CancelActivity(p, g_walker.activity);
        if (!DressInOwnClothes(p))
            mod::Log("tv: mascot walker %s couldn't change back", g_walker.name.c_str());
    }
    mod::Log("tv: mascot walker %s stops (%s) after %d spots", g_walker.name.c_str(), why, g_walker.spots);
    g_walker = Walker{};
}

void StartWalker(uintptr_t person, const std::string& name)
{
    if (!DressAsMascot(person)) {
        mod::Log("tv: mascot suit '%s' couldn't be put on %s", kSuit, name.c_str());
        Notify("The Mascot Statue", "The mascot suit didn't fit. (See MoviesMod.log.)", kYellow);
        return;
    }
    g_walker = Walker{};
    g_walker.name = name;
    g_walker.person = person;
    g_walker.nextTick = GetTickCount() + 3000;   // let the drop settle first
    std::string mascot = MascotName(false);
    mod::Log("tv: mascot walker %s puts on the suit (worn: '%s')", name.c_str(), WornName(person).c_str());
    Notify(Format("%s walks the lot", mascot.c_str()),
           Format("%s puts on the %s suit and walks the lot. Merchandise sells %d%% more while %s is out. Drop them on "
                  "the statue again, or put them to work, to take the suit off.",
                  name.c_str(), mascot.c_str(), static_cast<int>(kWalkerMerch * 100 + 0.5f), mascot.c_str()),
           kGold);
}

void WalkerTick()
{
    if (g_walker.name.empty() || GetTickCount() < g_walker.nextTick)
        return;
    g_walker.nextTick = GetTickCount() + kWalkerTickMs;
    if (!g_walker.person) {
        g_walker.person = FindByName(g_walker.name);
        if (!g_walker.person) {
            mod::Log("tv: mascot walker %s not found", g_walker.name.c_str());
            g_walker = Walker{};
            return;
        }
        mod::Log("tv: mascot walker %s found again", g_walker.name.c_str());
    }
    uintptr_t p = g_walker.person;
    if (!IsPlayersExtra(p)) {
        StopWalker("no longer the player's extra");
        return;
    }
    std::string zone = ZoneOf(p);
    if (!zone.empty() && zone.rfind(kStatueZone, 0) != 0) {
        StopWalker(Format("put in %s", zone.c_str()).c_str());
        return;
    }
    if (!Wearing(p)) {
        // Once: if the suit comes off again (or its name reads differently), leave it be.
        if (g_walker.redressed || !DressAsMascot(p)) {
            StopWalker(Format("the suit came off (wearing '%s')", WornName(p).c_str()).c_str());
            return;
        }
        g_walker.redressed = true;
        mod::Log("tv: mascot walker %s put back in the suit (now '%s')", g_walker.name.c_str(), WornName(p).c_str());
    }
    if (g_walker.activity && CurrentActivity(p) == g_walker.activity)
        return;   // still walking to, or standing at, this spot
    float where[2] = {};
    g_walker.activity = WalkToNextSpot(p, where);
    if (!g_walker.activity) {
        g_walker.nextTick = GetTickCount() + 10000;
        return;
    }
    g_walker.spots++;
    if (g_walker.spots <= 3 || g_walker.spots % 20 == 0)
        mod::Log("tv: mascot walker %s - spot %d at (%.1f, %.1f)", g_walker.name.c_str(), g_walker.spots, where[0], where[1]);
}

void Daily()
{
    bool built = StatueBuilt();
    if (built && !g_statueSeen) {
        std::string name = MascotName(false);
        if (name.empty())
            Notify("The Mascot Statue",
                   "The statue waits for its star: your first cartoon or short brings the studio's mascot to life.", kGold);
        else
            Notify(Format("A statue of %s", name.c_str()),
                   Format("%s now stands on the lot. Merchandise sells %d%% more, and audiences forget %s more slowly.",
                          name.c_str(), static_cast<int>(kStatueMerch * 100 + 0.5f), name.c_str()),
                   kGold);
        mod::Log("tv: mascot statue built (mascot '%s')", name.c_str());
    }
    g_statueSeen = built;
}

void Frame()
{
    if (!game::Ready())
        return;
    WalkerTick();
    float today = game::DateDays();
    if (today <= 0)
        return;
    if (g_lastCheck >= 0 && today >= g_lastCheck && today - g_lastCheck < 1)
        return;
    g_lastCheck = today;
    Daily();
}

} // namespace

float MascotLotMerch()
{
    return 1.0f + (StatueBuilt() ? kStatueMerch : 0.0f) + (g_walker.name.empty() ? 0.0f : kWalkerMerch);
}

float MascotLotFade()
{
    return StatueBuilt() ? kStatueFade : 1.0f;
}

bool MascotLotDrop(const std::string& zone, uintptr_t room, uintptr_t project, uintptr_t person)
{
    if (zone.rfind(kStatueZone, 0) != 0)
        return false;
    std::string name = MascotName(false);
    if (project) {
        Notify("The Mascot Statue", "Scripts and films go to the buildings that make them, not the statue.", kYellow);
        return true;
    }
    if (!person)
        return true;
    (void)room;
    std::string who = rooms::NameOf(person);
    if (!g_walker.name.empty() && (person == g_walker.person || who == g_walker.name)) {
        Notify(Format("%s takes a break", name.c_str()), Format("%s takes the suit off.", g_walker.name.c_str()), kGold);
        StopWalker("dropped on the statue");
        return true;
    }
    if (name.empty()) {
        Notify("The Mascot Statue",
               "There's no mascot to dress up as yet: your first cartoon or short brings it to life.", kYellow);
        return true;
    }
    if (!IsPlayersExtra(person) || who.empty()) {
        Notify("The Mascot Statue", Format("Only one of your extras can wear the %s suit.", name.c_str()), kYellow);
        return true;
    }
    if (!g_walker.name.empty())
        StopWalker(Format("%s takes over", who.c_str()).c_str());
    StartWalker(person, who);
    return true;
}

void InstallMascotLot()
{
    overlay::AddFrameCallback(&Frame);
    persist::Register(
        "mascot_lot",
        [](persist::Section& out) {
            if (g_statueSeen)
                out["statue_seen"] = "1";
            if (!g_walker.name.empty())
                out["walker"] = g_walker.name;
        },
        [](const persist::Section& in) {
            g_statueSeen = in.count("statue_seen") > 0;
            g_lastCheck = -1;
            g_walker = Walker{};
            auto it = in.find("walker");
            if (it != in.end() && !it->second.empty()) {
                g_walker.name = it->second;   // found again by name on the next tick
                g_walker.nextTick = GetTickCount() + 5000;
                mod::Log("tv: mascot walker %s restored", g_walker.name.c_str());
            }
        },
        [] {
            g_statueSeen = false;
            g_lastCheck = -1;
            g_walker = Walker{};
        });
}

} // namespace tv
