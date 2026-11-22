// Leisure buildings: the Studio Gym, the Games Yard, the Spa & Pool Deck, the Reading Garden
// and the Sculpture Park (tools/make_tv_models.py, 'spots'). Each is the game's own lot
// decorations - bench presses, pinball tables, sun loungers, fountains, alien rocks - with its
// own copies of their AI interactions ("explainers", data/explainers/l<xxx>_<kind><n>.ini).
// The game does the rest as it does for the decorations: anyone with the propinteract desire
// may use a free spot, a player can drop a star on one, and the spot's [aisliders] lower the
// star's stress, boredom, drunkenness or weight each time its scene fires them.
//
// This module makes stars go there instead of the bar:
//  * Every few seconds an idle star of the player's (not in a building, not working) who is
//    stressed, bored, drunk or getting heavy is sent to the best free spot for that: loungers
//    and tubs for stress, pinball and sculpture for boredom, a plunge in a fountain for a drunk
//    star, the gym for weight (each kind of spot has its own strengths, kSpotKinds).
//  * A star dropped on a leisure building's main zone (room_tvl_*) goes to a free spot there.
//  * Wrap parties: when one of the player's films finishes shooting, its cast goes off together
//    to the leisure building with the most free spots, and the papers report it.
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
#include "news.h"
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
constexpr int kStarWeight = 0xB3C, kStarDrunk = 0xB9C;   // floats: 60..100, 0..1
constexpr int kActivityDesire = 0x23C, kActivityExplainer = 0x274, kDesireName = 0x64;
constexpr uintptr_t kFindDesire = 0x0059C530;   // desire* __thiscall(person, const char*)
constexpr uintptr_t kCurrentAct = 0x005998E0;   // activity* __fastcall(person)
constexpr uintptr_t kCancelAct = 0x0059ADD0;    // __thiscall(person, activity)
constexpr uintptr_t kAddAction = 0x0059C020;    // __thiscall(person, activity)

// The buildings, by spot prefix.
struct Kind {
    const char* prefix;    // explainer names start with this
    const char* type;      // facility file name
    const char* zone;      // its drop zone
    const char* title;
};
const Kind kKinds[] = {
    { "lspa_", "facility_leisure_spa", "room_tvl_spa", "the Spa & Pool Deck" },
    { "lgym_", "facility_leisure_gym", "room_tvl_gym", "the Studio Gym" },
    { "lgam_", "facility_leisure_games", "room_tvl_games", "the Games Yard" },
    { "lgdn_", "facility_leisure_garden", "room_tvl_garden", "the Reading Garden" },
    { "lprk_", "facility_leisure_park", "room_tvl_park", "the Sculpture Park" },
};

// What each kind of spot is good for (higher = preferred), matching its sliders in
// tools/make_tv_models.py. The spot's name is <prefix><kind><n>.
struct SpotKind {
    const char* name;
    float       stress, boredom, drunk, weight;
};
const SpotKind kSpotKinds[] = {
    { "bench", 0.8f, 0.5f, 0, 1.0f },   { "weights", 0.8f, 0.5f, 0, 0.8f }, { "steps", 0.7f, 0.5f, 0, 0.6f },
    { "chinup", 0.7f, 0.5f, 0, 0.6f },  { "pinball", 0.3f, 1.0f, 0, 0 },    { "hoop", 0.4f, 1.0f, 0, 0.5f },
    { "ball", 0.3f, 0.9f, 0, 0.3f },    { "seat", 0.4f, 0.1f, 0, 0 },       { "drinks", 0.1f, 0.6f, 0.4f, 0 },
    { "gum", 0, 0.7f, 0, 0 },           { "water", 0.2f, 0, 0.6f, 0 },      { "lounger", 1.0f, 0.2f, 0, 0 },
    { "tub", 0.9f, 0, 0.8f, 0 },        { "parasol", 0.8f, 0.1f, 0, 0 },    { "lionfountain", 0.5f, 0.3f, 1.0f, 0 },
    { "longfountain", 0.5f, 0.2f, 1.0f, 0 }, { "tree", 0.5f, 0.3f, 0, 0 }, { "read", 0.5f, 0.6f, 0, 0 },
    { "plinth", 0.1f, 0.9f, 0, 0 },     { "rocka", 0.2f, 1.0f, 0, 0 },      { "rockb", 0.2f, 1.0f, 0, 0 },
    { "rockc", 0.2f, 1.0f, 0, 0 },      { "cactus", 0, 0.8f, 0, 0 },
};
const SpotKind kUnknownSpot = { "", 0.5f, 0.5f, 0, 0 };

bool  g_on = true;
float g_stressStart = 0.45f;    // of the star's stress threshold
float g_boredomStart = 0.55f;   // of the boredom threshold
float g_drunkStart = 0.35f;     // drunkenness, 0..1
float g_weightStart = 85.0f;    // weight, 60..100
bool  g_wrapParty = true;
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
    uintptr_t       ex = 0;
    uintptr_t       owner = 0;   // its building
    const Kind*     kind = nullptr;
    const SpotKind* what = &kUnknownSpot;
    char            name[32] = {};
    bool            free = false;
};

const SpotKind* SpotKindOf(const char* name)   // "lspa_tub2" -> tub
{
    char k[32] = {};
    strncpy_s(k, name + 5, _TRUNCATE);
    for (size_t n = strlen(k); n > 0 && k[n - 1] >= '0' && k[n - 1] <= '9'; n--)
        k[n - 1] = 0;
    for (const SpotKind& s : kSpotKinds)
        if (strcmp(k, s.name) == 0)
            return &s;
    return &kUnknownSpot;
}

// How worn down a star is, each 0 below its trigger and growing above it.
struct Need {
    float stress = 0, boredom = 0, drunk = 0, weight = 0;
    bool Any() const { return stress > 0 || boredom > 0 || drunk > 0 || weight > 0; }
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
        out->what = SpotKindOf(out->name);
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

// How worn down a star of the studio's is: stress and boredom as fractions of their
// thresholds, drunkenness (0..1) and weight (60..100).
bool Mood(uintptr_t star, uintptr_t studio, float* stress, float* boredom, float* drunk, float* weight)
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
        *drunk = Rd<float>(star + kStarDrunk);
        *weight = Rd<float>(star + kStarWeight);
        if (!(*drunk >= 0 && *drunk <= 1.5f))
            *drunk = 0;   // not a number we know
        if (!(*weight >= 30 && *weight <= 150))
            *weight = 0;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// What a star needs, past each trigger (all 0 = nothing). 'Always' counts everything a little
// (a dropped star, a wrap party).
bool NeedOf(uintptr_t star, uintptr_t studio, bool always, Need* need, char* why, int whySize)
{
    float stress = 0, boredom = 0, drunk = 0, weight = 0;
    if (!Mood(star, studio, &stress, &boredom, &drunk, &weight))
        return false;
    need->stress = stress >= g_stressStart ? stress : 0;
    need->boredom = boredom >= g_boredomStart ? boredom : 0;
    need->drunk = drunk >= g_drunkStart ? 1.0f + drunk : 0;   // sobering up comes first
    need->weight = g_weightStart > 0 && weight >= g_weightStart ? (weight - 60.0f) / 40.0f : 0;
    if (always) {
        need->stress += 0.3f + stress;
        need->boredom += 0.3f + boredom;
    }
    sprintf_s(why, whySize, "stress %.0f%%, boredom %.0f%%, drunk %.0f%%, weight %.0f", stress * 100, boredom * 100,
              drunk * 100, weight);
    return true;
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
        s.owner = OwnerOf(ex);
        auto it = mine.find(s.owner);
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

// The best free spot for a need, optionally only in one building.
const Spot* Best(const std::vector<Spot>& spots, const Need& need, uintptr_t onlyOwner = 0)
{
    const Spot* best = nullptr;
    float bestScore = -1;
    for (const Spot& s : spots) {
        if (!s.free || Reserved(s.ex) || (onlyOwner && s.owner != onlyOwner))
            continue;
        const SpotKind& k = *s.what;
        float score = need.stress * k.stress + need.boredom * k.boredom + need.drunk * k.drunk + need.weight * k.weight +
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

void TakeSpot(std::vector<Spot>& spots, uintptr_t ex)
{
    for (Spot& t : spots)
        if (t.ex == ex)
            t.free = false;
}

// --- Wrap parties -------------------------------------------------------------------

constexpr int kStageShoot = 5;
constexpr DWORD kPartyMs = 120000;   // how long the cast has to get there (they finish what they're doing)

struct Party {
    std::wstring           title;
    uintptr_t              venue = 0;      // the building
    const Kind*            kind = nullptr;
    std::vector<uintptr_t> guests;         // not yet sent
    std::vector<std::string> went;
    DWORD                  until = 0;
};
std::map<uintptr_t, int> g_stageOf;   // the player's productions' stages last time
std::vector<Party> g_parties;
bool g_stagesSeen = false;

std::string Utf8(const std::wstring& w)
{
    if (w.empty())
        return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), &s[0], n, nullptr, nullptr);
    return s;
}

void StartParty(const game::Project& film, uintptr_t studio, const std::vector<Spot>& spots)
{
    uintptr_t cast[64];
    int n = production::Cast(film.address, cast, 64);
    Party party;
    party.title = film.title;
    for (int i = 0; i < n; i++) {
        Need need;
        char why[96];
        if (NeedOf(cast[i], studio, true, &need, why, sizeof(why)))   // the player's own stars only
            party.guests.push_back(cast[i]);
    }
    // The building with the most free spots, so the cast stays together.
    std::map<uintptr_t, int> free;
    for (const Spot& s : spots)
        if (s.free && !Reserved(s.ex))
            free[s.owner]++;
    int most = 0;
    for (const auto& f : free)
        if (f.second > most) {
            most = f.second;
            party.venue = f.first;
        }
    for (const Spot& s : spots)
        if (s.owner == party.venue)
            party.kind = s.kind;
    mod::Log("leisure: '%ls' has wrapped - %d of its cast are the player's; party at %s (%d free spots)",
             film.title.c_str(), static_cast<int>(party.guests.size()), party.kind ? party.kind->title : "nowhere", most);
    if (party.guests.empty() || !party.venue)
        return;
    party.until = GetTickCount() + kPartyMs;
    g_parties.push_back(party);
}

void EndParty(const Party& p)
{
    if (p.went.empty())
        return;
    std::string names;
    for (size_t i = 0; i < p.went.size(); i++)
        names += (i == 0 ? "" : i + 1 == p.went.size() ? " and " : ", ") + p.went[i];
    std::string title = Utf8(p.title);
    std::string where = p.kind ? p.kind->title : "the lot";
    news::Post("WRAP PARTY!", "The cast of \"" + title + "\" lets off steam at " + where,
               "Filming is over and " + names + (p.went.size() == 1 ? " heads" : " head") + " straight to " + where +
                   " to celebrate. Insiders say spirits are high and the champagne is flowing.");
}

void WrapParties(uintptr_t studio, std::vector<Spot>& spots)
{
    // Films whose shoot has just ended.
    std::map<uintptr_t, int> now;
    for (const game::Project& p : game::PlayerProductions()) {
        now[p.address] = p.stage;
        auto was = g_stageOf.find(p.address);
        if (g_wrapParty && g_stagesSeen && was != g_stageOf.end() && was->second == kStageShoot && p.stage > kStageShoot)
            StartParty(p, studio, spots);
    }
    // A film that left the list straight from its shoot: read it where it is.
    for (const auto& was : g_stageOf)
        if (g_wrapParty && was.second == kStageShoot && !now.count(was.first)) {
            game::Project p = game::ReadProjectAt(was.first);
            if (p.stage > kStageShoot && p.stage <= 7)
                StartParty(p, studio, spots);
        }
    g_stageOf = now;
    g_stagesSeen = true;

    DWORD tick = GetTickCount();
    for (size_t i = 0; i < g_parties.size();) {
        Party& p = g_parties[i];
        for (size_t g = 0; g < p.guests.size();) {
            uintptr_t star = p.guests[g];
            Need need;
            char why[96];
            const Spot* s = nullptr;
            if (NeedOf(star, studio, true, &need, why, sizeof(why)) && IsIdle(star)) {
                s = Best(spots, need, p.venue);
                if (!s)
                    s = Best(spots, need);   // the venue is full: anywhere
            }
            if (s && Send(star, *s, "wrap party")) {
                TakeSpot(spots, s->ex);
                p.went.push_back(rooms::NameOf(star));
                p.guests.erase(p.guests.begin() + g);
            } else {
                g++;
            }
        }
        if (p.guests.empty() || static_cast<int>(tick - p.until) > 0) {
            EndParty(p);
            g_parties.erase(g_parties.begin() + i);
        } else {
            i++;
        }
    }
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
    if (spots.empty()) {
        g_stageOf.clear();   // nothing to celebrate at; don't fire for films that wrapped meanwhile
        g_stagesSeen = false;
        return;
    }
    WrapParties(studio, spots);
    if (!g_on)
        return;
    std::vector<uintptr_t> stars(2000);
    int n = ListStars(stars.data(), static_cast<int>(stars.size()));
    DWORD now = GetTickCount();
    for (int i = 0; i < n; i++) {
        uintptr_t star = stars[i];
        Need need;
        char why[96];
        if (!NeedOf(star, studio, false, &need, why, sizeof(why)) || !need.Any())
            continue;
        auto sent = g_sentAt.find(star);
        if (sent != g_sentAt.end() && now - sent->second < g_starCooldownMs)
            continue;
        if (!IsIdle(star))
            continue;
        const Spot* s = Best(spots, need);
        if (!s)
            return;   // everything's taken
        if (Send(star, *s, why))
            TakeSpot(spots, s->ex);
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
    if (!(g_on || g_wrapParty) || !game::Ready())
        return;
    DWORD now = GetTickCount();
    if (now - g_lastCheck < g_checkMs)
        return;
    g_lastCheck = now;
    if (!SafeCheck()) {
        mod::Log("leisure: sending stars FAILED - turned off");
        g_on = g_wrapParty = false;
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
    Need need;
    char why[96];
    if (!NeedOf(person, game::ReadPlayerStudio().address, true, &need, why, sizeof(why)))
        need.stress = need.boredom = 0.5f;   // not one of the player's stars: anything will do
    const Spot* s = Best(spots, need);
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
    g_drunkStart = IniFloat(ini, L"DrunkStart", g_drunkStart);
    g_weightStart = IniFloat(ini, L"WeightStart", g_weightStart);
    g_wrapParty = GetPrivateProfileIntW(L"Leisure", L"WrapParty", 1, ini) != 0;
    // The constructor hook finds the spots, for drops too: always on.
    if (MH_CreateHook(reinterpret_cast<LPVOID>(kExplainerCtor), reinterpret_cast<LPVOID>(&HookCtor),
                      reinterpret_cast<LPVOID*>(&g_origCtor)) != MH_OK ||
        MH_EnableHook(reinterpret_cast<LPVOID>(kExplainerCtor)) != MH_OK) {
        mod::Log("leisure: FAILED to hook the explainer constructor - leisure buildings off");
        g_on = g_wrapParty = false;
        return;
    }
    overlay::AddFrameCallback(&Frame);
    mod::Log("leisure: stars sent to unwind %s (stress from %.0f%%, boredom from %.0f%%, drunk from %.0f%%, weight from "
             "%.0f); wrap parties %s", g_on ? "on" : "off", g_stressStart * 100, g_boredomStart * 100, g_drunkStart * 100,
             g_weightStart, g_wrapParty ? "on" : "off");
}

} // namespace leisure
