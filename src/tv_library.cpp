// The library (Phase 4): every show with enough episodes can be sold as reruns. Local
// stations pay weekly, a network pays up front for exclusive rights, or your own cable
// channel airs them. Reruns wear a show out; time in the vault (faster with a Media
// Archive) freshens it. From 1960 cast and writers get residuals.

#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <cmath>

#include "mod.h"
#include "tv_internal.h"

namespace tv {

std::vector<LibraryDeal> g_deals;

namespace {

struct CableChannel {
    bool        exists = false;
    std::string name;
    float       founded = 0;
    float       reach = 0;            // share of cable homes carrying it
    double      lastWeekIncome = 0, lastWeekCost = 0;
} g_cable;

int  g_libSelected = -1;
char g_cableName[48] = {};

const char* const kRerunDealNames[RerunDealCount] = { "Local stations", "Exclusive", "Your cable channel",
                                                      "Your streaming service", "Streaming license" };

constexpr float kCableCost1950   = 1200000.0f;
constexpr float kCableUpkeep1950 = 6000.0f;

float Lookup(const float (*t)[2], int n, float year)
{
    if (year <= t[0][0])
        return t[0][1];
    for (int i = 1; i < n; i++)
        if (year <= t[i][0])
            return t[i - 1][1] + (year - t[i - 1][0]) / (t[i][0] - t[i - 1][0]) * (t[i][1] - t[i - 1][1]);
    return t[n - 1][1];
}

// How well a format stands up to being watched again.
float Evergreen(int format)
{
    static const float e[FormatCount] = { 1.3f, 1.2f, 0.8f, 0.5f, 1.1f, 0.15f, 0.3f, 0.3f, 0.5f, 0.9f };
    return e[format];
}

const char* EvergreenWord(int format)
{
    float e = Evergreen(format);
    return e >= 1.2f ? "Evergreen" : e >= 0.8f ? "Good" : e >= 0.4f ? "Fair" : "Poor";
}

// The appetite of stations (and later cable channels) for reruns.
float RerunMarket(float year)
{
    static const float t[][2] = { { 1946, 0.4f }, { 1955, 0.8f }, { 1965, 1.0f }, { 1975, 1.2f }, { 1985, 1.7f },
                                  { 1995, 2.0f }, { 2005, 1.6f }, { 2015, 1.2f }, { 2030, 1.0f } };
    return Lookup(t, 9, year);
}

float CableHomes(float year)   // millions
{
    static const float t[][2] = { { 1975, 9.8f }, { 1980, 17.7f }, { 1985, 36.0f }, { 1990, 50.0f },
                                  { 2000, 68.0f }, { 2010, 61.0f }, { 2020, 45.0f }, { 2030, 35.0f } };
    return Lookup(t, 8, year);
}

float Popularity(const Series& s)
{
    float avg = s.totalAired ? static_cast<float>(s.ratingTotal / s.totalAired) : s.bestRating * 0.8f;
    return std::clamp(avg / 15.0f, 0.4f, 2.0f);
}

float PackageSize(const Series& s)
{
    return std::clamp(std::pow(s.totalAired / 65.0f, 0.8f), 0.3f, 1.4f);
}

// A title's pull on a cable channel.
float CableAppeal(const Series& s)
{
    return Evergreen(s.format) * (s.animated ? kAnimatedReruns : 1.0f) * Popularity(s) * PackageSize(s) * (1.0f - s.wear);
}

bool FirstRunOnAir(const Series& s) { return InProduction(s) || s.status == RenewalOffered; }

} // namespace

void PayReruns(Series& s, double gross)
{
    double residual = gross * ResidualRate(Year());
    game::AddMoney(gross - residual);
    s.syndIncome += gross - residual;
    s.residuals += residual;
}

namespace {

std::string Years(float days) { return Format("%.1f years", days / kDaysPerYear); }

} // namespace

bool InLibrary(const Series& s)
{
    return s.totalAired >= kMinPackage || (s.format == Miniseries && s.status == Ended && s.totalAired > 0);
}

bool OwnsReruns(const Series& s) { return s.deal != DealNetworkOwned; }

LibraryDeal* ActiveDeal(int seriesId)
{
    for (LibraryDeal& d : g_deals)
        if (d.seriesId == seriesId)
            return &d;
    return nullptr;
}

float ResidualRate(float year) { return year < 1960 ? 0.0f : year < 1980 ? 0.10f : 0.14f; }

double LocalWeekly(const Series& s)
{
    float year = Year();
    double perAiring = kFormats[s.format].fee1950 * 0.06;
    double v = perAiring * 5 * Evergreen(s.format) * (s.animated ? kAnimatedReruns : 1.0f) * Popularity(s) * PackageSize(s) * (1.0f - s.wear) *
               (g_archives > 0 ? 1.25 : 1.0) * (HasTech(TechSalesTeam) ? 1.15 : 1.0) * RerunMarket(year);
    // Stations only have so many afternoon slots: the more of your shows they already run,
    // the less they pay for another.
    int running = 0;
    for (const LibraryDeal& d : g_deals)
        running += d.kind == RerunLocal && d.seriesId != s.id;
    return v * EraFactor(year) * g_incomeScale * 6.0 / (6.0 + running);
}

double ExclusiveOffer(const Series& s, int network, int years)
{
    const NetworkInfo& n = kNetworks[network];
    double taste = 1.0 + n.taste[s.format] / 40.0;
    return LocalWeekly(s) * 52.0 * years * 0.9 * taste * n.feeMul;
}

void SellLocal(Series& s, int years)
{
    LibraryDeal d;
    d.seriesId = s.id;
    d.kind = RerunLocal;
    d.start = Today();
    d.end = d.start + years * kDaysPerYear;
    d.weekly = LocalWeekly(s);
    g_deals.push_back(d);
    Notify(Format("'%s' in syndication", s.title.c_str()),
           Format("Local stations will pay %s a week for %d year%s of reruns.", Dollars(d.weekly).c_str(), years,
                  years == 1 ? "" : "s"),
           kGreen);
}

void SellExclusive(Series& s, int network, int years)
{
    LibraryDeal d;
    d.seriesId = s.id;
    d.kind = RerunExclusive;
    d.buyer = network;
    d.start = Today();
    d.end = d.start + years * kDaysPerYear;
    d.upfront = ExclusiveOffer(s, network, years);
    g_deals.push_back(d);
    PayReruns(s, d.upfront);
    Notify(Format("%s buys '%s'", kNetworks[network].shortName, s.title.c_str()),
           Format("%s up front for exclusive reruns, %d year%s.", Dollars(d.upfront).c_str(), years, years == 1 ? "" : "s"),
           kGold);
}

void SetOnCable(Series& s, bool on)
{
    LibraryDeal* d = ActiveDeal(s.id);
    if (on && !d && g_cable.exists) {
        LibraryDeal nd;
        nd.seriesId = s.id;
        nd.kind = RerunCable;
        nd.start = Today();
        nd.end = 1e9f;
        g_deals.push_back(nd);
    } else if (!on && d && d->kind == RerunCable) {
        g_deals.erase(g_deals.begin() + (d - g_deals.data()));
    }
}

double CableValue()
{
    return g_cable.exists ? std::max(0.0, g_cable.lastWeekIncome - g_cable.lastWeekCost) * 52.0 * 8.0 +
                                kCableCost1950 * EraFactor(Year())
                          : 0.0;
}

bool CanLaunchCable(std::string* why)
{
    std::string reason;
    if (g_cable.exists)
        reason = Format("You already run %s.", g_cable.name.c_str());
    else if (Year() < (HasTech(TechSatellite) ? 1972 : kCableYear))
        reason = Format("Cable channels arrive with satellite distribution in %d (1972 with your own Satellite Uplink).",
                        kCableYear);
    else if (g_controlRooms == 0 && g_detectionWorks)
        reason = "You need a Broadcast Control Room to uplink a channel.";
    else if (game::ReadMoney().capital < kCableCost1950 * EraFactor(Year()) * g_costScale)
        reason = "Not enough money.";
    if (why)
        *why = reason;
    return reason.empty();
}

void LaunchCable(const std::string& name)
{
    double cost = kCableCost1950 * EraFactor(Year()) * g_costScale;
    game::AddMoney(-cost);
    g_cable.exists = true;
    g_cable.name = name;
    g_cable.founded = Today();
    Notify(Format("%s launches", name.c_str()),
           "Your cable channel is on the air. Fill it with reruns from your library.", kGold);
    mod::Log("tv: cable channel '%s' launched for %.0f", name.c_str(), cost);
}

// Every Monday: rerun money, wear and recovery, expiring deals, the cable channel.
void LibraryWeekly(float day)
{
    if (static_cast<int>(day) % kNights != 0)
        return;
    float year = Year();
    for (size_t i = 0; i < g_deals.size();) {
        LibraryDeal& d = g_deals[i];
        Series* s = FindSeries(d.seriesId);
        if (!s || (d.kind != RerunCable && d.kind != RerunStream && day >= d.end)) {
            if (s)
                Notify(Format("Rerun rights to '%s' are back", s->title.c_str()),
                       "The show rests in your vault until you sell it again.", kGrey);
            g_deals.erase(g_deals.begin() + i);
            continue;
        }
        if (d.kind == RerunLocal)
            PayReruns(*s, d.weekly);
        static const float wearPerYear[RerunDealCount] = { 0.10f, 0.07f, 0.12f, 0.05f, 0.05f };
        s->wear = std::min(0.8f, s->wear + wearPerYear[d.kind] / 52.0f);
        i++;
    }
    for (Series& s : g_series) {
        if (!InLibrary(s) || ActiveDeal(s.id) || FirstRunOnAir(s))
            continue;
        s.wear = std::max(0.0f, s.wear - (g_archives > 0 ? 0.12f : 0.05f) / 52.0f);
    }

    if (g_cable.exists) {
        float appeal = 0;
        std::vector<Series*> titles;
        for (const LibraryDeal& d : g_deals)
            if (d.kind == RerunCable)
                if (Series* s = FindSeries(d.seriesId)) {
                    appeal += CableAppeal(*s);
                    titles.push_back(s);
                }
        g_cable.reach = 0.08f + 0.62f * appeal / (appeal + 2.5f) + (HasTech(TechSatellite) ? 0.1f : 0.0f);
        double gross = CableHomes(year) * 1e6 * g_cable.reach * 0.0035 * EraFactor(year) * g_incomeScale;
        double cost = kCableUpkeep1950 * EraFactor(year) * g_costScale;
        game::AddMoney(-cost);
        // The titles share the money by how much each draws viewers; residuals come out of it.
        double unclaimed = gross;
        for (Series* s : titles) {
            double part = appeal > 0 ? gross * CableAppeal(*s) / appeal : 0;
            PayReruns(*s, part);
            unclaimed -= part;
        }
        game::AddMoney(unclaimed);
        g_cable.lastWeekIncome = gross;
        g_cable.lastWeekCost = cost;
    }

    // On Casual the studio sells rested titles to local stations by itself.
    if (g_level == Casual)
        for (Series& s : g_series)
            if (InLibrary(s) && OwnsReruns(s) && !ActiveDeal(s.id) && s.wear < 0.35f)
                SellLocal(s, 2);
}

// ---------------------------------------------------------------------------------
// Library tab

void DrawLibrary()
{
    float year = Year();
    std::vector<int> titles;
    for (int i = 0; i < static_cast<int>(g_series.size()); i++)
        if (InLibrary(g_series[i]))
            titles.push_back(i);

    double earned = 0, residuals = 0;
    for (int i : titles) {
        earned += g_series[i].syndIncome;
        residuals += g_series[i].residuals;
    }
    ImGui::Text("Rerun income so far: %s", Dollars(earned).c_str());
    if (residuals > 0) {
        ImGui::SameLine(0, 20);
        ImGui::TextDisabled("Residuals paid to cast and writers: %s", Dollars(residuals).c_str());
    }
    ImGui::SameLine(0, 20);
    if (g_archives > 0)
        ImGui::TextColored(kGreen, "Media Archive: rerun prices +25%%, shows rest faster");
    else
        ImGui::TextDisabled("Build a Media Archive for better rerun prices");

    if (titles.empty()) {
        ImGui::Spacing();
        ImGui::TextWrapped("Your library is empty. A show joins it once %d episodes have aired (or a miniseries "
                           "finishes). Stations pay well for reruns, especially of sitcoms and procedurals - often more "
                           "than the show earned the first time round.",
                           kMinPackage);
    } else {
        constexpr ImGuiTableFlags flags =
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit;
        if (ImGui::BeginTable("library", 7, flags, ImVec2(0, std::max(200.0f, ImGui::GetContentRegionAvail().y * 0.45f)))) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Title", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Episodes");
            ImGui::TableSetupColumn("Avg rating");
            ImGui::TableSetupColumn("Reruns");
            ImGui::TableSetupColumn("Freshness");
            ImGui::TableSetupColumn("Rights");
            ImGui::TableSetupColumn("Earned");
            ImGui::TableHeadersRow();
            for (int i : titles) {
                const Series& s = g_series[i];
                const LibraryDeal* d = ActiveDeal(s.id);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                if (ImGui::Selectable(Format("%s##lib%d", s.title.c_str(), s.id).c_str(), g_libSelected == i,
                                      ImGuiSelectableFlags_SpanAllColumns))
                    g_libSelected = i;
                ImGui::TableNextColumn(); ImGui::Text("%d", s.totalAired);
                ImGui::TableNextColumn(); ImGui::Text("%.1f", s.totalAired ? s.ratingTotal / s.totalAired : 0.0);
                ImGui::TableNextColumn(); ImGui::TextUnformatted(EvergreenWord(s.format));
                ImGui::TableNextColumn();
                ImGui::TextColored(s.wear < 0.25f ? kGreen : s.wear < 0.5f ? kYellow : kRed, "%.0f%%", (1 - s.wear) * 100);
                ImGui::TableNextColumn();
                if (!OwnsReruns(s))
                    ImGui::TextDisabled("%s owns them", NetworkShort(s.network));
                else if (!d)
                    ImGui::TextColored(kGold, "In your vault");
                else if (d->kind == RerunExclusive)
                    ImGui::Text("%s until %s", kNetworks[d->buyer].shortName, DateText(d->end).c_str());
                else if (d->kind == RerunLocal)
                    ImGui::Text("Syndicated until %s", DateText(d->end).c_str());
                else if (d->kind == RerunStreamLicense)
                    ImGui::Text("%s until %s", kStreamers[d->buyer].shortName, DateText(d->end).c_str());
                else if (d->kind == RerunStream)
                    ImGui::Text("On %s", StreamingName());
                else
                    ImGui::Text("On %s", g_cable.name.c_str());
                ImGui::TableNextColumn();
                ImGui::TextColored(kGreen, "%s", Dollars(s.syndIncome).c_str());
            }
            ImGui::EndTable();
        }

        if (g_libSelected >= 0 && g_libSelected < static_cast<int>(g_series.size()) && InLibrary(g_series[g_libSelected])) {
            Series& s = g_series[g_libSelected];
            ImGui::SeparatorText(s.title.c_str());
            LibraryDeal* d = ActiveDeal(s.id);
            if (!OwnsReruns(s)) {
                ImGui::TextWrapped("%s financed this show, so the reruns are theirs.", NetworkName(s.network));
            } else if (d && d->kind == RerunLocal) {
                ImGui::Text("Local stations pay %s a week until %s.", Dollars(d->weekly).c_str(), DateText(d->end).c_str());
            } else if (d && d->kind == RerunExclusive) {
                ImGui::Text("%s paid %s for exclusive reruns until %s.", kNetworks[d->buyer].name,
                            Dollars(d->upfront).c_str(), DateText(d->end).c_str());
            } else if (d && d->kind == RerunCable) {
                ImGui::Text("Airing on %s.", g_cable.name.c_str());
                if (ImGui::Button("Take it off the channel"))
                    SetOnCable(s, false);
            } else if (d && d->kind == RerunStream) {
                ImGui::Text("Streaming on %s.", StreamingName());
                if (ImGui::Button("Take it off the service"))
                    SetOnStreaming(s, false);
            } else if (d && d->kind == RerunStreamLicense) {
                ImGui::Text("%s paid %s to stream it until %s.", kStreamers[d->buyer].name, Dollars(d->upfront).c_str(),
                            DateText(d->end).c_str());
            } else {
                static int years = 2;
                ImGui::SetNextItemWidth(160);
                ImGui::SliderInt("Contract length (years)", &years, 1, 5);
                ImGui::Text("Local stations: %s a week", Dollars(LocalWeekly(s)).c_str());
                ImGui::SameLine();
                if (ImGui::Button("Syndicate it"))
                    SellLocal(s, years);
                HelpMarker("Sold to stations all over the country, paid every week. Reruns wear a show out: the fresher "
                           "it is, the more it fetches.");
                for (int n = 0; n < kRivalCount; n++) {
                    ImGui::Text("%s: %s up front, exclusive", kNetworks[n].shortName,
                                Dollars(ExclusiveOffer(s, n, years)).c_str());
                    ImGui::SameLine();
                    if (ImGui::Button(Format("Sell to %s##ex%d", kNetworks[n].shortName, n).c_str()))
                        SellExclusive(s, n, years);
                }
                if (g_cable.exists) {
                    if (ImGui::Button(Format("Put it on %s", g_cable.name.c_str()).c_str()))
                        SetOnCable(s, true);
                }
                if (StreamingExists()) {
                    if (ImGui::Button(Format("Stream it on %s", StreamingName()).c_str()))
                        SetOnStreaming(s, true);
                }
                for (int n = 0; n < kStreamerCount; n++) {
                    if (!StreamerActive(n))
                        continue;
                    ImGui::Text("%s: %s up front to stream it", kStreamers[n].name,
                                Dollars(StreamLicenseOffer(s, n, years)).c_str());
                    ImGui::SameLine();
                    if (ImGui::Button(Format("License to %s##st%d", kStreamers[n].shortName, n).c_str()))
                        LicenseToStreamer(s, n, years);
                }
                if (FirstRunOnAir(s))
                    ImGui::TextDisabled("New episodes are still airing: reruns can be sold alongside them.");
            }
            if (s.wear > 0.01f)
                ImGui::TextDisabled("Freshness %.0f%%. Unsold titles recover in the vault.", (1 - s.wear) * 100);
            if (ResidualRate(year) > 0)
                ImGui::TextDisabled("Residuals: %.0f%% of rerun money goes to the cast and writers.", ResidualRate(year) * 100);
        }
    }

    ImGui::SeparatorText("Cable");
    if (g_cable.exists) {
        int count = 0;
        for (const LibraryDeal& d : g_deals)
            count += d.kind == RerunCable;
        ImGui::Text("%s: %d title%s, carried in %.0f%% of %.1f million cable homes", g_cable.name.c_str(), count,
                    count == 1 ? "" : "s", g_cable.reach * 100, CableHomes(year));
        ImGui::Text("Last week: earned %s, costs %s", Dollars(g_cable.lastWeekIncome).c_str(),
                    Dollars(g_cable.lastWeekCost).c_str());
        HelpMarker("More (and fresher, and more popular) titles get the channel into more homes.");
    } else if (year < (HasTech(TechSatellite) ? 1972 : kCableYear)) {
        ImGui::TextDisabled("Cable channels arrive in %d.", HasTech(TechSatellite) ? 1972 : kCableYear);
    } else {
        std::string why;
        bool can = CanLaunchCable(&why);
        ImGui::SetNextItemWidth(220);
        ImGui::InputTextWithHint("##cable", "Channel name", g_cableName, sizeof(g_cableName));
        ImGui::SameLine();
        ImGui::BeginDisabled(!can || !g_cableName[0]);
        if (ImGui::Button(Format("Launch (%s)", Dollars(kCableCost1950 * EraFactor(year) * g_costScale).c_str()).c_str()))
            LaunchCable(g_cableName);
        ImGui::EndDisabled();
        if (!can)
            ImGui::TextColored(kYellow, "%s", why.c_str());
    }
}

// ---------------------------------------------------------------------------------
// Save data

void LibrarySave(std::map<std::string, std::string>& out)
{
    out["deals"] = std::to_string(g_deals.size());
    for (size_t i = 0; i < g_deals.size(); i++) {
        const LibraryDeal& d = g_deals[i];
        out[Format("d%zu", i)] = Format("%d,%d,%d,%.3f,%.3f,%.2f,%.2f", d.seriesId, d.kind, d.buyer, d.start, d.end,
                                        d.weekly, d.upfront);
    }
    out["cable"] = g_cable.exists ? "1" : "0";
    out["cable_name"] = g_cable.name;
    out["cable_founded"] = Format("%.3f", g_cable.founded);
    out["cable_reach"] = Format("%.4f", g_cable.reach);
}

void LibraryReset()
{
    g_deals.clear();
    g_cable = CableChannel{};
    g_libSelected = -1;
}

void LibraryLoad(const std::map<std::string, std::string>& in)
{
    LibraryReset();
    auto get = [&](const std::string& k) {
        auto it = in.find(k);
        return it == in.end() ? std::string() : it->second;
    };
    int count = atoi(get("deals").c_str());
    for (int i = 0; i < count; i++) {
        LibraryDeal d;
        if (sscanf_s(get(Format("d%d", i)).c_str(), "%d,%d,%d,%f,%f,%lf,%lf", &d.seriesId, &d.kind, &d.buyer, &d.start,
                     &d.end, &d.weekly, &d.upfront) == 7) {
            d.kind = std::clamp(d.kind, 0, RerunDealCount - 1);
            d.buyer = std::clamp(d.buyer, 0, kRivalCount - 1);
            g_deals.push_back(d);
        }
    }
    g_cable.exists = get("cable") == "1";
    g_cable.name = get("cable_name");
    g_cable.founded = static_cast<float>(atof(get("cable_founded").c_str()));
    g_cable.reach = static_cast<float>(atof(get("cable_reach").c_str()));
}

} // namespace tv
