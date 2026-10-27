// Polish (Phase 6): the yearly Golden Antenna Awards, television history and random events
// (scandals, strikes, milestones), and the Media Empire Value that sums up your empire.

#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <cmath>

#include "mod.h"
#include "news.h"
#include "overlay.h"
#include "tv_internal.h"

namespace tv {

namespace {

constexpr int   kAwardsFirstYear = 1949;
constexpr float kAwardsDayOfYear = 135;   // mid-May
constexpr float kNominationsDayOfYear = kAwardsDayOfYear - 30;   // nominations a month before

enum Category { BestComedy, BestDrama, BestVariety, BestDocumentary, BestMiniseries, BestLead, BestWriting, CategoryCount };
const char* const kCategoryNames[CategoryCount] = {
    "Outstanding Comedy Series", "Outstanding Drama Series", "Outstanding Variety Program",
    "Outstanding Documentary", "Outstanding Miniseries", "Outstanding Lead Performance", "Outstanding Writing",
};
// What the presenters say each one is for.
const char* const kCategoryBlurbs[CategoryCount] = {
    "Awarded to the comedy of the year: the series the critics praised and the most viewers laughed along with.",
    "Awarded to the drama series of the year, judged on its quality and on the audience it held.",
    "Awarded to the best variety, talk, game or reality program on the air.",
    "Awarded to the documentary that best informed, and gripped, the nation.",
    "Awarded to the finest miniseries of the year.",
    "Awarded to the lead performer who shone brightest in a scripted series this year.",
    "Awarded to the writers' room behind the best-written scripted series.",
};

struct Trophy {
    int         year = 0;
    int         category = 0;
    std::string title;      // show (and performer)
    bool        player = false;
    int         network = 0;    // a rival network's show: whose studio stands in for it
};

struct Milestone {
    float       year;       // fractional year it happens
    const char* title;
    const char* body;
};

// Fixed points of television history. Effects live in EventAppeal / StrikeActive.
const Milestone kMilestones[] = {
    { 1951.70f, "Coast to coast", "The first transcontinental broadcast links the country. Networks will reach every home within a decade." },
    { 1954.00f, "Colour comes to television", "The first colour sets go on sale. For now they cost as much as a car." },
    { 1958.60f, "Quiz show scandal!", "Contestants were fed the answers. Audiences turn their backs on game shows for years." },
    { 1960.20f, "Writers walk out", "The writers' guild is on strike. Scripted shows stop production until it's settled; unscripted shows fill the gap." },
    { 1961.40f, "\"A vast wasteland\"", "The FCC chairman attacks the quality of television. Networks raise their standards for a year." },
    { 1969.55f, "One small step", "Six hundred million people watch the moon landing. Television has never mattered more." },
    { 1976.00f, "The satellite age", "Satellites carry cable channels to every system in the country. Launch your own from the Library tab." },
    { 1988.20f, "Writers walk out again", "A long writers' strike halts scripted production. Unscripted shows have their moment." },
    { 2007.00f, "Broadband changes everything", "Homes can stream video. Launch a streaming service from the Streaming tab." },
    { 2007.85f, "The great writers' strike", "Writers strike over online residuals. Scripted shows halt; reality TV booms." },
};
constexpr int kMilestoneCount = sizeof(kMilestones) / sizeof(kMilestones[0]);
// Strikes: milestone index -> weeks.
const int kStrikes[][2] = { { 3, 21 }, { 7, 22 }, { 9, 14 } };

int  g_lastAwards = 0;
int  g_fired = 0;                // bit per milestone
float g_strikeUntil = 0;
float g_wastelandUntil = 0;
std::vector<Trophy> g_trophies;  // every winner, player or not
std::vector<std::pair<float, std::string>> g_timeline;   // day, what happened
std::vector<std::pair<int, double>> g_mevHistory;        // year, value

bool Scripted(int format) { return format != TalkShow && format != GameShow && format != Reality && format != Documentary; }

void AddTimeline(const std::string& text)
{
    g_timeline.push_back({ Today(), text });
    if (g_timeline.size() > 60)
        g_timeline.erase(g_timeline.begin());
}

const char* const kFirst[] = { "Lucy", "Jack", "Ruth", "Eddie", "Martha", "Frank", "Dottie", "Walter", "Betty", "Joe" };
const char* const kLast[] = { "Baxter", "Kowalski", "Hennessy", "Marlowe", "Pruitt", "Delgado", "Finch", "Kramer", "Doyle", "Sterling" };

struct Nominee {
    std::string title;      // what the papers print
    float       score = 0;
    Series*     series = nullptr;
    int         network = 0;
    int         aiId = -1;      // a rival network's show
};

// This year's nominees, fixed when they're announced: the awards are decided among them.
struct NomKey {
    bool player;
    int  id;      // Series::id or AiShow::id
};
int g_nomYear = 0;
std::vector<NomKey> g_nominated[CategoryCount];
int g_eveYear = 0;

bool Nominated(int c, bool player, int id)
{
    for (const NomKey& k : g_nominated[c])
        if (k.player == player && k.id == id)
            return true;
    return false;
}

bool InCategory(int category, int format)
{
    switch (category) {
    case BestComedy:      return format == Sitcom;
    case BestDrama:       return format == Procedural || format == Serialized || format == Soap || format == SciFi;
    case BestVariety:     return format == TalkShow || format == GameShow || format == Reality;
    case BestDocumentary: return format == Documentary;
    case BestMiniseries:  return format == Miniseries;
    case BestLead:        return Scripted(format);
    case BestWriting:     return Scripted(format);
    default:              return false;
    }
}

// The year's five nominees in a category, best first (the same every time it's asked).
std::vector<Nominee> Field(int c, int year, float today)
{
    std::vector<Nominee> field;
    bool frozen = g_nomYear == year;
    for (Series& s : g_series) {
        if (frozen ? !Nominated(c, true, s.id)
                   : !InCategory(c, s.format) || s.totalAired == 0 || today - s.lastReleaseDay > kDaysPerYear)
            continue;
        if (c == BestLead && s.lead.empty())
            continue;
        float q = s.aired ? s.qualitySum / s.aired : (s.lastQuality > 0 ? s.lastQuality : s.pilotQuality);
        float bonus = c == BestWriting ? 0.02f * s.writers.size() : c == BestLead ? 0.03f : 0.0f;
        if (c == BestComedy && HasTech(TechLaughTrack))
            bonus -= 0.03f;   // critics can hear the canned laughter
        Nominee n;
        n.title = c == BestLead ? Format("%s, '%s'", s.lead.c_str(), s.title.c_str()) : "'" + s.title + "'";
        n.score = q * 0.75f + s.lastShare * 0.6f + bonus + Roll(year, s.id, c, -0.06f, 0.06f);
        n.series = &s;
        field.push_back(n);
    }
    for (const AiShow& a : g_aiShows) {
        if (frozen ? !Nominated(c, false, a.id) : !InCategory(c, a.format) || a.weeks == 0)
            continue;
        Nominee n;
        n.title = c == BestLead ? Format("%s %s, '%s'", kFirst[a.id % 10], kLast[(a.id / 10) % 10], a.title.c_str())
                                : "'" + a.title + "'";
        n.score = a.quality * 0.75f + (a.shareSum / std::max(1, a.weeks)) * 0.6f + Roll(year, 100000 + a.id, c, -0.06f, 0.06f);
        n.network = a.network;
        n.aiId = a.id;
        field.push_back(n);
    }
    std::sort(field.begin(), field.end(), [](const Nominee& x, const Nominee& y) { return x.score > y.score; });
    field.resize(std::min<size_t>(field.size(), 5));
    return field;
}

// The ceremony's programme: every category with nominees.
std::vector<CeremonyCategory> Programme(int year, float today)
{
    std::vector<CeremonyCategory> out;
    for (int c = 0; c < CategoryCount; c++) {
        std::vector<Nominee> field = Field(c, year, today);
        if (field.empty())
            continue;
        CeremonyCategory cat;
        cat.name = kCategoryNames[c];
        cat.blurb = kCategoryBlurbs[c];
        for (const Nominee& n : field)
            cat.nominees.push_back({ n.title, n.series != nullptr, n.series ? n.series->network : n.network, n.score });
        out.push_back(cat);
    }
    return out;
}

// The last ceremony, to watch again from the Empire tab (this session only).
std::vector<CeremonyCategory> g_lastShow;
int g_lastShowEdition = 0, g_lastShowYear = 0;
bool g_paperAfterShow = false;

void PaperAfterShow()
{
    RefreshStudioRatings();
    if (g_paperAfterShow && !g_papers.empty()) {
        g_paperOpen = static_cast<int>(g_papers.size()) - 1;
        overlay::Open();
    }
    g_paperAfterShow = false;
}

// A month before the awards: the nominees are announced (a story on the game's timeline and
// the lot's tannoy) and fixed; the winners are picked from them on the night.
void AnnounceNominations(int year)
{
    float today = Today();
    int edition = year - kAwardsFirstYear + 1;
    std::string body, yours;
    int mine = 0;
    for (int c = 0; c < CategoryCount; c++)
        g_nominated[c].clear();
    g_nomYear = 0;   // a fresh field, not last year's nominees
    for (int c = 0; c < CategoryCount; c++) {
        std::vector<Nominee> field = Field(c, year, today);
        if (field.empty())
            continue;
        std::string names;
        for (size_t i = 0; i < field.size(); i++) {
            const Nominee& n = field[i];
            g_nominated[c].push_back({ n.series != nullptr, n.series ? n.series->id : n.aiId });
            names += (i ? ", " : "") + n.title;
            if (n.series) {
                yours += Format("%s%s (%s)", mine ? "; " : "", n.title.c_str(), kCategoryNames[c]);
                mine++;
            }
        }
        body += Format("%s: %s\n", kCategoryNames[c], names.c_str());
    }
    g_nomYear = year;
    if (body.empty())
        return;
    std::string ordinal = Format("%d%s", edition, edition % 100 / 10 == 1 ? "th" : edition % 10 == 1 ? "st" : edition % 10 == 2 ? "nd" : edition % 10 == 3 ? "rd" : "th");
    news::Post("GOLDEN ANTENNA NOMINATIONS", Format("The nominees for the %s Golden Antenna Awards", ordinal.c_str()),
               body + "The winners are revealed on television's biggest night, in a month.");
    AnnounceAwardsSoon();
    Notify("Golden Antenna nominations",
           mine ? Format("Your shows have %d nomination%s: %s. The awards are in a month.", mine, mine == 1 ? "" : "s",
                         yours.c_str())
                : std::string("None of your shows were nominated this year. The awards are in a month."),
           mine ? kGold : kGrey);
    AddTimeline(mine ? Format("Nominated: %s", yours.c_str()) : std::string("Golden Antenna nominations announced"));
    mod::Log("tv: Golden Antenna nominations %d - %d yours", year, mine);
}

void HoldAwards(int year)
{
    float today = Today();
    int edition = year - kAwardsFirstYear + 1;
    std::vector<std::string> wins, noms;
    std::string winnersText;
    std::vector<CeremonyCategory> show = Programme(year, today);
    for (int c = 0; c < CategoryCount; c++) {
        std::vector<Nominee> field = Field(c, year, today);
        if (field.empty())
            continue;
        const Nominee& w = field[0];
        g_trophies.push_back({ year, c, w.title, w.series != nullptr, w.series ? w.series->network : w.network });
        winnersText += Format("%s: %s\n", kCategoryNames[c], w.title.c_str());
        for (size_t i = 0; i < field.size(); i++) {
            if (!field[i].series)
                continue;
            if (i == 0) {
                Series& s = *field[0].series;
                s.awards++;
                s.freshness = std::min(1.0f, s.freshness + 0.05f);
                if (!OwnOutlet(s.network))
                    s.feeMul *= 1.05f;
                wins.push_back(Format("%s (%s)", w.title.c_str(), kCategoryNames[c]));
            } else {
                noms.push_back(Format("%s (%s)", field[i].title.c_str(), kCategoryNames[c]));
            }
        }
    }
    if (winnersText.empty())
        return;
    g_lastAwards = year;

    Newspaper p;
    p.day = today;
    p.paper = year < 1960 ? "THE DAILY BROADCAST" : year < 1980 ? "TV TIMES" : year < 2000 ? "THE NATIONAL TV REVIEW" : "SCREEN DAILY";
    std::string ordinal = Format("%d%s", edition, edition % 100 / 10 == 1 ? "th" : edition % 10 == 1 ? "st" : edition % 10 == 2 ? "nd" : edition % 10 == 3 ? "rd" : "th");
    p.kicker = "THE " + ordinal + " GOLDEN ANTENNA AWARDS";
    if (wins.size() >= 2)
        p.headline = "YOUR STUDIO SWEEPS THE ANTENNAS";
    else if (wins.size() == 1)
        p.headline = Format("%s TAKES HOME AN ANTENNA", wins[0].substr(0, wins[0].find(" (")).c_str());
    else if (!noms.empty())
        p.headline = "SNUBBED! NOMINEES GO HOME EMPTY-HANDED";
    else
        p.headline = Format("%s WINS BIG", g_trophies[g_trophies.size() - 1].title.c_str());
    for (char& ch : p.headline)
        ch = static_cast<char>(toupper(static_cast<unsigned char>(ch)));
    p.subhead = wins.empty() ? "Television's finest gather for a night of glamour - and nerves"
                             : Format("%d award%s for your shows on television's biggest night", static_cast<int>(wins.size()),
                                      wins.size() == 1 ? "" : "s");
    p.body = "The winners:\n" + winnersText;
    if (!noms.empty()) {
        p.verdict = "Also nominated from your studio: ";
        for (size_t i = 0; i < noms.size(); i++)
            p.verdict += (i ? "; " : "") + noms[i];
        p.verdict += ".";
    }
    p.quote = wins.empty() ? "\"There's always next year.\"" : "\"I'd like to thank the network...\"";
    PrintPaper(p);
    for (const std::string& w : wins)
        AddTimeline("Won: " + w);
    bool contender = false;
    for (const Series& sr : g_series)
        contender |= InProduction(sr) || sr.status == RenewalOffered;
    // On the awards stage the results wait for the envelopes; the paper comes after the show.
    g_lastShow = show;
    g_lastShowEdition = edition;
    g_lastShowYear = year;
    g_paperAfterShow = (!wins.empty() || !noms.empty()) && g_paperAuto;
    if (QueueCeremony(edition, year, show, true, &PaperAfterShow)) {
        Notify(Format("The %s Golden Antenna Awards", ordinal.c_str()),
               !wins.empty() || !noms.empty() ? "Your shows are nominated! Television's biggest night is about to begin."
                                              : "Television's biggest night is about to begin.",
               kGold);
        mod::Log("tv: awards %d - %d wins, %d other nominations (on stage)", year, static_cast<int>(wins.size()),
                 static_cast<int>(noms.size()));
        return;
    }
    g_paperAfterShow = false;
    Notify(Format("The %s Golden Antenna Awards", ordinal.c_str()),
           wins.empty() ? (!noms.empty() ? std::string("Nominated, but no wins this year.")
                           : contender  ? std::string("None of your shows were nominated this year.")
                                        : Format("%s wins big. Get a show on the air and it could be you.",
                                                 g_trophies.back().title.c_str()))
                        : Format("Your shows won %d award%s!", static_cast<int>(wins.size()), wins.size() == 1 ? "" : "s"),
           wins.empty() ? kGrey : kGold);
    if ((!wins.empty() || !noms.empty()) && g_paperAuto) {
        g_paperOpen = static_cast<int>(g_papers.size()) - 1;
        overlay::Open();
    }
    RefreshStudioRatings();
    mod::Log("tv: awards %d - %d wins, %d other nominations", year, static_cast<int>(wins.size()), static_cast<int>(noms.size()));
}

void FireMilestone(int i)
{
    g_fired |= 1 << i;
    const Milestone& m = kMilestones[i];
    for (const auto& s : kStrikes)
        if (s[0] == i) {
            g_strikeUntil = Today() + s[1] * 7.0f;
            int halted = 0;
            for (Series& sr : g_series)
                if (InProduction(sr) && Scripted(sr.format)) {
                    sr.nextAirDay = std::max(sr.nextAirDay, g_strikeUntil);
                    halted++;
                }
            Notify(m.title, Format("%s %d of your shows are halted for about %d weeks.", m.body, halted, s[1]), kRed);
            AddTimeline(m.title);
            return;
        }
    if (i == 4)
        g_wastelandUntil = Today() + kDaysPerYear;
    Notify(m.title, m.body, i == 2 ? kRed : kGold);
    AddTimeline(m.title);
}

} // namespace

void RecordHistory(const std::string& text) { AddTimeline(text); }

// Format appeal changed by events: the quiz show scandal, unscripted shows during strikes.
bool WritersOnStrike() { return Today() < g_strikeUntil; }

float EventAppeal(int format, float year)
{
    float m = 1.0f;
    if (format == GameShow && year >= 1958.6f && year < 1962.0f)
        m *= 0.6f;
    if (!Scripted(format) && Today() < g_strikeUntil)
        m *= 1.25f;
    return m;
}

float StandardsBonus() { return Today() < g_wastelandUntil ? 6.0f : 0.0f; }

void EventsDaily(float day)
{
    float year = day / kDaysPerYear;
    for (int i = 0; i < kMilestoneCount; i++)
        if (!(g_fired & (1 << i)) && year >= kMilestones[i].year) {
            // Saves started after a milestone just mark it as history.
            if (year - kMilestones[i].year > 0.25f)
                g_fired |= 1 << i;
            else
                FireMilestone(i);
        }
    // During a strike, no scripted episodes are made.
    if (day < g_strikeUntil)
        for (Series& s : g_series)
            if (InProduction(s) && Scripted(s.format))
                s.nextAirDay = std::max(s.nextAirDay, g_strikeUntil);

    int y = static_cast<int>(year);
    float dayOfYear = day - y * kDaysPerYear;
    // Nominations a month before (or at once, for a game loaded after that day).
    if (y >= kAwardsFirstYear && y > g_lastAwards && y != g_nomYear && dayOfYear >= kNominationsDayOfYear &&
        dayOfYear < kAwardsDayOfYear && Unlocked())
        AnnounceNominations(y);
    if (g_nomYear == y && y > g_lastAwards && g_eveYear != y && dayOfYear >= kAwardsDayOfYear - 1 &&
        dayOfYear < kAwardsDayOfYear && Unlocked()) {
        g_eveYear = y;
        Notify("The Golden Antennas are tomorrow", "Television's biggest night: the winners are revealed tomorrow.", kGold);
    }
    // The ceremony is held when May comes round; a game loaded later in the year waits for next May.
    if (y >= kAwardsFirstYear && y > g_lastAwards && dayOfYear >= kAwardsDayOfYear && Unlocked()) {
        if (dayOfYear < kAwardsDayOfYear + 30)
            HoldAwards(y);
        else
            g_lastAwards = y;
    }

    if (static_cast<int>(day) % kNights != 0)
        return;
    // Stars get into trouble now and then (about one in ten a year).
    for (Series& s : g_series) {
        if (s.lead.empty() || s.scandal || !InProduction(s))
            continue;
        if (Roll(static_cast<uint32_t>(day), s.id, 404, 0.0f, 1.0f) < 0.002f) {
            s.scandal = true;
            s.freshness = std::max(kMinFreshness, s.freshness - 0.06f);
            Notify(Format("Scandal! %s in the papers", s.lead.c_str()),
                   Format("The star of '%s' is front-page news for all the wrong reasons. Stand by them or let them go "
                          "(My Shows).",
                          s.title.c_str()),
                   kRed);
            AddTimeline(Format("Scandal: %s ('%s')", s.lead.c_str(), s.title.c_str()));
        }
    }
    // A snapshot of the empire each new year.
    if (g_mevHistory.empty() || g_mevHistory.back().first < y)
        g_mevHistory.push_back({ y, MediaEmpireValue(nullptr) });
}

void ResolveScandal(Series& s, bool fire)
{
    if (!s.scandal)
        return;
    s.scandal = false;
    if (fire && !s.lead.empty()) {
        Notify(Format("%s is let go", s.lead.c_str()), Format("'%s' needs a new star.", s.title.c_str()), kYellow);
        s.formerLead = s.lead;
        s.lead.clear();
        s.leadPay = 1.0f;
        s.leadSeasons = 0;
        s.demand = 0;
    } else {
        // Standing by a star wins some loyalty back over time - or not.
        bool forgiven = Roll(s.id, s.season, 405, 0.0f, 1.0f) < 0.6f;
        s.freshness = std::clamp(s.freshness + (forgiven ? 0.04f : -0.03f), kMinFreshness, 1.0f);
        Notify(forgiven ? "The public forgives" : "The story won't go away",
               forgiven ? Format("Audiences rally behind %s.", s.lead.c_str())
                        : Format("Viewers are cooling on '%s'.", s.title.c_str()),
               forgiven ? kGreen : kYellow);
    }
}

double MediaEmpireValue(std::vector<std::pair<std::string, double>>* parts)
{
    float year = Year();
    double era = EraFactor(year);
    double cash = game::ReadMoney().capital;
    double library = 0;
    for (const Series& s : g_series)
        if (InLibrary(s) && OwnsReruns(s))
            library += LocalWeekly(s) * 52.0 * 4.0;
    double shows = 0;
    for (const Series& s : g_series)
        if (InProduction(s) || s.status == RenewalOffered)
            shows += kFormats[s.format].fee1950 * era * 10.0 * (s.lastShare > 0 ? s.lastShare * 3.0 : 1.0);
    double network = g_networks[kPlayerNetwork].exists ? kReachCost1950[g_networks[kPlayerNetwork].reach] * era * 1.5 : 0;
    double outlets = OutletValue();
    int won = 0;
    for (const Trophy& t : g_trophies)
        won += t.player;
    double prestige = won * 250000.0 * era;
    if (parts) {
        parts->push_back({ "Cash", cash });
        parts->push_back({ "Library (rerun value)", library });
        parts->push_back({ "Shows in production", shows });
        parts->push_back({ "Your network", network });
        parts->push_back({ "Cable and streaming", outlets });
        parts->push_back({ Format("Awards (%d)", won), prestige });
    }
    return cash + library + shows + network + outlets + prestige;
}

// ---------------------------------------------------------------------------------
// Empire tab

void DrawEmpire()
{
    std::vector<std::pair<std::string, double>> parts;
    double mev = MediaEmpireValue(&parts);
    ImGui::TextColored(kGold, "MEDIA EMPIRE VALUE");
    ImGui::SameLine();
    ImGui::Text("%s", Dollars(mev).c_str());
    HelpMarker("Everything your television business is worth: cash, the rerun value of your library, the shows you "
               "have in production, your network, cable channel and streaming service, and your awards.");
    if (ImGui::BeginTable("mev", 2, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg)) {
        for (const auto& p : parts) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(p.first.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%s", Dollars(p.second).c_str());
        }
        ImGui::EndTable();
    }
    if (g_mevHistory.size() > 1) {
        std::vector<float> v;
        for (const auto& h : g_mevHistory)
            v.push_back(static_cast<float>(h.second / 1e6));
        ImGui::PlotLines("##mev", v.data(), static_cast<int>(v.size()), 0, "Empire value by year ($ millions)", FLT_MAX,
                         FLT_MAX, ImVec2(-1, 70));
    }

    ImGui::SeparatorText("Trophy cabinet");
    int shown = 0;
    for (int i = static_cast<int>(g_trophies.size()) - 1; i >= 0; i--) {
        const Trophy& t = g_trophies[i];
        if (!t.player)
            continue;
        ImGui::BulletText("%d  %s - %s", t.year, kCategoryNames[t.category], t.title.c_str());
        shown++;
    }
    if (!shown)
        ImGui::TextDisabled("No Golden Antennas yet. The awards are held every May; good, popular shows win.");
    if (CeremonyEnabled()) {
        if (!g_lastShow.empty()) {
            if (ImGui::Button(Format("Watch the %d Golden Antenna Awards again", g_lastShowYear).c_str()))
                QueueCeremony(g_lastShowEdition, g_lastShowYear, g_lastShow, false, nullptr);
        } else if (g_devTools && Unlocked()) {
            // Nothing changes: the nominees as they stand, on stage.
            if (ImGui::Button("Preview this year's Golden Antenna ceremony (developer)")) {
                int y = static_cast<int>(Year());
                QueueCeremony(std::max(1, y - kAwardsFirstYear + 1), y, Programme(y, Today()), false, nullptr);
            }
        }
    }

    ImGui::SeparatorText("Television history");
    if (g_timeline.empty())
        ImGui::TextDisabled("Nothing yet.");
    for (int i = static_cast<int>(g_timeline.size()) - 1; i >= 0 && i >= static_cast<int>(g_timeline.size()) - 15; i--)
        ImGui::BulletText("%s  %s", DateText(g_timeline[i].first).c_str(), g_timeline[i].second.c_str());
    if (Today() < g_strikeUntil)
        ImGui::TextColored(kRed, "Writers' strike: scripted production resumes around %s.", DateText(g_strikeUntil).c_str());
}

// ---------------------------------------------------------------------------------
// Save data

std::vector<CeremonyNominee> LatestAntennaWinners()
{
    std::vector<CeremonyNominee> out;
    for (const Trophy& t : g_trophies)
        if (t.year == g_lastAwards)
            out.push_back({ t.title, t.player, t.network, 0 });
    return out;
}

void EventsSave(std::map<std::string, std::string>& out)
{
    out["state"] = Format("%d,%d,%.3f,%.3f", g_lastAwards, g_fired, g_strikeUntil, g_wastelandUntil);
    out["trophies"] = std::to_string(g_trophies.size());
    for (size_t i = 0; i < g_trophies.size(); i++) {
        out[Format("t%zu", i)] = Format("%d|%d|%d|%s", g_trophies[i].year, g_trophies[i].category, g_trophies[i].player ? 1 : 0,
                                        g_trophies[i].title.c_str());
        out[Format("t%zu.n", i)] = std::to_string(g_trophies[i].network);
    }
    out["timeline"] = std::to_string(g_timeline.size());
    for (size_t i = 0; i < g_timeline.size(); i++)
        out[Format("e%zu", i)] = Format("%.3f|%s", g_timeline[i].first, g_timeline[i].second.c_str());
    std::string h;
    for (const auto& m : g_mevHistory)
        h += Format(h.empty() ? "%d:%.0f" : ",%d:%.0f", m.first, m.second);
    out["mev"] = h;
    out["noms"] = Format("%d|%d", g_nomYear, g_eveYear);
    for (int c = 0; c < CategoryCount; c++) {
        std::string v;
        for (const NomKey& k : g_nominated[c])
            v += Format(v.empty() ? "%c%d" : ",%c%d", k.player ? 'p' : 'a', k.id);
        out[Format("nom%d", c)] = v;
    }
    out["show"] = Format("%d|%d|%zu", g_lastShowEdition, g_lastShowYear, g_lastShow.size());
    for (size_t i = 0; i < g_lastShow.size(); i++) {
        const CeremonyCategory& c = g_lastShow[i];
        out[Format("show%zu", i)] = Format("%zu|%s", c.nominees.size(), c.name.c_str());
        out[Format("show%zu.blurb", i)] = c.blurb;
        for (size_t k = 0; k < c.nominees.size(); k++) {
            const CeremonyNominee& n = c.nominees[k];
            out[Format("show%zu.%zu", i, k)] = Format("%d|%d|%.4f|%s", n.player ? 1 : 0, n.network, n.score, n.title.c_str());
        }
    }
}

void EventsReset()
{
    g_lastAwards = 0;
    g_fired = 0;
    g_strikeUntil = 0;
    g_wastelandUntil = 0;
    g_trophies.clear();
    g_timeline.clear();
    g_mevHistory.clear();
    g_lastShow.clear();
    g_lastShowEdition = g_lastShowYear = 0;
    g_nomYear = g_eveYear = 0;
    for (auto& n : g_nominated)
        n.clear();
    g_paperAfterShow = false;
}

void EventsLoad(const std::map<std::string, std::string>& in)
{
    EventsReset();
    auto get = [&](const std::string& k) {
        auto it = in.find(k);
        return it == in.end() ? std::string() : it->second;
    };
    sscanf_s(get("state").c_str(), "%d,%d,%f,%f", &g_lastAwards, &g_fired, &g_strikeUntil, &g_wastelandUntil);
    int n = atoi(get("trophies").c_str());
    for (int i = 0; i < n; i++) {
        std::string v = get(Format("t%d", i));
        Trophy t;
        int player = 0;
        int consumed = 0;
        if (sscanf_s(v.c_str(), "%d|%d|%d|%n", &t.year, &t.category, &player, &consumed) >= 3 && consumed > 0) {
            t.player = player != 0;
            t.category = std::clamp(t.category, 0, CategoryCount - 1);
            t.title = v.substr(consumed);
            t.network = atoi(get(Format("t%d.n", i)).c_str());
            g_trophies.push_back(t);
        }
    }
    n = atoi(get("timeline").c_str());
    for (int i = 0; i < n; i++) {
        std::string v = get(Format("e%d", i));
        size_t bar = v.find('|');
        if (bar != std::string::npos)
            g_timeline.push_back({ static_cast<float>(atof(v.substr(0, bar).c_str())), v.substr(bar + 1) });
    }
    std::string h = get("mev");
    for (size_t pos = 0; pos < h.size();) {
        size_t comma = h.find(',', pos);
        std::string item = h.substr(pos, comma - pos);
        size_t colon = item.find(':');
        if (colon != std::string::npos)
            g_mevHistory.push_back({ atoi(item.substr(0, colon).c_str()), atof(item.substr(colon + 1).c_str()) });
        pos = comma == std::string::npos ? h.size() : comma + 1;
    }
    sscanf_s(get("noms").c_str(), "%d|%d", &g_nomYear, &g_eveYear);
    for (int c = 0; c < CategoryCount; c++) {
        std::string v = get(Format("nom%d", c));
        for (size_t pos = 0; pos < v.size();) {
            size_t comma = v.find(',', pos);
            std::string item = v.substr(pos, comma - pos);
            if (item.size() > 1)
                g_nominated[c].push_back({ item[0] == 'p', atoi(item.c_str() + 1) });
            pos = comma == std::string::npos ? v.size() : comma + 1;
        }
    }
    int shows = 0;
    sscanf_s(get("show").c_str(), "%d|%d|%d", &g_lastShowEdition, &g_lastShowYear, &shows);
    for (int i = 0; i < shows && i < 32; i++) {
        std::string v = get(Format("show%d", i));
        int count = 0, consumed = 0;
        if (sscanf_s(v.c_str(), "%d|%n", &count, &consumed) < 1 || consumed == 0)
            continue;
        CeremonyCategory c;
        c.name = v.substr(consumed);
        c.blurb = get(Format("show%d.blurb", i));
        for (int k = 0; k < count && k < 8; k++) {
            std::string nv = get(Format("show%d.%d", i, k));
            CeremonyNominee n;
            int player = 0;
            consumed = 0;
            if (sscanf_s(nv.c_str(), "%d|%d|%f|%n", &player, &n.network, &n.score, &consumed) >= 3 && consumed > 0) {
                n.player = player != 0;
                n.title = nv.substr(consumed);
                c.nominees.push_back(n);
            }
        }
        if (!c.nominees.empty())
            g_lastShow.push_back(c);
    }
}

} // namespace tv
