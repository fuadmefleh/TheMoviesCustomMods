// Film production: continuity makeup and a second unit. Settings: [Production] in MoviesMod.ini.
//
// Continuity makeup. A long custom film takes years of game time to shoot and the cast
// visibly ages through it. Each person's look is driven by one render-only call,
// SetRenderAge (0x009D63A0: oldness = clamp((age - 30) * 0.025, 0, 1) on the render character
// at person+0x6FC, which blends the _old head layers, grey hair and aged skin). While a
// film shoots (stage 5) and the studio has a Makeover Department (facility_wardrobe), its
// actors and stunt doubles are drawn at the age they were on the first day of shooting;
// their real age and careers carry on. When the shoot wraps their look catches up.
//
// Second unit. Scenes are real-time animations played one after another, so there is no
// speed to raise. Instead, each scene the main unit finishes (CShot::Complete 0x004EACE0)
// earns the film credit when it has spare crew (its crew pool beyond CrewNeeded); each full
// credit has the second unit finish one more pending scene through the game's own path
// (0x004EB900, used by the debug "finish film"), from the end of the shooting order. A shoot
// can run up to ~1.8x faster, and every second-unit scene costs a fee.

#include <windows.h>
#include <cmath>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "MinHook.h"
#include "game.h"
#include "mod.h"
#include "overlay.h"

namespace production {
namespace {

template <typename T> T Rd(uintptr_t a) { return *reinterpret_cast<const T*>(a); }

constexpr int kStageShoot = 5;
// CProject
constexpr int kProjShoot = 0xA0, kProjShotsFirst = 0xAC, kProjShotsSentinel = 0xB8, kProjCrewBegin = 0x134,
              kProjCrewEnd = 0x138, kProjCrewNeeded = 0x300, kProjCast = 0x1F8;
// CCast / CRole / person
constexpr int kCastRolesBegin = 0x64, kCastRolesEnd = 0x68, kRoleValue = 0x14, kRoleActor = 0xAC, kRoleDouble = 0xC4;
constexpr int kPersonRenderChar = 0x6FC, kVtGetAge = 0x1E0, kVtApplyAge = 0x220;
// CShot / CShoot
constexpr int kShotProject = 0xB4, kShotForced = 0x144, kShotState = 0x1C0, kShootCurrent = 0x10C;
constexpr uintptr_t kSetRenderAge = 0x009D63A0;   // void __thiscall(RenderChar*, float years)
constexpr uintptr_t kShotComplete = 0x004EACE0;   // void __fastcall(CShot*)
constexpr uintptr_t kShotForceComplete = 0x004EB900;   // void __fastcall(CShot*)

bool  g_continuity = true, g_needMakeover = true, g_secondUnit = true;
int   g_extraCrew = 2;          // crew beyond the film's needs for a full second unit
float g_secondUnitFee = 1500;   // per scene, 1920s dollars

// ---------------------------------------------------------------------------------
// Continuity makeup

struct Pin {
    uintptr_t person;
    uintptr_t project;
    float     years;   // real age on the first day of shooting
};
std::vector<Pin> g_pins;
using SetRenderAge_t = void(__fastcall*)(uintptr_t rc, void* edx, float years);
SetRenderAge_t g_origSetRenderAge = nullptr;

float RealYears(uintptr_t person)
{
    float days = 0;
    __try {
        reinterpret_cast<float*(__thiscall*)(uintptr_t, float*)>(Rd<uintptr_t>(Rd<uintptr_t>(person) + kVtGetAge))(person, &days);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
    return days / 365.0f;
}

void __fastcall HookSetRenderAge(uintptr_t rc, void* edx, float years)
{
    for (const Pin& pin : g_pins) {
        if (Rd<uintptr_t>(pin.person + kPersonRenderChar) != rc)
            continue;
        float now = RealYears(pin.person);
        // Plain age: show the age on day one. Older-looking makeup from a costume stays.
        years = years > now + 0.001f ? (years > pin.years ? years : pin.years) : pin.years;
        break;
    }
    g_origSetRenderAge(rc, edx, years);
}

void Reapply(uintptr_t person)
{
    __try {
        uintptr_t rc = Rd<uintptr_t>(person + kPersonRenderChar);
        if (rc)
            reinterpret_cast<void(__thiscall*)(uintptr_t, uintptr_t)>(Rd<uintptr_t>(Rd<uintptr_t>(person) + kVtApplyAge))(person, rc);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

// The actors and stunt doubles cast in a film (each once).
int CastOf(uintptr_t project, uintptr_t* out, int max)
{
    int n = 0;
    __try {
        uintptr_t cast = Rd<uintptr_t>(project + kProjCast);
        if (!cast)
            return 0;
        uintptr_t begin = Rd<uintptr_t>(cast + kCastRolesBegin), end = Rd<uintptr_t>(cast + kCastRolesEnd);
        if (!begin || end < begin || end - begin > 64 * 0x18)
            return 0;
        for (uintptr_t e = begin; e < end && n < max; e += 0x18) {
            uintptr_t role = Rd<uintptr_t>(e + kRoleValue);
            if (!role)
                continue;
            const int offsets[2] = { kRoleActor, kRoleDouble };
            for (int k = 0; k < 2; k++) {
                uintptr_t p = Rd<uintptr_t>(role + offsets[k]);
                bool dup = false;
                for (int i = 0; i < n; i++)
                    dup |= out[i] == p;
                if (p && !dup && n < max)
                    out[n++] = p;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return n;
}

void UpdatePins()
{
    bool makeover = !g_needMakeover || game::CountFacilities("facility_wardrobe") > 0;
    std::vector<uintptr_t> shooting;
    if (makeover)
        for (const game::Project& p : game::PlayerProductions())
            if (p.stage == kStageShoot)
                shooting.push_back(p.address);

    // Wrapped (or no Makeover Department any more): let the look catch up.
    for (size_t i = 0; i < g_pins.size();) {
        bool still = false;
        for (uintptr_t p : shooting)
            still |= p == g_pins[i].project;
        if (still) {
            i++;
            continue;
        }
        uintptr_t person = g_pins[i].person;
        g_pins.erase(g_pins.begin() + i);
        Reapply(person);
    }
    for (uintptr_t project : shooting) {
        uintptr_t cast[64];
        int n = CastOf(project, cast, 64), added = 0;
        for (int i = 0; i < n; i++) {
            bool known = false;
            for (const Pin& pin : g_pins)
                known |= pin.person == cast[i];
            float years = known ? 0 : RealYears(cast[i]);
            if (known || !(years > 5 && years < 120))
                continue;
            g_pins.push_back({ cast[i], project, years });
            added++;
        }
        if (added) {
            game::Project p = game::ReadProjectAt(project);
            mod::Log("production: continuity makeup on '%ls' - %d cast member(s) keep their day-one look", p.title.c_str(),
                     added);
        }
    }
}

// ---------------------------------------------------------------------------------
// Second unit

using ShotFn = void(__fastcall*)(uintptr_t shot);
ShotFn g_origComplete = nullptr;
std::map<uintptr_t, float> g_credit;   // project -> second-unit credit
bool g_inSecondUnit = false;

int SpareCrew(uintptr_t project)
{
    uintptr_t b = Rd<uintptr_t>(project + kProjCrewBegin), e = Rd<uintptr_t>(project + kProjCrewEnd);
    int pool = b && e >= b ? static_cast<int>((e - b) / 0x18) : 0;
    return pool - Rd<int>(project + kProjCrewNeeded);
}

uintptr_t LastPendingShot(uintptr_t project, int* pending)
{
    uintptr_t shoot = Rd<uintptr_t>(project + kProjShoot);
    uintptr_t current = shoot ? Rd<uintptr_t>(shoot + kShootCurrent) : 0;
    uintptr_t sentinel = project + kProjShotsSentinel, last = 0;
    *pending = 0;
    uintptr_t node = Rd<uintptr_t>(project + kProjShotsFirst);
    for (int guard = 0; node && node != sentinel && guard < 2000; guard++, node = Rd<uintptr_t>(node + 4)) {
        uintptr_t shot = Rd<uintptr_t>(node + 8);
        if (shot && Rd<int>(shot + kShotState) == 0) {
            ++*pending;
            if (shot != current)
                last = shot;
        }
    }
    return last;
}

int SecondUnitScenes(uintptr_t project)
{
    int done = 0;
    __try {
        int spare = SpareCrew(project);
        if (spare <= 0)
            return 0;
        float bonus = 0.8f * (spare >= g_extraCrew ? 1.0f : static_cast<float>(spare) / g_extraCrew);
        float& credit = g_credit[project];
        credit += bonus;
        while (credit >= 1.0f) {
            int pending = 0;
            uintptr_t shot = LastPendingShot(project, &pending);
            if (pending < 2 || !shot)
                break;   // the main unit shoots the last scenes itself
            g_inSecondUnit = true;
            reinterpret_cast<ShotFn>(kShotForceComplete)(shot);
            g_inSecondUnit = false;
            credit -= 1.0f;
            done++;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_inSecondUnit = false;
        return -1;
    }
    return done;
}

bool ReadShot(uintptr_t shot, bool* natural, uintptr_t* project)
{
    __try {
        *natural = Rd<uint8_t>(shot + kShotForced) == 0;
        *project = Rd<uintptr_t>(shot + kShotProject);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void __fastcall HookComplete(uintptr_t shot)
{
    bool natural = false;
    uintptr_t project = 0;
    ReadShot(shot, &natural, &project);
    g_origComplete(shot);
    if (!g_secondUnit || g_inSecondUnit || !natural || !project)
        return;
    bool mine = false;
    for (const game::Project& p : game::PlayerProductions())
        if (p.address == project && p.stage == kStageShoot)
            mine = true;
    if (!mine)
        return;
    int done = SecondUnitScenes(project);
    if (done < 0) {
        mod::Log("production: second unit FAILED - turned off");
        g_secondUnit = false;
        return;
    }
    if (done > 0) {
        float year = game::DateDays() / 365.0f;
        double fee = g_secondUnitFee * (1.0 + (year > 1920 ? (year - 1920) / 20.0 : 0.0)) * done;
        game::AddMoney(-fee);
        static int logged = 0;
        if (logged++ < 20)
            mod::Log("production: second unit shot %d more scene(s) for %.0f", done, fee);
    }
}

// ---------------------------------------------------------------------------------
// Toon Studio spike: a film finished without being shot. The game's unused debug "finish film"
// (0x005B3B20) puts the project in project_phase_shoot, forces every scene complete and steps
// the phases to 6 (shot). A film's scenes exist from the moment its script is written
// (CProject::AddShot 0x005C0510), so no set or actor is needed for it.

constexpr uintptr_t kFinishFilm = 0x005B3B20;   // void __fastcall(CProject*)

void CountShots(uintptr_t project, int* total, int* done)
{
    *total = *done = 0;
    __try {
        uintptr_t sentinel = project + kProjShotsSentinel;
        uintptr_t node = Rd<uintptr_t>(project + kProjShotsFirst);
        for (int guard = 0; node && node != sentinel && guard < 2000; guard++, node = Rd<uintptr_t>(node + 4)) {
            uintptr_t shot = Rd<uintptr_t>(node + 8);
            ++*total;
            if (shot && Rd<int>(shot + kShotState) == 4)
                ++*done;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *total = *done = -1;
    }
}

bool CallFinishFilm(uintptr_t project)
{
    __try {
        reinterpret_cast<void(__fastcall*)(uintptr_t)>(kFinishFilm)(project);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// ---------------------------------------------------------------------------------

float g_lastDay = -1;

void Frame()
{
    if (!g_continuity || !game::Ready())
        return;
    float today = game::DateDays();
    if (today <= 0 || (g_lastDay >= 0 && today >= g_lastDay && today - g_lastDay < 1))
        return;
    if (g_lastDay < 0 || today < g_lastDay)
        g_pins.clear();   // a load: start over
    g_lastDay = today;
    UpdatePins();
}

} // namespace

bool FinishWithoutShoot(uintptr_t project)
{
    game::Project before = game::ReadProjectAt(project);
    if (before.stage < 3 || before.stage > 5) {
        mod::Log("toon spike: '%ls' is at stage %d - only films with a finished script (stages 3-5) can be finished",
                 before.title.c_str(), before.stage);
        return false;
    }
    int total = 0, done = 0;
    CountShots(project, &total, &done);
    uintptr_t cast[64];
    int castCount = CastOf(project, cast, 64);
    mod::Log("toon spike: finishing '%ls' without a shoot - stage %d, %d/%d scenes done, %d cast", before.title.c_str(),
             before.stage, done, total, castCount);
    if (total <= 0) {
        mod::Log("toon spike: no scenes on '%ls' - not finished", before.title.c_str());
        return false;
    }
    bool ok = CallFinishFilm(project);
    game::Project after = game::ReadProjectAt(project);
    CountShots(project, &total, &done);
    mod::Log("toon spike: %s - '%ls' now stage %d, %d/%d scenes done, quality %.2f (raw %.3f)", ok ? "ok" : "FAILED",
             after.title.c_str(), after.stage, done, total, after.quality, after.rawQuality);
    return ok;
}

void Install()
{
    wchar_t ini[MAX_PATH];
    swprintf_s(ini, L"%s\\MoviesMod.ini", mod::GameDir());
    g_continuity = GetPrivateProfileIntW(L"Production", L"ContinuityMakeup", 1, ini) != 0;
    g_needMakeover = GetPrivateProfileIntW(L"Production", L"NeedsMakeoverDepartment", 1, ini) != 0;
    g_secondUnit = GetPrivateProfileIntW(L"Production", L"SecondUnit", 1, ini) != 0;
    g_extraCrew = static_cast<int>(GetPrivateProfileIntW(L"Production", L"SecondUnitCrew", 2, ini));
    if (g_extraCrew < 1)
        g_extraCrew = 1;
    wchar_t buf[32];
    GetPrivateProfileStringW(L"Production", L"SecondUnitFee", L"", buf, 32, ini);
    if (buf[0])
        g_secondUnitFee = static_cast<float>(_wtof(buf));

    if (g_continuity) {
        if (MH_CreateHook(reinterpret_cast<LPVOID>(kSetRenderAge), reinterpret_cast<LPVOID>(&HookSetRenderAge),
                          reinterpret_cast<LPVOID*>(&g_origSetRenderAge)) == MH_OK &&
            MH_EnableHook(reinterpret_cast<LPVOID>(kSetRenderAge)) == MH_OK)
            overlay::AddFrameCallback(&Frame);
        else
            g_continuity = false;
    }
    if (g_secondUnit &&
        (MH_CreateHook(reinterpret_cast<LPVOID>(kShotComplete), reinterpret_cast<LPVOID>(&HookComplete),
                       reinterpret_cast<LPVOID*>(&g_origComplete)) != MH_OK ||
         MH_EnableHook(reinterpret_cast<LPVOID>(kShotComplete)) != MH_OK))
        g_secondUnit = false;
    mod::Log("production: continuity makeup %s%s, second unit %s (%d spare crew for a full unit)",
             g_continuity ? "on" : "off", g_needMakeover ? " (needs a Makeover Department)" : "",
             g_secondUnit ? "on" : "off", g_extraCrew);
}

} // namespace production
