// The Movies: Television & Streaming - core: rules, series, daily simulation, save data.
//
// Time runs on the game's own clock (days since year 0), so pausing and fast-forward
// behave like the rest of the game. Random outcomes are seeded from what they are about
// (series, network, pilot film, episode), so reloading a save never rerolls them.

#include "tv.h"

#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdarg>

#include "mod.h"
#include "overlay.h"
#include "persist.h"
#include "projectlist.h"
#include "rooms.h"
#include "tv_internal.h"

namespace tv {

// ---------------------------------------------------------------------------------
// Rules and data tables

const char* const kOfficeType      = "facility_tv_production_office";
const char* const kStageType       = "facility_tv_soundstage";
bool g_filmPilots = false;   // [tv] FilmPilots: films and bare scripts may be pitched
bool g_awardsCeremony = true;   // [tv] AwardsCeremony: the Golden Antennas on the game's awards stage
const char* const kHqType          = "facility_tv_network_hq";
const char* const kControlRoomType = "facility_tv_control_room";
const char* const kArchiveType     = "facility_tv_archive";
const char* const kDatacenterType  = "facility_tv_datacenter";

const FormatInfo kFormats[FormatCount] = {
    { "Sitcom",            1947,  9000, 10500, 1.10f, 1.05f, true,  0, "Cheap to shoot on one set, loved by everyone." },
    { "Procedural",        1949, 11000, 12500, 1.00f, 1.10f, false, 2, "A case every week. Reliable and endlessly repeatable." },
    { "Serialized Drama",  1950, 13000, 14500, 0.80f, 1.15f, false, 2, "Big stories across a season. Hard to join late." },
    { "Soap Opera",        1949,  5000,  6000, 0.95f, 0.75f, true,  1, "Daily drama, tiny budgets, fiercely loyal fans." },
    { "Science Fiction",   1949, 12000, 12500, 0.75f, 1.00f, false, 1, "Rockets and robots. Costly, with a cult following." },
    { "Talk Show",         1951,  4000,  5000, 0.80f, 0.90f, true,  2, "A host, a couch and a studio audience." },
    { "Game Show",         1941,  3500,  4500, 1.15f, 0.90f, true,  0, "Prizes, buzzers and huge ratings for very little." },
    { "Reality",           1973,  3000,  4200, 0.60f, 1.20f, false, 1, "No writers, no stars, no script. Cheap and addictive." },
    { "Documentary",       1951,  6000,  6000, 0.60f, 0.70f, false, 2, "Prestige over ratings. Networks love the reputation." },
    { "Miniseries",        1974, 20000, 21000, 1.10f, 1.15f, false, 1, "A six-part event. No renewals, big audiences." },
};

const NetworkInfo kNetworks[kRivalCount] = {
    { "Liberty Broadcasting Network", "LBN",
      "The prestige network. Serious drama, news and documentaries; it pays the most and expects the best.",
      48.0f, 1.25f, 1.15f, 0.50f, 0.96f,
      { -2, 8, 10, 0, -2, 2, -6, -6, 10, 8 } },
    { "Starlight Television", "STV",
      "Family entertainment for the whole country. Sitcoms, game shows and big personalities.",
      38.0f, 1.00f, 1.05f, 0.45f, 0.94f,
      { 10, 2, -2, 4, -2, 8, 10, 4, -8, 0 } },
    { "Meridian Network", "MRN",
      "The scrappy upstart. Takes risks on odd ideas, pays less, reaches fewer homes.",
      28.0f, 0.80f, 0.80f, 0.38f, 0.78f,
      { 2, -4, 4, 8, 10, 2, 0, 10, 0, 2 } },
};

const char* const kBudgetNames[BudgetCount] = { "Shoestring", "Standard", "Premium" };
const float kBudgetCost[BudgetCount]    = { 0.70f, 1.00f, 1.50f };
const float kBudgetQuality[BudgetCount] = { -0.08f, 0.0f, 0.08f };
const float kBudgetPitch[BudgetCount]   = { -3.0f, 0.0f, 3.0f };

const char* const kDealNames[DealCount] = { "License fee", "Advertising share", "Network-financed" };

const char* const kLevelNames[LevelCount] = { "Casual", "Studio Boss", "Media Mogul" };
const char* const kLevelHelp[LevelCount] = {
    "Pick a film and a format; the studio handles networks, budgets, renewals and your network's schedule.",
    "You choose the network and the budget, decide on every renewal and run your own schedule.",
    "Full control: deal terms too, and budgets can change mid-season.",
};

const char* const kStatusNames[StatusCount] = {
    "Pilot with network", "Rework requested", "Passed on", "Ordered", "On air", "On hold",
    "Renewal offered", "Cancelled", "Ended",
};

const char* const kNightNames[kNights] = { "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday" };
const char* const kHourNames[kHours] = { "8 PM", "9 PM", "10 PM" };

const char* const kReachNames[ReachCount] = { "Local", "Regional", "National" };
const float kReachShare[ReachCount]      = { 0.12f, 0.40f, 0.90f };
const float kReachCost1950[ReachCount]   = { 150000, 600000, 2000000 };
const float kReachUpkeep1950[ReachCount] = { 2000, 6000, 15000 };

// Ink colours for the light, ranking-table style panels.
const ImVec4 kGold(0.55f, 0.36f, 0.0f, 1.0f);
const ImVec4 kGreen(0.10f, 0.45f, 0.12f, 1.0f);
const ImVec4 kRed(0.66f, 0.09f, 0.08f, 1.0f);
const ImVec4 kYellow(0.60f, 0.42f, 0.0f, 1.0f);
const ImVec4 kGrey(0.30f, 0.37f, 0.41f, 1.0f);

// ---------------------------------------------------------------------------------
// State

std::vector<Series> g_series;
int   g_level = StudioBoss;
int   g_nextId = 1;
float g_incomeScale = 1.0f;
float g_costScale = 1.0f;
int   g_offices = 0, g_stages = 0, g_hqs = 0, g_controlRooms = 0, g_archives = 0, g_datacenters = 0;
bool  g_detectionWorks = false;
bool  g_devTools = false;   // [MoviesMod] DevTools = 1 shows the memory inspector toggle
std::deque<Toast> g_toasts;
PendingDrop g_pendingDrop;
std::vector<uintptr_t> g_droppedProjects;

namespace {
float g_lastDay = -1;
DWORD g_lastCount = 0;
} // namespace

// ---------------------------------------------------------------------------------
// Helpers

namespace {

// Piecewise-linear lookup over (year, value) pairs.
float Lerp(const float (*table)[2], int n, float year)
{
    if (year <= table[0][0])
        return table[0][1];
    for (int i = 1; i < n; i++)
        if (year <= table[i][0]) {
            float t = (year - table[i - 1][0]) / (table[i][0] - table[i - 1][0]);
            return table[i - 1][1] + t * (table[i][1] - table[i - 1][1]);
        }
    return table[n - 1][1];
}

uint32_t Hash(uint32_t a, uint32_t b, uint32_t c)
{
    uint32_t h = 2166136261u;
    for (uint32_t v : { a, b, c })
        for (int i = 0; i < 4; i++)
            h = (h ^ ((v >> (i * 8)) & 0xFF)) * 16777619u;
    return h;
}

} // namespace

float Today() { return game::DateDays(); }
float Year() { return Today() / kDaysPerYear; }

// Price level relative to 1950.
float EraFactor(float year)
{
    static const float t[][2] = { { 1920, 0.55f }, { 1940, 0.75f }, { 1950, 1.0f }, { 1960, 1.3f }, { 1970, 1.8f },
                                  { 1980, 3.6f }, { 1990, 5.4f }, { 2000, 7.0f }, { 2010, 8.6f }, { 2030, 12.0f } };
    return Lerp(t, 10, year);
}

// Homes with a television set, in millions.
float Households(float year)
{
    static const float t[][2] = { { 1938, 0.002f }, { 1946, 0.01f }, { 1950, 3.9f }, { 1955, 30.7f }, { 1960, 45.7f },
                                  { 1970, 59.6f }, { 1980, 76.3f }, { 1990, 92.1f }, { 2000, 100.8f }, { 2010, 114.9f },
                                  { 2030, 128.0f } };
    return Lerp(t, 11, year);
}

float Appeal(int format, float year)
{
    const FormatInfo& f = kFormats[format];
    float t = std::clamp((year - 1950.0f) / 50.0f, 0.0f, 1.0f);
    return (f.appeal1950 + t * (f.appeal2000 - f.appeal1950)) * EventAppeal(format, year);
}

int FullOrder(int format, float year)
{
    if (format == Miniseries)
        return 6;
    if (format == TalkShow || format == GameShow || format == Soap)
        return 39;
    return year < 1970 ? 26 : 22;
}

// Deterministic random number in [lo, hi].
float Roll(uint32_t a, uint32_t b, uint32_t c, float lo, float hi)
{
    return lo + (hi - lo) * (Hash(a, b, c) % 10000) / 9999.0f;
}

uint32_t HashText(const std::string& s)
{
    uint32_t h = 2166136261u;
    for (unsigned char c : s)
        h = (h ^ c) * 16777619u;
    return h;
}

std::string Utf8(const std::wstring& w)
{
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

std::string Dollars(double v)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "$%.0f", std::fabs(v));
    std::string s = buf;
    for (int i = static_cast<int>(s.size()) - 3; i > 1; i -= 3)
        s.insert(i, ",");
    return v < 0 ? "-" + s : s;
}

std::string Format(const char* fmt, ...)
{
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    return buf;
}

std::string DateText(float days)
{
    static const char* months[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    int year = static_cast<int>(days / kDaysPerYear);
    int dayOfYear = static_cast<int>(days - year * kDaysPerYear);
    int month = std::min(11, dayOfYear * 12 / 365);
    return Format("%s %d", months[month], year);
}

std::string PilotKey(const game::Project& p) { return Format("%s@%.2f", Utf8(p.title).c_str(), p.startDate); }

void Notify(const std::string& title, const std::string& body, ImVec4 colour)
{
    mod::Log("tv: %s - %s", title.c_str(), body.c_str());
    g_toasts.push_back({ title, body, colour, -1 });
    while (g_toasts.size() > 5)
        g_toasts.pop_front();
}

bool Unlocked() { return Year() >= kTelevisionYear; }
bool HasOffice() { return g_offices > 0 || !g_detectionWorks; }
int  Capacity() { return g_detectionWorks ? g_offices * kShowsPerOffice + g_stages * kShowsPerStage : 99; }

bool InProduction(const Series& s) { return s.status == Ordered || s.status == OnAir || s.status == OnHold; }

Series* FindSeries(int id)
{
    for (Series& s : g_series)
        if (s.id == id)
            return &s;
    return nullptr;
}

std::vector<game::Project> AvailablePilots()
{
    std::vector<game::Project> out;
    std::vector<std::string> seen;
    float today = Today();
    auto consider = [&](const game::Project& p) {
        bool released = p.stage >= 7 || (p.hasIncome && p.releaseDate > 0 && p.releaseDate <= today);
        std::string key = PilotKey(p);
        if (!released || p.title.empty() || std::find(seen.begin(), seen.end(), key) != seen.end())
            return;
        seen.push_back(key);
        for (const Series& s : g_series)
            if (s.pilotKey == key)
                return;
        out.push_back(p);
    };
    if (g_filmPilots) {
        for (const game::Project& p : game::PlayerBackCatalogue())
            consider(p);
        for (const game::Project& p : game::PlayerProductions())
            consider(p);
    }
    AddShelfPilots(out, seen);
    if (!g_filmPilots)
        return out;
    // Scripts and films dropped on a TV building, released or not.
    for (uintptr_t addr : g_droppedProjects) {
        game::Project p = game::ReadProjectAt(addr);
        std::string key = PilotKey(p);
        if (p.title.empty() || std::find(seen.begin(), seen.end(), key) != seen.end())
            continue;
        bool used = false;
        for (const Series& s : g_series)
            used |= s.pilotKey == key;
        if (!used) {
            seen.push_back(key);
            if (p.quality < 0.05f)
                p.quality = std::max(p.quality, p.rawQuality);   // an unshot script: judge it by what's on the page
            out.push_back(p);
        }
    }
    return out;
}

// ---------------------------------------------------------------------------------
// Series

namespace {

// A spinoff of a hit comes with an audience: its parent's network is keenest.
float SpinoffBonus(const Series& s, int network)
{
    const Series* parent = s.parentId ? FindSeries(s.parentId) : nullptr;
    if (!parent)
        return 0.0f;
    return parent->network == network ? 12.0f : 5.0f;
}

// Networks are keener on shows they get to own.
float DealBonus(const Series& s) { return s.deal == DealNetworkOwned ? 4.0f : 0.0f; }

// One network's fair share of the audience.
float FairShare()
{
    int active = 0;
    for (const NetworkState& ns : g_networks)
        active += ns.exists;
    return 1.0f / std::max(2, active);
}

} // namespace

// The dice for a pitch are keyed to the series, the network and the pilot film: showing
// the same film to the same network again gets the same reaction. Only a different film
// (or a different network) is a fresh chance.
float PitchScore(const Series& s, int network)
{
    game::Studio studio = game::ReadPlayerStudio();
    float prestige = studio.ok ? std::clamp(studio.prestige, 0.0f, 2.0f) : 0.0f;
    return s.pilotQuality * 80.0f + s.pilotStars * 10.0f + prestige * 8.0f + kNetworks[network].taste[s.format] +
           (Appeal(s.format, Year()) - 1.0f) * 20.0f + kBudgetPitch[s.budget] + SpinoffBonus(s, network) + DealBonus(s) +
           (HasTech(TechAudienceResearch) ? 4.0f : 0.0f) +
           Roll(s.id, network, HashText(s.pilotKey), -8.0f, 8.0f);
}

// The network expected to like this pilot most, ignoring the dice.
int BestNetwork(const Series& s)
{
    int best = -1;
    float bestMargin = -1e9f;
    for (int n = 0; n < kRivalCount; n++) {
        if ((s.passedMask | s.reworkMask) & (1 << n))
            continue;
        float margin = kNetworks[n].taste[s.format] - kNetworks[n].bar * 0.6f;
        if (margin > bestMargin) {
            bestMargin = margin;
            best = n;
        }
    }
    return best;
}

void StartPitch(Series& s, int network)
{
    s.network = network;
    s.status = Pitching;
    s.attempts++;
    s.verdictDay = Today() + kVerdictDelay;
    Notify("Pilot sent to " + std::string(kNetworks[network].shortName),
           Format("'%s' is being screened by %s executives.", s.title.c_str(), kNetworks[network].name), kGrey);
}

void OrderSeason(Series& s, int episodes, float firstAirDay)
{
    s.season++;
    s.order = episodes;
    s.aired = 0;
    s.ratingSum = 0;
    s.shareSum = 0;
    s.ratings.clear();
    s.qualitySum = 0;
    s.premiereRating = 0;
    s.askedForRaise = false;
    s.status = Ordered;
    s.nextAirDay = firstAirDay;
    if (!OwnOutlet(s.network) && s.slot < 0)
        AssignSlot(s);
}

namespace {

void Verdict(Series& s)
{
    const NetworkInfo& n = kNetworks[s.network];
    s.pitchScore = PitchScore(s, s.network);
    float bar = n.bar + StandardsBonus();
    if (s.pitchScore >= bar + 15.0f) {
        OrderSeason(s, FullOrder(s.format, Year()), Today() + kPremiereDelay);
        Notify(Format("%s orders a full season!", n.shortName),
               Format("'%s' gets %d episodes, %s. Premiere in two weeks.", s.title.c_str(), s.order,
                      SlotName(s.network, s.slot).c_str()),
               kGold);
    } else if (s.pitchScore >= bar) {
        OrderSeason(s, s.format == Miniseries ? 6 : kLimitedOrder, Today() + kPremiereDelay);
        Notify(Format("%s orders a limited run", n.shortName),
               Format("'%s' gets %d episodes to prove itself, %s.", s.title.c_str(), s.order,
                      SlotName(s.network, s.slot).c_str()),
               kGreen);
    } else if (s.pitchScore >= bar - 12.0f) {
        s.status = Rework;
        s.reworkMask |= 1 << s.network;
        Notify(Format("%s wants a rework", n.shortName),
               Format(g_filmPilots ? "'%s' shows promise. Pitch it again with a stronger pilot film, or try another network."
                                : "'%s' shows promise. Shoot a stronger pilot (a better script, a bigger star, a director) or try another network.",
                      s.title.c_str()),
               kYellow);
        // Casual players don't manage reworks: the show goes straight to the next network.
        if (g_level == Casual) {
            int next = BestNetwork(s);
            if (next >= 0)
                StartPitch(s, next);
            else
                s.status = PassedOn;
        }
    } else {
        s.passedMask |= 1 << s.network;
        s.status = PassedOn;
        Notify(Format("%s passes on '%s'", n.shortName, s.title.c_str()),
               (s.passedMask == (1 << kRivalCount) - 1) ? "Every network has turned it down."
                                                        : "Other networks may still be interested.",
               kRed);
        // Casual players get an automatic second try with the next best network.
        if (g_level == Casual) {
            int next = BestNetwork(s);
            if (next >= 0)
                StartPitch(s, next);
        }
    }
    mod::Log("tv: verdict for '%s' at %s: score %.1f vs bar %.0f", s.title.c_str(), n.shortName, s.pitchScore, bar);
}

} // namespace

float EpisodeQuality(const Series& s, int episode)
{
    float crew = 0.45f + kBudgetQuality[s.budget];
    if (g_stages > 0)
        crew += kFormats[s.format].multiCamera ? 0.08f : 0.03f;
    // People assigned through the TV Production Office's drop zones.
    float staff = (s.lead.empty() ? 0.0f : 0.06f) + 0.025f * std::min<int>(kMaxWriters, static_cast<int>(s.writers.size())) +
                  (s.showrunner.empty() ? 0.0f : 0.05f);
    float q = 0.55f * s.pilotQuality + 0.45f * crew + staff + PlanQuality(s, episode) + TechQualityBonus(s.format) + Roll(s.id, s.season * 1000 + episode, 7, -0.07f, 0.07f);
    return std::clamp(q, 0.05f, 1.0f);
}

// Per-episode pay of the people assigned to a show, in 1950 dollars.
double StaffPay1950(const Series& s)
{
    return LeadPay1950(s) + 400.0 * s.writers.size() + (s.showrunner.empty() ? 0 : 800);
}

void AssignPerson(Series& s, const std::string& room, const std::string& person)
{
    // One job per person: take them off any other show first.
    for (Series& other : g_series) {
        if (other.lead == person)
            other.lead.clear();
        if (other.showrunner == person)
            other.showrunner.clear();
        other.writers.erase(std::remove(other.writers.begin(), other.writers.end(), person), other.writers.end());
    }
    if (room == "room_tv_cast") {
        bool recast = !s.formerLead.empty();
        if (s.lead != person) {
            s.leadPay = 1.0f;
            s.leadSeasons = 0;
            s.demand = 0;
        }
        s.lead = person;
        s.formerLead.clear();
        Notify(Format("%s stars in '%s'", person.c_str(), s.title.c_str()),
               recast ? "The role is recast. Viewers will warm to the new face." : "A familiar face draws viewers.", kGold);
    } else if (room == "room_tv_showrun") {
        s.showrunner = person;
        Notify(Format("%s runs '%s'", person.c_str(), s.title.c_str()), "Steadier episodes, slower to go stale.", kGold);
    } else if (room == "room_tv_writers") {
        if (static_cast<int>(s.writers.size()) >= kMaxWriters)
            s.writers.erase(s.writers.begin());
        s.writers.push_back(person);
        Notify(Format("%s joins the writers of '%s'", person.c_str(), s.title.c_str()),
               Format("%d writer%s in the room.", static_cast<int>(s.writers.size()), s.writers.size() == 1 ? "" : "s"), kGold);
    }
    mod::Log("tv: %s assigned to '%s' via %s", person.c_str(), s.title.c_str(), room.c_str());
}

namespace {

// A script or a film dropped on a TV building: develop it as a series.
void DevelopFromObject(uintptr_t project)
{
    game::Project p = game::ReadProjectAt(project);
    mod::Log("tv: project dropped - '%s' stage %d quality %.2f raw %.2f stars %.2f", Utf8(p.title).c_str(), p.stage,
             p.quality, p.rawQuality, p.starRating);
    if (p.title.empty())
        return;
    AdoptIfWriting(project);
    if (!IsShelfScriptObject(project) &&
        std::find(g_droppedProjects.begin(), g_droppedProjects.end(), project) == g_droppedProjects.end())
        g_droppedProjects.push_back(project);
    Notify(Format("'%s' is on the writers' desk", Utf8(p.title).c_str()),
           p.stage >= 7 ? "Your film could become a TV series. Pick a format and a network."
                        : "This script could become a TV series. Pick a format and a network.",
           kGold);
    OpenPitchFor(project);
    overlay::Open();
}

// A dropped person's star rating (CStar+0xCD0), or a middling 0.4 for anyone else.
float StarRatingOf(const rooms::Drop& d)
{
    if (d.personClass != "CStar" || !d.person)
        return 0.4f;
    __try {
        float r = *reinterpret_cast<const float*>(d.person + 0xCD0);
        return r == r && r >= 0 && r <= 1 ? r : 0.4f;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0.4f;
    }
}

// TV scripts, pilot reels and the bags of shares: only the mod's buildings take them.
bool IsModObject(uintptr_t project) { return IsTvScriptProject(project) || IsShareObject(project); }

const char* ModObjectMesh(uintptr_t project)
{
    if (const char* m = ShareObjectMesh(project))
        return m;
    return PilotReelMesh(project);
}

// Their hover bubble says what they are and where they go, not "Ready For Casting".
bool ModObjectLabel(uintptr_t project, rooms::ObjectLabel* l)
{
    return ShareObjectLabel(project, &l->kind, &l->phase, &l->status) ||
           CartoonLabel(project, &l->kind, &l->phase, &l->status) ||
           TvScriptLabel(project, &l->kind, &l->phase, &l->status);
}

bool OnRoomDrop(const rooms::Drop& d)
{
    // Shares go to the Accounting Office; nothing else is dealt with there yet.
    if (d.project && SharesDrop(d.room, d.project))
        return true;
    // People on the Accounts Floor or the Animation Studio's desks: they sit down to work.
    if (!d.project && d.person && StaffDrop(d.room, d.roomObject, d.person))
        return true;
    // The Animation Studio: a film's script in the Story Room becomes a cartoon.
    if (AnimationDrop(d.room, d.project, d.person))
        return true;
    if (d.room.rfind("room_tvk_", 0) == 0) {
        if (d.project)
            Notify("The Accounting Office", "The accountants deal with the shares here: your bag of shares, or the "
                   "investors' briefcase.",
                   kYellow);
        else if (d.person)
            Notify("The Accounting Office", "Drop a jobseeker on the hiring desk to hire them as an accountant, or an "
                   "accountant on the Accounts Floor to put them to work.",
                   kYellow);
        return true;
    }
    // A finished pilot's reel: pitched at the Network HQ, aired from the Control Room.
    if (d.project && DropReel(d.room, d.project))
        return true;
    // A TV script dropped on any TV building: shoot its pilot (on a soundstage).
    if (d.room.rfind("room_tv", 0) == 0 && d.project && !g_filmPilots && IsUnshotScript(d.project)) {
        StartPilotForProject(d.project);
        return true;
    }
    // People dropped on a soundstage: the lead of the pilot being shot, or its director.
    if (d.room.rfind("room_tvs_", 0) == 0 && !d.project && d.person) {
        std::string name = d.personName.empty() ? Format("%s #%04X", d.personClass.c_str(), d.person & 0xFFFF) : d.personName;
        if (CastPilot(name, StarRatingOf(d), d.room == "room_tvs_control"))
            return true;
    }
    if (d.room.rfind("room_tv", 0) == 0 && d.project) {
        DevelopFromObject(d.project);
        return true;
    }
    if (IsWritersZone(d.room)) {
        AssignWriterToDesk(d.room,
                           d.personName.empty() ? Format("%s #%04X", d.personClass.c_str(), d.person & 0xFFFF)
                                                : d.personName,
                           d.scriptRoom ? d.roomObject : 0);
        return true;
    }
    if (d.room != "room_tv_cast" && d.room != "room_tv_writers" && d.room != "room_tv_showrun")
        return false;
    g_pendingDrop.active = true;
    g_pendingDrop.room = d.room;
    g_pendingDrop.personClass = d.personClass;
    g_pendingDrop.person = d.personName.empty() ? Format("%s #%04X", d.personClass.c_str(), d.person & 0xFFFF) : d.personName;
    overlay::Open();
    return true;
}

} // namespace

// Money and statistics for one airing. The rating comes from the audience model in
// tv_network.cpp: what else is on, the lead-in, and the network's reach.
void RecordAiring(Series& s, float quality, float rating, float share)
{
    float year = Year();
    float era = EraFactor(year);
    const FormatInfo& f = kFormats[s.format];
    int ep = s.aired + 1;

    double cost = (f.cost1950 * kBudgetCost[s.budget] * PlanCostMul(s) * TechCostMul() * (s.livePending ? 1.5 : 1.0) +
                   StaffPay1950(s)) * era * g_costScale;
    double income;
    if (s.network == kStreamingNetwork) {
        income = 0;   // streaming originals earn through subscriptions
    } else if (s.network == kPlayerNetwork) {
        // Your own network keeps all the advertising.
        income = rating / 100.0 * Households(year) * 1e6 * 0.0025 * era;
        g_networks[kPlayerNetwork].weekIncome += income * g_incomeScale;
        g_networks[kPlayerNetwork].weekCost += cost;
    } else {
        const NetworkInfo& n = kNetworks[s.network];
        double fee = f.fee1950 * era * n.feeMul * s.feeMul;
        // How the episode did against an even split of the audience (1 = par).
        float rel = std::clamp(share / FairShare(), 0.0f, 2.5f);
        if (s.deal == DealNetworkOwned)
            income = cost * 1.12 / g_incomeScale;   // cost plus, whatever the ratings
        else if (s.deal == DealLicense)
            income = fee * (1.0f + 0.5f * std::clamp(rel - 1.0f, 0.0f, 1.0f));   // a ratings bonus of up to half
        else
            income = fee * (0.3f + 0.9f * rel) * n.strength;   // advertising: a flop pays little, a smash a lot
    }
    income *= g_incomeScale;

    game::AddMoney(income - cost);
    s.income += income;
    s.cost += cost;
    s.aired = ep;
    s.totalAired++;
    s.lastRating = rating;
    s.lastShare = share;
    s.bestRating = std::max(s.bestRating, rating);
    s.ratingSum += rating;
    s.ratingTotal += rating;
    s.shareSum += share;
    s.ratings.push_back(rating);
    // A showrunner keeps the show consistent: freshness fades half as fast.
    s.freshness = std::max(kMinFreshness, s.freshness - FreshnessLoss(s));
    s.status = OnAir;
    AfterAiring(s, quality, rating);

    mod::Log("tv: '%s' S%dE%d %s quality %.2f rating %.1f share %.0f%% income %.0f cost %.0f", s.title.c_str(),
             s.season, ep, SlotName(s.network, s.slot).c_str(), quality, rating, share * 100, income, cost);
    if (ep == 1 && s.network == kStreamingNetwork)
        Notify(Format("Premiere: '%s'", s.title.c_str()),
               Format("%.0f%% of %s's subscribers watched the first episode.", share * 100, StreamingName()), kGold);
    else if (ep == 1)
        Notify(Format("Premiere: '%s'", s.title.c_str()),
               Format("%.1f rating (%.0f%% share) on %s - %.1f million homes tuned in.", rating, share * 100,
                      NetworkShort(s.network), rating / 100.0f * Households(year)),
               kGold);
}

// A rival network renews shows that win a fair share of the audience in their slot; the
// bar rises with the network's standards. A cliffhanger makes them a little keener.
float RenewalNeeded(const Series& s)
{
    if (OwnOutlet(s.network))
        return 0.2f;
    float needed = FairShare() * (0.75f + kNetworks[s.network].bar / 200.0f);
    return s.finale == FinaleCliffhanger ? needed * 0.95f : needed;
}

void EndSeason(Series& s)
{
    float avg = s.aired ? s.ratingSum / s.aired : 0;
    float avgShare = s.aired ? s.shareSum / s.aired : 0;
    float needed = RenewalNeeded(s);
    bool autoRenew = false;
    if (s.format == Miniseries) {
        s.status = Ended;
        ReleaseSlot(s);
        Notify(Format("'%s' concludes", s.title.c_str()), Format("The miniseries averaged a %.1f rating.", avg), kGold);
    } else if (OwnOutlet(s.network)) {
        // Your network (or service), your call.
        s.status = RenewalOffered;
        s.order = FullOrder(s.format, Year());
        Notify(Format("'%s' finishes season %d", s.title.c_str(), s.season),
               Format("Averaged %.1f (%.0f%% share). Order another season?", avg, avgShare * 100), kGreen);
        autoRenew = g_level == Casual && avgShare >= needed && s.freshness > kMinFreshness + 0.06f;
    } else if (avgShare >= needed && s.season >= 6 && s.freshness <= kMinFreshness + 0.06f) {
        // The audience has seen it all: the network gives a long-runner a send-off.
        s.status = Ended;
        ReleaseSlot(s);
        Notify(Format("'%s' takes its final bow", s.title.c_str()),
               Format("After %d seasons %s ends it while it still rates. Its reruns are yours to sell.", s.season,
                      kNetworks[s.network].shortName),
               kGold);
    } else if (avgShare >= needed) {
        const NetworkInfo& n = kNetworks[s.network];
        s.status = RenewalOffered;
        s.order = avgShare >= needed + FairShare() * 0.25f ? FullOrder(s.format, Year()) : kLimitedOrder;
        Notify(Format("%s wants more '%s'", n.shortName, s.title.c_str()),
               Format("Season %d averaged %.1f (%.0f%% share). They offer %d more episodes.", s.season, avg,
                      avgShare * 100, s.order),
               kGreen);
        autoRenew = g_level == Casual;
    } else {
        const NetworkInfo& n = kNetworks[s.network];
        s.status = Cancelled;
        ReleaseSlot(s);
        Notify(Format("'%s' cancelled", s.title.c_str()),
               Format("Season %d averaged a %.0f%% share; %s needed %.0f%%.", s.season, avgShare * 100, n.shortName,
                      needed * 100),
               kRed);
    }
    // Contracts and the newspaper need the finished season's numbers, so they come first.
    SeasonFinale(s, avgShare, needed);
    if (autoRenew) {
        int episodes = s.order;
        OrderSeason(s, episodes, Today() + kSummerHiatus);
        s.freshness = std::min(1.0f, s.freshness + 0.05f);
    }
}

// ---------------------------------------------------------------------------------
// Daily simulation

namespace {

void CountBuildings()
{
    DWORD now = GetTickCount();
    if (g_lastCount && now - g_lastCount < 3000)
        return;
    g_lastCount = now;
    int offices = 0, stages = 0, hqs = 0, controls = 0, archives = 0, centres = 0, writers = 0, total = 0;
    for (const game::Facility& f : game::Facilities()) {
        total++;
        if (!f.type.empty())
            g_detectionWorks = true;
        offices += f.type == kOfficeType;
        stages += f.type == kStageType;
        hqs += f.type == kHqType;
        controls += f.type == kControlRoomType;
        archives += f.type == kArchiveType;
        centres += f.type == kDatacenterType;
        writers += f.type == kWritersType;
    }
    if (offices != g_offices || stages != g_stages || hqs != g_hqs || controls != g_controlRooms || archives != g_archives)
        mod::Log("tv: buildings - %d TV Production Office, %d Multi-Camera Soundstage, %d Network HQ, %d Broadcast "
                 "Control Room, %d Media Archive (%d buildings on the lot)",
                 offices, stages, hqs, controls, archives, total);
    g_archives = archives;
    g_datacenters = centres;
    g_writerOffices = writers;
    g_offices = offices;
    g_stages = stages;
    g_hqs = hqs;
    g_controlRooms = controls;
}

// Shows beyond the studio's production capacity go on hold, newest first.
void ApplyCapacity()
{
    int capacity = HasOffice() ? Capacity() : 0;
    int used = 0;
    for (Series& s : g_series) {
        if (!InProduction(s))
            continue;
        if (used < capacity) {
            used++;
            if (s.status == OnHold) {
                s.status = s.aired ? OnAir : Ordered;
                s.nextAirDay = std::max(s.nextAirDay, Today() + 1.0f);
                Notify(Format("'%s' back in production", s.title.c_str()), "Your studio has room for it again.", kGreen);
            }
        } else if (s.status != OnHold) {
            s.status = OnHold;
            Notify(Format("'%s' on hold", s.title.c_str()),
                   HasOffice() ? "Not enough production capacity. Build another TV Production Office or a Multi-Camera Soundstage."
                               : "You need a TV Production Office to make shows.",
                   kYellow);
        }
    }
}

void Simulate()
{
    CeremonyFrame();
    AwardsTimelineFrame();
    if (!game::Ready())
        return;
    float today = Today();
    if (today <= 0)
        return;
    CountBuildings();
    if (g_lastDay < 0 || today < g_lastDay || today - g_lastDay > 400) {
        if (g_lastDay < 0)
            game::LogBlueprints("facility_tv_");
        g_lastDay = today;   // a game was just loaded: don't replay the gap
        return;
    }
    if (Unlocked()) {
        EnsureRivalSchedules();
        // Shows ordered before schedules existed (Phase 1 saves) get their slot now.
        for (Series& s : g_series)
            if (InProduction(s) && !OwnOutlet(s.network) && s.slot < 0)
                AssignSlot(s);
    }
    ApplyCapacity();
    for (Series& s : g_series)
        if (s.status == Pitching && today >= s.verdictDay)
            Verdict(s);
    // Reworks don't wait forever: Casual shows move on at once, others after a year untouched.
    for (Series& s : g_series) {
        if (s.status != Rework || (g_level != Casual && today < s.verdictDay + kDaysPerYear))
            continue;
        int next = BestNetwork(s);
        if (next >= 0) {
            StartPitch(s, next);
        } else {
            s.status = PassedOn;
            Notify(Format("'%s' goes back in the drawer", s.title.c_str()), "No network is left to pitch it to.", kGrey);
        }
    }

    // Prime time runs once per game day, for every day that has started since last frame.
    int from = static_cast<int>(std::floor(g_lastDay)) + 1;
    int to = static_cast<int>(std::floor(today));
    for (int day = std::max(from, to - 13); day <= to; day++) {
        SharesDaily(static_cast<float>(day));
        if (Unlocked()) {
            NetworkDaily(static_cast<float>(day));
            LibraryWeekly(static_cast<float>(day));
            StreamingDaily(static_cast<float>(day));
            EventsDaily(static_cast<float>(day));
            ResearchWeekly(static_cast<float>(day));
            WritersDaily(static_cast<float>(day));
            AirNight(static_cast<float>(day), day % kNights);
        }
    }
    g_lastDay = today;

    for (Series& s : g_series)
        if (s.status == OnAir && s.aired >= s.order)
            EndSeason(s);
}

// ---------------------------------------------------------------------------------
// Save data

void Save(persist::Section& out)
{
    out["level"] = std::to_string(g_level);
    out["next_id"] = std::to_string(g_nextId);
    out["count"] = std::to_string(g_series.size());
    for (size_t i = 0; i < g_series.size(); i++) {
        const Series& s = g_series[i];
        auto put = [&](const char* k, const std::string& v) { out[Format("s%zu.%s", i, k)] = v; };
        auto num = [&](const char* k, double v) { put(k, Format("%.6g", v)); };
        put("title", s.title);
        put("pilot_key", s.pilotKey);
        put("pilot_title", s.pilotTitle);
        num("id", s.id);
        num("format", s.format);
        num("status", s.status);
        num("network", s.network);
        num("budget", s.budget);
        num("deal", s.deal);
        num("pilot_quality", s.pilotQuality);
        num("pilot_stars", s.pilotStars);
        num("pitch_score", s.pitchScore);
        num("passed", s.passedMask);
        num("rework", s.reworkMask);
        num("attempts", s.attempts);
        num("season", s.season);
        num("order", s.order);
        num("aired", s.aired);
        num("total_aired", s.totalAired);
        num("slot", s.slot);
        put("verdict_day", Format("%.3f", s.verdictDay));
        put("next_air_day", Format("%.3f", s.nextAirDay));
        num("freshness", s.freshness);
        num("last_rating", s.lastRating);
        num("last_share", s.lastShare);
        num("best_rating", s.bestRating);
        num("rating_sum", s.ratingSum);
        num("share_sum", s.shareSum);
        put("income", Format("%.0f", s.income));
        put("cost", Format("%.0f", s.cost));
        std::string r;
        for (float v : s.ratings)
            r += Format(r.empty() ? "%.2f" : ",%.2f", v);
        put("ratings", r);
        put("lead", s.lead);
        put("showrunner", s.showrunner);
        std::string w;
        for (const std::string& name : s.writers)
            w += (w.empty() ? "" : "|") + name;
        put("writers", w);
        num("plan", s.plan);
        num("finale", s.finale);
        num("cliffhanger", s.cliffhanger);
        num("quality_sum", s.qualitySum);
        num("premiere_rating", s.premiereRating);
        num("last_quality", s.lastQuality);
        num("lead_pay", s.leadPay);
        num("lead_seasons", s.leadSeasons);
        num("demand", s.demand);
        put("former_lead", s.formerLead);
        num("fee_mul", s.feeMul);
        num("asked_raise", s.askedForRaise);
        num("parent", s.parentId);
        num("last_spinoff", s.lastSpinoffSeason);
        put("rating_total", Format("%.2f", s.ratingTotal));
        num("wear", s.wear);
        put("synd_income", Format("%.0f", s.syndIncome));
        put("residuals", Format("%.0f", s.residuals));
        num("binge", s.binge);
        put("last_release", Format("%.3f", s.lastReleaseDay));
        num("awards", s.awards);
        num("scandal", s.scandal);
        num("live_pending", s.livePending);
        num("live_season", s.liveSeason);
    }
}

void Reset()
{
    g_series.clear();
    g_level = StudioBoss;
    g_nextId = 1;
    g_lastDay = -1;
    g_toasts.clear();
    g_pendingDrop = PendingDrop{};
    g_droppedProjects.clear();
}

void Load(const persist::Section& in)
{
    Reset();
    auto get = [&](const std::string& k) {
        auto it = in.find(k);
        return it == in.end() ? std::string() : it->second;
    };
    auto num = [&](const std::string& k) { return atof(get(k).c_str()); };
    auto has = [&](const std::string& k) { return in.find(k) != in.end(); };
    g_level = std::clamp(static_cast<int>(num("level")), 0, LevelCount - 1);
    g_nextId = std::max(1, static_cast<int>(num("next_id")));
    int count = static_cast<int>(num("count"));
    for (int i = 0; i < count; i++) {
        std::string p = Format("s%d.", i);
        Series s;
        s.title = get(p + "title");
        s.pilotKey = get(p + "pilot_key");
        s.pilotTitle = get(p + "pilot_title");
        s.id = static_cast<int>(num(p + "id"));
        s.format = std::clamp(static_cast<int>(num(p + "format")), 0, FormatCount - 1);
        s.status = std::clamp(static_cast<int>(num(p + "status")), 0, StatusCount - 1);
        s.network = std::clamp(static_cast<int>(num(p + "network")), 0, kStreamingNetwork);
        s.budget = std::clamp(static_cast<int>(num(p + "budget")), 0, BudgetCount - 1);
        s.deal = std::clamp(static_cast<int>(num(p + "deal")), 0, DealCount - 1);
        s.pilotQuality = static_cast<float>(num(p + "pilot_quality"));
        s.pilotStars = static_cast<float>(num(p + "pilot_stars"));
        s.pitchScore = static_cast<float>(num(p + "pitch_score"));
        s.passedMask = static_cast<int>(num(p + "passed"));
        s.reworkMask = static_cast<int>(num(p + "rework"));
        s.attempts = static_cast<int>(num(p + "attempts"));
        s.season = static_cast<int>(num(p + "season"));
        s.order = static_cast<int>(num(p + "order"));
        s.aired = static_cast<int>(num(p + "aired"));
        s.totalAired = static_cast<int>(num(p + "total_aired"));
        s.slot = has(p + "slot") ? std::clamp(static_cast<int>(num(p + "slot")), -1, kSlots - 1) : -1;
        s.verdictDay = static_cast<float>(num(p + "verdict_day"));
        s.nextAirDay = static_cast<float>(num(p + "next_air_day"));
        s.freshness = static_cast<float>(num(p + "freshness"));
        s.lastRating = static_cast<float>(num(p + "last_rating"));
        s.lastShare = static_cast<float>(num(p + "last_share"));
        s.bestRating = static_cast<float>(num(p + "best_rating"));
        s.ratingSum = static_cast<float>(num(p + "rating_sum"));
        // Phase 1 saves have no shares; estimate them from the ratings (about 60% of homes watch).
        s.shareSum = has(p + "share_sum") ? static_cast<float>(num(p + "share_sum")) : s.ratingSum / 60.0f;
        s.income = num(p + "income");
        s.cost = num(p + "cost");
        std::string r = get(p + "ratings");
        for (size_t pos = 0; pos < r.size();) {
            size_t comma = r.find(',', pos);
            s.ratings.push_back(static_cast<float>(atof(r.substr(pos, comma - pos).c_str())));
            pos = comma == std::string::npos ? r.size() : comma + 1;
        }
        s.lead = get(p + "lead");
        s.showrunner = get(p + "showrunner");
        std::string w = get(p + "writers");
        for (size_t pos = 0; pos < w.size();) {
            size_t bar = w.find('|', pos);
            s.writers.push_back(w.substr(pos, bar - pos));
            pos = bar == std::string::npos ? w.size() : bar + 1;
        }
        s.plan = std::clamp(static_cast<int>(num(p + "plan")), 0, PlanCount - 1);
        s.finale = std::clamp(static_cast<int>(num(p + "finale")), 0, FinaleCount - 1);
        s.cliffhanger = num(p + "cliffhanger") != 0;
        s.qualitySum = static_cast<float>(num(p + "quality_sum"));
        s.premiereRating = static_cast<float>(num(p + "premiere_rating"));
        s.lastQuality = static_cast<float>(num(p + "last_quality"));
        s.leadPay = has(p + "lead_pay") ? static_cast<float>(num(p + "lead_pay")) : 1.0f;
        s.leadSeasons = static_cast<int>(num(p + "lead_seasons"));
        s.demand = static_cast<int>(num(p + "demand"));
        s.formerLead = get(p + "former_lead");
        s.feeMul = has(p + "fee_mul") ? static_cast<float>(num(p + "fee_mul")) : 1.0f;
        s.askedForRaise = num(p + "asked_raise") != 0;
        s.parentId = static_cast<int>(num(p + "parent"));
        s.lastSpinoffSeason = has(p + "last_spinoff") ? static_cast<int>(num(p + "last_spinoff")) : -9;
        // Older saves only know this season's ratings: estimate the rest from the best.
        s.ratingTotal = has(p + "rating_total") ? num(p + "rating_total")
                                                : s.ratingSum + (s.totalAired - s.aired) * s.bestRating * 0.75;
        s.wear = static_cast<float>(num(p + "wear"));
        s.syndIncome = num(p + "synd_income");
        s.residuals = num(p + "residuals");
        s.binge = num(p + "binge") != 0;
        s.lastReleaseDay = static_cast<float>(num(p + "last_release"));
        s.awards = static_cast<int>(num(p + "awards"));
        s.scandal = num(p + "scandal") != 0;
        s.livePending = num(p + "live_pending") != 0;
        s.liveSeason = has(p + "live_season") ? static_cast<int>(num(p + "live_season")) : -1;
        g_series.push_back(s);
    }
    mod::Log("tv: loaded %zu series (level %s)", g_series.size(), kLevelNames[g_level]);
}

void LoadSettings()
{
    wchar_t path[MAX_PATH];
    swprintf_s(path, L"%s\\MoviesMod.ini", mod::GameDir());
    wchar_t buf[64];
    GetPrivateProfileStringW(L"tv", L"IncomeScale", L"1", buf, 64, path);
    g_incomeScale = std::clamp(static_cast<float>(_wtof(buf)), 0.0f, 100.0f);
    GetPrivateProfileStringW(L"tv", L"CostScale", L"1", buf, 64, path);
    g_costScale = std::clamp(static_cast<float>(_wtof(buf)), 0.0f, 100.0f);
    g_devTools = GetPrivateProfileIntW(L"MoviesMod", L"DevTools", 0, path) != 0;
    g_filmPilots = GetPrivateProfileIntW(L"tv", L"FilmPilots", 0, path) != 0;
    g_awardsCeremony = GetPrivateProfileIntW(L"tv", L"AwardsCeremony", 1, path) != 0;
    mod::Log("tv: income x%.2f, costs x%.2f", g_incomeScale, g_costScale);
}

} // namespace

void Install()
{
    LoadSettings();
    overlay::AddPanel("television", &DrawWindow);
    overlay::AddHud(&DrawToasts);
    overlay::AddFrameCallback(&Simulate);
    persist::Register("tv", &Save, &Load, &Reset);
    persist::Register("tv_networks", &NetworksSave, &NetworksLoad, &NetworksReset);
    persist::Register("tv_press", &StorySave, &StoryLoad, &StoryReset);
    persist::Register("tv_library", &LibrarySave, &LibraryLoad, &LibraryReset);
    persist::Register("tv_streaming", &StreamingSave, &StreamingLoad, &StreamingReset);
    persist::Register("tv_events", &EventsSave, &EventsLoad, &EventsReset);
    persist::Register("tv_research", &ResearchSave, &ResearchLoad, &ResearchReset);
    persist::Register("tv_writers", &WritersSave, &WritersLoad, &WritersReset);
    persist::Register("tv_shares", &SharesSave, &SharesLoad, &SharesReset);
    InstallNative();
    rooms::SetHandler(&OnRoomDrop);
    rooms::SetTvScriptTest(&IsModObject);
    rooms::SetMeshChooser(&ModObjectMesh);
    rooms::SetLabeler(&ModObjectLabel);
    InstallStaff();
    InstallAnimation();
    InstallCeremony(g_awardsCeremony);
    InstallAwardsTimeline();
    // The bag and the briefcase get a group of their own on the HUD's project list.
    projectlist::SetCategory(&IsShareObject, &ShareObjectPicture, [](uintptr_t) { OpenSharesScreen(); });
}

} // namespace tv
