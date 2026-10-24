// The Television window (F8) and the news ticker.

#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <cmath>

#include "mod.h"
#include "overlay.h"
#include "tv_internal.h"

namespace tv {
namespace {

struct PitchForm {
    char  title[64] = {};
    int   format = Sitcom;
    int   pilot = 0;
    int   network = -1;   // -1 = let the studio choose, kPlayerNetwork = your own network
    int   budget = BudgetStandard;
    int   deal = DealLicense;
    bool  fullOrder = true;
    std::string lastPilotKey;
} g_form;

int  g_selected = -1;
int  g_scheduleView = 0;
char g_networkName[48] = {};

} // namespace

void HelpMarker(const char* text)
{
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

namespace {

ImVec4 StatusColour(int status)
{
    switch (status) {
    case OnAir:          return kGreen;
    case Ordered:        return kGold;
    case RenewalOffered: return kYellow;
    case Rework:         return kYellow;
    case OnHold:         return kYellow;
    case PassedOn:
    case Cancelled:      return kRed;
    default:             return kGrey;
    }
}

ImVec4 NetworkColour(int network)
{
    static const ImVec4 colours[kStreamingNetwork + 1] = { ImVec4(0.12f, 0.31f, 0.62f, 1), ImVec4(0.71f, 0.28f, 0.11f, 1),
                                                           ImVec4(0.42f, 0.25f, 0.63f, 1), kGold, ImVec4(0.69f, 0.09f, 0.16f, 1) };
    return colours[network];
}

void DrawHeader()
{
    ImGui::SetNextItemWidth(170);
    if (ImGui::Combo("Management", &g_level, kLevelNames, LevelCount))
        mod::Log("tv: management level %s", kLevelNames[g_level]);
    HelpMarker(kLevelHelp[g_level]);

    ImGui::SameLine(0, 30);
    if (!g_detectionWorks) {
        ImGui::TextColored(kYellow, "Building check unavailable - assuming a TV Production Office");
    } else {
        ImGui::Text("Production Offices: %d   Soundstages: %d", g_offices, g_stages);
        if (g_archives) {
            ImGui::SameLine(0, 12);
            ImGui::TextDisabled("Archive");
        }
        int used = 0;
        for (const Series& s : g_series)
            used += InProduction(s) && s.status != OnHold;
        ImGui::SameLine(0, 20);
        ImGui::TextColored(used >= Capacity() && Capacity() > 0 ? kYellow : kGrey, "Shows in production: %d / %d", used,
                           Capacity());
    }
}

void DrawLocked()
{
    ImGui::Spacing();
    if (!Unlocked()) {
        ImGui::TextColored(kYellow, "Television hasn't arrived yet.");
        ImGui::TextWrapped("Regular television broadcasts begin in %d. Until then, keep making films: every film you "
                           "release can later become the pilot of a TV series.",
                           kTelevisionYear);
    } else {
        ImGui::TextColored(kYellow, "Build a TV Production Office to enter television.");
        ImGui::TextWrapped("You'll find it in the build menu. Each office can keep %d shows in production; a "
                           "Multi-Camera Soundstage adds room for one more and makes sitcoms, soaps, talk and game "
                           "shows look better.",
                           kShowsPerOffice);
    }
}

// ---------------------------------------------------------------------------------
// Someone was dropped on a TV Production Office zone: pick their show.

void DrawPendingDrop()
{
    if (!g_pendingDrop.active)
        return;
    const char* role = g_pendingDrop.room == "room_tv_cast"      ? "star"
                       : g_pendingDrop.room == "room_tv_showrun" ? "showrunner"
                                                                 : "writer";
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(460, 0));
    if (ImGui::Begin("Assign to a show", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextColored(kGold, "%s", g_pendingDrop.person.c_str());
        ImGui::TextWrapped("Which show should they join as %s %s?", role[0] == 's' && role[1] == 't' ? "the" : "a", role);
        int shown = 0;
        for (Series& s : g_series) {
            if (!(InProduction(s) || s.status == RenewalOffered || s.status == Pitching || s.status == Rework))
                continue;
            shown++;
            std::string current = g_pendingDrop.room == "room_tv_cast"      ? (s.lead.empty() ? "no star yet" : "now: " + s.lead)
                                  : g_pendingDrop.room == "room_tv_showrun" ? (s.showrunner.empty() ? "no showrunner yet" : "now: " + s.showrunner)
                                                                            : Format("%d/%d writers", static_cast<int>(s.writers.size()), kMaxWriters);
            if (ImGui::Button(Format("%s##assign%d", s.title.c_str(), s.id).c_str(), ImVec2(240, 0))) {
                AssignPerson(s, g_pendingDrop.room, g_pendingDrop.person);
                g_pendingDrop.active = false;
            }
            ImGui::SameLine();
            ImGui::TextDisabled("%s - %s", kFormats[s.format].name, current.c_str());
        }
        if (!shown)
            ImGui::TextColored(kYellow, "You have no shows in development or on air yet.");
        ImGui::Separator();
        if (ImGui::Button("Cancel"))
            g_pendingDrop.active = false;
    }
    ImGui::End();
}

// ---------------------------------------------------------------------------------
// My Shows

void DrawRepitch(Series& s)
{
    if (s.passedMask == (1 << kRivalCount) - 1) {
        ImGui::TextDisabled("Every network has passed. This idea is dead.");
        return;
    }
    ImGui::TextWrapped(s.status == Rework ? (g_filmPilots ? "Swap in a stronger pilot film and pitch again - or shop it elsewhere."
                                                         : "Swap in a stronger pilot and pitch again - or shop it elsewhere.")
                                          : "Try another network.");
    std::vector<game::Project> pilots = AvailablePilots();
    static int newPilot = -1;
    if (newPilot >= static_cast<int>(pilots.size()))
        newPilot = -1;
    std::string preview = newPilot >= 0 ? Utf8(pilots[newPilot].title) : "Keep current pilot";
    ImGui::SetNextItemWidth(260);
    if (ImGui::BeginCombo("New pilot", preview.c_str())) {
        if (ImGui::Selectable("Keep current pilot", newPilot < 0))
            newPilot = -1;
        for (int i = 0; i < static_cast<int>(pilots.size()); i++) {
            std::string label = Format("%s (%.0f%%)##%d", Utf8(pilots[i].title).c_str(), pilots[i].quality * 100, i);
            if (ImGui::Selectable(label.c_str(), newPilot == i))
                newPilot = i;
        }
        ImGui::EndCombo();
    }
    for (int i = 0; i < kRivalCount; i++) {
        if (s.passedMask & (1 << i))
            continue;
        ImGui::SameLine();
        if (ImGui::Button(Format("Pitch to %s##%d", kNetworks[i].shortName, i).c_str())) {
            if (newPilot >= 0) {
                ApplyPilot(s, pilots[newPilot]);
                s.reworkMask = 0;   // a new film is a fresh look for everyone
                newPilot = -1;
            }
            StartPitch(s, i);
        }
    }
    if (newPilot < 0 && s.status == Rework)
        ImGui::TextDisabled("The same film shown to %s again gets the same reaction.", kNetworks[s.network].shortName);
}

void DrawStarDemand(Series& s)
{
    if (!s.demand || s.lead.empty())
        return;
    ImGui::PushStyleColor(ImGuiCol_Text, kYellow);
    ImGui::TextWrapped("%s wants a %d%% raise to keep starring in '%s'.", s.lead.c_str(), s.demand, s.title.c_str());
    ImGui::PopStyleColor();
    double now = LeadPay1950(s) * EraFactor(Year()) * g_costScale;
    if (ImGui::Button(Format("Pay it (%s more per episode)", Dollars(now * s.demand / 100.0).c_str()).c_str()))
        ResolveDemand(s, true);
    ImGui::SameLine();
    if (ImGui::Button("Refuse"))
        ResolveDemand(s, false);
    HelpMarker("Refuse and the star may walk out: the show then airs without them (viewers notice) until you "
               "recast the role. Stars who stay without a raise are unhappy. Unanswered demands count as refused "
               "when the next season premieres.");
}

void DrawSeasonPlan(Series& s)
{
    if (s.format == Miniseries && s.season > 0 && s.aired > 0)
        return;
    bool planOpen = s.status == RenewalOffered || (InProduction(s) && s.aired == 0) || s.status == Pitching ||
                    s.status == Rework;
    bool finaleOpen = planOpen || (InProduction(s) && s.aired + 1 < s.order);
    if (!planOpen && !finaleOpen)
        return;
    ImGui::SetNextItemWidth(220);
    if (planOpen) {
        ImGui::Combo(s.status == RenewalOffered ? "Next season's plan" : "Season plan", &s.plan, kPlanNames, PlanCount);
    } else {
        ImGui::TextDisabled("Season plan: %s", kPlanNames[s.plan]);
    }
    HelpMarker(kPlanHelp[s.plan]);
    if (s.format != Miniseries) {
        ImGui::SameLine(0, 24);
        ImGui::SetNextItemWidth(140);
        if (finaleOpen)
            ImGui::Combo("Finale", &s.finale, kFinaleNames, FinaleCount);
        else
            ImGui::TextDisabled("Finale: %s", kFinaleNames[s.finale]);
        HelpMarker("A cliffhanger draws a bigger finale audience, makes the network keener to renew, and the next "
                   "premiere becomes an event. If the show is cancelled anyway, the fans will never forgive it.");
    }
    if (s.cliffhanger)
        ImGui::TextColored(kGold, "Last season ended on a cliffhanger: the premiere will be an event.");
}

void DrawSpinoff(Series& s)
{
    static int forId = -1, format = 0, variant = 0;
    static char title[64] = {};
    std::string why;
    if (!CanSpinOff(s, &why)) {
        if (s.season >= 1 && s.status != Cancelled)
            ImGui::TextDisabled("Spinoff: %s", why.c_str());
        return;
    }
    if (!ImGui::CollapsingHeader("Develop a spinoff"))
        return;
    if (forId != s.id) {
        forId = s.id;
        format = s.format;
        variant = 0;
        strncpy_s(title, SpinoffTitle(s, format, variant).c_str(), _TRUNCATE);
    }
    ImGui::TextWrapped("Take a character from '%s' into a show of their own. No pilot film needed: the new show "
                       "starts from the parent's quality and its fans.",
                       s.title.c_str());
    ImGui::SetNextItemWidth(160);
    if (ImGui::BeginCombo("Format##spin", kFormats[format].name)) {
        for (int f = 0; f < FormatCount; f++) {
            if (f == Miniseries || Year() < kFormats[f].fromYear)
                continue;
            if (ImGui::Selectable(kFormats[f].name, f == format)) {
                format = f;
                strncpy_s(title, SpinoffTitle(s, format, variant).c_str(), _TRUNCATE);
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(280);
    ImGui::InputText("Title##spin", title, sizeof(title));
    ImGui::SameLine();
    if (ImGui::Button("Another idea")) {
        variant++;
        strncpy_s(title, SpinoffTitle(s, format, variant).c_str(), _TRUNCATE);
    }
    std::string label = s.network == kPlayerNetwork ? "Greenlight it on your network"
                                                    : Format("Pitch it to %s", NetworkShort(s.network));
    if (ImGui::Button(label.c_str()) && title[0]) {
        CreateSpinoff(s, title, format);   // may move g_series: don't touch 's' after this
        forId = -1;
        g_selected = static_cast<int>(g_series.size()) - 1;
    }
}

void DrawSeriesDetail(Series& s)
{
    ImGui::SeparatorText(s.title.c_str());
    ImGui::Text("%s on %s", kFormats[s.format].name, NetworkName(s.network));
    ImGui::SameLine(0, 20);
    ImGui::TextColored(StatusColour(s.status), "%s", kStatusNames[s.status]);
    if (InProduction(s) || s.status == RenewalOffered)
        ImGui::Text("Time slot: %s", SlotName(s.network, s.slot).c_str());
    ImGui::Text("Pilot film: %s (quality %.0f%%)", s.pilotTitle.c_str(), s.pilotQuality * 100);
    {
        std::string writers;
        for (const std::string& w : s.writers)
            writers += (writers.empty() ? "" : ", ") + w;
        ImGui::Text("Star: %s   Showrunner: %s   Writers: %s", s.lead.empty() ? "-" : s.lead.c_str(),
                    s.showrunner.empty() ? "-" : s.showrunner.c_str(), writers.empty() ? "-" : writers.c_str());
        HelpMarker("Drop people on the TV Production Office's zones to assign them: a star in Cast, a director in "
                   "Showrunner, scriptwriters in the Writers' Room. Each one improves episodes and is paid per episode.");
        if (double pay = StaffPay1950(s))
            ImGui::TextDisabled("Pay: %s per episode", Dollars(pay * EraFactor(Year()) * g_costScale).c_str());
        if (!s.lead.empty() && (s.leadPay > 1.001f || s.leadSeasons > 0)) {
            ImGui::SameLine(0, 20);
            ImGui::TextDisabled("Star contract: %d season%s, paid %.0f%% of the standard rate", s.leadSeasons,
                                s.leadSeasons == 1 ? "" : "s", s.leadPay * 100);
        }
        if (s.lead.empty() && !s.formerLead.empty())
            ImGui::TextColored(kRed, "%s has left the show. Drop a new star on the Cast zone to recast the role.",
                               s.formerLead.c_str());
    }
    if (s.parentId)
        if (const Series* parent = FindSeries(s.parentId))
            ImGui::TextDisabled("Spinoff of '%s'", parent->title.c_str());
    if (s.feeMul > 1.001f)
        ImGui::TextDisabled("Renegotiated: %.0f%% of the original fee", s.feeMul * 100);
    if (s.awards)
        ImGui::TextColored(kGold, "Golden Antenna winner (%d)", s.awards);
    {
        std::string why;
        if (s.livePending) {
            ImGui::TextColored(kGold, "The next episode goes out LIVE.");
        } else if (CanGoLive(s, &why)) {
            if (ImGui::Button("Do a live episode"))
                PlanLive(s);
            HelpMarker("Once a season: the next episode airs live. Far more viewers tune in and it costs 50% more. "
                       "Live television can go wrong - or go down in history. Remote Broadcast Vans make it safer.");
        }
    }
    if (s.scandal && !s.lead.empty()) {
        ImGui::TextColored(kRed, "%s is caught up in a scandal.", s.lead.c_str());
        if (ImGui::Button("Stand by them"))
            ResolveScandal(s, false);
        ImGui::SameLine();
        if (ImGui::Button("Let them go"))
            ResolveScandal(s, true);
    }
    DrawStarDemand(s);
    if (s.season > 0) {
        float avg = s.aired ? s.ratingSum / s.aired : 0;
        float share = s.aired ? s.shareSum / s.aired : 0;
        ImGui::Text("Season %d: %d of %d episodes aired   Average %.1f rating, %.0f%% share   Best %.1f", s.season,
                    s.aired, s.order, avg, share * 100, s.bestRating);
        ImGui::Text("Earned %s   Spent %s   ", Dollars(s.income).c_str(), Dollars(s.cost).c_str());
        ImGui::SameLine();
        double profit = s.income - s.cost;
        ImGui::TextColored(profit >= 0 ? kGreen : kRed, "Profit %s", Dollars(profit).c_str());
        if (!s.ratings.empty())
            ImGui::PlotHistogram("##ratings", s.ratings.data(), static_cast<int>(s.ratings.size()), 0,
                                 "Ratings this season", 0.0f, std::max(30.0f, s.bestRating * 1.1f), ImVec2(-1, 70));
    }

    switch (s.status) {
    case Pitching:
        ImGui::TextDisabled("Verdict expected around %s.", DateText(s.verdictDay).c_str());
        break;
    case Ordered:
        if (s.slot < 0 && s.network == kPlayerNetwork)
            ImGui::TextColored(kYellow, "Give it a time slot in the Schedule tab.");
        else
            ImGui::TextDisabled("Premieres around %s.", DateText(s.nextAirDay).c_str());
        break;
    case Rework:
    case PassedOn:
        DrawRepitch(s);
        break;
    case RenewalOffered:
        if (OwnOutlet(s.network))
            ImGui::TextColored(kYellow, "Order season %d (%d episodes)?", s.season + 1, s.order);
        else
            ImGui::TextColored(kYellow, "%s offers season %d: %d episodes.", NetworkShort(s.network), s.season + 1, s.order);
        if (s.network != kPlayerNetwork && !s.askedForRaise) {
            if (ImGui::Button("Ask for more money"))
                AskForRaise(s);
            HelpMarker("Ask the network for 20% more per episode. The better the show did against what they "
                       "needed, the likelier they say yes. If they refuse, they may cut the order.");
            ImGui::SameLine();
        }
        if (ImGui::Button("Renew"))
            RenewSeries(s);
        ImGui::SameLine();
        if (ImGui::Button("End the series"))
            EndSeries(s);
        break;
    default:
        break;
    }

    if (g_level >= StudioBoss && InProduction(s)) {
        ImGui::SetNextItemWidth(160);
        if (g_level == MediaMogul || s.status == Ordered)
            ImGui::Combo("Episode budget", &s.budget, kBudgetNames, BudgetCount);
        else
            ImGui::TextDisabled("Episode budget: %s (Media Moguls can change it mid-season)", kBudgetNames[s.budget]);
    }
    DrawSeasonPlan(s);
    DrawSpinoff(s);
    for (int i = static_cast<int>(g_papers.size()) - 1; i >= 0; i--)
        if (g_papers[i].seriesId == s.id) {
            if (ImGui::Button("Read the finale's newspaper"))
                g_paperOpen = i;
            break;
        }
}

void DrawShows()
{
    if (g_series.empty()) {
        ImGui::TextDisabled("No shows yet. Open 'Pitch a Pilot' to turn one of your films into a series.");
        return;
    }
    constexpr ImGuiTableFlags flags =
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit;
    if (ImGui::BeginTable("shows", 8, flags, ImVec2(0, std::max(190.0f, ImGui::GetContentRegionAvail().y * 0.45f)))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Series", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Format");
        ImGui::TableSetupColumn("Network");
        ImGui::TableSetupColumn("Slot");
        ImGui::TableSetupColumn("Status");
        ImGui::TableSetupColumn("Episodes");
        ImGui::TableSetupColumn("Rating");
        ImGui::TableSetupColumn("Profit");
        ImGui::TableHeadersRow();
        for (int i = static_cast<int>(g_series.size()) - 1; i >= 0; i--) {
            const Series& s = g_series[i];
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (ImGui::Selectable(Format("%s##row%d", s.title.c_str(), s.id).c_str(), g_selected == i,
                                  ImGuiSelectableFlags_SpanAllColumns))
                g_selected = i;
            ImGui::TableNextColumn(); ImGui::TextUnformatted(kFormats[s.format].name);
            ImGui::TableNextColumn(); ImGui::TextColored(NetworkColour(s.network), "%s", NetworkShort(s.network));
            ImGui::TableNextColumn();
            if (s.slot >= 0) ImGui::Text("%.3s %s", kNightNames[s.slot / kHours], kHourNames[s.slot % kHours]);
            else ImGui::TextDisabled("-");
            ImGui::TableNextColumn(); ImGui::TextColored(StatusColour(s.status), "%s", kStatusNames[s.status]);
            ImGui::TableNextColumn();
            if (s.season > 0) ImGui::Text("S%d  %d/%d", s.season, s.aired, s.order); else ImGui::TextDisabled("-");
            ImGui::TableNextColumn();
            if (s.aired > 0) ImGui::Text("%.1f", s.lastRating); else ImGui::TextDisabled("-");
            ImGui::TableNextColumn();
            double profit = s.income - s.cost;
            if (s.totalAired > 0) ImGui::TextColored(profit >= 0 ? kGreen : kRed, "%s", Dollars(profit).c_str());
            else ImGui::TextDisabled("-");
        }
        ImGui::EndTable();
    }
    if (g_selected >= 0 && g_selected < static_cast<int>(g_series.size()))
        DrawSeriesDetail(g_series[g_selected]);
    else
        ImGui::TextDisabled("Select a show for details.");
}

// ---------------------------------------------------------------------------------
// Pitch a Pilot

void DrawPitch()
{
    std::vector<game::Project> pilots = AvailablePilots();
    if (pilots.empty()) {
        ImGui::TextWrapped("You need a pilot: a released film, a TV script from the Writers' Office, or a script or film "
                           "dropped on a TV building.");
        return;
    }
    g_form.pilot = std::clamp(g_form.pilot, 0, static_cast<int>(pilots.size()) - 1);
    const game::Project& pilot = pilots[g_form.pilot];
    bool ownNetwork = g_networks[kPlayerNetwork].exists;
    if (!ownNetwork && g_form.network == kPlayerNetwork)
        g_form.network = -1;
    if (!StreamingExists() && g_form.network == kStreamingNetwork)
        g_form.network = -1;

    ImGui::SetNextItemWidth(320);
    std::string preview = Format("%s (%.0f%%)", Utf8(pilot.title).c_str(), pilot.quality * 100);
    if (ImGui::BeginCombo("Pilot film", preview.c_str())) {
        for (int i = 0; i < static_cast<int>(pilots.size()); i++) {
            std::string label = Format("%s  -  quality %.0f%%%s##p%d", Utf8(pilots[i].title).c_str(), pilots[i].quality * 100,
                                       pilots[i].stage == -2 ? "  (TV script)"
                                       : pilots[i].stage >= 7 || pilots[i].hasIncome ? "" : "  (film script)", i);
            if (ImGui::Selectable(label.c_str(), g_form.pilot == i))
                g_form.pilot = i;
        }
        ImGui::EndCombo();
    }
    HelpMarker("The film shown to the network as the pilot. Its quality decides most of the verdict and sets the "
               "standard for the episodes that follow.");

    // Default the series title to the film's title whenever a different film is picked.
    std::string key = PilotKey(pilot);
    if (key != g_form.lastPilotKey) {
        g_form.lastPilotKey = key;
        snprintf(g_form.title, sizeof(g_form.title), "%s", Utf8(pilot.title).c_str());
        if (int scriptFormat = ShelfScriptFormat(pilot); scriptFormat >= 0)
            g_form.format = scriptFormat;   // a TV script is written for its format
    }
    ImGui::SetNextItemWidth(320);
    ImGui::InputText("Series title", g_form.title, sizeof(g_form.title));

    float year = Year();
    ImGui::SetNextItemWidth(320);
    if (ImGui::BeginCombo("Format", kFormats[g_form.format].name)) {
        for (int i = 0; i < FormatCount; i++) {
            bool available = year >= kFormats[i].fromYear;
            std::string label = available ? kFormats[i].name : Format("%s (from %d)", kFormats[i].name, kFormats[i].fromYear);
            if (ImGui::Selectable(label.c_str(), g_form.format == i, available ? 0 : ImGuiSelectableFlags_Disabled))
                g_form.format = i;
        }
        ImGui::EndCombo();
    }
    if (year < kFormats[g_form.format].fromYear)
        g_form.format = GameShow;
    const FormatInfo& f = kFormats[g_form.format];
    float era = EraFactor(year);
    ImGui::TextDisabled("%s  About %s per episode; %d-episode seasons; best at %s.", f.blurb,
                        Dollars(f.cost1950 * era * g_costScale).c_str(), FullOrder(g_form.format, year),
                        kHourNames[f.bestHour]);

    if (g_level >= StudioBoss || ownNetwork || StreamingExists()) {
        ImGui::SetNextItemWidth(320);
        std::string net = g_form.network < 0 ? "Studio's choice"
                          : g_form.network == kStreamingNetwork ? Format("%s (your streaming service)", StreamingName())
                          : g_form.network == kPlayerNetwork ? Format("%s (yours)", NetworkName(kPlayerNetwork))
                                                             : std::string(kNetworks[g_form.network].name);
        if (ImGui::BeginCombo("Network", net.c_str())) {
            if (ImGui::Selectable("Studio's choice", g_form.network < 0))
                g_form.network = -1;
            for (int i = 0; i < kRivalCount; i++)
                if (ImGui::Selectable(kNetworks[i].name, g_form.network == i))
                    g_form.network = i;
            if (ownNetwork &&
                ImGui::Selectable(Format("%s (yours)", NetworkName(kPlayerNetwork)).c_str(), g_form.network == kPlayerNetwork))
                g_form.network = kPlayerNetwork;
            if (StreamingExists() &&
                ImGui::Selectable(Format("%s (your streaming service)", StreamingName()).c_str(),
                                  g_form.network == kStreamingNetwork))
                g_form.network = kStreamingNetwork;
            ImGui::EndCombo();
        }
    }
    bool greenlight = OwnOutlet(g_form.network);
    bool streaming = g_form.network == kStreamingNetwork;
    static bool binge = true;
    if (streaming) {
        ImGui::Checkbox("Release every episode at once", &binge);
        HelpMarker("All at once: a burst of sign-ups, but many subscribers leave once they've finished. Weekly: "
                   "smaller but steadier, and the show stays talked about for longer.");
    }
    if (g_level >= StudioBoss) {
        ImGui::SetNextItemWidth(320);
        ImGui::Combo("Episode budget", &g_form.budget, kBudgetNames, BudgetCount);
        HelpMarker("Shoestring: 70% of the cost, weaker episodes. Premium: 150% of the cost, better episodes and a "
                   "slightly warmer reception from networks.");
    }
    if (greenlight && g_form.format != Miniseries) {
        ImGui::Checkbox("Order a full season", &g_form.fullOrder);
        HelpMarker("On your own network nobody needs convincing: order a full season, or a short run of 8 to test it.");
    }
    if (g_level >= StudioBoss && !greenlight) {
        ImGui::SetNextItemWidth(320);
        ImGui::Combo("Deal", &g_form.deal, kDealNames, DealCount);
        HelpMarker("License fee: the network pays a fixed fee per episode plus a bonus for strong ratings. You keep "
                   "the rerun rights.\n"
                   "Advertising share: half the fee, but you take a cut of every advertising dollar - a hit pays far "
                   "more, a flop far less. You keep the rerun rights.\n"
                   "Network-financed: the network pays the full cost plus 12%, whatever the ratings - no risk at all. "
                   "But the network owns the reruns, and networks like that deal.");
    }

    ImGui::Spacing();
    ImGui::BeginDisabled(g_form.title[0] == 0);
    if (ImGui::Button(greenlight ? "Greenlight the series" : "Pitch the pilot", ImVec2(220, 0))) {
        Series s;
        s.id = g_nextId++;
        s.title = g_form.title;
        s.format = g_form.format;
        s.budget = g_level >= StudioBoss ? g_form.budget : BudgetStandard;
        s.deal = g_level >= StudioBoss && !greenlight ? g_form.deal : DealLicense;
        ApplyPilot(s, pilot);
        if (greenlight) {
            s.network = g_form.network;
            s.binge = streaming && binge;
            int full = streaming ? 10 : FullOrder(s.format, year);
            int episodes = s.format == Miniseries ? 6 : g_form.fullOrder ? full : (streaming ? 6 : kLimitedOrder);
            OrderSeason(s, episodes, Today() + kPremiereDelay);
            Notify(Format("'%s' greenlit", s.title.c_str()),
                   streaming ? Format("%d episodes for %s, streaming in two weeks.", episodes, StreamingName())
                             : Format("%d episodes for %s. Give it a time slot in the Schedule tab.", episodes,
                                      NetworkName(kPlayerNetwork)),
                   kGold);
        } else {
            int network = g_level >= StudioBoss && g_form.network >= 0 ? g_form.network : BestNetwork(s);
            StartPitch(s, std::max(0, network));
        }
        g_series.push_back(s);
        g_selected = static_cast<int>(g_series.size()) - 1;
        g_form.lastPilotKey.clear();
        mod::Log("tv: %s '%s' (%s, pilot '%s' q %.2f) %s %s", greenlight ? "greenlit" : "pitched", s.title.c_str(),
                 kFormats[s.format].name, s.pilotTitle.c_str(), s.pilotQuality, greenlight ? "for" : "to",
                 NetworkShort(s.network));
        if (greenlight && !streaming && (g_level == Casual || g_networks[kPlayerNetwork].director))
            AutoSchedule();
    }
    ImGui::EndDisabled();
}

// ---------------------------------------------------------------------------------
// Schedule

void DrawSchedule()
{
    for (int n = 0; n < kNetworkSlots; n++) {
        if (!g_networks[n].exists)
            continue;
        if (n)
            ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, NetworkColour(n));
        ImGui::RadioButton(NetworkShort(n), &g_scheduleView, n);
        ImGui::PopStyleColor();
    }
    if (!g_networks[g_scheduleView].exists)
        g_scheduleView = 0;
    int view = g_scheduleView;
    NetworkState& ns = g_networks[view];
    bool own = view == kPlayerNetwork;
    ImGui::SameLine(0, 20);
    ImGui::TextDisabled("%s - reaches %.0f%% of TV homes", NetworkName(view), NetworkReach(view, Year()) * 100);
    if (own) {
        if (ImGui::Button("Fill empty slots"))
            AutoSchedule();
        HelpMarker("Places your unscheduled shows where the competition is weakest. A Programming Director (Networks "
                   "tab) does this every week; on Casual the studio does it for you.");
    }

    // Your shows that can go on your network's schedule.
    std::vector<Series*> mine;
    if (own)
        for (Series& s : g_series)
            if (s.network == kPlayerNetwork && (InProduction(s) || s.status == RenewalOffered))
                mine.push_back(&s);

    constexpr ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchSame;
    if (!ImGui::BeginTable("schedule", 1 + kHours, flags))
        return;
    ImGui::TableSetupColumn("Night", ImGuiTableColumnFlags_WidthFixed, 90);
    for (int h = 0; h < kHours; h++)
        ImGui::TableSetupColumn(kHourNames[h]);
    ImGui::TableHeadersRow();
    for (int night = 0; night < kNights; night++) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(kNightNames[night]);
        for (int h = 0; h < kHours; h++) {
            ImGui::TableNextColumn();
            int slot = SlotIndex(night, h);
            const SlotEntry& e = ns.slots[slot];
            Series* s = e.seriesId ? FindSeries(e.seriesId) : nullptr;
            const AiShow* a = e.aiShowId ? FindAiShow(e.aiShowId) : nullptr;
            if (own) {
                std::string current = s ? s->title : "(reruns and old films)";
                ImGui::SetNextItemWidth(-1);
                if (ImGui::BeginCombo(Format("##slot%d", slot).c_str(), current.c_str())) {
                    if (ImGui::Selectable("(reruns and old films)", !s) && s) {
                        ReleaseSlot(*s);
                    }
                    for (Series* m : mine) {
                        std::string label = m->title + (m->slot >= 0 && m->slot != slot ? Format("  (now %.3s %s)", kNightNames[m->slot / kHours], kHourNames[m->slot % kHours]) : "");
                        if (ImGui::Selectable(Format("%s##m%d", label.c_str(), m->id).c_str(), m == s)) {
                            if (s && s != m)
                                ReleaseSlot(*s);
                            if (m->slot >= 0)
                                ReleaseSlot(*m);
                            ns.slots[slot].seriesId = m->id;
                            m->slot = slot;
                            mod::Log("tv: scheduled '%s' %s", m->title.c_str(), SlotName(kPlayerNetwork, slot).c_str());
                        }
                    }
                    ImGui::EndCombo();
                }
            } else if (s) {
                ImGui::TextColored(kGold, "%s", s->title.c_str());
            } else if (a) {
                ImGui::TextUnformatted(a->title.c_str());
            } else {
                ImGui::TextDisabled("-");
            }
            if (s || a)
                ImGui::TextDisabled("%s  %.1f", kFormats[s ? s->format : a->format].name, ns.lastRating[slot]);
            else
                ImGui::TextDisabled("%.1f", ns.lastRating[slot]);
        }
    }
    ImGui::EndTable();
    ImGui::TextDisabled("Numbers are each slot's latest rating (percent of all TV homes).");
}

// ---------------------------------------------------------------------------------
// Ratings

// Every studio's films side by side: the game's own charts only rank them by stars.
void DrawBoxOffice()
{
    static std::vector<game::Project> films;
    static DWORD refreshed = 0;
    if (films.empty() || GetTickCount() - refreshed > 2000) {
        refreshed = GetTickCount();
        films = game::RivalFilms();
        std::vector<uintptr_t> mine;
        for (const std::vector<game::Project>& list : { game::PlayerBackCatalogue(), game::PlayerProductions() })
            for (const game::Project& p : list)
                if ((p.stage >= 7 || p.releaseDate > 0) && std::find(mine.begin(), mine.end(), p.address) == mine.end()) {
                    mine.push_back(p.address);
                    films.push_back(p);
                }
    }
    float today = Today();
    auto gross = [](const game::Project& p) { return p.hasIncome ? p.incomeInitial - p.incomeRemaining : 0.0; };

    auto table = [&](const char* id, std::vector<const game::Project*> rows) {
        constexpr ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit;
        if (!ImGui::BeginTable(id, 6, flags))
            return;
        ImGui::TableSetupColumn("#");
        ImGui::TableSetupColumn("Film", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Studio");
        ImGui::TableSetupColumn("Released");
        ImGui::TableSetupColumn("Stars");
        ImGui::TableSetupColumn("Box office");
        ImGui::TableHeadersRow();
        for (size_t i = 0; i < rows.size() && i < 15; i++) {
            const game::Project& p = *rows[i];
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); RankBadge(static_cast<int>(i) + 1);
            ImGui::TableNextColumn();
            if (!p.rival) ImGui::TextColored(kGold, "%s", Utf8(p.title).c_str()); else ImGui::TextUnformatted(Utf8(p.title).c_str());
            ImGui::TableNextColumn(); ImGui::TextUnformatted(p.rival ? Utf8(p.studio).c_str() : "Your studio");
            ImGui::TableNextColumn(); ImGui::Text("%d", static_cast<int>(p.releaseDate / 365.0f));
            ImGui::TableNextColumn();
            if (p.starRating >= 0) ImGui::Text("%.1f", p.starRating * 5); else ImGui::TextDisabled("-");
            ImGui::TableNextColumn();
            if (p.hasIncome) ImGui::TextUnformatted(Dollars(gross(p)).c_str()); else ImGui::TextDisabled("-");
        }
        ImGui::EndTable();
    };

    std::vector<const game::Project*> year, best;
    for (const game::Project& p : films) {
        if (p.releaseDate > 0 && today - p.releaseDate <= 365)
            year.push_back(&p);
        best.push_back(&p);
    }
    std::sort(year.begin(), year.end(), [&](auto a, auto b) { return gross(*a) > gross(*b); });
    std::sort(best.begin(), best.end(), [](auto a, auto b) { return a->starRating > b->starRating; });

    ImGui::SeparatorText("This year's biggest films (box office so far)");
    if (year.empty())
        ImGui::TextDisabled("No films released in the last twelve months.");
    else
        table("year", year);
    ImGui::SeparatorText("Best reviewed films still in cinemas and on the charts");
    if (best.empty())
        ImGui::TextDisabled("No films yet.");
    else
        table("best", best);
    int rivals = 0;
    for (const game::Project& p : films)
        rivals += p.rival;
    ImGui::TextDisabled("%d rival films, %d of yours. Rival networks sometimes turn a rival studio's hit into a series.",
                        rivals, static_cast<int>(films.size()) - rivals);
}

void DrawRatings()
{
    if (g_lastWeek.empty()) {
        ImGui::TextDisabled("The first week's ratings arrive after Sunday night.");
        return;
    }
    ImGui::SeparatorText("Last week's top shows");
    constexpr ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit;
    if (ImGui::BeginTable("chart", 7, flags)) {
        ImGui::TableSetupColumn("#");
        ImGui::TableSetupColumn("Show", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Network");
        ImGui::TableSetupColumn("Format");
        ImGui::TableSetupColumn("Slot");
        ImGui::TableSetupColumn("Rating");
        ImGui::TableSetupColumn("Share");
        ImGui::TableHeadersRow();
        for (int i = 0; i < static_cast<int>(g_lastWeek.size()) && i < 15; i++) {
            const ChartEntry& c = g_lastWeek[i];
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); RankBadge(i + 1);
            ImGui::TableNextColumn();
            if (c.player) ImGui::TextColored(kGold, "%s", c.title.c_str()); else ImGui::TextUnformatted(c.title.c_str());
            ImGui::TableNextColumn(); ImGui::TextColored(NetworkColour(c.network), "%s", NetworkShort(c.network));
            ImGui::TableNextColumn(); ImGui::TextUnformatted(kFormats[c.format].name);
            ImGui::TableNextColumn(); ImGui::Text("%.3s %s", kNightNames[c.slot / kHours], kHourNames[c.slot % kHours]);
            ImGui::TableNextColumn(); ImGui::Text("%.1f", c.rating);
            ImGui::TableNextColumn(); ImGui::Text("%.0f%%", c.share * 100);
        }
        ImGui::EndTable();
    }
    // Your shows that missed the top 15.
    for (int i = 15; i < static_cast<int>(g_lastWeek.size()); i++)
        if (g_lastWeek[i].player)
            ImGui::TextColored(kGold, "#%d  %s  %.1f (%.0f%%)", i + 1, g_lastWeek[i].title.c_str(), g_lastWeek[i].rating,
                               g_lastWeek[i].share * 100);

    ImGui::SeparatorText("Network shares last week");
    float sum[kNetworkSlots] = {};
    int count[kNetworkSlots] = {};
    for (const ChartEntry& c : g_lastWeek) {
        sum[c.network] += c.share;
        count[c.network]++;
    }
    for (int n = 0; n < kNetworkSlots; n++) {
        if (!g_networks[n].exists)
            continue;
        float avg = count[n] ? sum[n] / count[n] : 0;
        ImGui::TextColored(NetworkColour(n), "%-4s", NetworkShort(n));
        ImGui::SameLine(70);
        ImGui::ProgressBar(avg, ImVec2(260, 0), Format("%.0f%%", avg * 100).c_str());
    }
}

// ---------------------------------------------------------------------------------
// Networks

void DrawOwnNetwork()
{
    NetworkState& own = g_networks[kPlayerNetwork];
    float era = EraFactor(Year());
    ImGui::SeparatorText("Your network");
    if (!own.exists) {
        ImGui::TextWrapped("Own the screen, not just the shows: your network keeps all the advertising money from "
                           "everything it airs, and you decide the schedule. It also pays station costs every week, "
                           "and empty slots earn very little.");
        if (g_networkName[0] == 0) {
            game::Studio studio = game::ReadPlayerStudio();
            snprintf(g_networkName, sizeof(g_networkName), "%s Television", Utf8(studio.name).c_str());
        }
        ImGui::SetNextItemWidth(320);
        ImGui::InputText("Network name", g_networkName, sizeof(g_networkName));
        std::string why;
        bool can = CanFoundNetwork(&why);
        ImGui::BeginDisabled(!can || g_networkName[0] == 0);
        if (ImGui::Button(Format("Launch a local station (%s)", Dollars(kReachCost1950[ReachLocal] * era * g_costScale).c_str()).c_str()))
            FoundNetwork(g_networkName);
        ImGui::EndDisabled();
        if (!can)
            ImGui::TextColored(kYellow, "%s", why.c_str());
        return;
    }
    ImGui::Text("%s (%s) - %s, reaching %.0f%% of TV homes", own.name.c_str(), own.shortName.c_str(),
                kReachNames[own.reach], kReachShare[own.reach] * 100);
    ImGui::Text("Last week: earned %s, spent %s", Dollars(own.lastWeekIncome).c_str(), Dollars(own.lastWeekCost).c_str());
    ImGui::SameLine();
    double net = own.lastWeekIncome - own.lastWeekCost;
    ImGui::TextColored(net >= 0 ? kGreen : kRed, "(%s)", Dollars(net).c_str());
    ImGui::TextDisabled("Station costs %s a week.", Dollars(kReachUpkeep1950[own.reach] * era * g_costScale).c_str());

    if (own.reach < ReachNational) {
        int next = own.reach + 1;
        std::string why;
        bool can = CanUpgradeReach(&why);
        ImGui::BeginDisabled(!can);
        if (ImGui::Button(Format("Go %s (%s)", kReachNames[next], Dollars(kReachCost1950[next] * era * g_costScale).c_str()).c_str()))
            UpgradeReach();
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled("reach %.0f%%, station costs %s a week", kReachShare[next] * 100,
                            Dollars(kReachUpkeep1950[next] * era * g_costScale).c_str());
        if (!can)
            ImGui::TextColored(kYellow, "%s", why.c_str());
    }

    if (g_level == Casual) {
        ImGui::TextDisabled("On Casual the studio schedules your network for you.");
    } else if (ImGui::Checkbox(Format("Programming Director (%s a week)", Dollars(800 * era * g_costScale).c_str()).c_str(),
                               &own.director)) {
        Notify(own.director ? "Programming Director hired" : "Programming Director let go",
               own.director ? "They'll put your new shows into the best open slots every Monday." : "You run the schedule now.",
               kGrey);
    }
}

void DrawNetworks()
{
    float year = Year();
    DrawOwnNetwork();
    for (int i = 0; i < kRivalCount; i++) {
        const NetworkInfo& n = kNetworks[i];
        ImGui::PushStyleColor(ImGuiCol_Text, NetworkColour(i));
        ImGui::SeparatorText(Format("%s (%s)", n.name, n.shortName).c_str());
        ImGui::PopStyleColor();
        ImGui::TextWrapped("%s", n.identity);
        const char* standards = n.bar >= 45 ? "Very high" : n.bar >= 35 ? "High" : "Relaxed";
        ImGui::Text("Standards: %s   Pays: %.0f%%   Reaches %.0f%% of TV homes", standards, n.feeMul * 100,
                    NetworkReach(i, year) * 100);
        std::string likes, dislikes;
        for (int f = 0; f < FormatCount; f++) {
            if (n.taste[f] >= 8)
                likes += (likes.empty() ? "" : ", ") + std::string(kFormats[f].name);
            else if (n.taste[f] <= -6)
                dislikes += (dislikes.empty() ? "" : ", ") + std::string(kFormats[f].name);
        }
        ImGui::TextColored(kGreen, "Loves: %s", likes.c_str());
        if (!dislikes.empty())
            ImGui::TextColored(kRed, "Avoids: %s", dislikes.c_str());
    }
}

} // namespace

// ---------------------------------------------------------------------------------
// The Television screen: like the game's own management screens it takes over the whole
// display, hiding the lot behind a deep blue backdrop, with big rounded tabs.

namespace {

ImVec4 Rgb(unsigned rgb, float a = 1.0f)
{
    return ImVec4(((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f, a);
}

enum Tab { TabShows, TabWriters, TabPitch, TabSchedule, TabRatings, TabNetworks, TabLibrary, TabStreaming, TabResearch,
           TabEmpire, TabPress, TabBoxOffice, TabCount };
const char* const kTabNames[] = { "My Shows", "Writers", "Pitch a Pilot", "Schedule", "Ratings", "Networks",
                                          "Library", "Streaming", "Research", "Empire", "Press", "Box Office" };
static_assert(sizeof(kTabNames) / sizeof(kTabNames[0]) == TabCount, "one name per tab");
int g_tab = TabShows;

// The lot disappears behind a royal blue backdrop with the ranking screen's sunburst.
void DrawBackdrop()
{
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    ImVec2 size = ImGui::GetIO().DisplaySize;
    ImU32 top = ImGui::GetColorU32(Rgb(0x1C4A9C, 0.97f));
    ImU32 bottom = ImGui::GetColorU32(Rgb(0x0B2560, 0.97f));
    dl->AddRectFilledMultiColor(ImVec2(0, 0), size, top, top, bottom, bottom);
    ImVec2 centre(size.x * 0.5f, size.y * 0.12f);
    float reach = std::max(size.x, size.y) * 1.2f;
    for (int i = 0; i < 24; i++) {
        float a0 = i * 3.14159265f * 2 / 24, a1 = a0 + 3.14159265f / 24;
        dl->AddTriangleFilled(centre, ImVec2(centre.x + std::cos(a0) * reach, centre.y + std::sin(a0) * reach),
                              ImVec2(centre.x + std::cos(a1) * reach, centre.y + std::sin(a1) * reach),
                              ImGui::GetColorU32(Rgb(0xFFFFFF, 0.035f)));
    }
    if (void* flourish = overlay::Texture("league_flourish")) {
        float r = size.y * 0.32f;
        dl->AddImage(static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(flourish)), ImVec2(centre.x - r, centre.y - r), ImVec2(centre.x + r, centre.y + r), ImVec2(0, 0),
                     ImVec2(1, 1), ImGui::GetColorU32(Rgb(0xF9E81E, 0.10f)));
    }
}

// A big rounded tab, like the buttons on the game's screens.
bool TabButton(const char* label, bool selected, float width)
{
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 20.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 3.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, selected ? Rgb(0xF9E81E) : Rgb(0xD7E3F9));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, selected ? Rgb(0xF9E81E) : Rgb(0xFFFFFF));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, Rgb(0xF9E81E));
    ImGui::PushStyleColor(ImGuiCol_Border, Rgb(0x77909F));
    ImGui::PushStyleColor(ImGuiCol_Text, Rgb(0x303F3F));
    bool pressed = ImGui::Button(label, ImVec2(width, 40));
    ImGui::PopStyleColor(5);
    ImGui::PopStyleVar(2);
    return pressed;
}

// The round close button in the screen's corner.
bool CloseButton(ImVec2 centre, float radius)
{
    ImGui::SetCursorScreenPos(ImVec2(centre.x - radius, centre.y - radius));
    bool pressed = ImGui::InvisibleButton("##close_tv", ImVec2(radius * 2, radius * 2));
    bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddCircleFilled(ImVec2(centre.x + 2, centre.y + 3), radius, IM_COL32(0, 0, 0, 70), 32);
    dl->AddCircleFilled(centre, radius, ImGui::GetColorU32(hovered ? Rgb(0xF9E81E) : Rgb(0xD7E3F9)), 32);
    dl->AddCircle(centre, radius, ImGui::GetColorU32(Rgb(0x77909F)), 32, 4.0f);
    float k = radius * 0.38f;
    ImU32 ink = ImGui::GetColorU32(Rgb(0x303F3F));
    dl->AddLine(ImVec2(centre.x - k, centre.y - k), ImVec2(centre.x + k, centre.y + k), ink, 5.0f);
    dl->AddLine(ImVec2(centre.x - k, centre.y + k), ImVec2(centre.x + k, centre.y - k), ink, 5.0f);
    if (hovered)
        ImGui::SetTooltip("Back to the lot (F8)");
    return pressed;
}

// Text with a dark outline, for titles on the blue backdrop.
void OutlinedText(ImDrawList* dl, ImFont* font, float size, ImVec2 pos, ImU32 colour, const char* text)
{
    ImU32 outline = ImGui::GetColorU32(Rgb(0x0B2560, 0.9f));
    for (int dx = -2; dx <= 2; dx += 2)
        for (int dy = -2; dy <= 2; dy += 2)
            if (dx || dy)
                dl->AddText(font, size, ImVec2(pos.x + dx, pos.y + dy), outline, text);
    dl->AddText(font, size, pos, colour, text);
}

// The slate banner with yellow stars that crowns each table on the ranking screen.
void Banner(ImDrawList* dl, const char* texture, ImVec2 centreTop, float width)
{
    int w = 0, h = 0;
    if (void* tex = overlay::Texture(texture, &w, &h)) {
        float height = width * h / std::max(1, w);
        dl->AddImage(static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(tex)), ImVec2(centreTop.x - width / 2, centreTop.y), ImVec2(centreTop.x + width / 2, centreTop.y + height));
    }
}

} // namespace

// A rank in a yellow rosette, like the positions on the ranking screen.
void RankBadge(int rank)
{
    float d = ImGui::GetTextLineHeight() + 8;
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 centre(p.x + d / 2, p.y + d / 2 - 3);
    if (void* tex = overlay::Texture(rank <= 3 ? "league_rosettesmall" : "league_tables_roundel"))
        dl->AddImage(static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(tex)), ImVec2(centre.x - d / 2, centre.y - d / 2), ImVec2(centre.x + d / 2, centre.y + d / 2), ImVec2(0, 0),
                     ImVec2(1, 1), rank <= 3 ? IM_COL32_WHITE : IM_COL32(255, 255, 255, 150));
    else
        dl->AddCircleFilled(centre, d / 2, ImGui::GetColorU32(Rgb(rank <= 3 ? 0xF9E81E : 0xC2D2EA)), 20);
    std::string n = std::to_string(rank);
    ImVec2 ts = ImGui::CalcTextSize(n.c_str());
    dl->AddText(ImVec2(centre.x - ts.x / 2, centre.y - ts.y / 2), ImGui::GetColorU32(Rgb(0x303F3F)), n.c_str());
    ImGui::Dummy(ImVec2(d, d - 6));
}

namespace {

const char* const kTabBanners[] = {
    "league_tables_actors_top", "league_tables_movies_top", "league_tables_movies_top", "league_tables_studios_top", "league_tables_actors_top",
    "league_tables_studios_top", "league_tables_movies_top", "league_tables_movies_top", "league_tables_actors_top",
    "league_tables_studios_top", "league_tables_actors_top", "league_tables_movies_top",
};
static_assert(sizeof(kTabBanners) / sizeof(kTabBanners[0]) == TabCount, "one banner per tab");

} // namespace

void DrawWindow()
{
    DrawBackdrop();
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(io.DisplaySize, ImGuiCond_Always);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, Rgb(0x000000, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                       ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus |
                                       ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings;
    bool begun = ImGui::Begin("Television##screen", nullptr, flags);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
    if (begun) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 size = io.DisplaySize;
        float margin = std::max(16.0f, size.x * 0.04f);
        float y = 10;

        // The banner and the title.
        float bannerW = std::min(560.0f, size.x * 0.42f);
        Banner(dl, "league_tables_studios_top", ImVec2(size.x / 2, y), bannerW);
        y += bannerW * 60 / 200 - 10;
        ImFont* title = overlay::TitleFont() ? overlay::TitleFont() : ImGui::GetFont();
        float titleSize = overlay::TitleFont() ? title->FontSize : ImGui::GetFontSize() * 2;
        ImVec2 ts = title->CalcTextSizeA(titleSize, FLT_MAX, 0, "TELEVISION");
        OutlinedText(dl, title, titleSize, ImVec2(size.x / 2 - ts.x / 2, y), IM_COL32_WHITE, "TELEVISION");
        y += ts.y + 2;
        if (game::Ready()) {
            std::string info = Format("%s   -   %.1f million homes with a TV   -   Empire value %s", DateText(Today()).c_str(),
                                      Households(Year()), Dollars(MediaEmpireValue(nullptr)).c_str());
            ImVec2 is = ImGui::CalcTextSize(info.c_str());
            OutlinedText(dl, ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(size.x / 2 - is.x / 2, y),
                         ImGui::GetColorU32(Rgb(0xF9E81E)), info.c_str());
            y += is.y + 12;
        }
        if (CloseButton(ImVec2(size.x - margin * 0.6f - 26, 40), 26))
            overlay::Close();

        if (!game::Ready()) {
            ImGui::SetCursorScreenPos(ImVec2(margin, y + 20));
            ImGui::TextColored(Rgb(0xFFFFFF), "Start or load a game to run your television business.");
        } else {
            DrawPendingDrop();
            DrawNewspaper();
            bool open = Unlocked() && HasOffice();

            // Tabs, centred under the title.
            if (open) {
                ImFont* heading = overlay::HeadingFont();
                if (heading)
                    ImGui::PushFont(heading);
                // One row if every label fits whole, two otherwise.
                float need = 0;
                for (int t = 0; t < TabCount; t++)
                    need = std::max(need, ImGui::CalcTextSize(kTabNames[t]).x + 24.0f);
                float avail = size.x - margin * 2;
                int perRow = (avail - 8.0f * (TabCount - 1)) / TabCount >= need ? TabCount : (TabCount + 1) / 2;
                float tabW = std::min(std::max(165.0f, need), (avail - 8.0f * (perRow - 1)) / perRow);
                for (int t = 0; t < TabCount; t++) {
                    int inRow = std::min(perRow, TabCount - t / perRow * perRow);
                    if (t % perRow == 0) {
                        float rowW = tabW * inRow + 8.0f * (inRow - 1);
                        ImGui::SetCursorScreenPos(ImVec2(size.x / 2 - rowW / 2, y + (t / perRow) * 44.0f));
                    } else {
                        ImGui::SameLine(0, 8);
                    }
                    if (TabButton(kTabNames[t], g_tab == t, tabW))
                        g_tab = t;
                }
                if (heading)
                    ImGui::PopFont();
                y += 44.0f * ((TabCount + perRow - 1) / perRow) + 10;
            }

            // The table: periwinkle with slate edges, its banner over the top edge.
            // The table's banner sits on its top edge, so leave room for the part above it.
            float bottomBannerH = 48;
            float tableBannerW = std::min(360.0f, (size.x - margin * 2) * 0.32f);
            ImVec2 p0(margin, y + tableBannerW * 60 / 200 * 0.62f + 4);
            ImVec2 p1(size.x - margin, size.y - bottomBannerH - 34);
            dl->AddRectFilled(ImVec2(p0.x + 4, p0.y + 6), ImVec2(p1.x + 4, p1.y + 6), IM_COL32(0, 0, 0, 60), 18.0f);
            dl->AddRectFilled(p0, p1, ImGui::GetColorU32(Rgb(0xA7BBD7)), 18.0f);
            dl->AddRect(p0, p1, ImGui::GetColorU32(Rgb(0x77909F)), 18.0f, 0, 7.0f);
            dl->AddRect(ImVec2(p0.x + 7, p0.y + 7), ImVec2(p1.x - 7, p1.y - 7), ImGui::GetColorU32(Rgb(0xD7E3F9, 0.7f)),
                        13.0f, 0, 1.5f);
            Banner(dl, open ? kTabBanners[g_tab] : "league_tables_studios_top", ImVec2(size.x / 2, p0.y - tableBannerW * 60 / 200 * 0.62f),
                   tableBannerW);
            Banner(dl, "league_tables_studios_bottom", ImVec2(size.x / 2, p1.y - 14), std::min(360.0f, (p1.x - p0.x) * 0.3f));

            float bannerClear = tableBannerW * 60 / 200 * 0.38f;
            ImGui::SetCursorScreenPos(ImVec2(p0.x + 22, p0.y + 14 + bannerClear));
            ImGui::PushStyleColor(ImGuiCol_ChildBg, Rgb(0x000000, 0.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4, 4));
            if (ImGui::BeginChild("tv_page", ImVec2(p1.x - p0.x - 44, p1.y - p0.y - 28 - bannerClear - 30), ImGuiChildFlags_None)) {
                if (ImFont* f = overlay::HeadingFont()) {
                    ImGui::PushFont(f);
                    ImGui::TextUnformatted(open ? kTabNames[g_tab] : "Television");
                    ImGui::PopFont();
                }
                DrawHeader();
                ImGui::Separator();
                ImGui::Spacing();
                if (!open) {
                    DrawLocked();
                    if (!g_series.empty())
                        DrawShows();
                } else {
                    switch (g_tab) {
                    case TabShows:     DrawShows(); break;
                    case TabWriters:   DrawWriters(); break;
                    case TabPitch:     DrawPitch(); break;
                    case TabSchedule:  DrawSchedule(); break;
                    case TabRatings:   DrawRatings(); break;
                    case TabNetworks:  DrawNetworks(); break;
                    case TabLibrary:   DrawLibrary(); break;
                    case TabStreaming: DrawStreaming(); break;
                    case TabResearch:  DrawResearch(); break;
                    case TabEmpire:    DrawEmpire(); break;
                    case TabPress:     DrawPressTab(); break;
                    case TabBoxOffice: DrawBoxOffice(); break;
                    }
                }
            }
            ImGui::EndChild();
            ImGui::PopStyleVar();
            ImGui::PopStyleColor();

            static int shotTaken = 0;
            static int shotTab = -1;
            static double shotSince = 0;
            int tab = open ? g_tab : TabCount;
            if (tab != shotTab) {
                shotTab = tab;
                shotSince = ImGui::GetTime();
            }
            if (!(shotTaken & (1 << tab)) && ImGui::GetTime() - shotSince > 1.5) {
                shotTaken |= 1 << tab;
                overlay::RequestScreenshot(Format("tv_%s", open ? kTabNames[tab] : "locked").c_str());
            }
        }

        // Footer on the backdrop.
        ImGui::SetCursorScreenPos(ImVec2(margin, size.y - 34));
        ImGui::PushStyleColor(ImGuiCol_Text, Rgb(0xFFFFFF));
        if (g_devTools) {
            static bool inspector = false;
            if (ImGui::Checkbox("Developer inspector", &inspector))
                mod::ShowInspector(inspector);
            ImGui::SameLine(0, 30);
        }
        ImGui::TextColored(Rgb(0xD7E3F9), "F8 or the round button returns to the lot");
        ImGui::PopStyleColor();
    }
    ImGui::End();
}

void OpenPitchFor(uintptr_t project)
{
    std::vector<game::Project> pilots = AvailablePilots();
    for (int i = 0; i < static_cast<int>(pilots.size()); i++)
        if (pilots[i].address == project) {
            g_form.pilot = i;
            g_form.lastPilotKey.clear();
        }
    g_tab = TabPitch;
}

void OpenPitchForKey(const std::string& key)
{
    std::vector<game::Project> pilots = AvailablePilots();
    for (int i = 0; i < static_cast<int>(pilots.size()); i++)
        if (PilotKey(pilots[i]) == key) {
            g_form.pilot = i;
            g_form.lastPilotKey.clear();
        }
    g_tab = TabPitch;
}

// News ticker: recent events slide in at the top of the screen and fade after a while.
void DrawToasts()
{
    constexpr double kLife = 9.0;
    double now = ImGui::GetTime();
    while (!g_toasts.empty() && g_toasts.front().born >= 0 && now - g_toasts.front().born > kLife)
        g_toasts.pop_front();
    if (g_toasts.empty())
        return;

    ImGuiIO& io = ImGui::GetIO();
    float y = 70.0f;
    int index = 0;
    for (Toast& t : g_toasts) {
        if (t.born < 0)
            t.born = now;
        double age = now - t.born;
        float alpha = static_cast<float>(std::min({ 1.0, age * 4.0, (kLife - age) * 1.5 }));
        ImGui::SetNextWindowBgAlpha(0.92f * alpha);
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, y), ImGuiCond_Always, ImVec2(0.5f, 0.0f));
        ImGui::SetNextWindowSize(ImVec2(440, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
        ImGui::PushStyleColor(ImGuiCol_Border, t.colour);
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav |
                                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                 ImGuiWindowFlags_AlwaysAutoResize;
        if (ImGui::Begin(Format("##toast%d", index++).c_str(), nullptr, flags)) {
            ImGui::TextColored(t.colour, "%s", t.title.c_str());
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(t.body.c_str());
            ImGui::PopTextWrapPos();
            y += ImGui::GetWindowHeight() + 6.0f;
        }
        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
    }
}

// For the simulator (tools/sim), which draws every tab to catch UI mistakes.
int  TabTotal() { return TabCount; }
void SelectTab(int tab) { g_tab = std::clamp(tab, 0, TabCount - 1); }

void RenewSeries(Series& s)
{
    int episodes = s.order;
    OrderSeason(s, episodes, Today() + kSummerHiatus);
    s.freshness = std::min(1.0f, s.freshness + 0.05f);
    Notify(Format("'%s' renewed", s.title.c_str()), Format("Season %d returns after the summer.", s.season), kGreen);
}

void EndSeries(Series& s)
{
    s.status = Ended;
    ReleaseSlot(s);
    Notify(Format("'%s' ends its run", s.title.c_str()), "You took a bow while the show was still wanted.", kGrey);
}

} // namespace tv
