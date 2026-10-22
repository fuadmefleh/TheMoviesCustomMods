// Studio manager: takes chores off the player (the game's most common complaint).
//
//  * Rest breaks. An idle star - not in a building, not rehearsing, shooting or changing -
//    slowly unwinds, through the game's own CWorkBar::AddStress / AddBoredom, so stars
//    don't need dragging to the bar between films.
//  * Handyman dispatch. Every few seconds the most run-down building gets an idle
//    handyman, sent the way a player drop sends one: the building's repair force explainer
//    (CRepairForceExplainer::Apply) gives him the repair job.
//
// Settings: [Manager] in MoviesMod.ini.

#include <windows.h>
#include <cstdint>
#include <cstring>
#include <vector>

#include "game.h"
#include "mod.h"
#include "overlay.h"

namespace manager {
namespace {

template <typename T> T Rd(uintptr_t a) { return *reinterpret_cast<const T*>(a); }

// Global InLists: first node at +8, sentinel at +0x14, node {prev, next, item}.
constexpr uintptr_t kAllStars      = 0x0104D054;   // InList<CStar>
constexpr uintptr_t kAllStaff      = 0x0104CFC0;   // InList<CStaff>
constexpr uintptr_t kAllExplainers = 0x01050254;   // InList<TMForceExplainer>

// People (CStaff; CStar derives from it).
constexpr int kPersonRoom = 0x198, kPersonActivities = 0x4F0, kStaffTask = 0x814, kStaffEmployer = 0x934,
              kStarMood = 0xC98, kMoodWorkBar = 0x128;
constexpr int kTaskHandyman = 5;
// CWorkBar
constexpr int kStress = 0x8C, kBoredom = 0x90, kStressThreshold = 0xA4, kBoredomThreshold = 0xA8;
constexpr uintptr_t kAddStress = 0x00475130, kAddBoredom = 0x00475040;   // void __thiscall(CWorkBar*, float delta)
// Activities and desires
constexpr int kActivityDesire = 0x23C, kActivityExplainer = 0x274, kDesireName = 0x64;
// Force explainers
constexpr uintptr_t kRepairExplainerVtbl = 0x00D65504;
constexpr int kExplainerAsset = 0x1D4, kExplainerManager = 0x1BC, kManagerUsers = 0x6C;
constexpr int kVApply = 0x20, kAssetBuilt = 0xF0, kAssetCondition = 0xB8;

bool  g_rest = true, g_repair = true;
float g_restPerDay = 0.06f;
float g_repairBelow = 0.6f;
float g_lastDay = -1;
DWORD g_lastRepair = 0;
int   g_dispatches = 0;

std::vector<uintptr_t> Items(uintptr_t list)
{
    std::vector<uintptr_t> out;
    uintptr_t sentinel = list + 0x14;
    uintptr_t node = Rd<uintptr_t>(list + 8);
    for (int i = 0; node && node != sentinel && i < 2000; i++, node = Rd<uintptr_t>(node + 4))
        if (uintptr_t item = Rd<uintptr_t>(node + 8))
            out.push_back(item);
    return out;
}

uintptr_t CurrentActivity(uintptr_t person)
{
    uintptr_t node = Rd<uintptr_t>(person + kPersonActivities + 8);
    return node && node != person + kPersonActivities + 0x14 ? Rd<uintptr_t>(node + 8) : 0;
}

const char* DesireName(uintptr_t activity)
{
    uintptr_t desire = activity ? Rd<uintptr_t>(activity + kActivityDesire) : 0;
    if (!desire)
        return "";
    const char* text = Rd<const char*>(desire + kDesireName);
    int len = Rd<int>(desire + kDesireName + 4);
    return text && len > 0 && len < 64 && !IsBadStringPtrA(text, 64) ? text : "";
}

bool IsIdle(uintptr_t person)
{
    if (Rd<uintptr_t>(person + kPersonRoom))
        return false;   // in a building: on set, in make-up, at the bar...
    static const char* const kBusy[] = { "rehearse", "readyposition", "changecostume", "stunttrain", "writescript",
                                         "task_janitor", "task_repairman", "task_builder", "fetchfilmgear", "edit",
                                         "brainstorm", "leave", "getdrunk", "dolunch", "visittrailer" };
    const char* name = DesireName(CurrentActivity(person));
    for (const char* b : kBusy)
        if (_stricmp(name, b) == 0)
            return false;
    return true;
}

// ---------------------------------------------------------------------------------

int RestStars(uintptr_t studio, float days)
{
    using Add_t = void(__thiscall*)(uintptr_t, float);
    int rested = 0;
    for (uintptr_t star : Items(kAllStars)) {
        if (Rd<uintptr_t>(star + kStaffEmployer) != studio || !IsIdle(star))
            continue;
        uintptr_t mood = Rd<uintptr_t>(star + kStarMood);
        uintptr_t wb = mood ? Rd<uintptr_t>(mood + kMoodWorkBar) : 0;
        if (!wb)
            continue;
        float amount = g_restPerDay * days;
        // Only the ones getting worn down: a fresh star needs no break.
        if (Rd<float>(wb + kStress) > 0.4f * Rd<float>(wb + kStressThreshold)) {
            reinterpret_cast<Add_t>(kAddStress)(wb, -amount);
            rested++;
        }
        if (Rd<float>(wb + kBoredom) > 0.4f * Rd<float>(wb + kBoredomThreshold))
            reinterpret_cast<Add_t>(kAddBoredom)(wb, -amount);
    }
    return rested;
}

float Condition(uintptr_t asset)
{
    uintptr_t vtbl = Rd<uintptr_t>(asset);
    float out = 1;
    float* r = reinterpret_cast<float*(__thiscall*)(uintptr_t, float*)>(Rd<uintptr_t>(vtbl + kAssetCondition))(asset, &out);
    return r ? *r : out;
}

bool Built(uintptr_t asset)
{
    uintptr_t vtbl = Rd<uintptr_t>(asset);
    int r = reinterpret_cast<int(__thiscall*)(uintptr_t)>(Rd<uintptr_t>(vtbl + kAssetBuilt))(asset);
    return (r & 0xFF) == 1;
}

void DispatchHandyman(uintptr_t studio)
{
    // The player's buildings.
    std::vector<uintptr_t> mine;
    for (const game::Facility& f : game::Facilities())
        mine.push_back(f.address);

    uintptr_t worst = 0;
    float worstCondition = g_repairBelow;
    for (uintptr_t ex : Items(kAllExplainers)) {
        if (Rd<uintptr_t>(ex) != kRepairExplainerVtbl)
            continue;
        uintptr_t asset = Rd<uintptr_t>(ex + kExplainerAsset);
        uintptr_t manager = Rd<uintptr_t>(ex + kExplainerManager);
        if (!asset || !manager || Rd<int>(manager + kManagerUsers) > 0)
            continue;   // someone is already on it
        bool ours = false;
        for (uintptr_t f : mine)
            ours |= f == asset;
        if (!ours || !Built(asset))
            continue;
        float c = Condition(asset);
        if (c < worstCondition) {
            worstCondition = c;
            worst = ex;
        }
    }
    if (!worst)
        return;

    for (uintptr_t staff : Items(kAllStaff)) {
        if (Rd<uintptr_t>(staff + kStaffEmployer) != studio || Rd<int>(staff + kStaffTask) != kTaskHandyman)
            continue;
        if (Rd<uintptr_t>(staff + kPersonRoom))
            continue;
        uintptr_t activity = CurrentActivity(staff);
        uintptr_t explainer = activity ? Rd<uintptr_t>(activity + kActivityExplainer) : 0;
        if (explainer && Rd<uintptr_t>(explainer) == kRepairExplainerVtbl)
            continue;   // already repairing something
        reinterpret_cast<void(__thiscall*)(uintptr_t, uintptr_t)>(Rd<uintptr_t>(Rd<uintptr_t>(worst) + kVApply))(worst, staff);
        if (g_dispatches++ < 10)
            mod::Log("manager: handyman %08X sent to %s (condition %.0f%%)", static_cast<unsigned>(staff),
                     game::ClassName(Rd<uintptr_t>(worst + kExplainerAsset)).c_str(), worstCondition * 100);
        return;
    }
}

// SEH guards stay out of functions with C++ objects.
int SafeRest(uintptr_t studio, float days)
{
    __try {
        return RestStars(studio, days);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

bool SafeDispatch(uintptr_t studio)
{
    __try {
        DispatchHandyman(studio);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void Frame()
{
    if (!game::Ready())
        return;
    uintptr_t studio = game::ReadPlayerStudio().address;
    float today = game::DateDays();
    if (!studio || today <= 0)
        return;

    if (g_rest) {
        if (g_lastDay < 0 || today < g_lastDay || today - g_lastDay > 30)
            g_lastDay = today;   // a load or a new game
        float days = today - g_lastDay;
        if (days >= 0.25f) {
            g_lastDay = today;
            static int logged = 0;
            int rested = SafeRest(studio, days);
            if (rested < 0) {
                mod::Log("manager: rest breaks FAILED - turned off");
                g_rest = false;
            } else if (rested > 0 && logged++ < 5) {
                mod::Log("manager: %d idle star(s) taking a breather", rested);
            }
        }
    }
    if (g_repair && GetTickCount() - g_lastRepair > 4000) {
        g_lastRepair = GetTickCount();
        if (!SafeDispatch(studio)) {
            mod::Log("manager: handyman dispatch FAILED - turned off");
            g_repair = false;
        }
    }
}

float IniFloat(const wchar_t* ini, const wchar_t* key, float def)
{
    wchar_t buf[64];
    GetPrivateProfileStringW(L"Manager", key, L"", buf, 64, ini);
    return buf[0] ? static_cast<float>(_wtof(buf)) : def;
}

} // namespace

void Install()
{
    wchar_t ini[MAX_PATH];
    swprintf_s(ini, L"%s\\MoviesMod.ini", mod::GameDir());
    g_rest        = GetPrivateProfileIntW(L"Manager", L"RestBreaks", 1, ini) != 0;
    g_repair      = GetPrivateProfileIntW(L"Manager", L"SendHandymen", 1, ini) != 0;
    g_restPerDay  = IniFloat(ini, L"RestPerDay", g_restPerDay);
    g_repairBelow = IniFloat(ini, L"RepairBelow", g_repairBelow);
    if (!g_rest && !g_repair)
        return;
    overlay::AddFrameCallback(&Frame);
    mod::Log("manager: rest breaks %s (%.2f a day), handymen %s (below %.0f%%)", g_rest ? "on" : "off", g_restPerDay,
             g_repair ? "on" : "off", g_repairBelow * 100);
}

} // namespace manager
