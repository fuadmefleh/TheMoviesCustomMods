// Fairer film scoring. The game's formulas are in docs/GAME_INTERNALS.md ("How films are
// scored"); this file changes three of them, each switchable from [Ratings] in MoviesMod.ini:
//
//  * Production effect on quality (CQuality::Compute @ 0x005DCB10). The game adds
//    shape(x) to the script's quality, where x measures cast, crew, sets and relationships.
//    shape() counts each 0.1 of a gain 0.8, 0.64, 0.512... so even perfect production adds
//    at most +0.4, while a loss is taken in full. We recompute it with a slower decay
//    (ProductionGainDecay) and soften the losses (ProductionLossScale).
//
//  * Box office (CProjectIncome creation @ 0x005CDB80 and its twin @ 0x005CDD90). The pool is
//    revenue_base + revenue_multiplier * pow(success, 2): one hit earns many times a decent
//    film. Both sites load the exponent from a shared constant; we point them at our own
//    (BoxOfficeExponent).
//
//  * Outdated technology and costume boredom - see the success hook below.

#include <windows.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "MinHook.h"
#include "game.h"
#include "mod.h"

namespace {

template <typename T> T& At(uintptr_t addr) { return *reinterpret_cast<T*>(addr); }

float Clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

// ---------------------------------------------------------------------------------
// Quality

constexpr uintptr_t kQualityCompute = 0x005DCB10;   // void __thiscall (CQuality*, float* out)
// Tunables the game loaded from releasing_movies, and constants of the formula.
constexpr uintptr_t kQualityAdder    = 0x00E54B50;  // quality_overall_max_adder (0.8)
constexpr uintptr_t kQualityShifter  = 0x00E54B54;  // quality_overall_max_shifter (0.1)
constexpr uintptr_t kQualityMidPoint = 0x00E54B58;  // quality_overall_shifter_mid_point (0.4)
constexpr uintptr_t kMinusHalf       = 0x00D171A8;  // -0.5
constexpr uintptr_t kSlice           = 0x00D16A78;  // 0.1

// CQuality fields the original fills in before combining them.
constexpr int kQPerformance = 0xA4, kQScript = 0xAC, kQCrew = 0xB0, kQRelations = 0xB4, kQSets = 0xB8,
              kQResult = 0x90;
// The film: [+0x8C], or when +0x74 is set (a film being made), [[+0x74]+0xB4].
constexpr int kQFilm = 0x8C, kQProduction = 0x74, kProductionFilm = 0xB4;

uintptr_t QualityFilm(uintptr_t self)
{
    uintptr_t production = At<uintptr_t>(self + kQProduction);
    return production ? At<uintptr_t>(production + kProductionFilm) : At<uintptr_t>(self + kQFilm);
}

float g_gainDecay = 0.9f;    // game: 0.8
float g_lossScale = 0.75f;   // game: 1
int   g_qualityLogs = 0;

using QualityCompute_t = void(__fastcall*)(uintptr_t self, void* edx, float* out);
QualityCompute_t g_origQuality = nullptr;

// Diminishing returns on a gain: each slice counts decay^k.
float Shape(float x, float decay, float lossScale)
{
    if (x <= 0)
        return x * lossScale;
    float slice = At<float>(kSlice), sum = 0, weight = decay;
    for (; x > 0; x -= slice, weight *= decay)
        sum += (x < slice ? x : slice) * weight;
    return sum;
}

void __fastcall HookQuality(uintptr_t self, void* edx, float* out)
{
    g_origQuality(self, edx, out);
    __try {
        // A cartoon has no actors on screen: its voice cast gives the performance.
        float voices;
        if (tv::VoicePerformance(QualityFilm(self), &voices))
            At<float>(self + kQPerformance) = voices;
        float script = At<float>(self + kQScript);
        float product = At<float>(self + kQRelations) * At<float>(self + kQSets) * At<float>(self + kQCrew) *
                        At<float>(self + kQPerformance);
        float x = ((product - 1) + 2 * (script - At<float>(kQualityMidPoint)) * At<float>(kMinusHalf) *
                                       At<float>(kQualityShifter)) * At<float>(kQualityAdder);
        float game = At<float>(self + kQResult);
        float q = Clamp01(script + Shape(x, g_gainDecay, g_lossScale));
        if (!(q == q))   // NaN: keep the game's
            return;
        At<float>(self + kQResult) = q;
        if (out)
            *out = q;
        if (g_qualityLogs < 12) {
            g_qualityLogs++;
            mod::Log("ratings: quality script %.2f production %.2f -> game %.3f (check %.3f), mod %.3f", script, x,
                     game, Clamp01(script + Shape(x, 0.8f, 1.0f)), q);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

// ---------------------------------------------------------------------------------
// Box office

// FLD qword ptr [0x00D1D258] (the 2.0 handed to pow) at both pool computations.
constexpr uintptr_t kPowSites[] = { 0x005CDC17, 0x005CDE35 };
constexpr uint32_t  kPowConstant = 0x00D1D258;
double g_boxOfficeExponent = 1.5;   // game: 2

void PatchBoxOffice()
{
    int patched = 0;
    for (uintptr_t site : kPowSites) {
        auto code = reinterpret_cast<uint8_t*>(site);
        if (code[0] != 0xDD || code[1] != 0x05 || At<uint32_t>(site + 2) != kPowConstant) {
            mod::Log("ratings: box office site %08X is not FLD [%08X] - left alone", static_cast<unsigned>(site),
                     kPowConstant);
            continue;
        }
        DWORD old;
        if (!VirtualProtect(code, 6, PAGE_EXECUTE_READWRITE, &old))
            continue;
        At<uint32_t>(site + 2) = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&g_boxOfficeExponent));
        VirtualProtect(code, 6, old, &old);
        FlushInstructionCache(GetCurrentProcess(), code, 6);
        patched++;
    }
    mod::Log("ratings: box office = base + multiplier x success^%.2f (%d of 2 sites)", g_boxOfficeExponent, patched);
}

// ---------------------------------------------------------------------------------
// Success

constexpr uintptr_t kSuccessCompute  = 0x005DEDC0;  // void __fastcall (CProjectSuccess*)
constexpr uintptr_t kSuccessAdder    = 0x00E54C18;  // success_overall_max_adder (0.8)
constexpr uintptr_t kSuccessShifter  = 0x00E54C1C;  // success_overall_max_shifter (0.12)
constexpr uintptr_t kSuccessMidPoint = 0x00E54C20;  // success_overall_shifter_mid_point (0.5)
constexpr uintptr_t kGameDecay       = 0x00D1DD0C;  // 0.8, the decay of the game's shape()

// CProjectSuccess: the result and the five factors the original multiplies together
// (public interest, genre, cast stars, boredom, technology).
constexpr int kSResult = 0x90, kSFactors = 0x94, kSFactorCount = 5, kSProject = 0x74;

using SuccessCompute_t = void(__fastcall*)(uintptr_t self);
SuccessCompute_t g_origSuccess = nullptr;
int g_successLogs = 0;

// The game's success for a film of quality q whose factors multiply to 'product'.
float SuccessFor(float q, float product)
{
    float x = ((product - 1) + 2 * (q - At<float>(kSuccessMidPoint)) * At<float>(kMinusHalf) *
                                   At<float>(kSuccessShifter)) * At<float>(kSuccessAdder);
    float s = q + Shape(x, At<float>(kGameDecay), 1.0f);
    return s < 0 ? 0 : s;
}

// The quality the original worked from: SuccessFor rises with q, so bisect.
float QualityBehind(float success, float product)
{
    float lo = 0, hi = 1;
    for (int i = 0; i < 30; i++) {
        float mid = (lo + hi) / 2;
        (SuccessFor(mid, product) < success ? lo : hi) = mid;
    }
    return (lo + hi) / 2;
}

// ---------------------------------------------------------------------------------
// Staleness: the game loads technology_min/max and costume_boredom_bound but never uses
// them (it tracks boredom for sets only, and only checks technology for the pioneering
// bonus). We supply both factors.

constexpr uintptr_t kTechnologyMin  = 0x00E54C0C;   // 0.7: a film shot with the oldest kit
constexpr uintptr_t kTechnologyMax  = 0x00E54C10;   // 1.0: the newest
constexpr uintptr_t kCostumeBound   = 0x00E54C00;   // 0.8: the floor for fully bored audiences
constexpr uintptr_t kBoredomHalfLife = 0x00E51878;  // years

// CProject -> CCast -> roles -> costume; CProject -> CEquipment -> Techs[5].
constexpr int kProjCast = 0x1F8, kProjEquipment = 0x258, kProjStart = 0x2C8;
constexpr int kCastRolesBegin = 0x64, kCastRolesEnd = 0x68, kCastRoleStride = 0x18, kRoleValue = 0x14;
constexpr int kRoleType = 0x88, kRoleCostume = 0xDC, kCostumeName = 0x78;
constexpr int kEquipTechs = 0x60, kEquipTechCount = 5, kEquipTechStride = 0x20;

// Every CTech is linked through CTech+0xF0 {next, prev, owner}, newest first.
constexpr uintptr_t kTechListHead = 0x010509A4;
constexpr int kTechRank = 0x100, kTechName = 0x104, kTechType = 0x148;
// Research packs: InList at 0x0104A738 (first node at +8, sentinel +0x14, next +4, item +8).
constexpr uintptr_t kPackFirst = 0x0104A740, kPackSentinel = 0x0104A74C;
constexpr int kPackName = 0x80, kPackCanResearch = 0xA8;

bool  g_outdatedTech = true;
bool  g_costumeBoredom = true;
float g_costumeBoost = 0.15f;   // the game's boredomboost for sets
bool  g_staleLogged = false;

// The game's narrow String: {char* text; int len; int cap; char inline[20]}.
std::string ReadString(uintptr_t at)
{
    const char* text = At<const char*>(at);
    int len = At<int>(at + 4);
    if (!text || len <= 0 || len > 260 || IsBadReadPtr(text, len))
        return {};
    return std::string(text, len);
}

struct Tech {
    std::string name, type;
    int         rank = 0;
    float       invented = 0;   // when research could start (0 = from the beginning)
};

std::vector<Tech> g_techs;   // technologies and research dates don't change in a game

const std::vector<Tech>& AllTechs()
{
    if (!g_techs.empty())
        return g_techs;
    std::map<std::string, float> invented;
    uintptr_t node = At<uintptr_t>(kPackFirst);
    for (int i = 0; node && node != kPackSentinel && i < 400; i++, node = At<uintptr_t>(node + 4))
        if (uintptr_t pack = At<uintptr_t>(node + 8))
            invented[ReadString(pack + kPackName)] = At<float>(pack + kPackCanResearch);

    std::vector<Tech> techs;
    node = At<uintptr_t>(kTechListHead);
    for (int i = 0; node && node != kTechListHead && i < 200; i++, node = At<uintptr_t>(node)) {
        uintptr_t tech = At<uintptr_t>(node + 8);
        if (!tech)
            continue;
        Tech t;
        t.name = ReadString(tech + kTechName);
        t.type = ReadString(tech + kTechType);
        t.rank = At<int>(tech + kTechRank);
        if (auto it = invented.find(t.name); it != invented.end())
            t.invented = it->second;
        techs.push_back(t);
    }
    if (!g_staleLogged) {
        mod::Log("ratings: %u research packs, %u technologies", static_cast<unsigned>(invented.size()),
                 static_cast<unsigned>(techs.size()));
        for (const Tech& t : techs)
            mod::Log("ratings:   tech %-28s %-14s rank %d invented day %.0f%s", t.name.c_str(), t.type.c_str(), t.rank,
                     t.invented, invented.count(t.name) ? "" : " (no research pack)");
    }
    if (!invented.empty()) {
        g_techs = techs;
        return g_techs;
    }
    static std::vector<Tech> early;   // research list not built yet: use it, look again next time
    early = techs;
    return early;
}

// 0..1: how close the film's kit is to the best of each kind that existed when it started.
float Modernity(uintptr_t project, float start)
{
    uintptr_t equipment = At<uintptr_t>(project + kProjEquipment);
    if (!equipment)
        return 1;
    const std::vector<Tech>& techs = AllTechs();
    float sum = 0;
    int counted = 0;
    for (int i = 0; i < kEquipTechCount; i++) {
        std::string name = ReadString(equipment + kEquipTechs + i * kEquipTechStride);
        if (name.empty())
            continue;
        const Tech* used = nullptr;
        for (const Tech& t : techs)
            if (_stricmp(t.name.c_str(), name.c_str()) == 0)
                used = &t;
        if (!used || used->type.empty())
            continue;
        int best = used->rank;
        for (const Tech& t : techs)
            if (t.type == used->type && t.invented <= start)
                best = (std::max)(best, t.rank);
        float modern = best > 0 ? static_cast<float>(used->rank) / best : 1.0f;
        if (!g_staleLogged)
            mod::Log("ratings:   uses %s (%s rank %d of %d)", name.c_str(), used->type.c_str(), used->rank, best);
        sum += modern;
        counted++;
    }
    return counted ? sum / counted : 1.0f;
}

// Costume names worn by a film's acting parts.
std::vector<std::string> Costumes(uintptr_t project)
{
    std::vector<std::string> out;
    uintptr_t cast = At<uintptr_t>(project + kProjCast);
    if (!cast)
        return out;
    uintptr_t begin = At<uintptr_t>(cast + kCastRolesBegin), end = At<uintptr_t>(cast + kCastRolesEnd);
    if (!begin || end < begin || end - begin > 64 * kCastRoleStride)
        return out;
    for (uintptr_t e = begin; e < end; e += kCastRoleStride) {
        uintptr_t role = At<uintptr_t>(e + kRoleValue);
        if (!role || At<int>(role + kRoleType) < 1 || At<int>(role + kRoleType) > 3)
            continue;
        if (uintptr_t costume = At<uintptr_t>(role + kRoleCostume)) {
            std::string name = ReadString(costume + kCostumeName);
            if (!name.empty() && name.find("_none") == std::string::npos)
                out.push_back(name);
        }
    }
    return out;
}

// 0..1: how tired audiences are of the film's costumes. Each released film boosts boredom
// with its costumes, decaying with the game's boredom half-life - the rule the game
// applies to sets, worked out from the back catalogue so nothing extra is saved.
float CostumeBoredom(uintptr_t project, float now)
{
    std::vector<std::string> mine = Costumes(project);
    if (mine.empty())
        return 0;
    float halfLife = At<float>(kBoredomHalfLife);
    if (!(halfLife > 0.1f && halfLife < 100))
        halfLife = 2;
    std::map<std::string, float> bored;
    // The studio's back catalogue can be empty while it has released films, so take the
    // project lists too (as tv.cpp does).
    std::vector<game::Project> films = game::PlayerBackCatalogue();
    for (const game::Project& p : game::AllProjects())
        films.push_back(p);
    for (const game::Project& p : game::PlayerProductions())
        films.push_back(p);
    std::vector<uintptr_t> counted;
    for (const game::Project& film : films) {
        if (film.address == project || film.releaseDate <= 0 || film.releaseDate > now ||
            std::find(counted.begin(), counted.end(), film.address) != counted.end())
            continue;
        counted.push_back(film.address);
        float weight = g_costumeBoost * std::exp(-0.693147f / halfLife * (now - film.releaseDate) / 365.0f);
        for (const std::string& c : Costumes(film.address))
            bored[c] = (std::min)(1.0f, bored[c] + weight);
    }
    float sum = 0;
    for (const std::string& c : mine)
        sum += bored[c];
    if (!g_staleLogged) {
        std::string list;
        for (const std::string& c : mine)
            list += " " + c + (bored[c] > 0 ? "*" : "");
        mod::Log("ratings:   costumes%s (* = seen before)", list.c_str());
    }
    return sum / mine.size();
}

// How much audiences mark the film down for dated technology and tired costumes (1 = not at all).
float Staleness(uintptr_t success)
{
    uintptr_t project = At<uintptr_t>(success + kSProject);
    if (!project)   // a rival's film: the game keeps no cast or kit for those
        return 1.0f;
    float now = game::DateDays();
    static std::map<uintptr_t, std::pair<float, float>> cache;   // project -> (day, factor)
    if (auto it = cache.find(project); it != cache.end() && std::fabs(it->second.first - now) < 1)
        return it->second.second;
    float factor = 1;
    float tech = 1, boredom = 0;
    if (g_outdatedTech) {
        tech = Modernity(project, At<float>(project + kProjStart));
        float lo = At<float>(kTechnologyMin), hi = At<float>(kTechnologyMax);
        if (lo > 0 && hi >= lo && hi <= 2)
            factor *= lo + (hi - lo) * Clamp01(tech);
    }
    if (g_costumeBoredom && now > 0) {
        boredom = CostumeBoredom(project, now);
        float bound = At<float>(kCostumeBound);
        if (bound > 0 && bound <= 1)
            factor *= (1 - boredom) * (1 - bound) + bound;
    }
    if (!g_staleLogged) {
        g_staleLogged = true;
        mod::Log("ratings: staleness: modern kit %.2f, costume boredom %.2f -> x%.3f", tech, boredom, factor);
    }
    if (cache.size() > 500)
        cache.clear();
    cache[project] = { now, factor };
    return factor;
}

void __fastcall HookSuccess(uintptr_t self)
{
    g_origSuccess(self);
    __try {
        float factor = Staleness(self);
        float game = At<float>(self + kSResult);
        if (factor >= 0.999f || game <= 0)
            return;
        float product = 1;
        for (int i = 0; i < kSFactorCount; i++)
            product *= At<float>(self + kSFactors + 4 * i);
        float q = QualityBehind(game, product);
        float s = SuccessFor(q, product * factor);
        if (!(s == s))
            return;
        At<float>(self + kSResult) = s;
        if (g_successLogs < 12) {
            g_successLogs++;
            mod::Log("ratings: success quality %.2f factors %.2f x staleness %.2f: game %.3f (check %.3f) -> %.3f", q,
                     product, factor, game, SuccessFor(q, product), s);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

// ---------------------------------------------------------------------------------
// Script Office: Ultimate (facility_script_5star, revived by MoviesMod's data files).
// FinishScript caps a script's quality by the best script office the player has unlocked:
// 0.8 with the 4-star office, 0.6, 0.4, else 0.2 - there is no 5-star step. When its
// argument is 0 it skips the cap, so with the Ultimate office unlocked we pass 0.

constexpr uintptr_t kFinishScript       = 0x004C3AE0;   // void __thiscall(CScript*, char applyCap)
constexpr uintptr_t kFindBlueprint      = 0x009623A0;   // TMBlueprint* __cdecl(const String* name, int)
constexpr uintptr_t kBlueprintAvailable = 0x00960F30;   // bool __thiscall(TMBlueprint*)

struct NarrowString {
    const char* text;
    int         len;
    int         cap;
    char        inline_[20];
};

using FinishScript_t = void(__fastcall*)(void* self, void* edx, char applyCap);
FinishScript_t g_origFinishScript = nullptr;
bool g_loggedUltimate = false;

bool UltimateOfficeUnlocked()
{
    static char name[] = "facility_script_5star";
    NarrowString key{ name, static_cast<int>(sizeof(name) - 1), 0x14, {} };
    __try {
        void* bp = reinterpret_cast<void*(__cdecl*)(NarrowString*, int)>(kFindBlueprint)(&key, 1);
        return bp && reinterpret_cast<bool(__thiscall*)(void*)>(kBlueprintAvailable)(bp);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void __fastcall HookFinishScript(void* self, void* edx, char applyCap)
{
    if (applyCap && UltimateOfficeUnlocked()) {
        if (!g_loggedUltimate) {
            g_loggedUltimate = true;
            mod::Log("ratings: Script Office: Ultimate unlocked - scripts can reach 5 stars");
        }
        applyCap = 0;
    }
    g_origFinishScript(self, edx, applyCap);
}

float IniFloat(const wchar_t* ini, const wchar_t* key, float def)
{
    wchar_t buf[64];
    GetPrivateProfileStringW(L"Ratings", key, L"", buf, 64, ini);
    return buf[0] ? static_cast<float>(_wtof(buf)) : def;
}

} // namespace

namespace ratings {

void Install()
{
    wchar_t ini[MAX_PATH];
    swprintf_s(ini, L"%s\\MoviesMod.ini", mod::GameDir());
    // Always on: it only matters once the Script Office: Ultimate is unlocked.
    auto finish = reinterpret_cast<LPVOID>(kFinishScript);
    if (MH_CreateHook(finish, reinterpret_cast<LPVOID>(&HookFinishScript), reinterpret_cast<LPVOID*>(&g_origFinishScript)) !=
            MH_OK ||
        MH_EnableHook(finish) != MH_OK)
        mod::Log("ratings: script cap hook FAILED");

    if (!GetPrivateProfileIntW(L"Ratings", L"Enabled", 1, ini)) {
        mod::Log("ratings: disabled in MoviesMod.ini");
        return;
    }

    g_gainDecay = std::clamp(IniFloat(ini, L"ProductionGainDecay", g_gainDecay), 0.0f, 0.99f);
    g_lossScale = std::clamp(IniFloat(ini, L"ProductionLossScale", g_lossScale), 0.0f, 2.0f);
    auto target = reinterpret_cast<LPVOID>(kQualityCompute);
    if (MH_CreateHook(target, reinterpret_cast<LPVOID>(&HookQuality), reinterpret_cast<LPVOID*>(&g_origQuality)) ==
            MH_OK &&
        MH_EnableHook(target) == MH_OK)
        mod::Log("ratings: production gain decay %.2f, loss scale %.2f", g_gainDecay, g_lossScale);
    else
        mod::Log("ratings: quality hook FAILED");

    target = reinterpret_cast<LPVOID>(kSuccessCompute);
    if (MH_CreateHook(target, reinterpret_cast<LPVOID>(&HookSuccess), reinterpret_cast<LPVOID*>(&g_origSuccess)) !=
            MH_OK ||
        MH_EnableHook(target) != MH_OK)
        mod::Log("ratings: success hook FAILED");

    g_outdatedTech   = GetPrivateProfileIntW(L"Ratings", L"OutdatedTechnology", 1, ini) != 0;
    g_costumeBoredom = GetPrivateProfileIntW(L"Ratings", L"CostumeBoredom", 1, ini) != 0;
    g_costumeBoost   = std::clamp(IniFloat(ini, L"CostumeBoredomBoost", g_costumeBoost), 0.0f, 1.0f);
    mod::Log("ratings: outdated technology %s, costume boredom %s (boost %.2f per film)", g_outdatedTech ? "on" : "off",
             g_costumeBoredom ? "on" : "off", g_costumeBoost);

    g_boxOfficeExponent = std::clamp(IniFloat(ini, L"BoxOfficeExponent", 1.5f), 0.5f, 3.0f);
    if (g_boxOfficeExponent != 2.0)
        PatchBoxOffice();
}

} // namespace ratings
