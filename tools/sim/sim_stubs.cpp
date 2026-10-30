// Stand-ins for the game, the overlay, save files and drop zones, so the TV code can run
// outside The Movies. See sim_main.cpp.

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <intrin.h>

#include "mod.h"
#include "nativeui.h"
#include "news.h"
#include "overlay.h"
#include "projectlist.h"
#include "rooms.h"
#include "sim.h"
#include "tv_internal.h"

namespace sim {

float  g_date = 0;
double g_money = 0;
float  g_prestige = 0.5f;
std::vector<game::Facility> g_facilities;
std::vector<game::Project>  g_films;
std::vector<Registered>     g_sections;
std::vector<void (*)()>     g_frameCallbacks;
std::vector<std::pair<std::string, void (*)()>> g_panels;
std::vector<void (*)()>     g_huds;
bool   g_overlayOpen = false;
int    g_logLines = 0;
int    g_logErrors = 0;
FILE*  g_logFile = nullptr;
bool   g_echo = false;
std::map<uintptr_t, double> g_moneyBy;

std::map<uintptr_t, std::string>   g_classes;
std::map<uintptr_t, game::Project> g_scripts;   // script objects put on the lot
uintptr_t g_nextObject = 0x10000000;

uintptr_t NewObject(const char* className)
{
    uintptr_t a = g_nextObject;
    g_nextObject += 0x1000;
    g_classes[a] = className;
    return a;
}

void AddFacility(const char* type) { g_facilities.push_back({ NewObject("CFacility"), type }); }

} // namespace sim

DWORD SimTickCount() { return static_cast<DWORD>(sim::g_date * 10000.0f); }   // ten seconds a day

// ---------------------------------------------------------------------------------
namespace game {

bool  Ready() { return true; }
float DateDays() { return sim::g_date; }

Money ReadMoney()
{
    Money m;
    m.ok = true;
    m.capital = m.display = sim::g_money;
    return m;
}

bool AddMoney(double dollars, int)
{
    sim::g_moneyBy[reinterpret_cast<uintptr_t>(_ReturnAddress())] += dollars;
    if (!std::isfinite(dollars)) {
        mod::Log("sim: FAILED AddMoney(%f) - not a number", dollars);
        return false;
    }
    sim::g_money += dollars;
    return true;
}

Studio ReadPlayerStudio()
{
    Studio s;
    s.ok = true;
    s.name = L"Sim Pictures";
    s.prestige = sim::g_prestige;
    s.starRating = 0.5f;
    s.moviesReleased = static_cast<int32_t>(sim::g_films.size());
    return s;
}

std::vector<Project> PlayerBackCatalogue() { return sim::g_films; }

std::vector<Project> PlayerProductions()
{
    std::vector<Project> out;
    for (const auto& s : sim::g_scripts)
        if (sim::g_classes.count(s.first))
            out.push_back(s.second);
    return out;
}

std::vector<Project> AllProjects() { return sim::g_films; }

Project ReadProjectAt(uintptr_t project)
{
    for (const Project& p : sim::g_films)
        if (p.address == project)
            return p;
    auto it = sim::g_scripts.find(project);
    return it == sim::g_scripts.end() ? Project{} : it->second;
}

std::vector<StudioFilms> AllStudios() { return {}; }

// A rival studio releases a film every couple of months.
std::vector<Project> RivalFilms()
{
    static const wchar_t* const kTitles[] = { L"Midnight Express", L"The Long Goodbye", L"Laugh Riot", L"Star Raiders",
                                              L"Heartstrings", L"City of Shadows", L"The Big Heist", L"Moonlight Serenade" };
    std::vector<Project> out;
    int now = static_cast<int>(sim::g_date / 60);
    for (int i = now - 30; i <= now; i++) {
        if (i < 0)
            continue;
        Project p;
        p.address = 0x20000000u + i;
        p.rival = true;
        p.title = std::wstring(kTitles[i % 8]) + L" " + std::to_wstring(i);
        p.studio = i % 2 ? L"Gorilla Pictures" : L"Mammoth Studios";
        p.releaseDate = p.startDate = i * 60.0f;
        p.stage = 7;
        p.quality = p.rawQuality = 0.3f + 0.07f * (i * 7 % 10);
        p.starRating = p.quality;
        p.hasIncome = true;
        p.incomeInitial = 1e6 * (1 + i % 5);
        p.incomeRemaining = p.incomeInitial * 0.3;
        out.push_back(p);
    }
    return out;
}
std::vector<Facility> Facilities() { return sim::g_facilities; }

int CountFacilities(const char* type)
{
    int n = 0;
    for (const Facility& f : sim::g_facilities)
        n += f.type == type;
    return n;
}

void LogBlueprints(const char*) {}

bool FacilityPlacement(uintptr_t, float pos[3], float* angle)
{
    pos[0] = pos[1] = pos[2] = 0;
    *angle = 0;
    return true;
}

uintptr_t SpawnScript(const std::wstring& title, float, float)
{
    uintptr_t a = sim::NewObject("CProject");
    Project p;
    p.address = a;
    p.title = title;
    p.stage = 2;
    p.startDate = sim::g_date;
    sim::g_scripts[a] = p;
    return a;
}

bool SetProjectTitle(uintptr_t project, const std::wstring& title)
{
    auto it = sim::g_scripts.find(project);
    if (it == sim::g_scripts.end())
        return false;
    it->second.title = title;
    return true;
}

std::string ClassName(uintptr_t object)
{
    auto it = sim::g_classes.find(object);
    return it == sim::g_classes.end() ? "" : it->second;
}

uintptr_t FindObjectOfClass(uintptr_t, const char*, int, int*) { return 0; }

} // namespace game

// ---------------------------------------------------------------------------------
namespace overlay {

void AddPanel(const char* name, DrawFn draw) { sim::g_panels.push_back({ name, draw }); }
void AddHud(DrawFn draw) { sim::g_huds.push_back(draw); }
void AddFrameCallback(void (*fn)()) { sim::g_frameCallbacks.push_back(fn); }
void SetToggleHandler(bool (*)()) {}
bool IsOpen() { return sim::g_overlayOpen; }
void Open() { sim::g_overlayOpen = true; }
void Close() { sim::g_overlayOpen = false; }
ImFont* TitleFont() { return nullptr; }
ImFont* HeadingFont() { return nullptr; }

void* Texture(const char* name, int* width, int* height)
{
    if (!name || !*name)
        mod::Log("sim: FAILED overlay::Texture called without a name");
    if (width)
        *width = 0;
    if (height)
        *height = 0;
    return nullptr;
}

void RequestScreenshot(const char*) {}

} // namespace overlay

// ---------------------------------------------------------------------------------
namespace persist {

void Register(const char* name, SaveFn save, LoadFn load, ResetFn reset)
{
    sim::g_sections.push_back({ name, save, load, reset });
}

const std::wstring& CurrentSave()
{
    static std::wstring none;
    return none;
}

} // namespace persist

// ---------------------------------------------------------------------------------
namespace rooms {

void SetHandler(DropHandler) {}
uintptr_t ScriptRoomProject(uintptr_t) { return 0; }
uintptr_t ScriptRoomOffice(uintptr_t) { return 0; }
uintptr_t OfficeProject(uintptr_t) { return 0; }
void SetTvScriptTest(ProjectTest) {}
void RefreshScriptLook(uintptr_t) {}
void SetMeshChooser(ProjectMesh) {}
void SetLabeler(ProjectLabel) {}
std::string ZoneName(uintptr_t) { return ""; }
bool SeatInScene(uintptr_t, uintptr_t) { return false; }

} // namespace rooms

// ---------------------------------------------------------------------------------
namespace mod {

void Log(const char* fmt, ...)
{
    char buf[2048];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    sim::g_logLines++;
    if (strstr(buf, "FAILED") || strstr(buf, "nan") || strstr(buf, "inf%") || strstr(buf, "-nan"))
        sim::g_logErrors++;
    int y = static_cast<int>(sim::g_date / 365.0f);
    int d = static_cast<int>(sim::g_date) - y * 365;
    if (sim::g_logFile)
        fprintf(sim::g_logFile, "[%d d%03d] %s\n", y, d, buf);
    if (sim::g_echo)
        printf("[%d d%03d] %s\n", y, d, buf);
}

const wchar_t* GameDir()
{
    static wchar_t dir[MAX_PATH] = L"";
    if (!dir[0]) {
        GetModuleFileNameW(nullptr, dir, MAX_PATH);
        if (wchar_t* slash = wcsrchr(dir, L'\\'))
            *slash = 0;
    }
    return dir;
}

void ShowInspector(bool) {}

} // namespace mod

// ---------------------------------------------------------------------------------
namespace news {

void Post(const std::string& headline, const std::string&, const std::string&)
{
    mod::Log("news: %s", headline.c_str());
}

} // namespace news

// ---------------------------------------------------------------------------------
// The native screen: every page is built (and checked for stray markup) when it opens.
namespace nativeui {

bool Ready() { return true; }
bool Enabled() { return true; }
void ShotOnce(const char*, unsigned) {}
void GameOverDialog(const std::wstring&, const std::wstring&) { mod::Log("sim: game over dialog"); }
void Confirm(const std::wstring&, const std::wstring&, std::function<void()> onYes, std::function<void()>)
{
    if (onYes)
        onYes();
}
void Open(const Screen& screen, int)
{
    for (const Button& b : screen.buttons)
        if (b.tab >= 0) {
            std::wstring page = screen.html(b.tab);
            int open = 0;
            for (wchar_t c : page)
                open += c == L'<' ? 1 : c == L'>' ? -1 : 0;
            if (open != 0)
                mod::Log("sim: FAILED native page %d has unbalanced markup", b.tab);
        }
}
void Close() {}
bool IsOpen() { return false; }
void Refresh() {}
std::vector<std::function<void()>> g_hudClicks;
void AddHudButton(const char*, const std::wstring&, std::function<void()> onClick) { g_hudClicks.push_back(onClick); }
std::wstring Escape(const std::string& utf8)
{
    std::wstring w(utf8.begin(), utf8.end());
    for (wchar_t& c : w)
        if (c == L'<' || c == L'>')
            c = L'(';
    return w;
}
void Install() {}

} // namespace nativeui

// projectlist.cpp patches the HUD's project list.
namespace projectlist {
void SetCategory(ProjectTest, ProjectPicture, ProjectInfo) {}
} // namespace projectlist

// tv_staff.cpp hooks the game's hiring; the simulator's studio has one accountant.
namespace tv {
int Accountants() { return 1; }
int Animators() { return 0; }
int StaffWorkingIn(int, const char*) { return 0; }
void StaffReset() {}
void InstallStaff() {}
bool StaffDrop(const std::string&, uintptr_t, uintptr_t) { return false; }
std::string ZoneOf(uintptr_t) { return ""; }
bool AnimationDrop(const std::string&, uintptr_t, uintptr_t, uintptr_t, const std::string&, const std::string&, float)
{
    return false;
}
bool CartoonLabel(uintptr_t, std::wstring*, std::wstring*, std::wstring*) { return false; }
bool IsCartoon(uintptr_t) { return false; }
bool CartoonInProgress() { return false; }
void InstallAnimation() {}
void InstallMascot() {}
void InstallShorts() {}
void InstallVault() {}
bool IsVaultObject(uintptr_t) { return false; }
const char* VaultMesh(uintptr_t) { return nullptr; }
const char* VaultPicture(uintptr_t) { return nullptr; }
bool VaultLabel(uintptr_t, std::wstring*, std::wstring*, std::wstring*) { return false; }
bool VaultDrop(const std::string&, uintptr_t) { return false; }
void VaultInfo() {}
} // namespace tv

// tv_ceremony.cpp stages the game's awards show; the simulator has none (the papers report).
namespace tv {
bool CeremonyEnabled() { return false; }
bool QueueCeremony(int, int, std::vector<CeremonyCategory>, bool, void (*)()) { return false; }
void AnnounceAwardsSoon() {}
void CeremonyFrame() {}
void InstallCeremony(bool) {}
void RefreshStudioRatings() {}
void AwardsTimelineFrame() {}
void InstallAwardsTimeline() {}
} // namespace tv
