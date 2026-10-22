#pragma once
// The simulator's fake game: what the stubs in sim_stubs.cpp report to the TV code.

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "game.h"
#include "persist.h"

namespace sim {

struct Registered {
    std::string     name;
    persist::SaveFn save;
    persist::LoadFn load;
    persist::ResetFn reset;
};

extern float  g_date;      // days since year 0
extern double g_money;
extern float  g_prestige;
extern std::vector<game::Facility> g_facilities;
extern std::vector<game::Project>  g_films;          // released films
extern std::vector<Registered>     g_sections;
extern std::vector<void (*)()>     g_frameCallbacks;
extern std::vector<std::pair<std::string, void (*)()>> g_panels;
extern std::vector<void (*)()>     g_huds;
extern bool   g_overlayOpen;
extern int    g_logLines;
extern int    g_logErrors;     // lines containing FAILED / error / nan
extern FILE*  g_logFile;
extern bool   g_echo;          // print the mod's log to stdout too
extern std::map<uintptr_t, double> g_moneyBy;   // AddMoney totals by caller

void AddFacility(const char* type);
uintptr_t NewObject(const char* className);   // fake game object with an RTTI name

} // namespace sim
