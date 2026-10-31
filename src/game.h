#pragma once
// Read/write access to The Movies' game state (MoviesSE.exe v1.2).
// Every read is fault-tolerant: a wrong offset yields "unavailable", never a crash.

#include <cstdint>
#include <string>
#include <vector>

namespace game {

// Money is stored as fixed point: dollars * 2^23.
constexpr double kMoneyScale = 8388608.0;

struct Money {
    bool   ok = false;
    double capital = 0;    // live balance (CFinance+0x90)
    double display = 0;    // balance shown in the UI (CFinance+0x98, refreshed by the game)
};

struct Project {
    uintptr_t    address = 0;
    std::wstring title;
    float        quality = -1;      // 0..1, via the game's own accessor
    float        rawQuality = -1;   // CProject+0x360
    float        starRating = -1;   // CProject+0x170
    float        releaseDate = 0;   // game date (days since year 0), 0 if unreleased
    float        startDate = 0;
    int          stage = -1;        // production phase (7 appears to mean released)
    double       incomeInitial = 0; // box-office pool at release
    double       incomeRemaining = 0;
    bool         hasIncome = false;
    bool         rival = false;     // a rival studio's film (CProjectAI)
    std::wstring studio;            // rival films: the studio's name
};

struct Studio {
    bool         ok = false;
    uintptr_t    address = 0;
    std::wstring name;
    float        prestige = -1;
    float        starRating = -1;
    int32_t      moviesReleased = -1;
};

struct Facility {
    uintptr_t   address = 0;
    std::string type;        // building file name, e.g. "facility_stage" or "tv_production_office"
};

bool  Ready();                       // a game is loaded and the player studio exists
float DateDays();                    // in-game date, in days (-1 if unavailable)
Money ReadMoney();
bool  AddMoney(double dollars, int category = 8);   // through the game's own ledger; 8 = uncategorised
Studio ReadPlayerStudio();
std::vector<Project> PlayerBackCatalogue();   // released movies owned by the player
std::vector<Project> PlayerProductions();     // movies in development / production
std::vector<Project> AllProjects();            // global project list (in practice only the player's)
Project ReadProjectAt(uintptr_t project);      // one CProject, e.g. carried by a script object

// Every studio in the world (player and rivals) with all its films: the union of every
// InList<CProject> found inside the studio object (back catalogue, productions...).
struct StudioFilms {
    uintptr_t            address = 0;
    std::wstring         name;
    std::string          className;    // CStudioPlayer or CStudioAI
    bool                 isPlayer = false;
    std::vector<int>     listOffsets;  // where its project lists live (for the log)
    std::vector<Project> films;
};
std::vector<StudioFilms> AllStudios();

// Every rival studio's released film still in the world (the game drops the oldest past
// a cap). Title, studio, release date, quality, star rating and box office.
std::vector<Project> RivalFilms();

// Every building on the lot. The type (its "facility/<name>" path) is located at runtime
// and the layout logged once; on v1.2 it is a String at facility+0x33C.
std::vector<Facility> Facilities();
int CountFacilities(const char* type);
void LogBlueprints(const char* filter);   // building blueprints whose name contains filter (all if null)

// A building's position on the lot and the angle it faces (radians).
bool FacilityPlacement(uintptr_t facility, float pos[3], float* angle);

// Puts a finished script on the lot at (x, y), the way a research pack hands out sample
// scripts: a new player CProject with its draggable script object (and a puff of sparkles),
// titled 'title'. Returns the CProject, or 0 if the game refused.
uintptr_t SpawnScript(const std::wstring& title, float x, float y);
// Renames a project (its title shows on the script object and in the studio's lists).
bool SetProjectTitle(uintptr_t project, const std::wstring& title);

// MSVC RTTI class name of a polymorphic object ("CFacility"), or "" if it isn't one.
std::string ClassName(uintptr_t object);
// The first pointer inside 'object' (within 'range' bytes) to an object of RTTI class 'cls'.
uintptr_t FindObjectOfClass(uintptr_t object, const char* cls, int range = 0x400, int* offset = nullptr);

// Every star in the game (the player's and the rivals'), and their star rating (0..1, CStar+0xCD0).
std::vector<uintptr_t> AllStars();
float StarRating(uintptr_t star);
// A bonus added to a star's rating (CStar+0xCCC, unused by the game). Setting it recomputes the rating.
float StarBonus(uintptr_t star);
bool  SetStarBonus(uintptr_t star, float bonus);

} // namespace game
