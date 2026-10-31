// Long-running shows (Phase 3): season plans and finales, star contracts and departures,
// renewal talks, spinoffs, and the Season Finale newspaper's story.

#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <cmath>

#include "mod.h"
#include "news.h"
#include "overlay.h"
#include "tv_internal.h"

namespace tv {

const char* const kPlanNames[PlanCount] = { "Standalone episodes", "Season-long story arc", "Event season" };
const char* const kPlanHelp[PlanCount] = {
    "Self-contained episodes. Steady, and easy for new viewers to pick up.",
    "One story across the season: slower early episodes that build to a big finale, and audiences stay hooked "
    "longer. Best for dramas, soaps and science fiction.",
    "Bigger stories and guest stars: better episodes and bigger premieres and finales, for 35% more per episode.",
};
const char* const kFinaleNames[FinaleCount] = { "Closure", "Cliffhanger" };

std::deque<Newspaper> g_papers;
int  g_paperOpen = -1;
bool g_paperAuto = true;

namespace {

constexpr size_t kMaxPapers = 24;

} // namespace

void PrintPaper(const Newspaper& p)
{
    g_papers.push_back(p);
    while (g_papers.size() > kMaxPapers)
        g_papers.pop_front();
    std::string body = p.body;
    if (!p.verdict.empty())
        body += "\n\n" + p.verdict;
    if (!p.quote.empty())
        body += "\n\n" + p.quote;
    news::Post(p.headline, p.kicker.empty() ? p.subhead : p.kicker + ": " + p.subhead, body);
}

namespace {

// How much a format gains from a story told across the season.
float ArcStrength(int format)
{
    switch (format) {
    case Serialized: case Soap: case Miniseries: return 1.0f;
    case SciFi:      return 0.9f;
    case Procedural: return 0.5f;
    case Reality:    return 0.5f;
    case Sitcom:     return 0.35f;
    default:         return 0.2f;
    }
}

const char* const kCities[] = { "Metro", "Harbor City", "Central City", "Bay City", "Union City", "Lakeside", "Capitol",
                                "Midtown", "Westside", "Southport" };
const char* const kFrontiers[] = { "Mars", "Orion", "Andromeda", "Saturn", "the Rings", "Station Nine" };

std::string Upper(std::string s)
{
    for (char& c : s)
        c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
    return s;
}

std::string Homes(float millions)
{
    if (millions >= 1.0f)
        return Format("%.1f million", millions);
    return Format("%.0f thousand", millions * 1000.0f);
}

int WeekRank(float rating)
{
    int rank = 1;
    for (const ChartEntry& e : g_lastWeek)
        rank += e.rating > rating;
    return rank;
}

std::string CliffhangerLine(const Series& s)
{
    std::string who = s.lead.empty() ? "" : s.lead;
    switch (s.format) {
    case Sitcom:      return "Wedding-day chaos leaves the nation asking: will they or won't they?";
    case Procedural:  return (who.empty() ? std::string("The detective") : who) + " left for dead as the credits roll";
    case Serialized:  return "A family empire in ruins - and a body in the lake";
    case Soap:        return "WHO SHOT " + (who.empty() ? std::string("THE PATRIARCH") : Upper(who)) + "? The question on everyone's lips";
    case SciFi:       return Format("Ship lost beyond %s as the season ends in darkness", kFrontiers[s.id % 6]);
    case TalkShow:    return "Surprise guest storms off - live";
    case GameShow:    return "Record jackpot left on the table";
    case Reality:     return "Shock exit leaves the house in tears";
    case Documentary: return "Expedition falls silent";
    default:          return "Nothing will ever be the same";
    }
}

void PublishPaper(Series& s, float avgShare, float needed)
{
    float year = Year();
    float rating = s.lastRating;
    float share = s.lastShare;
    float homes = rating / 100.0f * Households(year);
    float avg = s.aired ? s.ratingSum / s.aired : 0;
    bool ended = s.status == Ended;
    bool cancelled = s.status == Cancelled;
    bool renewed = s.status == Ordered || s.status == RenewalOffered;
    uint32_t dice = HashText(s.title) + s.season;

    Newspaper p;
    p.day = Today();
    p.seriesId = s.id;
    p.paper = year < 1960 ? "THE DAILY BROADCAST" : year < 1980 ? "TV TIMES" : year < 2000 ? "THE NATIONAL TV REVIEW"
                                                                                          : "SCREEN DAILY";
    p.kicker = s.format == Miniseries ? "MINISERIES FINALE" : (ended || cancelled) ? "SERIES FINALE"
                                                                                  : Format("SEASON %d FINALE", s.season);
    bool record = rating >= std::max(s.premiereRating, s.bestRating) - 0.05f && share >= 0.38f;
    std::string title = Upper(s.title);
    if (cancelled)
        p.headline = Format("%s PULLS THE PLUG ON '%s'", NetworkShort(s.network), title.c_str());
    else if (ended && s.format != Miniseries)
        p.headline = Format("FAREWELL, '%s'", title.c_str());
    else if (record)
        p.headline = dice % 2 ? Format("%s HOMES TUNE IN FOR '%s'", Upper(Homes(homes)).c_str(), title.c_str())
                              : Format("THE NATION STOPS FOR '%s'", title.c_str());
    else if (avgShare >= needed)
        p.headline = share >= 0.4f ? Format("'%s' FINALE WINS THE NIGHT", title.c_str())
                                   : Format("'%s' DELIVERS FOR %s", title.c_str(), Upper(NetworkShort(s.network)).c_str());
    else
        p.headline = Format("'%s' LIMPS TO SEASON'S END", title.c_str());

    if (s.finale == FinaleCliffhanger && s.format != Miniseries)
        p.subhead = CliffhangerLine(s) + (cancelled || ended ? " - and now we may never know." : "");
    else
        p.subhead = s.format == Miniseries ? "A story told in full, and told well"
                    : ended                ? "The last bow: loose ends tied, hearts won"
                                           : Format("A satisfying close to season %d", s.season);

    std::string trend;
    if (s.premiereRating > 0 && s.aired > 1)
        trend = rating >= s.premiereRating ? Format(", up from %.1f at the premiere", s.premiereRating)
                                           : Format(", down from %.1f at the premiere", s.premiereRating);
    if (s.network == kStreamingNetwork)
        p.body = Format("%s of %s's %.1f million subscribers made it to the end of '%s'%s. %s",
                        Format("%.0f%%", share * 100).c_str(), NetworkName(s.network), StreamingSubscribers(),
                        s.title.c_str(), s.binge ? ", most of them within days of the drop" : "",
                        avgShare >= 0.25f ? "Analysts call it a subscriber magnet." : "Analysts wonder if it was worth it.");
    else
        p.body = Format("%s homes watched the finale of '%s' on %s: a %.1f rating and %.0f%% of everyone watching TV "
                        "that hour%s. It ranked number %d for the week. The season averaged a %.1f rating.",
                        Homes(homes).c_str(), s.title.c_str(), NetworkName(s.network), rating, share * 100, trend.c_str(),
                        WeekRank(rating), avg);
    if (!s.lead.empty())
        p.body += Format(" Star %s %s.", s.lead.c_str(),
                         record ? "is the talk of the town" : avgShare >= needed ? "earned warm notices" : "deserved better material");
    if (s.parentId)
        if (const Series* parent = FindSeries(s.parentId))
            p.body += Format(" The spinoff of '%s' %s.", parent->title.c_str(),
                             avgShare >= needed ? "has found its own audience" : "never escaped its parent's shadow");

    if (cancelled) {
        p.verdict = Format("%s has cancelled the show after %d season%s.", NetworkName(s.network), s.season,
                           s.season == 1 ? "" : "s");
        if (s.finale == FinaleCliffhanger)
            p.verdict += " Fans have flooded the network with letters demanding an ending.";
    } else if (ended) {
        p.verdict = Format("The series bows out after %d season%s and %d episodes.", s.season, s.season == 1 ? "" : "s",
                           s.totalAired);
    } else if (OwnOutlet(s.network)) {
        p.verdict = s.status == Ordered ? Format("Season %d is on its way.", s.season)
                                        : "Will it be back? That's your network's call (My Shows tab).";
    } else if (renewed) {
        p.verdict = s.status == Ordered ? Format("%s has ordered season %d.", NetworkName(s.network), s.season)
                                        : Format("%s wants %d more episodes.", NetworkName(s.network), s.order);
    }
    if (s.demand)
        p.verdict += Format(" Insiders say %s wants a %d%% raise to return.", s.lead.c_str(), s.demand);
    if (!s.formerLead.empty())
        p.verdict += Format(" %s will not be back.", s.formerLead.c_str());

    float q = s.lastQuality;
    p.quote = q >= 0.75f ? "\"Television at its very finest.\""
              : q >= 0.6f ? "\"Smart, warm and worth your evening.\""
              : q >= 0.45f ? "\"Pleasant enough, if you're home anyway.\""
                           : "\"Our set deserved better.\"";

    int n = 0;
    for (const ChartEntry& e : g_lastWeek) {
        if (n == 5)
            break;
        p.sidebar += Format("%d. %s (%s) %.1f\n", ++n, e.title.c_str(), NetworkShort(e.network), e.rating);
    }

    PrintPaper(p);
    mod::Log("tv: newspaper - %s / %s", p.headline.c_str(), p.subhead.c_str());
    if (g_paperAuto) {
        g_paperOpen = static_cast<int>(g_papers.size()) - 1;
        overlay::Open();
    }
}

void StarLeaves(Series& s, const char* why)
{
    Notify(Format("%s leaves '%s'", s.lead.c_str(), s.title.c_str()),
           Format("%s Recast the role: drop a new star on the TV Production Office's Cast zone.", why), kRed);
    s.formerLead = s.lead;
    s.lead.clear();
    s.leadPay = 1.0f;
    s.leadSeasons = 0;
    s.demand = 0;
}

} // namespace

double LeadPay1950(const Series& s) { return s.lead.empty() ? 0.0 : 1500.0 * s.leadPay; }

float PlanQuality(const Series& s, int episode)
{
    switch (s.plan) {
    case PlanArc: {
        float t = s.order > 1 ? static_cast<float>(episode - 1) / (s.order - 1) : 1.0f;
        return ArcStrength(s.format) * (-0.025f + 0.085f * t);
    }
    case PlanEvent:
        return 0.045f;
    default:
        return 0.0f;
    }
}

float PlanCostMul(const Series& s) { return s.plan == PlanEvent ? 1.35f : 1.0f; }

float EventBoost(const Series& s)
{
    int ep = s.aired + 1;
    float b = 1.0f;
    if (ep == 1) {
        b *= 1.15f;
        if (s.cliffhanger)
            b *= 1.30f;   // everyone wants to know how it ends
        if (s.plan == PlanEvent)
            b *= 1.10f;
        if (s.parentId && s.season == 1)
            b *= 1.12f;   // the parent show's fans come to look
        b *= 1.0f + 0.05f * std::min(s.awards, 3);   // award winners draw a crowd
    }
    if (ep == s.order && s.order > 1) {
        b *= 1.10f;
        if (s.finale == FinaleCliffhanger)
            b *= 1.08f;
        if (s.plan == PlanArc)
            b *= 1.0f + 0.08f * ArcStrength(s.format);
        if (s.plan == PlanEvent)
            b *= 1.10f;
    }
    if (s.lead.empty() && !s.formerLead.empty())
        b *= 0.92f;       // viewers miss the star
    if (s.livePending)
        b *= 1.35f;       // live television: everyone tunes in
    b *= TechAppeal(s.format);
    return b;
}

float FreshnessLoss(const Series& s)
{
    float loss = s.showrunner.empty() ? 0.006f : 0.003f;
    // Long runs wear out: from the fourth season every season tires the audience faster.
    loss *= 1.0f + 0.15f * std::max(0, s.season - 3);
    if (s.plan == PlanArc)
        loss *= 1.0f - 0.35f * ArcStrength(s.format);
    else if (s.plan == PlanEvent)
        loss *= 0.85f;
    return loss;
}

void AfterAiring(Series& s, float quality, float rating)
{
    s.qualitySum += quality;
    s.lastReleaseDay = Today();
    if (s.livePending)
        LiveOutcome(s);
    if (s.aired == 1) {
        s.premiereRating = rating;
        s.cliffhanger = false;
        if (s.demand)
            ResolveDemand(s, false);   // nobody answered the star in time
    }
    if (s.order > 2 && s.aired == s.order - 1)
        Notify(Format("Finale next week: '%s'", s.title.c_str()),
               s.finale == FinaleCliffhanger ? "The cast has been sworn to secrecy." : "The season wraps up next week.",
               kGold);
}

// Season over and the network has decided: the star's contract, cliffhangers and the paper.
void SeasonFinale(Series& s, float avgShare, float needed)
{
    s.lastQuality = s.aired ? s.qualitySum / s.aired : s.pilotQuality;
    bool continues = s.status == Ordered || s.status == RenewalOffered;
    if (s.finale == FinaleCliffhanger && s.format != Miniseries) {
        if (continues)
            s.cliffhanger = true;
        else if (s.status == Cancelled)
            Notify(Format("Fans furious over '%s'", s.title.c_str()), "The cliffhanger will never be resolved.", kRed);
    }

    if (s.animated && !s.lead.empty())   // the lead's voice gets known, hit or not
        VoiceSeason(s.lead, s.title, needed > 0 ? avgShare / needed : 1.0f);
    if (s.mascot)   // a season on television counts for the mascot, as a cartoon less
        MascotShown(s.title, s.lastQuality, 0.6f);
    if (continues && !s.lead.empty()) {
        s.leadSeasons++;
        // Stars of hits want more money; a star who has carried a show for years may move on.
        float success = needed > 0 ? avgShare / needed : 1.0f;
        int raise = static_cast<int>(std::clamp((success - 1.0f) * 80.0f + (s.leadSeasons - 1) * 6.0f, 0.0f, 60.0f));
        raise = raise / 5 * 5;
        if (s.leadSeasons >= 3 && Roll(s.id, s.season, 31, 0.0f, 1.0f) < 0.12f) {
            StarLeaves(s, Format("After %d seasons, %s is off to make films.", s.leadSeasons, s.lead.c_str()).c_str());
        } else if (raise >= 10) {
            s.demand = raise;
            Notify(Format("%s wants a raise", s.lead.c_str()),
                   Format("The star of '%s' wants %d%% more to come back for season %d.", s.title.c_str(), raise,
                          s.season + (s.status == Ordered ? 0 : 1)),
                   kYellow);
            if (g_level == Casual)
                ResolveDemand(s, s.income >= s.cost);
        }
    }
    PublishPaper(s, avgShare, needed);
}

void ResolveDemand(Series& s, bool pay)
{
    if (!s.demand || s.lead.empty())
        return;
    int demand = s.demand;
    s.demand = 0;
    if (pay) {
        s.leadPay *= 1.0f + demand / 100.0f;
        Notify(Format("%s signs on", s.lead.c_str()),
               Format("Back for more '%s' at %d%% more per episode.", s.title.c_str(), demand), kGreen);
        return;
    }
    float walk = std::clamp(0.15f + demand / 120.0f, 0.2f, 0.75f);
    if (Roll(s.id, s.season * 7 + demand, 53, 0.0f, 1.0f) < walk) {
        StarLeaves(s, "Talks broke down.");
    } else {
        s.freshness = std::max(kMinFreshness, s.freshness - 0.03f);
        Notify(Format("%s returns, unhappy", s.lead.c_str()),
               "No raise. The star is back, but the mood on set has soured.", kYellow);
    }
}

void AskForRaise(Series& s)
{
    if (s.askedForRaise || s.status != RenewalOffered || OwnOutlet(s.network))
        return;
    s.askedForRaise = true;
    float avgShare = s.aired ? s.shareSum / s.aired : 0;
    float needed = std::max(0.01f, RenewalNeeded(s));
    float margin = (avgShare - needed) / needed;
    float chance = std::clamp(0.3f + margin * 2.0f, 0.1f, 0.9f);
    const char* net = NetworkShort(s.network);
    if (Roll(s.id, s.season, 71, 0.0f, 1.0f) < chance) {
        s.feeMul *= 1.2f;
        Notify(Format("%s agrees to pay more", net),
               Format("'%s' earns 20%% more per episode from season %d.", s.title.c_str(), s.season + 1), kGreen);
    } else if (s.order > kLimitedOrder) {
        s.order = kLimitedOrder;
        Notify(Format("%s won't budge", net),
               Format("No raise - and the order for '%s' is cut to %d episodes.", s.title.c_str(), s.order), kRed);
    } else {
        Notify(Format("%s won't budge", net), "The offer stands, take it or leave it.", kYellow);
    }
}

bool CanSpinOff(const Series& s, std::string* why)
{
    auto fail = [&](const char* text) {
        if (why)
            *why = text;
        return false;
    };
    if (s.format == Miniseries || s.format == Documentary || s.format == GameShow)
        return fail("This kind of show doesn't spin off.");
    if (s.status == Cancelled || s.status == PassedOn || s.status == Pitching || s.status == Rework)
        return fail("Only a show that's on the air, or ended on a high, can spin off.");
    bool hit = s.season >= 2 || (s.season == 1 && s.aired >= s.order && s.bestRating >= 20.0f);
    if (!hit)
        return fail("Audiences need to love it first: finish a second season (or a first with a 20+ rating).");
    if (s.season - s.lastSpinoffSeason < 2)
        return fail("It spun off recently. Give it two more seasons.");
    int children = 0;
    for (const Series& c : g_series)
        children += c.parentId == s.id;
    if (children >= 2)
        return fail("It already has two spinoffs.");
    return true;
}

std::string SpinoffTitle(const Series& parent, int format, int variant)
{
    const std::string& t = parent.title;
    const char* city = kCities[(parent.id + variant) % 10];
    std::string options[3];
    switch (format) {
    case Procedural:
        options[0] = t + ": " + city;
        options[1] = t + ": Special Unit";
        options[2] = Format("%s: %s Precinct", t.c_str(), city);
        break;
    case Sitcom:
        options[0] = t + " & Co.";
        options[1] = "Life After " + t;
        options[2] = (parent.lead.empty() ? t : parent.lead) + "!";
        break;
    case Soap:
    case Serialized:
        options[0] = t + ": The Early Years";
        options[1] = "Return to " + t;
        options[2] = t + ": " + city;
        break;
    case SciFi:
        options[0] = t + ": The Next Generation";
        options[1] = t + ": Deep Space";
        options[2] = t + ": " + kFrontiers[(parent.id + variant) % 6];
        break;
    case TalkShow:
        options[0] = t + " Late Night";
        options[1] = t + " Weekend";
        options[2] = "After " + t;
        break;
    default:
        options[0] = t + ": The Next Chapter";
        options[1] = t + ": " + city;
        options[2] = t + " Returns";
        break;
    }
    return options[((variant % 3) + 3) % 3];
}

Series& CreateSpinoff(const Series& parent, const std::string& title, int format)
{
    Series n;
    n.id = g_nextId++;
    n.title = title;
    n.format = format;
    n.budget = parent.budget;
    n.deal = parent.deal;
    n.pilotKey = Format("spinoff:%d:%d", parent.id, n.id);
    n.pilotTitle = parent.title + " (spinoff)";
    float base = parent.lastQuality > 0 ? parent.lastQuality : parent.pilotQuality;
    n.pilotQuality = std::clamp(base * 0.95f + (format == parent.format ? 0.0f : -0.03f), 0.05f, 1.0f);
    n.pilotStars = parent.pilotStars;
    n.parentId = parent.id;
    n.plan = parent.plan;
    int network = parent.network;
    int parentId = parent.id;
    int parentSeason = parent.season;
    std::string parentTitle = parent.title;

    g_series.push_back(n);   // invalidates 'parent'
    Series& s = g_series.back();
    if (Series* p = FindSeries(parentId))
        p->lastSpinoffSeason = parentSeason;
    if (OwnOutlet(network)) {
        s.network = network;
        s.binge = network == kStreamingNetwork;
        OrderSeason(s, network == kStreamingNetwork ? 10 : FullOrder(format, Year()), Today() + kPremiereDelay);
        Notify(Format("Spinoff greenlit: '%s'", s.title.c_str()),
               Format("From the world of '%s'. Give it a time slot in the Schedule tab.", parentTitle.c_str()), kGold);
    } else {
        StartPitch(s, network);
    }
    mod::Log("tv: spinoff '%s' from '%s' (pilot quality %.2f)", s.title.c_str(), parentTitle.c_str(), s.pilotQuality);
    return s;
}

// ---------------------------------------------------------------------------------
// Save data: the newspapers.

void StorySave(std::map<std::string, std::string>& out)
{
    out["auto"] = g_paperAuto ? "1" : "0";
    out["papers"] = std::to_string(g_papers.size());
    for (size_t i = 0; i < g_papers.size(); i++) {
        const Newspaper& p = g_papers[i];
        auto put = [&](const char* k, const std::string& v) {
            std::string esc;
            for (char c : v)
                esc += c == '\n' ? std::string("\\n") : std::string(1, c);
            out[Format("p%zu.%s", i, k)] = esc;
        };
        put("day", Format("%.3f", p.day));
        put("series", std::to_string(p.seriesId));
        put("paper", p.paper);
        put("kicker", p.kicker);
        put("headline", p.headline);
        put("subhead", p.subhead);
        put("body", p.body);
        put("verdict", p.verdict);
        put("quote", p.quote);
        put("sidebar", p.sidebar);
    }
}

void StoryReset()
{
    g_papers.clear();
    g_paperOpen = -1;
    g_paperAuto = true;
}

void StoryLoad(const std::map<std::string, std::string>& in)
{
    StoryReset();
    auto get = [&](const std::string& k) {
        auto it = in.find(k);
        std::string v = it == in.end() ? std::string() : it->second, out;
        for (size_t i = 0; i < v.size(); i++) {
            if (v[i] == '\\' && i + 1 < v.size() && v[i + 1] == 'n') {
                out += '\n';
                i++;
            } else {
                out += v[i];
            }
        }
        return out;
    };
    g_paperAuto = get("auto") != "0";
    int count = atoi(get("papers").c_str());
    for (int i = 0; i < count; i++) {
        std::string k = Format("p%d.", i);
        Newspaper p;
        p.day = static_cast<float>(atof(get(k + "day").c_str()));
        p.seriesId = atoi(get(k + "series").c_str());
        p.paper = get(k + "paper");
        p.kicker = get(k + "kicker");
        p.headline = get(k + "headline");
        p.subhead = get(k + "subhead");
        p.body = get(k + "body");
        p.verdict = get(k + "verdict");
        p.quote = get(k + "quote");
        p.sidebar = get(k + "sidebar");
        g_papers.push_back(p);
    }
}

} // namespace tv
