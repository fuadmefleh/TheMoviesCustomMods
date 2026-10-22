// Streaming Wars (Phase 5): your own streaming service from 2007. Subscribers come and go
// with your content, your price and your reliability; rival streamers compete for the
// same homes and bid for your library. Originals are released weekly or all at once.

#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <cmath>

#include "mod.h"
#include "tv_internal.h"

namespace tv {

const StreamerInfo kStreamers[kStreamerCount] = {
    { "Flixnet", "FLX", 2007, 1.0f },
    { "Primereel", "PRM", 2011, 0.8f },
    { "Kingdom+", "KNG", 2019, 1.15f },
};

const RegionInfo kRegions[kRegionCount] = {
    { "United States", 1.00f, 1.00f, 0 },
    { "Canada and the UK", 0.35f, 0.90f, 4000000 },
    { "Europe", 0.90f, 0.80f, 12000000 },
    { "Latin America", 0.60f, 0.45f, 8000000 },
    { "Asia-Pacific", 1.20f, 0.50f, 20000000 },
};

namespace {

struct Service {
    bool        exists = false;
    std::string name, shortName;
    float       founded = 0;
    double      subs = 0;          // millions
    float       price = 7.99f;     // per month, in the day's dollars
    int         recLevel = 0;      // recommendation engine, 0..3
    int         regions = 1;       // bit per kRegions entry; the US is always on
    float       buzz = 0;          // recent releases
    int         hangover = 0;      // weeks of extra churn after a binge drop
    int         outageDays = 0;
    int         outages = 0;
    double      lastRevenue = 0, lastCost = 0, lastSignups = 0, lastChurn = 0;
    std::vector<float> history;    // weekly subscribers, newest last
} g_svc;

char g_svcName[48] = {};

constexpr float kLaunchCost1950     = 5000000.0f;
constexpr float kStaffWeekly1950    = 40000.0f;
constexpr float kRecCost1950[3]     = { 1500000.0f, 4000000.0f, 9000000.0f };
const char* const kRecNames[4]      = { "None", "Basic (\"Because you watched\")", "Personalised rows", "Machine learning" };

float Lookup(const float (*t)[2], int n, float year)
{
    if (year <= t[0][0])
        return t[0][1];
    for (int i = 1; i < n; i++)
        if (year <= t[i][0])
            return t[i - 1][1] + (year - t[i - 1][0]) / (t[i][0] - t[i - 1][0]) * (t[i][1] - t[i - 1][1]);
    return t[n - 1][1];
}

// Homes that could stream (millions, US) and how many services each one pays for.
float BroadbandHomes(float year)
{
    static const float t[][2] = { { 2005, 35 }, { 2007, 50 }, { 2010, 70 }, { 2015, 90 }, { 2020, 105 }, { 2030, 120 } };
    return Lookup(t, 6, year);
}
float StreamingAdoption(float year)
{
    static const float t[][2] = { { 2007, 0.05f }, { 2010, 0.2f }, { 2015, 0.5f }, { 2020, 0.8f }, { 2030, 0.9f } };
    return Lookup(t, 5, year);
}
float ServicesPerHome(float year)
{
    static const float t[][2] = { { 2007, 1.0f }, { 2010, 1.1f }, { 2015, 1.6f }, { 2020, 2.5f }, { 2030, 3.0f } };
    return Lookup(t, 5, year);
}
float FairPrice(float year)
{
    static const float t[][2] = { { 2007, 8.0f }, { 2015, 10.0f }, { 2020, 14.0f }, { 2030, 18.0f } };
    return Lookup(t, 4, year);
}
// What rival streamers pay to license old shows: a boom in the 2010s, then everyone
// keeps their own.
float LicenseMarket(float year)
{
    static const float t[][2] = { { 2007, 0.8f }, { 2012, 2.5f }, { 2019, 2.5f }, { 2025, 1.0f }, { 2030, 0.8f } };
    return Lookup(t, 5, year);
}

float Potential(float year)   // millions of subscriptions in your regions
{
    float regions = 0;
    for (int r = 0; r < kRegionCount; r++)
        if (g_svc.regions & (1 << r))
            regions += kRegions[r].potential;
    return BroadbandHomes(year) * StreamingAdoption(year) * ServicesPerHome(year) * regions;
}

float BlendedPrice()
{
    float pot = 0, paid = 0;
    for (int r = 0; r < kRegionCount; r++)
        if (g_svc.regions & (1 << r)) {
            pot += kRegions[r].potential;
            paid += kRegions[r].potential * kRegions[r].priceFactor;
        }
    return pot > 0 ? g_svc.price * paid / pot : g_svc.price;
}

float Capacity(float year)   // millions of subscribers the servers can carry
{
    float perCentre = 4.0f + std::max(0.0f, year - kStreamingYear) * 1.5f;
    return (2.0f + g_datacenters * perCentre) * (HasTech(TechCDN) ? 1.5f : 1.0f);   // 2 million on rented servers
}

float TitleAppeal(const Series& s)
{
    float pop = s.totalAired ? std::clamp(static_cast<float>(s.ratingTotal / s.totalAired) / 15.0f, 0.4f, 2.0f) : 0.6f;
    float package = std::clamp(std::pow(std::max(1, s.totalAired) / 65.0f, 0.8f), 0.3f, 1.4f);
    static const float evergreen[FormatCount] = { 1.3f, 1.2f, 0.9f, 0.5f, 1.1f, 0.15f, 0.3f, 0.4f, 0.6f, 1.0f };
    return evergreen[s.format] * pop * package * (1.0f - s.wear);
}

// What your service has to watch: library titles and originals.
float ContentScore()
{
    float score = 0;
    for (const LibraryDeal& d : g_deals)
        if (d.kind == RerunStream)
            if (const Series* s = FindSeries(d.seriesId))
                score += TitleAppeal(*s);
    float now = Today();
    for (const Series& s : g_series) {
        if (s.network != kStreamingNetwork || s.totalAired == 0)
            continue;
        float q = s.lastQuality > 0 ? s.lastQuality : s.qualitySum / std::max(1, s.aired);
        float age = std::max(0.0f, (now - s.lastReleaseDay) / kDaysPerYear);
        score += q * 2.5f * std::sqrt(static_cast<float>(std::max(1, s.season))) * std::max(0.3f, 1.0f - 0.25f * age);
    }
    return score;
}

float RivalScore(int i, float year)
{
    const StreamerInfo& r = kStreamers[i];
    if (year < r.launch)
        return 0.0f;
    float grown = std::clamp((year - r.launch) / 6.0f, 0.1f, 1.0f);
    float originals = 1.0f + std::max(0.0f, year - 2013.0f) * 0.1f;
    float licensed = 0;
    for (const LibraryDeal& d : g_deals)
        if (d.kind == RerunStreamLicense && d.buyer == i)
            if (const Series* s = FindSeries(d.seriesId))
                licensed += TitleAppeal(*s);
    return r.strength * 12.0f * grown * originals + licensed;
}

float OurAppeal(float year)
{
    float content = std::pow(std::max(0.05f, ContentScore()), 0.8f);
    float price = std::pow(FairPrice(year) / std::max(1.0f, g_svc.price), 1.2f);
    float rec = 1.0f + 0.08f * g_svc.recLevel;
    float outage = g_svc.outageDays > 0 ? 0.7f : 1.0f;
    return content * price * rec * outage * (1.0f + g_svc.buzz);
}

void ReleaseEpisode(Series& s, float day)
{
    float q = EpisodeQuality(s, s.aired + 1);
    float year = Year();
    // "Rating" for a streamer: the share of subscribers who watch the episode.
    float watch = std::clamp(0.08f + 0.5f * std::pow(q, 1.3f) * Appeal(s.format, year) * s.freshness * EventBoost(s), 0.02f, 0.9f);
    RecordAiring(s, q, watch * 100.0f, watch);
    s.lastReleaseDay = day;
    g_svc.buzz += (s.binge ? 0.06f : 0.02f) * q;
}

} // namespace

bool StreamingExists() { return g_svc.exists; }

double OutletValue()
{
    double streaming = g_svc.exists ? g_svc.subs * 1e6 * BlendedPrice() * 12.0 * 3.0 : 0.0;
    return CableValue() + streaming;
}
const char* StreamingName() { return g_svc.name.c_str(); }
const char* StreamingShort() { return g_svc.shortName.c_str(); }
float StreamingSubscribers() { return static_cast<float>(g_svc.subs); }
bool StreamerActive(int i) { return Year() >= kStreamers[i].launch; }

double StreamLicenseOffer(const Series& s, int streamer, int years)
{
    return LocalWeekly(s) * 52.0 * years * LicenseMarket(Year()) * kStreamers[streamer].strength;
}

void LicenseToStreamer(Series& s, int streamer, int years)
{
    LibraryDeal d;
    d.seriesId = s.id;
    d.kind = RerunStreamLicense;
    d.buyer = streamer;
    d.start = Today();
    d.end = d.start + years * kDaysPerYear;
    d.upfront = StreamLicenseOffer(s, streamer, years);
    g_deals.push_back(d);
    PayReruns(s, d.upfront);
    Notify(Format("%s licenses '%s'", kStreamers[streamer].name, s.title.c_str()),
           Format("%s up front for %d year%s of streaming.", Dollars(d.upfront).c_str(), years, years == 1 ? "" : "s"),
           kGold);
}

void SetOnStreaming(Series& s, bool on)
{
    LibraryDeal* d = ActiveDeal(s.id);
    if (on && !d && g_svc.exists) {
        LibraryDeal nd;
        nd.seriesId = s.id;
        nd.kind = RerunStream;
        nd.start = Today();
        nd.end = 1e9f;
        g_deals.push_back(nd);
    } else if (!on && d && d->kind == RerunStream) {
        g_deals.erase(g_deals.begin() + (d - g_deals.data()));
    }
}

// Releases originals every day; the service's books once a week.
void StreamingDaily(float day)
{
    if (!g_svc.exists)
        return;
    for (Series& s : g_series) {
        if (s.network != kStreamingNetwork || !(s.status == Ordered || s.status == OnAir) || day < s.nextAirDay)
            continue;
        if (s.binge) {
            while (s.aired < s.order)
                ReleaseEpisode(s, day);
            g_svc.hangover = 4;
            Notify(Format("All of '%s' is streaming", s.title.c_str()),
                   Format("%d episodes dropped at once on %s.", s.order, g_svc.name.c_str()), kGold);
        } else {
            ReleaseEpisode(s, day);
            s.nextAirDay = day + kNights;
        }
    }
    if (g_svc.outageDays > 0)
        g_svc.outageDays--;
    if (static_cast<int>(day) % kNights != 0)
        return;

    float year = Year();
    float ours = OurAppeal(year);
    float rivals = 0;
    for (int i = 0; i < kStreamerCount; i++)
        rivals += RivalScore(i, year) > 0 ? std::pow(RivalScore(i, year), 0.8f) : 0.0f;
    double target = Potential(year) * ours / (ours + rivals + 0.001f);
    double before = g_svc.subs;
    float churnMonthly = 0.04f - 0.005f * g_svc.recLevel + (g_svc.hangover > 0 ? 0.04f : 0.0f) +
                         std::max(0.0f, g_svc.price / FairPrice(year) - 1.0f) * 0.03f;
    // Too many viewers for the servers: the service goes down now and then.
    float load = static_cast<float>(g_svc.subs) / Capacity(year);
    if (load > 1.0f && g_svc.outageDays == 0 &&
        Roll(static_cast<uint32_t>(day), 911, g_svc.outages, 0.0f, 1.0f) < std::clamp((load - 1.0f) * 0.8f, 0.05f, 0.6f)) {
        g_svc.outageDays = 1 + static_cast<int>(Roll(static_cast<uint32_t>(day), 912, 0, 0.0f, 2.99f));
        g_svc.outages++;
        churnMonthly += 0.12f;
        Notify(Format("%s is down", g_svc.name.c_str()),
               Format("Too many viewers for your servers (%.0f%% of capacity). Build another Data Center.", load * 100),
               kRed);
    }
    double churn = g_svc.subs * std::max(0.005f, churnMonthly) * (HasTech(TechRecAI) ? 0.7f : 1.0f) / 4.33;
    double signups = std::max(0.0, (target - g_svc.subs) * 0.08) + churn;
    g_svc.subs = std::max(0.0, g_svc.subs + signups - churn);
    g_svc.lastSignups = signups;
    g_svc.lastChurn = churn;
    g_svc.buzz *= 0.85f;
    if (g_svc.hangover > 0)
        g_svc.hangover--;

    double revenue = g_svc.subs * 1e6 * BlendedPrice() / 4.33 * g_incomeScale;
    // Delivery, marketing and the licensed catalogue eat about half of every subscription.
    double cost = (kStaffWeekly1950 * EraFactor(year) + g_svc.subs * 1e6 * (0.25 + 0.5 * BlendedPrice()) / 4.33) * g_costScale;
    game::AddMoney(revenue - cost);
    g_svc.lastRevenue = revenue;
    g_svc.lastCost = cost;
    g_svc.history.push_back(static_cast<float>(g_svc.subs));
    if (g_svc.history.size() > 156)
        g_svc.history.erase(g_svc.history.begin());
    if (std::floor(before / 10.0) < std::floor(g_svc.subs / 10.0) && g_svc.subs >= 10.0)
        Notify(Format("%s passes %.0f million subscribers", g_svc.name.c_str(), std::floor(g_svc.subs / 10.0) * 10),
               "The streaming wars are yours to lose.", kGold);
}

bool CanLaunchStreaming(std::string* why)
{
    std::string reason;
    float year = Year();
    if (StreamingExists())
        reason = Format("You already run %s.", StreamingName());
    else if (year < kStreamingYear)
        reason = Format("Broadband homes can't stream video until %d.", kStreamingYear);
    else if (game::ReadMoney().capital < kLaunchCost1950 * EraFactor(year) * g_costScale)
        reason = "Not enough money.";
    if (why)
        *why = reason;
    return reason.empty();
}

void LaunchStreaming(const std::string& name)
{
    float year = Year();
    double cost = kLaunchCost1950 * EraFactor(year) * g_costScale;
    game::AddMoney(-cost);
    g_svc = Service{};
    g_svc.exists = true;
    g_svc.name = name;
    for (char c : name)
        if (isupper(static_cast<unsigned char>(c)) && g_svc.shortName.size() < 4)
            g_svc.shortName += c;
    if (g_svc.shortName.size() < 2)
        g_svc.shortName = name.substr(0, 4);
    g_svc.founded = Today();
    g_svc.price = std::floor(FairPrice(year)) - 0.01f;
    Notify(Format("%s is live", name.c_str()),
           "Put library shows on it and make originals. Subscribers follow the content.", kGold);
    mod::Log("tv: streaming service '%s' launched for %.0f", name.c_str(), cost);
}

// ---------------------------------------------------------------------------------
// Streaming tab

void DrawStreaming()
{
    float year = Year();
    if (!g_svc.exists) {
        if (year < kStreamingYear) {
            ImGui::TextWrapped("Streaming arrives in %d, once enough homes have broadband. Until then, build up your "
                               "library: every show you own will be worth streaming.",
                               kStreamingYear);
            return;
        }
        ImGui::TextWrapped("Launch your own streaming service. Subscribers pay every month; they come for your "
                           "content and leave when it runs dry, when the price is too high or when the service keeps "
                           "falling over.");
        std::string why;
        bool can = CanLaunchStreaming(&why);
        ImGui::SetNextItemWidth(220);
        ImGui::InputTextWithHint("##svc", "Service name", g_svcName, sizeof(g_svcName));
        ImGui::SameLine();
        ImGui::BeginDisabled(!can || !g_svcName[0]);
        if (ImGui::Button(Format("Launch (%s)", Dollars(kLaunchCost1950 * EraFactor(year) * g_costScale).c_str()).c_str()))
            LaunchStreaming(g_svcName);
        ImGui::EndDisabled();
        if (!can)
            ImGui::TextColored(kYellow, "%s", why.c_str());
    } else {
        ImGui::TextColored(kGold, "%s", g_svc.name.c_str());
        ImGui::SameLine(0, 20);
        ImGui::Text("%.2f million subscribers", g_svc.subs);
        ImGui::SameLine(0, 20);
        ImGui::TextDisabled("+%.2f / -%.2f million last week", g_svc.lastSignups, g_svc.lastChurn);
        if (g_svc.history.size() > 1)
            ImGui::PlotLines("##subs", g_svc.history.data(), static_cast<int>(g_svc.history.size()), 0, "Subscribers",
                             0.0f, FLT_MAX, ImVec2(-1, 70));
        ImGui::Text("Last week: revenue %s, costs %s", Dollars(g_svc.lastRevenue).c_str(), Dollars(g_svc.lastCost).c_str());

        ImGui::SetNextItemWidth(200);
        ImGui::SliderFloat("Monthly price", &g_svc.price, 2.99f, 29.99f, "$%.2f");
        HelpMarker(Format("Typical for the time: $%.2f. Cheaper brings more subscribers, dearer earns more from each "
                          "but more of them leave.",
                          FairPrice(year))
                       .c_str());

        float cap = Capacity(year);
        float load = static_cast<float>(g_svc.subs) / cap;
        ImGui::TextColored(load > 1.0f ? kRed : load > 0.8f ? kYellow : kGreen,
                           "Servers: %.0f%% of capacity (%.1f million)%s", load * 100, cap,
                           g_svc.outageDays > 0 ? " - DOWN RIGHT NOW" : "");
        HelpMarker("Each Data Center carries more subscribers (more every year as hardware improves). Over capacity, "
                   "the service goes down now and then and subscribers leave.");
        if (g_svc.outages)
            ImGui::SameLine(), ImGui::TextDisabled("%d outage%s so far", g_svc.outages, g_svc.outages == 1 ? "" : "s");

        ImGui::Text("Recommendations: %s", kRecNames[g_svc.recLevel]);
        if (g_svc.recLevel < 3) {
            double c = kRecCost1950[g_svc.recLevel] * EraFactor(year) * g_costScale;
            ImGui::SameLine();
            ImGui::BeginDisabled(game::ReadMoney().capital < c);
            if (ImGui::Button(Format("Upgrade (%s)", Dollars(c).c_str()).c_str())) {
                game::AddMoney(-c);
                g_svc.recLevel++;
                Notify("Recommendations upgraded", "Subscribers find more to watch, and fewer of them leave.", kGreen);
            }
            ImGui::EndDisabled();
        }

        ImGui::SeparatorText("Regions");
        for (int r = 0; r < kRegionCount; r++) {
            if (g_svc.regions & (1 << r)) {
                ImGui::TextColored(kGreen, "%s", kRegions[r].name);
                continue;
            }
            double c = kRegions[r].cost1950 * EraFactor(year) * g_costScale;
            ImGui::TextDisabled("%s", kRegions[r].name);
            ImGui::SameLine(200);
            ImGui::BeginDisabled(game::ReadMoney().capital < c);
            if (ImGui::Button(Format("Launch there (%s)##r%d", Dollars(c).c_str(), r).c_str())) {
                game::AddMoney(-c);
                g_svc.regions |= 1 << r;
                Notify(Format("%s goes international", g_svc.name.c_str()),
                       Format("Now streaming in %s.", kRegions[r].name), kGold);
            }
            ImGui::EndDisabled();
        }

        ImGui::SeparatorText("What's on");
        int library = 0, originals = 0;
        for (const LibraryDeal& d : g_deals)
            library += d.kind == RerunStream;
        for (const Series& s : g_series)
            originals += s.network == kStreamingNetwork;
        ImGui::Text("%d library show%s, %d original%s. Content score %.1f", library, library == 1 ? "" : "s", originals,
                    originals == 1 ? "" : "s", ContentScore());
        HelpMarker("Add library shows from the Library tab. Make originals in 'Pitch a Pilot' by choosing your "
                   "service as the network.");
    }

    if (year >= kStreamingYear) {
        ImGui::SeparatorText("Rival streamers");
        for (int i = 0; i < kStreamerCount; i++) {
            if (!StreamerActive(i)) {
                ImGui::TextDisabled("%s launches in %d", kStreamers[i].name, kStreamers[i].launch);
                continue;
            }
            ImGui::Text("%s", kStreamers[i].name);
            ImGui::SameLine(160);
            ImGui::TextDisabled("content score %.1f", RivalScore(i, year));
        }
        ImGui::TextDisabled("Rival streamers pay well to license your old shows (see the Library tab), most of all "
                            "in the 2010s.");
    }
}

// ---------------------------------------------------------------------------------
// Save data

void StreamingSave(std::map<std::string, std::string>& out)
{
    out["exists"] = g_svc.exists ? "1" : "0";
    out["name"] = g_svc.name;
    out["short"] = g_svc.shortName;
    out["state"] = Format("%.3f,%.5f,%.2f,%d,%d,%.4f,%d,%d,%d", g_svc.founded, g_svc.subs, g_svc.price, g_svc.recLevel,
                          g_svc.regions, g_svc.buzz, g_svc.hangover, g_svc.outageDays, g_svc.outages);
    std::string h;
    for (float v : g_svc.history)
        h += Format(h.empty() ? "%.3f" : ",%.3f", v);
    out["history"] = h;
}

void StreamingReset() { g_svc = Service{}; }

void StreamingLoad(const std::map<std::string, std::string>& in)
{
    StreamingReset();
    auto get = [&](const char* k) {
        auto it = in.find(k);
        return it == in.end() ? std::string() : it->second;
    };
    g_svc.exists = get("exists") == "1";
    g_svc.name = get("name");
    g_svc.shortName = get("short");
    sscanf_s(get("state").c_str(), "%f,%lf,%f,%d,%d,%f,%d,%d,%d", &g_svc.founded, &g_svc.subs, &g_svc.price,
             &g_svc.recLevel, &g_svc.regions, &g_svc.buzz, &g_svc.hangover, &g_svc.outageDays, &g_svc.outages);
    g_svc.recLevel = std::clamp(g_svc.recLevel, 0, 3);
    g_svc.regions |= 1;
    std::string h = get("history");
    for (size_t pos = 0; pos < h.size();) {
        size_t comma = h.find(',', pos);
        g_svc.history.push_back(static_cast<float>(atof(h.substr(pos, comma - pos).c_str())));
        pos = comma == std::string::npos ? h.size() : comma + 1;
    }
}

} // namespace tv
