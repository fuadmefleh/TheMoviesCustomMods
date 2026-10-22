// Networks and prime time: the rival networks' schedules and their own shows, the
// audience model that turns a night's line-up into ratings, the fall season, and the
// player's own network (founding, reach, ad revenue, Programming Director).

#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <cmath>

#include "mod.h"
#include "tv_internal.h"

namespace tv {

NetworkState g_networks[kNetworkSlots];
std::vector<AiShow> g_aiShows;
std::vector<ChartEntry> g_lastWeek;

namespace {

constexpr float kPrimeTimeViewing = 0.62f;   // share of TV homes watching in prime time
constexpr float kAdRatePerHome    = 0.0025f; // 1950 dollars per home reached, per airing
constexpr float kDirectorSalary   = 800.0f;  // 1950 dollars per week
constexpr int   kFallDayOfYear    = 258;     // mid-September

std::vector<ChartEntry> g_thisWeek;
int g_nextAiId = 1;
int g_lastFallYear = 0;

// ---------------------------------------------------------------------------------
// Rival shows

const char* const kFirst[] = { "Lucy", "Jack", "Ruth", "Eddie", "Martha", "Frank", "Dottie", "Walter", "Betty", "Joe",
                               "Molly", "Harry", "Peggy", "Ray", "Dolores", "Sam", "Vera", "Tom", "Hazel", "Gus" };
const char* const kSurname[] = { "Baxter", "Kowalski", "Hennessy", "Marlowe", "Pruitt", "Delgado", "Abernathy",
                                 "Finch", "Kramer", "O'Malley", "Whitfield", "Castellano", "Brannigan", "Doyle",
                                 "Fairbanks", "Gallagher", "Lindqvist", "Pemberton", "Sterling", "Quill" };
const char* const kPlace[] = { "Maple Street", "Elm Grove", "Pine Hollow", "Riverside", "Oak Ridge", "Willow Falls",
                               "Bay Ridge", "Fairview", "Cedar Point", "Hartwell" };
const char* const kCity[] = { "Metro", "Harbor City", "Central City", "Bay City", "Union City", "Lakeside", "Capitol",
                              "Midtown", "Westside", "Southport" };
const char* const kFrontier[] = { "Mars", "Venus", "Orion", "Andromeda", "Saturn", "the Stars", "Tomorrow",
                                  "Station Nine", "the Void", "the Rings" };
const char* const kNoun[] = { "Hope", "Tomorrow", "Our Lives", "Desire", "Promises", "Secrets", "Fortune", "Courage",
                              "Science", "History" };

const char* const kPatterns[FormatCount][6] = {
    { "The {S}s", "Life with {F}", "{F} and {G}", "Meet the {S}s", "My Friend {F}", "{P}" },
    { "{C} Precinct", "Detective {S}", "{S}, M.D.", "{C} Homicide", "The {S} Files", "Night Beat" },
    { "{P}", "The House on {P}", "The {S} Legacy", "Bitter Harvest", "{C} Nights", "Empire of {S}" },
    { "{P} Heights", "Days of {N}", "The Edge of {N}", "Search for {N}", "All My {S}s", "The {S} Family" },
    { "Captain {S} of {X}", "{X} Patrol", "Rocket {F}", "Beyond {X}", "The {X} Frontier", "Space Ranger {S}" },
    { "Tonight with {F} {S}", "The {F} {S} Show", "{F} After Dark", "Late with {S}", "Coffee with {F}", "{S} at Large" },
    { "Jackpot Alley", "Prize Parade", "Double or Nothing", "Lucky Numbers", "{S}'s Treasure Chest", "Guess Again" },
    { "Real Lives: {C}", "The {C} Experiment", "Survive {P}", "House Swap", "{C} Rescue", "Stranded" },
    { "This Week in {N}", "Window on the World", "{C} Uncovered", "Frontiers of {N}", "Our World", "The Living Earth" },
    { "{S}: A Life", "The {S} Saga", "Winds over {P}", "North of {P}", "The {S} Inheritance", "Shogun of {C}" },
};

struct Dice {
    uint32_t state;
    uint32_t Next() { state = state * 1664525u + 1013904223u; return state >> 8; }
    int Pick(int n) { return static_cast<int>(Next() % static_cast<uint32_t>(n)); }
    float Range(float lo, float hi) { return lo + (hi - lo) * (Next() % 10000) / 9999.0f; }
};

std::string MakeTitle(int format, Dice& d)
{
    std::string pattern = kPatterns[format][d.Pick(6)];
    std::string out;
    for (size_t i = 0; i < pattern.size(); i++) {
        if (pattern[i] == '{' && i + 2 < pattern.size() && pattern[i + 2] == '}') {
            switch (pattern[i + 1]) {
            case 'F': out += kFirst[d.Pick(20)]; break;
            case 'G': out += kFirst[d.Pick(20)]; break;
            case 'S': out += kSurname[d.Pick(20)]; break;
            case 'P': out += kPlace[d.Pick(10)]; break;
            case 'C': out += kCity[d.Pick(10)]; break;
            case 'X': out += kFrontier[d.Pick(10)]; break;
            case 'N': out += kNoun[d.Pick(10)]; break;
            }
            i += 2;
        } else {
            out += pattern[i];
        }
    }
    return out;
}

} // namespace

std::string ShowTitle(int format, uint32_t seed)
{
    Dice d{ seed ^ 0x5bd1e995u };
    for (int i = 0; i < 3; i++)
        d.Next();
    return MakeTitle(format, d);
}

namespace {

AiShow MakeAiShow(int network, int slot)
{
    float year = Year();
    int hour = slot % kHours;
    const NetworkInfo& n = kNetworks[network];
    Dice d{ 0x9E3779B9u ^ static_cast<uint32_t>(g_nextAiId * 7919 + network * 131 + slot) };
    for (int i = 0; i < 3; i++)
        d.Next();

    // The network picks formats it likes and audiences want, suited to the hour.
    float weights[FormatCount] = {};
    float total = 0;
    for (int f = 0; f < FormatCount; f++) {
        if (f == Miniseries || year < kFormats[f].fromYear)
            continue;
        weights[f] = std::max(0.1f, 1.0f + n.taste[f] / 10.0f) * Appeal(f, year) * (kFormats[f].bestHour == hour ? 1.6f : 1.0f);
        total += weights[f];
    }
    int format = GameShow;
    float pick = d.Range(0, total);
    for (int f = 0; f < FormatCount; f++) {
        if (weights[f] <= 0)
            continue;
        format = f;
        if ((pick -= weights[f]) <= 0)
            break;
    }

    AiShow show;
    show.id = g_nextAiId++;
    show.network = network;
    show.format = format;
    show.title = MakeTitle(format, d);
    // Rival craft improves with the decades, and every network has the odd hit and flop.
    float craft = 0.08f * std::clamp((year - 1950.0f) / 40.0f, 0.0f, 1.0f);
    show.quality = std::clamp(n.showQuality + craft + d.Range(-0.2f, 0.2f), 0.1f, 0.9f);

    // Now and then a network turns a rival studio's recent hit film into a series.
    bool adaptable = format == Sitcom || format == Procedural || format == Serialized || format == SciFi || format == Soap;
    if (adaptable && d.Pick(4) == 0) {
        std::vector<game::Project> hits;
        float today = Today();
        for (const game::Project& f : game::RivalFilms()) {
            if (f.quality < 0.5f || today - f.releaseDate > 5 * 365.0f || f.title.empty())
                continue;
            std::string title = Utf8(f.title);
            bool taken = false;
            for (const AiShow& a : g_aiShows)
                taken |= a.title == title;
            if (!taken)
                hits.push_back(f);
        }
        if (!hits.empty()) {
            const game::Project& f = hits[d.Pick(static_cast<int>(hits.size()))];
            show.title = Utf8(f.title);
            show.quality = std::clamp(0.5f * show.quality + 0.5f * f.quality, 0.1f, 0.9f);
            mod::Log("tv: %s adapts %ls's film '%s' as a %s", n.name, f.studio.c_str(), show.title.c_str(),
                     kFormats[format].name);
        }
    }
    return show;
}

void RemoveAiShow(int id)
{
    g_aiShows.erase(std::remove_if(g_aiShows.begin(), g_aiShows.end(), [id](const AiShow& a) { return a.id == id; }),
                    g_aiShows.end());
}

int ActiveNetworks()
{
    int n = 0;
    for (const NetworkState& ns : g_networks)
        n += ns.exists;
    return std::max(1, n);
}

// A slot's series reference is only valid while that series is still on the schedule.
bool SeriesHoldsSlot(int network, int slot, int seriesId)
{
    Series* s = FindSeries(seriesId);
    return s && s->network == network && s->slot == slot && (InProduction(*s) || s->status == RenewalOffered);
}

void FallSeason()
{
    std::string summary;
    int premieres = 0;
    for (int n = 0; n < kRivalCount; n++) {
        // Each network judges its shows against its own lineup, so a weak network does not
        // cancel everything it airs. The worst performers go first, at most a third a year.
        struct Candidate { SlotEntry* entry; float score; };
        std::vector<Candidate> weak, spent;
        float sum = 0;
        int rated = 0, lineup = 0;
        for (SlotEntry& e : g_networks[n].slots) {
            AiShow* a = e.aiShowId ? FindAiShow(e.aiShowId) : nullptr;
            if (!a)
                continue;
            lineup++;
            if (a->weeks >= 4) {
                sum += a->shareSum / a->weeks;
                rated++;
            }
        }
        float mean = rated ? sum / rated : 0.0f;
        int year = static_cast<int>(Year());
        for (SlotEntry& e : g_networks[n].slots) {
            AiShow* a = e.aiShowId ? FindAiShow(e.aiShowId) : nullptr;
            if (!a || a->weeks < 4)
                continue;
            // Rookies get a short leash; veterans are kept until they fade. Each network's
            // patience differs a little from year to year.
            float rel = a->shareSum / a->weeks / std::max(mean, 0.001f);
            float leash = (a->season <= 1 ? 0.88f : 0.68f) + Roll(a->id, year, 404, -0.08f, 0.08f);
            // A show the audience has grown tired of ends its run, however it rates.
            if (a->freshness < 0.42f && a->season >= 3)
                spent.push_back({ &e, 0 });
            else if (rel < leash)
                weak.push_back({ &e, rel + a->freshness - 1.0f });
        }
        std::sort(weak.begin(), weak.end(), [](const Candidate& x, const Candidate& y) { return x.score < y.score; });
        weak.resize(std::min<size_t>(weak.size(), std::max(1, lineup / 4)));

        int cancelled = static_cast<int>(weak.size()), ended = static_cast<int>(spent.size());
        weak.insert(weak.end(), spent.begin(), spent.end());
        for (Candidate& c : weak) {
            RemoveAiShow(c.entry->aiShowId);
            c.entry->aiShowId = 0;
        }
        for (SlotEntry& e : g_networks[n].slots) {
            AiShow* a = e.aiShowId ? FindAiShow(e.aiShowId) : nullptr;
            if (!a)
                continue;
            a->season++;
            a->weeks = 0;
            a->shareSum = 0;
            a->freshness = std::max(0.3f, a->freshness - 0.03f);
        }
        premieres += cancelled + ended;
        summary += Format("%s%s cancels %d", summary.empty() ? "" : ", ", kNetworks[n].shortName, cancelled);
        if (ended)
            summary += Format(" and ends %d", ended);
    }
    EnsureRivalSchedules();
    Notify("The fall season begins", summary + Format(". %d new shows premiere this week.", premieres), kGold);
}

void CloseWeek()
{
    std::sort(g_thisWeek.begin(), g_thisWeek.end(), [](const ChartEntry& a, const ChartEntry& b) { return a.rating > b.rating; });
    if (!g_thisWeek.empty())
        g_lastWeek = g_thisWeek;
    g_thisWeek.clear();

    NetworkState& own = g_networks[kPlayerNetwork];
    if (!own.exists)
        return;
    own.lastWeekIncome = own.weekIncome;
    own.lastWeekCost = own.weekCost;
    own.weekIncome = 0;
    own.weekCost = 0;
    // Station costs and staff, paid weekly.
    float era = EraFactor(Year());
    double upkeep = (kReachUpkeep1950[own.reach] + (own.director ? kDirectorSalary : 0.0f)) * era * g_costScale;
    game::AddMoney(-upkeep);
    own.weekCost += upkeep;
}

} // namespace

// ---------------------------------------------------------------------------------
// Queries

const char* NetworkName(int network)
{
    if (network == kStreamingNetwork)
        return StreamingName();
    return network == kPlayerNetwork ? g_networks[kPlayerNetwork].name.c_str() : kNetworks[network].name;
}

const char* NetworkShort(int network)
{
    if (network == kStreamingNetwork)
        return StreamingShort();
    return network == kPlayerNetwork ? g_networks[kPlayerNetwork].shortName.c_str() : kNetworks[network].shortName;
}

// Rival networks grow from a few cities to the whole country by the early 1960s.
float NetworkReach(int network, float year)
{
    if (network == kPlayerNetwork)
        return kReachShare[g_networks[kPlayerNetwork].reach];
    float grown = std::clamp((year - kTelevisionYear) / 24.0f, 0.0f, 1.0f);
    return kNetworks[network].reachCap * (0.35f + 0.65f * grown);
}

std::string SlotName(int network, int slot)
{
    if (network == kStreamingNetwork)
        return Format("streaming on %s", StreamingName());
    if (slot < 0)
        return "no time slot yet";
    return Format("%ss at %s on %s", kNightNames[slot / kHours], kHourNames[slot % kHours], NetworkShort(network));
}

AiShow* FindAiShow(int id)
{
    for (AiShow& a : g_aiShows)
        if (a.id == id)
            return &a;
    return nullptr;
}

// ---------------------------------------------------------------------------------
// Schedules

void EnsureRivalSchedules()
{
    for (int n = 0; n < kRivalCount; n++) {
        NetworkState& ns = g_networks[n];
        if (!ns.exists) {
            ns.exists = true;
            // A brand-new schedule should not be purged by a fall season in the same year.
            float day = Today();
            int year = static_cast<int>(day / kDaysPerYear);
            if (g_lastFallYear < year)
                g_lastFallYear = (day - year * kDaysPerYear >= kFallDayOfYear) ? year : year - 1;
        }
        for (int i = 0; i < kSlots; i++) {
            SlotEntry& e = ns.slots[i];
            if (e.seriesId && !SeriesHoldsSlot(n, i, e.seriesId))
                e.seriesId = 0;
            if (e.aiShowId && !FindAiShow(e.aiShowId))
                e.aiShowId = 0;
            if (!e.seriesId && !e.aiShowId) {
                g_aiShows.push_back(MakeAiShow(n, i));
                e.aiShowId = g_aiShows.back().id;
            }
        }
    }
    NetworkState& own = g_networks[kPlayerNetwork];
    if (own.exists)
        for (int i = 0; i < kSlots; i++)
            if (own.slots[i].seriesId && !SeriesHoldsSlot(kPlayerNetwork, i, own.slots[i].seriesId))
                own.slots[i].seriesId = 0;
}

// The network drops its weakest show to make room, preferring the hour that suits the format.
void AssignSlot(Series& s)
{
    if (OwnOutlet(s.network))
        return;
    EnsureRivalSchedules();
    NetworkState& ns = g_networks[s.network];
    float fair = 1.0f / ActiveNetworks();
    int best = -1;
    float bestScore = 1e9f;
    for (int i = 0; i < kSlots; i++) {
        const SlotEntry& e = ns.slots[i];
        if (e.seriesId)
            continue;
        const AiShow* a = e.aiShowId ? FindAiShow(e.aiShowId) : nullptr;
        float strength = a ? (a->weeks ? a->shareSum / a->weeks : fair) * a->freshness : 0.0f;
        float score = strength + 0.04f * std::abs(i % kHours - kFormats[s.format].bestHour) +
                      Roll(s.id, i, 3, 0.0f, 0.02f);
        if (score < bestScore) {
            bestScore = score;
            best = i;
        }
    }
    if (best < 0)
        return;
    SlotEntry& e = ns.slots[best];
    if (const AiShow* a = FindAiShow(e.aiShowId))
        mod::Log("tv: %s drops '%s' for '%s' (%s)", NetworkShort(s.network), a->title.c_str(), s.title.c_str(),
                 SlotName(s.network, best).c_str());
    RemoveAiShow(e.aiShowId);
    e.aiShowId = 0;
    e.seriesId = s.id;
    s.slot = best;
}

void ReleaseSlot(Series& s)
{
    if (s.slot >= 0 && s.slot < kSlots && s.network < kNetworkSlots) {
        SlotEntry& e = g_networks[s.network].slots[s.slot];
        if (e.seriesId == s.id)
            e.seriesId = 0;   // rivals refill it with a new show of their own
    }
    s.slot = -1;
}

// Puts your network's unscheduled shows into free slots, best show first, each where the
// competition is weakest and the hour suits its format.
void AutoSchedule()
{
    NetworkState& own = g_networks[kPlayerNetwork];
    if (!own.exists)
        return;
    std::vector<Series*> waiting;
    for (Series& s : g_series)
        if (s.network == kPlayerNetwork && s.slot < 0 && (InProduction(s) || s.status == RenewalOffered))
            waiting.push_back(&s);
    std::sort(waiting.begin(), waiting.end(), [](const Series* a, const Series* b) { return a->pilotQuality > b->pilotQuality; });
    int placed = 0;
    for (Series* s : waiting) {
        int best = -1;
        float bestScore = 1e9f;
        for (int i = 0; i < kSlots; i++) {
            if (own.slots[i].seriesId)
                continue;
            float rival = 0;
            for (int n = 0; n < kRivalCount; n++)
                rival = std::max(rival, g_networks[n].lastRating[i]);
            float score = rival + 3.0f * std::abs(i % kHours - kFormats[s->format].bestHour);
            if (score < bestScore) {
                bestScore = score;
                best = i;
            }
        }
        if (best < 0)
            break;
        own.slots[best].seriesId = s->id;
        s->slot = best;
        placed++;
        mod::Log("tv: scheduled '%s' %s", s->title.c_str(), SlotName(kPlayerNetwork, best).c_str());
    }
    if (placed)
        Notify("Schedule updated", Format("%d show%s placed on %s.", placed, placed == 1 ? "" : "s", own.name.c_str()), kGrey);
}

// ---------------------------------------------------------------------------------
// Prime time

// Everything airing tonight competes hour by hour. A network's pull is its show's quality,
// the format's popularity, freshness, premieres and finales, the brand, its reach and the
// audience it inherits from the previous hour (lead-in). Shares split the viewers.
void AirNight(float day, int night)
{
    if (night == 0)
        CloseWeek();
    float year = Year();
    float era = EraFactor(year);
    int active = ActiveNetworks();
    float prevShare[kNetworkSlots] = {};

    for (int hour = 0; hour < kHours; hour++) {
        int slot = SlotIndex(night, hour);
        struct Offer {
            Series* series = nullptr;
            AiShow* ai = nullptr;
            float   quality = 0;
            float   weight = 0;
        } offers[kNetworkSlots];
        float total = 0;
        for (int n = 0; n < kNetworkSlots; n++) {
            NetworkState& ns = g_networks[n];
            if (!ns.exists)
                continue;
            Offer& o = offers[n];
            const SlotEntry& e = ns.slots[slot];
            Series* s = e.seriesId ? FindSeries(e.seriesId) : nullptr;
            bool airs = s && s->network == n && (s->status == Ordered || s->status == OnAir) && s->aired < s->order &&
                        day >= s->nextAirDay;
            int format = -1;
            float freshness = 1.0f, event = 1.0f;
            if (airs) {
                o.series = s;
                o.quality = EpisodeQuality(*s, s->aired + 1);
                format = s->format;
                freshness = s->freshness;
                event = EventBoost(*s);
            } else if (AiShow* a = e.aiShowId ? FindAiShow(e.aiShowId) : nullptr) {
                o.ai = a;
                o.quality = a->quality;
                format = a->format;
                freshness = a->freshness;
            } else {
                o.quality = 0.12f;   // reruns, old films, test pattern
            }
            float brand = n == kPlayerNetwork ? 1.0f : kNetworks[n].strength;
            float leadIn = hour == 0 ? 1.0f : std::clamp(1.0f + 0.35f * (prevShare[n] * active - 1.0f), 0.75f, 1.35f);
            float appeal = std::pow(o.quality, 1.3f) * (format >= 0 ? Appeal(format, year) : 0.6f) * freshness * event *
                           brand * NetworkReach(n, year) * leadIn *
                           Roll(static_cast<uint32_t>(day), n, slot, 0.9f, 1.1f);
            o.weight = std::pow(std::max(appeal, 0.001f), 1.5f);
            total += o.weight;
        }
        for (int n = 0; n < kNetworkSlots; n++) {
            NetworkState& ns = g_networks[n];
            if (!ns.exists)
                continue;
            Offer& o = offers[n];
            float share = total > 0 ? o.weight / total : 0;
            float rating = kPrimeTimeViewing * share * 100.0f;
            prevShare[n] = share;
            ns.lastShare[slot] = share;
            ns.lastRating[slot] = rating;
            if (o.series) {
                RecordAiring(*o.series, o.quality, rating, share);
                o.series->nextAirDay = day + 1.0f;
                g_thisWeek.push_back({ o.series->title, n, o.series->format, slot, rating, share, true });
            } else if (o.ai) {
                o.ai->weeks++;
                o.ai->shareSum += share;
                o.ai->lastRating = rating;
                o.ai->freshness = std::max(0.3f, o.ai->freshness - 0.0025f);
                g_thisWeek.push_back({ o.ai->title, n, o.ai->format, slot, rating, share, false });
            } else if (n == kPlayerNetwork) {
                // Filler still sells a little advertising, at bargain rates.
                double income = rating / 100.0 * Households(year) * 1e6 * kAdRatePerHome * 0.35 * era * g_incomeScale;
                game::AddMoney(income);
                ns.weekIncome += income;
            }
        }
    }
}

void NetworkDaily(float day)
{
    int year = static_cast<int>(day / kDaysPerYear);
    float dayOfYear = day - year * kDaysPerYear;
    if (g_networks[0].exists && dayOfYear >= kFallDayOfYear && g_lastFallYear < year) {
        g_lastFallYear = year;
        FallSeason();
    }
    // Monday: the Programming Director (or, on Casual, the studio) fills empty slots.
    if (static_cast<int>(day) % kNights == 0 && g_networks[kPlayerNetwork].exists &&
        (g_networks[kPlayerNetwork].director || g_level == Casual))
        AutoSchedule();
}

// ---------------------------------------------------------------------------------
// Your own network

bool CanFoundNetwork(std::string* why)
{
    float year = Year();
    double cost = kReachCost1950[ReachLocal] * EraFactor(year) * g_costScale;
    std::string reason;
    if (g_networks[kPlayerNetwork].exists)
        reason = "You already own a network.";
    else if (year < kOwnNetworkYear)
        reason = Format("Independent networks become possible in %d.", kOwnNetworkYear);
    else if (g_detectionWorks && g_hqs == 0)
        reason = "Build a Network HQ first.";
    else if (game::ReadMoney().capital < cost)
        reason = Format("Launching a local station costs %s.", Dollars(cost).c_str());
    if (why)
        *why = reason;
    return reason.empty();
}

void FoundNetwork(const std::string& name)
{
    NetworkState& own = g_networks[kPlayerNetwork];
    double cost = kReachCost1950[ReachLocal] * EraFactor(Year()) * g_costScale;
    game::AddMoney(-cost);
    own = NetworkState{};
    own.exists = true;
    own.name = name;
    own.reach = ReachLocal;
    // Call letters from the initials, e.g. "Pore Productions Television" -> "PPT".
    for (size_t i = 0; i < name.size() && own.shortName.size() < 3; i++)
        if ((i == 0 || name[i - 1] == ' ') && isalpha(static_cast<unsigned char>(name[i])))
            own.shortName += static_cast<char>(toupper(static_cast<unsigned char>(name[i])));
    if (own.shortName.size() < 2)
        own.shortName = "YOU";
    Notify(Format("%s goes on the air!", name.c_str()),
           Format("Your local station reaches %.0f%% of TV homes. Schedule shows in the Schedule tab.",
                  kReachShare[ReachLocal] * 100),
           kGold);
    mod::Log("tv: founded network '%s' (%s) for %.0f", name.c_str(), own.shortName.c_str(), cost);
}

bool CanUpgradeReach(std::string* why)
{
    const NetworkState& own = g_networks[kPlayerNetwork];
    std::string reason;
    if (!own.exists) {
        reason = "Found a network first.";
    } else if (own.reach >= ReachNational) {
        reason = "Your network already reaches the whole country.";
    } else {
        int next = own.reach + 1;
        double cost = kReachCost1950[next] * EraFactor(Year()) * g_costScale;
        if (g_detectionWorks && g_controlRooms == 0)
            reason = "Build a Broadcast Control Room to run a regional or national network.";
        else if (next == ReachNational && Year() < 1950)
            reason = "Coast-to-coast links arrive in 1950.";
        else if (game::ReadMoney().capital < cost)
            reason = Format("Going %s costs %s.", kReachNames[next], Dollars(cost).c_str());
    }
    if (why)
        *why = reason;
    return reason.empty();
}

void UpgradeReach()
{
    NetworkState& own = g_networks[kPlayerNetwork];
    int next = own.reach + 1;
    double cost = kReachCost1950[next] * EraFactor(Year()) * g_costScale;
    game::AddMoney(-cost);
    own.reach = next;
    Notify(Format("%s goes %s", own.name.c_str(), kReachNames[next]),
           Format("Your network now reaches %.0f%% of TV homes.", kReachShare[next] * 100), kGold);
}

// ---------------------------------------------------------------------------------
// Save data (section "tv_networks")

void NetworksReset()
{
    for (NetworkState& ns : g_networks)
        ns = NetworkState{};
    g_aiShows.clear();
    g_lastWeek.clear();
    g_thisWeek.clear();
    g_nextAiId = 1;
    g_lastFallYear = 0;
}

void NetworksSave(std::map<std::string, std::string>& out)
{
    out["next_ai_id"] = std::to_string(g_nextAiId);
    out["last_fall_year"] = std::to_string(g_lastFallYear);
    for (int n = 0; n < kNetworkSlots; n++) {
        const NetworkState& ns = g_networks[n];
        std::string p = Format("n%d.", n);
        out[p + "exists"] = ns.exists ? "1" : "0";
        if (!ns.exists)
            continue;
        std::string slots, ratings;
        for (int i = 0; i < kSlots; i++) {
            const SlotEntry& e = ns.slots[i];
            slots += i ? "," : "";
            slots += e.seriesId ? Format("s%d", e.seriesId) : e.aiShowId ? Format("a%d", e.aiShowId) : "-";
            ratings += Format(i ? ",%.2f/%.3f" : "%.2f/%.3f", ns.lastRating[i], ns.lastShare[i]);
        }
        out[p + "slots"] = slots;
        out[p + "last"] = ratings;
        if (n == kPlayerNetwork) {
            out[p + "name"] = ns.name;
            out[p + "short"] = ns.shortName;
            out[p + "reach"] = std::to_string(ns.reach);
            out[p + "director"] = ns.director ? "1" : "0";
        }
    }
    out["ai.count"] = std::to_string(g_aiShows.size());
    for (size_t i = 0; i < g_aiShows.size(); i++) {
        const AiShow& a = g_aiShows[i];
        out[Format("ai%zu", i)] = Format("%d|%d|%d|%.4f|%.4f|%d|%d|%.4f|%.2f|", a.id, a.network, a.format, a.quality,
                                         a.freshness, a.season, a.weeks, a.shareSum, a.lastRating) + a.title;
    }
}

void NetworksLoad(const std::map<std::string, std::string>& in)
{
    NetworksReset();
    auto get = [&](const std::string& k) {
        auto it = in.find(k);
        return it == in.end() ? std::string() : it->second;
    };
    g_nextAiId = std::max(1, atoi(get("next_ai_id").c_str()));
    g_lastFallYear = atoi(get("last_fall_year").c_str());
    int count = atoi(get("ai.count").c_str());
    for (int i = 0; i < count; i++) {
        std::string v = get(Format("ai%d", i));
        AiShow a;
        int consumed = 0;
        if (sscanf_s(v.c_str(), "%d|%d|%d|%f|%f|%d|%d|%f|%f|%n", &a.id, &a.network, &a.format, &a.quality,
                     &a.freshness, &a.season, &a.weeks, &a.shareSum, &a.lastRating, &consumed) < 9 || !consumed)
            continue;
        a.title = v.substr(consumed);
        a.network = std::clamp(a.network, 0, kRivalCount - 1);
        a.format = std::clamp(a.format, 0, FormatCount - 1);
        g_aiShows.push_back(a);
    }
    for (int n = 0; n < kNetworkSlots; n++) {
        NetworkState& ns = g_networks[n];
        std::string p = Format("n%d.", n);
        ns.exists = get(p + "exists") == "1";
        if (!ns.exists)
            continue;
        std::string slots = get(p + "slots"), ratings = get(p + "last");
        size_t pos = 0, rpos = 0;
        for (int i = 0; i < kSlots; i++) {
            size_t comma = slots.find(',', pos);
            std::string code = slots.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
            pos = comma == std::string::npos ? slots.size() : comma + 1;
            if (code.size() > 1 && code[0] == 's')
                ns.slots[i].seriesId = atoi(code.c_str() + 1);
            else if (code.size() > 1 && code[0] == 'a')
                ns.slots[i].aiShowId = atoi(code.c_str() + 1);
            size_t rcomma = ratings.find(',', rpos);
            std::string r = ratings.substr(rpos, rcomma == std::string::npos ? std::string::npos : rcomma - rpos);
            rpos = rcomma == std::string::npos ? ratings.size() : rcomma + 1;
            sscanf_s(r.c_str(), "%f/%f", &ns.lastRating[i], &ns.lastShare[i]);
        }
        if (n == kPlayerNetwork) {
            ns.name = get(p + "name");
            ns.shortName = get(p + "short");
            ns.reach = std::clamp(atoi(get(p + "reach").c_str()), 0, ReachCount - 1);
            ns.director = get(p + "director") == "1";
        }
    }
    mod::Log("tv: loaded networks - %zu rival shows, own network %s", g_aiShows.size(),
             g_networks[kPlayerNetwork].exists ? g_networks[kPlayerNetwork].name.c_str() : "none");
}

} // namespace tv
