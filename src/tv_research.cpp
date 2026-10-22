// Research, challenges and live episodes.
//
// Research: television technology from the laugh track to recommendation AI, one project at a
// time; each one changes how your shows are made, watched or sold.
// Challenges: six goals across the eras, each with a reward and a front page.
// Live episodes: once a season a show can go out live, for a big audience and a real risk.

#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <cmath>

#include "mod.h"
#include "overlay.h"
#include "tv_internal.h"

namespace tv {

const TechInfo kTechs[TechCount] = {
    { "Laugh Track", 1950, 40000, 8,
      "Canned laughter tells viewers when it's funny.", "Sitcoms draw 6% more viewers; critics roll their eyes at awards time." },
    { "Audience Research", 1950, 60000, 10,
      "Your own ratings panels and focus groups.", "Pitches score 4 points higher: networks trust your numbers." },
    { "Multi-Camera Technique", 1951, 80000, 12,
      "Three cameras, one take, a live studio audience.", "Sitcoms, soaps, talk and game shows get better episodes." },
    { "Remote Broadcast Vans", 1952, 120000, 12,
      "Take the cameras anywhere.", "Live episodes go wrong half as often; talk, game and documentary shows improve." },
    { "Syndication Sales Team", 1955, 90000, 10,
      "Salesmen in every TV market in the country.", "Local stations pay 15% more for your reruns." },
    { "Videotape", 1956, 200000, 16,
      "Record, edit, and air it whenever you like.", "Every episode costs 8% less to make." },
    { "Colour Broadcasting", 1960, 400000, 20,
      "Your shows in living colour.", "Your shows draw 6% more viewers; episodes cost 5% more." },
    { "Satellite Uplink", 1972, 900000, 20,
      "Beam your programmes across the continent.", "Launch a cable channel from 1972; it reaches more homes." },
    { "Stereo Sound", 1984, 600000, 12,
      "Television you can hear in stereo.", "Your shows draw 3% more viewers." },
    { "High Definition", 1998, 2000000, 24,
      "Every pore, every bead of sweat.", "Better episodes, for 8% more each." },
    { "Content Delivery Network", 2006, 3000000, 20,
      "Servers close to every viewer.", "Your streaming service carries 50% more subscribers before it falls over." },
    { "Recommendation AI", 2012, 5000000, 24,
      "It knows what you want to watch next.", "Streaming subscribers leave 30% less often." },
};

namespace {

int   g_done = 0;            // bit per tech
int   g_current = -1;
float g_progress = 0;        // weeks spent on the current project
int   g_challengesDone = 0;  // bit per challenge
int   g_numberOnes = 0;      // weeks with your show at #1

struct Challenge {
    const char* name;
    const char* goal;
    float       reward1950;
};
constexpr int kChallengeCount = 6;
const Challenge kChallenges[kChallengeCount] = {
    { "Must-See TV", "Have the number one show in the weekly ratings.", 50000 },
    { "Network Pioneer", "Found your own network and take it national.", 150000 },
    { "Long Runner", "Make 100 episodes of one show.", 100000 },
    { "Rerun Riches", "Earn $1,000,000 (in 1950 money) from reruns.", 200000 },
    { "Golden Touch", "Win five Golden Antenna Awards.", 150000 },
    { "Streaming Giant", "Reach 25 million streaming subscribers.", 2000000 },
};

// Progress towards a challenge, 0..1, and a line describing it.
float ChallengeProgress(int i, std::string* text)
{
    float p = 0;
    std::string t;
    switch (i) {
    case 0:
        p = g_numberOnes > 0 ? 1.0f : 0.0f;
        t = g_numberOnes ? Format("%d week%s at number one", g_numberOnes, g_numberOnes == 1 ? "" : "s")
                         : "Not yet. Check the Ratings tab after Sunday night.";
        break;
    case 1: {
        const NetworkState& own = g_networks[kPlayerNetwork];
        p = own.exists ? (own.reach + 1) / 3.0f : 0.0f;
        t = own.exists ? Format("%s: %s", own.name.c_str(), kReachNames[own.reach]) : "No network yet";
        break;
    }
    case 2: {
        int best = 0;
        for (const Series& s : g_series)
            best = std::max(best, s.totalAired);
        p = std::min(1.0f, best / 100.0f);
        t = Format("Longest-running show: %d episodes", best);
        break;
    }
    case 3: {
        double total = 0;
        for (const Series& s : g_series)
            total += s.syndIncome / EraFactor(Year());
        p = static_cast<float>(std::min(1.0, total / 1000000.0));
        t = Format("%s in 1950 money", Dollars(total).c_str());
        break;
    }
    case 4: {
        int won = 0;
        for (const Series& s : g_series)
            won += s.awards;
        p = std::min(1.0f, won / 5.0f);
        t = Format("%d of 5 Golden Antennas", won);
        break;
    }
    case 5:
        p = std::min(1.0f, StreamingSubscribers() / 25.0f);
        t = StreamingExists() ? Format("%.1f million subscribers", StreamingSubscribers()) : "No streaming service yet";
        break;
    }
    if (text)
        *text = t;
    return p;
}

void CompleteChallenge(int i)
{
    g_challengesDone |= 1 << i;
    const Challenge& c = kChallenges[i];
    double reward = c.reward1950 * EraFactor(Year()) * g_incomeScale;
    game::AddMoney(reward);
    Notify(Format("Challenge complete: %s", c.name), Format("%s Reward: %s.", c.goal, Dollars(reward).c_str()), kGold);
    RecordHistory(Format("Challenge complete: %s", c.name));

    Newspaper p;
    p.day = Today();
    float year = Year();
    p.paper = year < 1960 ? "THE DAILY BROADCAST" : year < 1980 ? "TV TIMES" : year < 2000 ? "THE NATIONAL TV REVIEW" : "SCREEN DAILY";
    p.kicker = "INDUSTRY NEWS";
    std::string name = c.name;
    for (char& ch : name)
        ch = static_cast<char>(toupper(static_cast<unsigned char>(ch)));
    p.headline = "STUDIO ACHIEVES " + name;
    p.subhead = c.goal;
    std::string progress;
    ChallengeProgress(i, &progress);
    p.body = Format("Industry watchers are taking note: %s. The studio collects %s in bonuses and goodwill.",
                    progress.c_str(), Dollars(reward).c_str());
    p.quote = "\"They're changing what television can be.\"";
    PrintPaper(p);
    if (g_paperAuto) {
        g_paperOpen = static_cast<int>(g_papers.size()) - 1;
        overlay::Open();
    }
}

} // namespace

bool HasTech(int tech) { return (g_done & (1 << tech)) != 0; }

float TechQualityBonus(int format)
{
    float b = 0;
    bool studioFormat = format == Sitcom || format == Soap || format == TalkShow || format == GameShow;
    if (HasTech(TechMultiCamera) && studioFormat)
        b += 0.04f;
    if (HasTech(TechRemoteVans) && (format == TalkShow || format == GameShow || format == Documentary))
        b += 0.03f;
    if (HasTech(TechHD))
        b += 0.04f;
    return b;
}

float TechCostMul()
{
    float m = 1.0f;
    if (HasTech(TechVideotape))
        m *= 0.92f;
    if (HasTech(TechColour))
        m *= 1.05f;
    if (HasTech(TechHD))
        m *= 1.08f;
    return m;
}

float TechAppeal(int format)
{
    float m = 1.0f;
    if (HasTech(TechLaughTrack) && format == Sitcom)
        m *= 1.06f;
    if (HasTech(TechColour))
        m *= 1.06f;
    if (HasTech(TechStereo))
        m *= 1.03f;
    return m;
}

// ---------------------------------------------------------------------------------
// Live episodes

bool CanGoLive(const Series& s, std::string* why)
{
    std::string reason;
    if (s.format == Miniseries)
        reason = "Miniseries are filmed.";
    else if (!(s.status == OnAir || s.status == Ordered) || s.aired >= s.order)
        reason = "Only a show in production can go live.";
    else if (s.network == kStreamingNetwork)
        reason = "Streaming shows aren't broadcast live.";
    else if (s.liveSeason == s.season)
        reason = "One live episode a season.";
    if (why)
        *why = reason;
    return reason.empty();
}

void PlanLive(Series& s)
{
    s.livePending = true;
    s.liveSeason = s.season;
    Notify(Format("'%s' goes live!", s.title.c_str()),
           "The next episode airs live: a bigger audience, at 50% more cost - and anything can happen.", kGold);
}

// Called after the live episode aired.
void LiveOutcome(Series& s)
{
    s.livePending = false;
    bool unscripted = s.format == TalkShow || s.format == GameShow || s.format == Reality || s.format == Documentary;
    float risk = (unscripted ? 0.18f : 0.30f) * (HasTech(TechRemoteVans) ? 0.5f : 1.0f);
    float roll = Roll(s.id, s.season * 31 + s.aired, 77, 0.0f, 1.0f);
    if (roll < risk * 0.4f) {
        s.freshness = std::min(1.0f, s.freshness + 0.08f);
        Notify(Format("Legendary blunder on '%s'!", s.title.c_str()),
               "A live mistake so glorious the whole country is talking about it. Viewers love the show even more.", kGold);
        RecordHistory(Format("A legendary live blunder on '%s'", s.title.c_str()));
    } else if (roll < risk) {
        s.freshness = std::max(kMinFreshness, s.freshness - 0.05f);
        Notify(Format("Live disaster on '%s'", s.title.c_str()),
               "Missed cues, a collapsing set and a swear word on air. The network is not amused.", kRed);
        RecordHistory(Format("A live disaster on '%s'", s.title.c_str()));
    } else {
        s.freshness = std::min(1.0f, s.freshness + 0.03f);
        Notify(Format("'%s' nails it live", s.title.c_str()), "A flawless live episode. The critics are impressed.", kGreen);
    }
}

// ---------------------------------------------------------------------------------
// Weekly

void ResearchWeekly(float day)
{
    if (static_cast<int>(day) % kNights != 0)
        return;
    if (g_current >= 0) {
        g_progress += 1.0f;
        if (g_progress >= kTechs[g_current].weeks) {
            int t = g_current;
            g_done |= 1 << t;
            g_current = -1;
            g_progress = 0;
            Notify(Format("Research complete: %s", kTechs[t].name), kTechs[t].effect, kGold);
            RecordHistory(Format("Research: %s", kTechs[t].name));
        }
    }
    if (!g_lastWeek.empty() && g_lastWeek[0].player)
        g_numberOnes++;
    for (int i = 0; i < kChallengeCount; i++)
        if (!(g_challengesDone & (1 << i)) && ChallengeProgress(i, nullptr) >= 1.0f)
            CompleteChallenge(i);
}

// ---------------------------------------------------------------------------------
// Research tab (also lists the challenges)

void DrawResearch()
{
    float year = Year();
    float era = EraFactor(year);
    if (g_current >= 0) {
        const TechInfo& t = kTechs[g_current];
        ImGui::Text("Researching: %s", t.name);
        ImGui::ProgressBar(g_progress / t.weeks, ImVec2(360, 0),
                           Format("%d of %d weeks", static_cast<int>(g_progress), t.weeks).c_str());
    } else {
        ImGui::TextColored(kGrey, "Nothing in the lab right now. Pick a project below.");
    }
    ImGui::Spacing();
    constexpr ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit;
    if (ImGui::BeginTable("research", 4, flags)) {
        ImGui::TableSetupColumn("Technology");
        ImGui::TableSetupColumn("What it does", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Cost");
        ImGui::TableSetupColumn("");
        ImGui::TableHeadersRow();
        for (int i = 0; i < TechCount; i++) {
            const TechInfo& t = kTechs[i];
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(t.name);
            ImGui::TextColored(kGrey, "from %d, %d weeks", t.year, t.weeks);
            ImGui::TableNextColumn();
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(t.blurb);
            ImGui::TextColored(kGreen, "%s", t.effect);
            ImGui::PopTextWrapPos();
            ImGui::TableNextColumn();
            double cost = t.cost1950 * era * g_costScale;
            ImGui::TextUnformatted(Dollars(cost).c_str());
            ImGui::TableNextColumn();
            if (HasTech(i)) {
                ImGui::TextColored(kGreen, "Done");
            } else if (g_current == i) {
                ImGui::TextColored(kGold, "In the lab");
            } else if (year < t.year) {
                ImGui::TextColored(kGrey, "Not yet");
            } else {
                ImGui::BeginDisabled(g_current >= 0 || game::ReadMoney().capital < cost);
                if (ImGui::Button(Format("Research##t%d", i).c_str())) {
                    game::AddMoney(-cost);
                    g_current = i;
                    g_progress = 0;
                    Notify(Format("Research begins: %s", t.name), Format("About %d weeks in the lab.", t.weeks), kGrey);
                }
                ImGui::EndDisabled();
            }
        }
        ImGui::EndTable();
    }

    ImGui::SeparatorText("Challenges");
    for (int i = 0; i < kChallengeCount; i++) {
        const Challenge& c = kChallenges[i];
        std::string text;
        float p = ChallengeProgress(i, &text);
        bool done = (g_challengesDone & (1 << i)) != 0;
        if (done)
            RankBadge(i + 1);
        else
            ImGui::Dummy(ImVec2(ImGui::GetTextLineHeight() + 8, 1));
        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::TextColored(done ? kGold : ImGui::GetStyleColorVec4(ImGuiCol_Text), "%s%s", c.name, done ? "  - complete!" : "");
        ImGui::TextColored(kGrey, "%s  Reward %s.", c.goal, Dollars(c.reward1950 * EraFactor(year) * g_incomeScale).c_str());
        ImGui::ProgressBar(done ? 1.0f : p, ImVec2(360, 0), text.c_str());
        ImGui::EndGroup();
    }
}

// ---------------------------------------------------------------------------------
// Save data

void ResearchSave(std::map<std::string, std::string>& out)
{
    out["state"] = Format("%d,%d,%.2f,%d,%d", g_done, g_current, g_progress, g_challengesDone, g_numberOnes);
}

void ResearchReset()
{
    g_done = 0;
    g_current = -1;
    g_progress = 0;
    g_challengesDone = 0;
    g_numberOnes = 0;
}

void ResearchLoad(const std::map<std::string, std::string>& in)
{
    ResearchReset();
    auto it = in.find("state");
    if (it != in.end())
        sscanf_s(it->second.c_str(), "%d,%d,%f,%d,%d", &g_done, &g_current, &g_progress, &g_challengesDone, &g_numberOnes);
    if (g_current >= TechCount)
        g_current = -1;
}

} // namespace tv
