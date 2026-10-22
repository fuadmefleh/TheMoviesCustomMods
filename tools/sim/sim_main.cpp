// MoviesMod TV simulator: runs the Television & Streaming code outside the game, with a
// bot playing the studio, to find balance problems, crashes and UI mistakes without a
// test session.
//
//   tvsim [--seed N] [--level 0|1|2] [--from 1950] [--to 2030] [--ui] [--echo] [--log file]
//
// Each simulated day: buildings appear on a timetable, the studio releases films now and
// then, the TV code's frame callback runs once, and the bot acts like a sensible player.
// With --ui every tab of the TV screen is drawn weekly (debug build: ImGui asserts stop the
// run). Every five years each save section is saved, reset, loaded and saved again, and
// the two saves compared. A yearly table goes to stdout.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>

#include <windows.h>
#include <dbghelp.h>

#include <functional>
#include <vector>
namespace nativeui { extern std::vector<std::function<void()>> g_hudClicks; }
#include "../../src/tv.h"
#include "../../src/tv_internal.h"
#include "sim.h"

namespace tv {
bool CanLaunchStreaming(std::string* why);
void LaunchStreaming(const std::string& name);
}

void SimAssert(const char* expr, const char* file, int line)
{
    fprintf(stderr, "\nIMGUI ASSERT at %.0f days: %s\n  %s:%d\n", sim::g_date, expr, file, line);
    if (sim::g_logFile)
        fflush(sim::g_logFile);
    exit(3);
}

namespace {

using namespace tv;

std::mt19937 g_rng;
float Rand(float lo, float hi) { return std::uniform_real_distribution<float>(lo, hi)(g_rng); }

struct Timetable {
    int         year;
    const char* type;
};
const Timetable kBuildings[] = {
    { 1950, "facility_tv_production_office" }, { 1950, "facility_tv_soundstage" }, { 1950, "facility_tv_script_office" },
    { 1956, "facility_tv_network_hq" },        { 1960, "facility_tv_control_room" },
    { 1966, "facility_tv_archive" },           { 1972, "facility_tv_production_office" },
    { 1985, "facility_tv_soundstage" },        { 2008, "facility_tv_datacenter" },
};

int  g_anomalies = 0;
bool g_ui = false;
bool g_dump = false;

void Anomaly(const char* what)
{
    g_anomalies++;
    if (g_anomalies <= 40)
        printf("  ! %.0f: %s\n", sim::g_date / 365.0f, what);
}

void ReleaseFilm()
{
    game::Project p;
    p.address = sim::NewObject("CProject");
    p.title = L"Sim Film " + std::to_wstring(sim::g_films.size() + 1);
    p.quality = Rand(0.25f, 0.9f);
    p.rawQuality = p.quality;
    p.starRating = Rand(0.2f, 0.9f);
    p.startDate = sim::g_date - 120;
    p.releaseDate = sim::g_date;
    p.stage = 7;
    p.hasIncome = true;
    sim::g_films.push_back(p);
}

int ActiveShows()
{
    int n = 0;
    for (const Series& s : g_series)
        n += InProduction(s) || s.status == Pitching || s.status == Rework || s.status == RenewalOffered;
    return n;
}

// What a reasonable player does, once a week.
void Bot()
{
    float year = Year();
    AutoPilots(true);   // the bot shoots its best scripts with a stand-in lead
    if (ActiveShows() < Capacity() + 1) {
        std::vector<game::Project> pilots = AvailablePilots();
        std::sort(pilots.begin(), pilots.end(), [](const game::Project& a, const game::Project& b) { return a.quality > b.quality; });
        if (!pilots.empty()) {
            const game::Project& p = pilots.front();
            Series s;
            s.id = g_nextId++;
            s.title = Utf8(p.title) + " TV";
            int fmt = ShelfScriptFormat(p);
            if (fmt < 0) {
                std::vector<int> ok;
                for (int f = 0; f < FormatCount; f++)
                    if (kFormats[f].fromYear <= year)
                        ok.push_back(f);
                fmt = ok[g_rng() % ok.size()];
            }
            s.format = fmt;
            ApplyPilot(s, p);
            int net = BestNetwork(s);
            if (net >= 0) {
                StartPitch(s, net);
                g_series.push_back(s);
            }
        }
    }
    for (Series& s : g_series) {
        if (s.scandal)
            ResolveScandal(s, Rand(0, 1) < 0.5f);
        if (s.status == RenewalOffered && g_level != Casual) {
            if (s.demand > 0)
                ResolveDemand(s, true);
            if (s.season >= 9 && Rand(0, 1) < 0.3f) {
                s.status = Ended;
                ReleaseSlot(s);
            } else {
                OrderSeason(s, s.order, Today() + kSummerHiatus);
                s.freshness = std::min(1.0f, s.freshness + 0.05f);
            }
        }
        if (InLibrary(s) && OwnsReruns(s) && !ActiveDeal(s.id))
            SellLocal(s, 3);
    }
    std::string why;
    if (sim::g_money > 2e6 && CanFoundNetwork(&why))
        FoundNetwork("SimNet");
    if (sim::g_money > 5e6 && CanUpgradeReach(&why))
        UpgradeReach();
    if (sim::g_money > 5e6 && CanLaunchCable(&why))
        LaunchCable("SimCable");
    if (sim::g_money > 2e7 && CanLaunchStreaming(&why))
        LaunchStreaming("SimFlix");
    if (g_networks[kPlayerNetwork].exists)
        AutoSchedule();
}

void CheckNumbers()
{
    auto bad = [](double v) { return !std::isfinite(v); };
    for (const Series& s : g_series) {
        if (bad(s.freshness) || bad(s.lastRating) || bad(s.lastShare) || bad(s.income) || bad(s.cost) || bad(s.pitchScore) ||
            bad(s.feeMul) || bad(s.wear))
            Anomaly(Format("series '%s' has a non-finite number", s.title.c_str()).c_str());
        if (s.freshness < -0.01f || s.freshness > 1.01f)
            Anomaly(Format("series '%s' freshness %.2f", s.title.c_str(), s.freshness).c_str());
        if (s.status == OnAir && s.aired > s.order + 1)
            Anomaly(Format("series '%s' aired %d of %d", s.title.c_str(), s.aired, s.order).c_str());
    }
    for (const AiShow& a : g_aiShows)
        if (bad(a.quality) || bad(a.freshness) || bad(a.shareSum))
            Anomaly(Format("rival show '%s' has a non-finite number", a.title.c_str()).c_str());
    if (bad(sim::g_money))
        Anomaly("money is not finite");
}

void RoundTrip()
{
    for (const sim::Registered& r : sim::g_sections) {
        persist::Section a, b;
        r.save(a);
        r.reset();
        r.load(a);
        r.save(b);
        if (a == b)
            continue;
        for (const auto& kv : a) {
            auto it = b.find(kv.first);
            if (it == b.end() || it->second != kv.second) {
                Anomaly(Format("save '%s' key '%s': '%s' -> '%s'", r.name.c_str(), kv.first.c_str(),
                               kv.second.substr(0, 60).c_str(), it == b.end() ? "(missing)" : it->second.substr(0, 60).c_str())
                            .c_str());
                break;
            }
        }
        for (const auto& kv : b)
            if (!a.count(kv.first)) {
                Anomaly(Format("save '%s' key '%s' appeared after a load", r.name.c_str(), kv.first.c_str()).c_str());
                break;
            }
    }
}

void DrawUi()
{
    ImGuiIO& io = ImGui::GetIO();
    io.DeltaTime = 1.0f / 30.0f;
    sim::g_overlayOpen = true;
    for (int t = 0; t < TabTotal(); t++) {
        SelectTab(t);
        ImGui::NewFrame();
        for (auto hud : sim::g_huds)
            hud();
        for (auto& p : sim::g_panels)
            p.second();
        ImGui::EndFrame();
        ImGui::Render();
    }
    sim::g_overlayOpen = false;
    // The native screen: its HUD button builds every page (the stub checks the markup).
    for (auto& click : nativeui::g_hudClicks)
        click();
}

// The longest runners and what they're up against, for balancing.
void DumpVeterans()
{
    for (const Series& s : g_series) {
        if (s.season < 8 || !(InProduction(s) || s.status == RenewalOffered))
            continue;
        printf("    '%s' %s S%d fresh %.2f lastQ %.2f share %.0f%% net %s slot %d:", s.title.c_str(), kFormats[s.format].name,
               s.season, s.freshness, s.lastQuality, s.lastShare * 100, NetworkShort(s.network), s.slot);
        if (s.slot >= 0)
            for (int n = 0; n < kNetworkSlots; n++) {
                if (!g_networks[n].exists || n == s.network)
                    continue;
                const SlotEntry& e = g_networks[n].slots[s.slot];
                const AiShow* a = e.aiShowId ? FindAiShow(e.aiShowId) : nullptr;
                const Series* o = e.seriesId ? FindSeries(e.seriesId) : nullptr;
                if (a)
                    printf("  [%s q%.2f f%.2f]", NetworkShort(n), a->quality, a->freshness);
                else if (o)
                    printf("  [%s yours f%.2f]", NetworkShort(n), o->freshness);
                else
                    printf("  [%s filler]", NetworkShort(n));
            }
        printf("\n");
    }
}

// AddMoney totals by calling function since the last call, named from the debug symbols.
void MoneyByCaller()
{
    static bool init = false;
    HANDLE proc = GetCurrentProcess();
    if (!init) {
        SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
        SymInitialize(proc, nullptr, TRUE);
        init = true;
    }
    std::map<std::string, double> byFn;
    for (const auto& kv : sim::g_moneyBy) {
        char buf[sizeof(SYMBOL_INFO) + 256] = {};
        auto* sym = reinterpret_cast<SYMBOL_INFO*>(buf);
        sym->SizeOfStruct = sizeof(SYMBOL_INFO);
        sym->MaxNameLen = 255;
        DWORD64 disp = 0;
        std::string name = SymFromAddr(proc, kv.first, &disp, sym) ? sym->Name : Format("%08X", static_cast<unsigned>(kv.first));
        byFn[name] += kv.second;
    }
    sim::g_moneyBy.clear();
    std::vector<std::pair<double, std::string>> rows;
    for (const auto& kv : byFn)
        rows.push_back({ kv.second, kv.first });
    std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) { return std::fabs(a.first) > std::fabs(b.first); });
    for (size_t i = 0; i < rows.size() && i < 8; i++)
        printf("      %+16.0f  %s\n", rows[i].first, rows[i].second.c_str());
}

// Where the money came from since the last call (decade totals).
void Breakdown()
{
    static double lastMoney = 1e6, lastRival = 0, lastOwn = 0, lastSynd = 0;
    double rival = 0, own = 0, synd = 0;
    for (const Series& s : g_series) {
        (OwnOutlet(s.network) ? own : rival) += s.income - s.cost;
        synd += s.syndIncome;
    }
    double total = sim::g_money - lastMoney;
    printf("    money: %+.0f = rival-network shows %+.0f, own-outlet shows %+.0f, reruns %+.0f, other %+.0f\n", total,
           rival - lastRival, own - lastOwn, synd - lastSynd,
           total - (rival - lastRival) - (own - lastOwn) - (synd - lastSynd));
    const NetworkState& net = g_networks[kPlayerNetwork];
    if (net.exists)
        printf("    own network week: income %.0f cost %.0f (reach %s), era %.2f, homes %.0fM\n", net.lastWeekIncome,
               net.lastWeekCost, kReachNames[net.reach], EraFactor(Year()), Households(Year()));
    lastMoney = sim::g_money;
    lastRival = rival;
    lastOwn = own;
    lastSynd = synd;
}

void YearRow(int year)
{
    int counts[StatusCount] = {};
    float best = 0;
    for (const Series& s : g_series) {
        counts[s.status]++;
        best = std::max(best, s.bestRating);
    }
    int rivals[kRivalCount] = {};
    for (const AiShow& a : g_aiShows)
        if (a.network >= 0 && a.network < kRivalCount)
            rivals[a.network]++;
    printf("%d %13.0f %13.0f %4d %4d %4d %4d %4d %4d %5.1f %3d/%d/%d %6.1f\n", year, sim::g_money,
           MediaEmpireValue(nullptr), counts[Pitching] + counts[Rework], counts[Ordered] + counts[OnAir] + counts[OnHold],
           counts[RenewalOffered], counts[Cancelled], counts[Ended], counts[PassedOn], best, rivals[0], rivals[1],
           rivals[2], StreamingSubscribers());
}

} // namespace

int main(int argc, char** argv)
{
    unsigned seed = 1;
    int level = Casual, from = 1950, to = 2030;
    const char* logPath = "tvsim.log";
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&]() { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--seed") seed = static_cast<unsigned>(atoi(next()));
        else if (a == "--level") level = atoi(next());
        else if (a == "--from") from = atoi(next());
        else if (a == "--to") to = atoi(next());
        else if (a == "--ui") g_ui = true;
        else if (a == "--echo") sim::g_echo = true;
        else if (a == "--dump") g_dump = true;
        else if (a == "--log") logPath = next();
    }
    g_rng.seed(seed);
    sim::g_logFile = fopen(logPath, "w");

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(1600, 1200);
    io.IniFilename = nullptr;
    unsigned char* pixels;
    int w, h;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
    io.Fonts->SetTexID(static_cast<ImTextureID>(1));

    tv::Install();
    for (const sim::Registered& r : sim::g_sections)
        r.reset();
    g_level = level;
    sim::g_money = 1e6;
    AssignWriterToDesk("room_tvw_sitcom", "Ann Writer");
    AssignWriterToDesk("room_tvw_crime", "Bob Writer");
    AssignWriterToDesk("room_tvw_drama", "Cat Writer");
    AssignWriterToDesk("room_tvw_drama", "Dan Writer");

    printf("seed %u, level %s, %d-%d%s\n", seed, kLevelNames[level], from, to, g_ui ? ", drawing the UI" : "");
    printf("year         money         empire pitch prod renew canc  end pass  best rivals   subs(M)\n");
    float nextFilm = from * 365.0f + 30;
    size_t built = 0;
    for (int day = from * 365; day < to * 365; day++) {
        sim::g_date = day + 0.5f;
        int year = day / 365;
        while (built < std::size(kBuildings) && kBuildings[built].year <= year)
            sim::AddFacility(kBuildings[built++].type);
        if (sim::g_date >= nextFilm) {
            ReleaseFilm();
            nextFilm = sim::g_date + Rand(90, 220);
        }
        for (auto cb : sim::g_frameCallbacks)
            cb();
        if (day % 7 == 0) {
            Bot();
            CheckNumbers();
            if (g_ui && day % 28 == 0)
                DrawUi();
            g_toasts.clear();
        }
        if (day % 365 == 364) {
            YearRow(year);
            if (g_dump && year % 10 == 9) {
                DumpVeterans();
                Breakdown();
                MoneyByCaller();
            }
            if (year % 5 == 4)
                RoundTrip();
        }
    }
    printf("\n%d log lines, %d log errors, %d anomalies, %zu series, %zu papers\n", sim::g_logLines, sim::g_logErrors,
           g_anomalies, g_series.size(), g_papers.size());
    if (sim::g_logFile)
        fclose(sim::g_logFile);
    ImGui::DestroyContext();
    return g_anomalies || sim::g_logErrors ? 1 : 0;
}
