// Phase 0 verification panel: shows the game state the TV & Streaming systems will
// build on, so it can be compared against what the game itself displays.

#include <windows.h>
#include <map>
#include <string>

#include "game.h"
#include "imgui.h"
#include "mod.h"
#include "overlay.h"
#include "persist.h"

namespace {

bool g_visible = false;

// Phase 0 save test: a value stored in the save's .tvs file.
int         g_saveTestCounter = 0;
std::string g_saveTestStamp;

std::string Utf8(const std::wstring& w)
{
    if (w.empty())
        return "(no title)";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

std::string Dollars(double v)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "$%.0f", v);
    std::string s = buf;
    int start = (s[1] == '-') ? 2 : 1;
    for (int i = static_cast<int>(s.size()) - 3; i > start; i -= 3)
        s.insert(i, ",");
    return s;
}

void ProjectTable(const char* id, const std::vector<game::Project>& projects)
{
    if (projects.empty()) {
        ImGui::TextDisabled("none");
        return;
    }
    constexpr ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
                                      ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit;
    if (!ImGui::BeginTable(id, 8, flags, ImVec2(0, 220)))
        return;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Title", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Stage");
    ImGui::TableSetupColumn("Quality");
    ImGui::TableSetupColumn("Raw q");
    ImGui::TableSetupColumn("Stars");
    ImGui::TableSetupColumn("Released");
    ImGui::TableSetupColumn("Box office so far");
    ImGui::TableSetupColumn("Pool left");
    ImGui::TableHeadersRow();
    for (const game::Project& p : projects) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn(); ImGui::TextUnformatted(Utf8(p.title).c_str());
        ImGui::TableNextColumn(); ImGui::Text("%d", p.stage);
        ImGui::TableNextColumn(); ImGui::Text("%.0f%%", p.quality * 100);
        ImGui::TableNextColumn(); ImGui::Text("%.3f", p.rawQuality);
        ImGui::TableNextColumn(); ImGui::Text("%.2f", p.starRating);
        ImGui::TableNextColumn(); ImGui::Text("%.2f", p.releaseDate / 365.0f);
        ImGui::TableNextColumn(); ImGui::TextUnformatted(p.hasIncome ? Dollars(p.incomeInitial - p.incomeRemaining).c_str() : "-");
        ImGui::TableNextColumn(); ImGui::TextUnformatted(p.hasIncome ? Dollars(p.incomeRemaining).c_str() : "-");
    }
    ImGui::EndTable();
}

// Writes what the inspector sees to MoviesMod.log, so values can be checked after a session.
void LogSnapshot()
{
    if (!game::Ready()) {
        mod::Log("snapshot: no game loaded");
        return;
    }
    game::Studio studio = game::ReadPlayerStudio();
    game::Money money = game::ReadMoney();
    float days = game::DateDays();
    mod::Log("snapshot: studio '%s' prestige %.2f stars %.2f released %d | date day %.2f (%.2f y) | money live %s ui %s",
             Utf8(studio.name).c_str(), studio.prestige, studio.starRating, studio.moviesReleased, days, days / 365.0f,
             money.ok ? Dollars(money.capital).c_str() : "?", money.ok ? Dollars(money.display).c_str() : "?");
    auto logList = [](const char* label, const std::vector<game::Project>& list) {
        mod::Log("snapshot: %s: %zu", label, list.size());
        int n = 0;
        for (const game::Project& p : list) {
            if (n++ == 12) {
                mod::Log("snapshot:   ...");
                break;
            }
            mod::Log("snapshot:   '%s' stage %d quality %.2f (raw %.3f) stars %.2f start %.2f release %.2f box office %s pool left %s",
                     Utf8(p.title).c_str(), p.stage, p.quality, p.rawQuality, p.starRating, p.startDate / 365.0f, p.releaseDate / 365.0f,
                     p.hasIncome ? Dollars(p.incomeInitial - p.incomeRemaining).c_str() : "-",
                     p.hasIncome ? Dollars(p.incomeRemaining).c_str() : "-");
        }
    };
    logList("back catalogue", game::PlayerBackCatalogue());
    logList("in production", game::PlayerProductions());
    mod::Log("snapshot: all projects in world: %zu", game::AllProjects().size());
    for (const game::StudioFilms& s : game::AllStudios()) {
        std::string offsets;
        for (int off : s.listOffsets)
            offsets += (offsets.empty() ? "+0x" : ", +0x") + [](int v) { char b[16]; snprintf(b, sizeof(b), "%X", v); return std::string(b); }(off);
        mod::Log("snapshot: studio '%s' (%s%s) project lists at %s",
                 Utf8(s.name).c_str(), s.className.c_str(), s.isPlayer ? ", player" : "", offsets.c_str());
        logList("  films", s.films);
    }
    std::map<std::string, int> types;
    auto facilities = game::Facilities();
    for (const game::Facility& f : facilities)
        types[f.type.empty() ? "(unknown " + game::ClassName(f.address) + ")" : f.type]++;
    mod::Log("snapshot: buildings: %zu", facilities.size());
    for (const auto& [type, count] : types)
        mod::Log("snapshot:   %s x%d", type.c_str(), count);
}

void DrawInspector()
{
    if (!g_visible)
        return;
    // A gap in frame time means the inspector was just shown.
    static double lastDraw = -10.0;
    double now = ImGui::GetTime();
    if (now - lastDraw > 0.5)
        LogSnapshot();
    lastDraw = now;

    ImGui::SetNextWindowSize(ImVec2(760, 640), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(40, 40), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("MoviesMod - Studio Inspector (F8)")) {
        ImGui::End();
        return;
    }
    if (!game::Ready()) {
        ImGui::TextWrapped("No game loaded yet. Start or load a game.");
        ImGui::End();
        return;
    }

    float days = game::DateDays();
    game::Money money = game::ReadMoney();
    game::Studio studio = game::ReadPlayerStudio();

    ImGui::SeparatorText("Studio");
    ImGui::Text("Name: %s", Utf8(studio.name).c_str());
    ImGui::Text("Prestige: %.2f   Star rating: %.2f   Movies released: %d", studio.prestige, studio.starRating, studio.moviesReleased);
    ImGui::Text("Date: day %.1f  (= %.2f years)", days, days / 365.0f);
    if (money.ok)
        ImGui::Text("Money: %s live   /   %s shown in game UI", Dollars(money.capital).c_str(), Dollars(money.display).c_str());
    else
        ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "Money: unavailable");

    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows) && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        mod::Log("inspector: click received at (%.0f, %.0f)", ImGui::GetMousePos().x, ImGui::GetMousePos().y);

    auto testTransaction = [](double amount) {
        game::Money before = game::ReadMoney();
        bool ok = game::AddMoney(amount);
        game::Money after = game::ReadMoney();
        mod::Log("test AddMoney(%+.0f): %s - live %s -> %s, ui %s -> %s", amount, ok ? "ok" : "FAILED",
                 Dollars(before.capital).c_str(), Dollars(after.capital).c_str(),
                 Dollars(before.display).c_str(), Dollars(after.display).c_str());
    };
    if (ImGui::Button("Test: add $100,000"))
        testTransaction(100000.0);
    ImGui::SameLine();
    if (ImGui::Button("Test: remove $100,000"))
        testTransaction(-100000.0);

    ImGui::SeparatorText("Save data test");
    const std::wstring& save = persist::CurrentSave();
    ImGui::Text("Save file: %s", save.empty() ? "(new game - not saved yet)" : Utf8(save.substr(save.find_last_of(L'\\') + 1)).c_str());
    ImGui::Text("Test counter: %d   %s", g_saveTestCounter, g_saveTestStamp.c_str());
    ImGui::SameLine();
    if (ImGui::Button("+1")) {
        g_saveTestCounter++;
        char stamp[64];
        snprintf(stamp, sizeof(stamp), "(last changed on day %.0f)", game::DateDays());
        g_saveTestStamp = stamp;
        mod::Log("save test: counter now %d", g_saveTestCounter);
    }
    ImGui::TextDisabled("Bump the counter, save the game, load another save, then reload this one.");

    auto catalogue = game::PlayerBackCatalogue();
    auto productions = game::PlayerProductions();
    ImGui::SeparatorText(("Back catalogue (" + std::to_string(catalogue.size()) + ")").c_str());
    ProjectTable("catalogue", catalogue);
    ImGui::SeparatorText(("In production (" + std::to_string(productions.size()) + ")").c_str());
    ProjectTable("production", productions);
    // Toon Studio spike: finish a film without shooting it, then release and play it back.
    for (const game::Project& p : productions) {
        if (p.stage < 3 || p.stage > 5)
            continue;
        if (ImGui::Button(("Finish without shooting: " + Utf8(p.title) + "##toon" + std::to_string(p.address)).c_str()))
            production::FinishWithoutShoot(p.address);
    }

    for (const game::StudioFilms& s : game::AllStudios()) {
        std::string header = Utf8(s.name) + (s.isPlayer ? " (you)" : "") + " - " + std::to_string(s.films.size()) +
                             " films##" + std::to_string(s.address);
        if (ImGui::CollapsingHeader(header.c_str()))
            ProjectTable(("studio" + std::to_string(s.address)).c_str(), s.films);
    }
    ImGui::End();
}

} // namespace

namespace mod {

void ShowInspector(bool show) { g_visible = show; }

void RegisterInspector()
{
    overlay::AddPanel("inspector", &DrawInspector);
    persist::Register(
        "phase0",
        [](persist::Section& out) {
            out["counter"] = std::to_string(g_saveTestCounter);
            out["stamp"] = g_saveTestStamp;
        },
        [](const persist::Section& in) {
            auto c = in.find("counter");
            g_saveTestCounter = c != in.end() ? atoi(c->second.c_str()) : 0;
            auto s = in.find("stamp");
            g_saveTestStamp = s != in.end() ? s->second : "";
            mod::Log("save test: counter restored to %d", g_saveTestCounter);
        },
        [] {
            g_saveTestCounter = 0;
            g_saveTestStamp.clear();
        });
}

} // namespace mod
