// Leisure buildings: the Studio Gym, the Games Yard, the Spa & Pool Deck and the Reading
// Garden (tools/make_tv_models.py, 'spots'). Each is the game's own lot decorations - bench
// presses, pinball tables, sun loungers, park benches - with its own copies of their AI
// interactions ("explainers", data/explainers/l<xxx>_<kind><n>.ini). The game does the rest
// as it does for the decorations: anyone with the propinteract desire may use a free spot, a
// player can drop a star on one, and the spot's [aisliders] lower the star's stress and
// boredom (and weight, at the gym) each time its scene fires them.
//
// This module makes stars go there instead of the bar:
//  * Every few seconds an idle star of the player's (not in a building, not working) whose
//    stress or boredom is getting high is sent to the best free spot for them: the spa or the
//    gym for stress, the games for boredom, the garden for either.
//  * A star dropped on a leisure building's main zone (room_tvl_*) goes to a free spot there.
//
// Explainers (CAssetActionExplainer, vtable 0x00D6450C, built by 0x008B2B80) are found by
// hooking their constructor: they are in no list the mod already walks. A spot is only used
// while it still reads as one of ours and belongs to a built leisure building of the player's.
// Settings: [Leisure] in MoviesMod.ini. Unverified in game (re/ex_*.txt).

#include <windows.h>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "MinHook.h"
#include "game.h"
#include "mod.h"
#include "overlay.h"
#include "rooms.h"

namespace leisure {
namespace {

template <typename T> T Rd(uintptr_t a) { return *reinterpret_cast<const T*>(a); }

// CAssetActionExplainer
constexpr uintptr_t kExplainerCtor = 0x008B2B80;   // this __thiscall(mem), RET 0
constexpr uintptr_t kExplainerVtbl = 0x00D6450C;
constexpr int kExType = 0x180;       // String: the type name, e.g. "lspa_lounger3"
constexpr int kExOwner = 0x1D4;      // the facility it belongs to
constexpr int kExUsers = 0x6C, kExCapacity = 0x23C;
constexpr int kVExplain = 0x08;      // CActivity* __thiscall(ex, person, desire, int), RET 0xC
// People
constexpr uintptr_t kAllStars = 0x0104D054;   // InList<CStar>: first node +8, sentinel +0x14
constexpr int kPersonRoom = 0x198, kStaffEmployer = 0x934, kStarMood = 0xC98, kMoodWorkBar = 0x128;
constexpr int kStress = 0x8C, kBoredom = 0x90, kStressThreshold = 0xA4, kBoredomThreshold = 0xA8;
constexpr int kActivityDesire = 0x23C, kActivityExplainer = 0x274, kDesireName = 0x64;
constexpr uintptr_t kFindDesire = 0x0059C530;   // desire* __thiscall(person, const char*)
constexpr uintptr_t kCurrentAct = 0x005998E0;   // activity* __fastcall(person)
constexpr uintptr_t kCancelAct = 0x0059ADD0;    // __thiscall(person, activity)
constexpr uintptr_t kAddAction = 0x0059C020;    // __thiscall(person, activity)

// The buildings, by spot prefix: what each is best for (higher = preferred).
struct Kind {
    const char* prefix;    // explainer names start with this
    const char* type;      // facility file name
    const char* zone;      // its drop zone
    const char* title;
    float       forStress, forBoredom;
};
const Kind kKinds[] = {
    { "lspa_", "facility_leisure_spa", "room_tvl_spa", "the Spa & Pool Deck", 1.0f, 0.2f },
    { "lgym_", "facility_leisure_gym", "room_tvl_gym", "the Studio Gym", 0.8f, 0.5f },
    { "lgam_", "facility_leisure_games", "room_tvl_games", "the Games Yard", 0.3f, 1.0f },
    { "lgdn_", "facility_leisure_garden", "room_tvl_garden", "the Reading Garden", 0.5f, 0.4f },
};

bool  g_on = true;
float g_stressStart = 0.45f;    // of the star's stress threshold
float g_boredomStart = 0.55f;   // of the boredom threshold
DWORD g_checkMs = 3000, g_starCooldownMs = 45000;

CRITICAL_SECTION g_lock;
std::vector<uintptr_t> g_made;             // every CAssetActionExplainer built (pruned as read)
using Ctor_t = uintptr_t(__fastcall*)(uintptr_t self);
Ctor_t g_origCtor = nullptr;

uintptr_t __fastcall HookCtor(uintptr_t self)
{
    uintptr_t r = g_origCtor(self);
    EnterCriticalSection(&g_lock);
    g_made.push_back(self);
    LeaveCriticalSection(&g_lock);
    return r;
}

struct Spot {
    uintptr_t   ex = 0;
    const Kind* kind = nullptr;
    char        name[32] = {};
    bool        free = false;
};

std::map<uintptr_t, DWORD> g_sentAt;         // star -> when we last sent them
std::map<uintptr_t, uintptr_t> g_reserved;   // spot -> star on the way
DWORD g_lastCheck = 0;
int   g_sends = 0;

// --- Raw reads (no C++ objects: SEH) -----------------------------------------------

// Reads one recorded explainer: false if it isn't a live leisure spot (any more).
bool ReadSpot(uintptr_t ex, Spot* out)
{
    __try {
        if (Rd<uintptr_t>(ex) != kExplainerVtbl)
            return false;
        const char* text = Rd<const char*>(ex + kExType);
        int len = Rd<int>(ex + kExType + 4);
        if (!text || len < 6 || len > 30 || IsBadReadPtr(text, len))
            return false;
        memcpy(out->name, text, len);
        out->name[len] = 0;
        out->kind = nullptr;
        for (const Kind& k : kKinds)
            if (strncmp(out->name, k.prefix, 5) == 0)
                out->kind = &k;
        if (!out->kind)
            return false;
        out->ex = ex;
        out->free = Rd<int>(ex + kExUsers) < (Rd<int>(ex + kExCapacity) > 0 ? Rd<int>(ex + kExCapacity) : 1);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

uintptr_t OwnerOf(uintptr_t ex)
{
    __try {
        return Rd<uintptr_t>(ex + kExOwner);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

int ListStars(uintptr_t* out, int max)
{
    int n = 0;
    __try {
        uintptr_t node = Rd<uintptr_t>(kAllStars + 8);
        for (int i = 0; node && node != kAllStars + 0x14 && i < 2000 && n < max; i++, node = Rd<uintptr_t>(node + 4))
            if (uintptr_t p = Rd<uintptr_t>(node + 8))
                out[n++] = p;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return n;
}

uintptr_t CurrentActivity(uintptr_t person)
{
    __try {
        return reinterpret_cast<uintptr_t(__fastcall*)(uintptr_t)>(kCurrentAct)(person);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

bool DesireNameOf(uintptr_t activity, char* out, int size)
{
    out[0] = 0;
    __try {
        uintptr_t desire = activity ? Rd<uintptr_t>(activity + kActivityDesire) : 0;
        if (!desire)
            return false;
        const char* text = Rd<const char*>(desire + kDesireName);
        int len = Rd<int>(desire + kDesireName + 4);
        if (!text || len <= 0 || len >= size || IsBadReadPtr(text, len))
            return false;
        memcpy(out, text, len);
        out[len] = 0;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

uintptr_t ExplainerOf(uintptr_t activity)
{
    __try {
        return activity ? Rd<uintptr_t>(activity + kActivityExplainer) : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

// How worn down a star is: stress and boredom as fractions of their thresholds.
bool Mood(uintptr_t star, uintptr_t studio, float* stress, float* boredom)
{
    __try {
        if (Rd<uintptr_t>(star + kStaffEmployer) != studio)
            return false;
        uintptr_t mood = Rd<uintptr_t>(star + kStarMood);
        uintptr_t wb = mood ? Rd<uintptr_t>(mood + kMoodWorkBar) : 0;
        if (!wb)
            return false;
        float st = Rd<float>(wb + kStressThreshold), bt = Rd<float>(wb + kBoredomThreshold);
        *stress = st > 0 ? Rd<float>(wb + kStress) / st : 0;
        *boredom = bt > 0 ? Rd<float>(wb + kBoredom) / bt : 0;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool InBuilding(uintptr_t person)
{
    __try {
        return Rd<uintptr_t>(person + kPersonRoom) != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return true;
    }
}

// Sends someone to use a spot, the way the AI does: the explainer makes the activity for the
// propinteract desire, and it replaces what they are doing.
bool SendRaw(uintptr_t person, uintptr_t ex, int* step)
{
    __try {
        *step = 1;
        uintptr_t desire = reinterpret_cast<uintptr_t(__thiscall*)(uintptr_t, const char*)>(kFindDesire)(person, "propinteract");
        if (!desire)
            return false;
        *step = 2;
        using Explain_t = uintptr_t(__thiscall*)(uintptr_t, uintptr_t, uintptr_t, int);
        uintptr_t act = reinterpret_cast<Explain_t>(Rd<uintptr_t>(Rd<uintptr_t>(ex) + kVExplain))(ex, person, desire, 0);
        if (!act)
            return false;
        *step = 3;
        uintptr_t current = reinterpret_cast<uintptr_t(__fastcall*)(uintptr_t)>(kCurrentAct)(person);
        if (current)
            reinterpret_cast<void(__thiscall*)(uintptr_t, uintptr_t)>(kCancelAct)(person, current);
        *step = 4;
        reinterpret_cast<void(__thiscall*)(uintptr_t, uintptr_t)>(kAddAction)(person, act);
        *step = 5;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// --- Spots --------------------------------------------------------------------------

// The leisure spots of the player's built leisure buildings (optionally only one building type).
std::vector<Spot> Spots(const char* onlyType = nullptr)
{
    std::map<uintptr_t, const Kind*> mine;   // facility -> its kind
    for (const game::Facility& f : game::Facilities())
        if (f.built)
            for (const Kind& k : kKinds)
                if (f.type == k.type && (!onlyType || f.type == onlyType))
                    mine[f.address] = &k;
    std::vector<Spot> out;
    if (mine.empty())
        return out;
    EnterCriticalSection(&g_lock);
    std::vector<uintptr_t> made = g_made;
    LeaveCriticalSection(&g_lock);
    std::vector<uintptr_t> keep;
    for (uintptr_t ex : made) {
        Spot s;
        if (!ReadSpot(ex, &s))
            continue;   // freed, reused, or not one of ours
        keep.push_back(ex);
        auto it = mine.find(OwnerOf(ex));
        if (it != mine.end() && it->second == s.kind)
            out.push_back(s);
    }
    if (keep.size() < made.size()) {
        EnterCriticalSection(&g_lock);
        // Only prune what was there when we copied (new ones may have been added since).
        std::vector<uintptr_t> rest(g_made.begin() + made.size(), g_made.end());
        g_made = keep;
        g_made.insert(g_made.end(), rest.begin(), rest.end());
        LeaveCriticalSection(&g_lock);
    }
    return out;
}

bool Reserved(uintptr_t ex)
{
    auto it = g_reserved.find(ex);
    if (it == g_reserved.end())
        return false;
    // Taken while the star is still on the way to it (or using it).
    if (ExplainerOf(CurrentActivity(it->second)) == ex)
        return true;
    g_reserved.erase(it);
    return false;
}

const Spot* Best(const std::vector<Spot>& spots, float stress, float boredom)
{
    const Spot* best = nullptr;
    float bestScore = -1;
    for (const Spot& s : spots) {
        if (!s.free || Reserved(s.ex))
            continue;
        float score = stress * s.kind->forStress + boredom * s.kind->forBoredom +
                      static_cast<float>((s.ex >> 4) % 97 + GetTickCount() % 89) * 0.0001f;   // spread them out
        if (score > bestScore) {
            bestScore = score;
            best = &s;
        }
    }
    return best;
}

bool Send(uintptr_t star, const Spot& s, const char* why)
{
    int step = 0;
    if (!SendRaw(star, s.ex, &step)) {
        mod::Log("leisure: FAILED to send %08X to %s (step %d)", static_cast<unsigned>(star), s.name, step);
        return false;
    }
    g_reserved[s.ex] = star;
    g_sentAt[star] = GetTickCount();
    if (g_sends++ < 40)
        mod::Log("leisure: %s goes to %s (%s, %s)", rooms::NameOf(star).c_str(), s.name, s.kind->title, why);
    return true;
}


// Not in a building and not busy: what the manager's rest breaks count as idle, plus already
// on the way to (or using) a decoration or a leisure spot.
bool IsIdle(uintptr_t star)
{
    if (InBuilding(star))
        return false;
    static const char* const kBusy[] = { "rehearse", "readyposition", "changecostume", "stunttrain", "writescript",
                                         "fetchfilmgear", "edit", "brainstorm", "leave", "getdrunk", "overdrink",
                                         "dolunch", "match_dolunch", "visittrailer", "buyfood", "overeat",
                                         "propinteract", "health", "urgent", "respondtodrop", "getautograph",
                                         "crap_male", "crap_female", "match_sexintrailer", "match_makelove",
                                         "match_fight", "tantrum", "trailertantrum", "vomit", "arrest" };
    char name[64];
    DesireNameOf(CurrentActivity(star), name, sizeof(name));
    for (const char* b : kBusy)
        if (_stricmp(name, b) == 0)
            return false;
    return true;
}

void Check()
{
    uintptr_t studio = game::ReadPlayerStudio().address;
    if (!studio)
        return;
    std::vector<Spot> spots = Spots();
    static size_t lastCount = static_cast<size_t>(-1);
    if (spots.size() != lastCount) {
        lastCount = spots.size();
        mod::Log("leisure: %d spot(s) in the player's leisure buildings", static_cast<int>(spots.size()));
    }
    if (spots.empty())
        return;
    std::vector<uintptr_t> stars(2000);
    int n = ListStars(stars.data(), static_cast<int>(stars.size()));
    DWORD now = GetTickCount();
    for (int i = 0; i < n; i++) {
        uintptr_t star = stars[i];
        float stress = 0, boredom = 0;
        if (!Mood(star, studio, &stress, &boredom))
            continue;
        if (stress < g_stressStart && boredom < g_boredomStart)
            continue;
        auto sent = g_sentAt.find(star);
        if (sent != g_sentAt.end() && now - sent->second < g_starCooldownMs)
            continue;
        if (!IsIdle(star))
            continue;
        const Spot* s = Best(spots, stress >= g_stressStart ? stress : 0, boredom >= g_boredomStart ? boredom : 0);
        if (!s)
            return;   // everything's taken
        char why[64];
        sprintf_s(why, "stress %.0f%%, boredom %.0f%%", stress * 100, boredom * 100);
        if (Send(star, *s, why)) {
            for (Spot& t : spots)
                if (t.ex == s->ex)
                    t.free = false;
        }
    }
}

bool SafeCheck()
{
    __try {
        Check();
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void Frame()
{
    if (!g_on || !game::Ready())
        return;
    DWORD now = GetTickCount();
    if (now - g_lastCheck < g_checkMs)
        return;
    g_lastCheck = now;
    if (!SafeCheck()) {
        mod::Log("leisure: sending stars FAILED - turned off");
        g_on = false;
    }
}

float IniFloat(const wchar_t* ini, const wchar_t* key, float def)
{
    wchar_t buf[64];
    GetPrivateProfileStringW(L"Leisure", key, L"", buf, 64, ini);
    return buf[0] ? static_cast<float>(_wtof(buf)) : def;
}

} // namespace

bool IsZone(const std::string& zone) { return zone.rfind("room_tvl_", 0) == 0; }

std::string OnDrop(const std::string& zone, uintptr_t person)
{
    const Kind* kind = nullptr;
    for (const Kind& k : kKinds)
        if (zone == k.zone)
            kind = &k;
    if (!kind)
        return "";
    if (!person)
        return std::string("Drop a star on ") + kind->title + " to send them to unwind there.";
    std::vector<Spot> spots = Spots(kind->type);
    float stress = 0.5f, boredom = 0.5f;
    Mood(person, game::ReadPlayerStudio().address, &stress, &boredom);
    const Spot* s = Best(spots, stress + 0.01f, boredom + 0.01f);
    if (!s) {
        mod::Log("leisure: %s dropped on %s - no free spot (%d spots)", rooms::NameOf(person).c_str(), zone.c_str(),
                 static_cast<int>(spots.size()));
        return spots.empty() ? std::string("Wait until ") + kind->title + " is finished."
                             : std::string("Every spot at ") + kind->title + " is taken. Try again in a moment.";
    }
    if (!Send(person, *s, "dropped there"))
        return "They couldn't be sent there (see MoviesMod.log).";
    return "";
}

void Install()
{
    wchar_t ini[MAX_PATH];
    swprintf_s(ini, L"%s\\MoviesMod.ini", mod::GameDir());
    InitializeCriticalSection(&g_lock);
    g_on = GetPrivateProfileIntW(L"Leisure", L"SendStars", 1, ini) != 0;
    g_stressStart = IniFloat(ini, L"StressStart", g_stressStart);
    g_boredomStart = IniFloat(ini, L"BoredomStart", g_boredomStart);
    // The constructor hook finds the spots, for drops too: always on.
    if (MH_CreateHook(reinterpret_cast<LPVOID>(kExplainerCtor), reinterpret_cast<LPVOID>(&HookCtor),
                      reinterpret_cast<LPVOID*>(&g_origCtor)) != MH_OK ||
        MH_EnableHook(reinterpret_cast<LPVOID>(kExplainerCtor)) != MH_OK) {
        mod::Log("leisure: FAILED to hook the explainer constructor - leisure buildings off");
        g_on = false;
        return;
    }
    overlay::AddFrameCallback(&Frame);
    mod::Log("leisure: stars sent to unwind %s (stress from %.0f%%, boredom from %.0f%%)", g_on ? "on" : "off",
             g_stressStart * 100, g_boredomStart * 100);
}

} // namespace leisure
