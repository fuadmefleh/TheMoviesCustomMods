// Toon Studio: the studio mascot.
//
// The first short the animators start (tv_shorts.cpp), or else the first cartoon the studio
// releases, introduces its mascot (the star on the Animation Studio's mascot board), named from
// a list. Every cartoon released after that stars it, and every short shown before a film
// counts too, less:
//
//  * appeal (0..1) grows with each release, more for a better cartoon and less the higher it
//    already is, and fades over the years without one (half-life kFadeYears, or kIconFadeYears
//    once the mascot is an icon);
//  * staleness grows with each release and wears off over time (half-life kStaleYears):
//    releases too close together win less appeal, and a release while audiences are tired of
//    the mascot loses some;
//  * stages: Newcomer, Familiar Face, Star, Icon, each announced.
//
// What it does: the player's cartoons draw more at the box office (MascotPull, used by the
// success hook in ratings.cpp), up to +kMaxPull at full appeal, and from kMerchYear the mascot
// sells merchandise: income every month, reported once a year.

#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <set>

#include "mod.h"
#include "overlay.h"
#include "persist.h"
#include "tv_internal.h"

namespace tv {
namespace {

constexpr float kFadeYears = 8.0f, kIconFadeYears = 30.0f, kStaleYears = 1.5f;
constexpr float kStalePerRelease = 0.35f, kTiredAt = 0.75f, kTiredLoss = 0.06f;
constexpr float kMaxPull = 0.30f;
constexpr int kMerchYear = 1935;
constexpr double kMerchMonthly1950 = 30000;   // at full appeal, in 1950 dollars
constexpr float kDaysPerYear = 365.25f;

// The mascot board on the Animation Studio shows a beaming yellow star in white gloves.
const char* const kNames[] = {
    "Sparky",       "Twinkles",      "Lucky Star", "Dazzle",       "Comet Charlie", "Zippy",
    "Shooting Sam", "Starry Stan",   "Glimmer",    "Twinkle Toes", "Flash Freddy",  "Nova Nellie",
    "Spangles",     "Stellar Stu",   "Blinky",     "Gleamy Gus",
};

struct Stage {
    float       from;
    const char* name;
    const char* news;   // printf: the mascot's name
};
const Stage kStages[] = {
    { 0.00f, "Newcomer", "" },
    { 0.25f, "Familiar Face", "Audiences know %s now: your cartoons draw a little more." },
    { 0.50f, "Star", "%s is a star. Children ask for your cartoons by name." },
    { 0.80f, "Icon", "%s is an icon: known everywhere, and not soon forgotten." },
};
constexpr int kStageCount = sizeof(kStages) / sizeof(kStages[0]);

struct Mascot {
    std::string name;        // empty: not born yet
    float born = 0;          // day
    float appeal = 0, staleness = 0;
    float lastRelease = 0;   // day
    int   stage = 0;
    int   cartoons = 0;
    std::set<std::string> counted;   // released cartoons already counted (titles)
    double merchYear = 0;    // merchandise income so far this year
    int    merchYearNo = 0;
    float  lastMerch = 0;    // day of the last monthly income
};
Mascot g_mascot;
float g_lastDay = -1;

int StageOf(float appeal)
{
    int s = 0;
    for (int i = 0; i < kStageCount; i++)
        if (appeal >= kStages[i].from)
            s = i;
    return s;
}

void Name(const std::string& origin, float today)
{
    g_mascot.name = kNames[(GetTickCount() / 7 + static_cast<unsigned>(origin.size())) % _countof(kNames)];
    g_mascot.born = today;
    mod::Log("tv: mascot %s born with '%s'", g_mascot.name.c_str(), origin.c_str());
}

void Born(const std::string& firstCartoon, float today)
{
    Name(firstCartoon, today);
    Notify(Format("Meet %s", g_mascot.name.c_str()),
           Format("\"%s\" introduces your studio's mascot. Every cartoon you release stars %s: give audiences a "
                  "good one now and then, but not too often, and %s could become an icon.",
                  firstCartoon.c_str(), g_mascot.name.c_str(), g_mascot.name.c_str()),
           kGold);
}

// A cartoon came out (weight 1), or a short was shown (less): the mascot gains appeal (less
// when stale), and gets staler.
void Released(const std::string& title, float quality, float today, float weight = 1.0f)
{
    if (g_mascot.name.empty())
        Born(title, today);
    float q = std::clamp(quality, 0.0f, 1.0f);
    float before = g_mascot.appeal;
    bool tired = g_mascot.staleness >= kTiredAt;
    if (tired)
        g_mascot.appeal = std::max(0.0f, g_mascot.appeal - kTiredLoss * weight);
    else
        g_mascot.appeal += weight * (0.08f + 0.30f * q) * (1 - g_mascot.appeal) * (1 - g_mascot.staleness);
    g_mascot.staleness = std::min(1.0f, g_mascot.staleness + kStalePerRelease * weight);
    g_mascot.lastRelease = today;
    g_mascot.cartoons++;
    mod::Log("tv: mascot %s - '%s' (quality %.2f): appeal %.2f -> %.2f, staleness %.2f%s", g_mascot.name.c_str(),
             title.c_str(), q, before, g_mascot.appeal, g_mascot.staleness, tired ? " (tired)" : "");
    if (tired)
        Notify(Format("Audiences are tired of %s", g_mascot.name.c_str()),
               Format("\"%s\" came too soon after the last cartoon or short. Give %s a rest before the next one.", title.c_str(),
                      g_mascot.name.c_str()),
               kYellow);
}

void CheckStage()
{
    int stage = StageOf(g_mascot.appeal);
    if (stage == g_mascot.stage)
        return;
    bool up = stage > g_mascot.stage;
    g_mascot.stage = stage;
    mod::Log("tv: mascot %s is now %s (appeal %.2f)", g_mascot.name.c_str(), kStages[stage].name, g_mascot.appeal);
    if (up && kStages[stage].news[0])
        Notify(Format("%s: %s", g_mascot.name.c_str(), kStages[stage].name),
               Format(kStages[stage].news, g_mascot.name.c_str()), kGold);
    else if (!up)
        Notify(Format("%s is fading", g_mascot.name.c_str()),
               Format("Audiences are forgetting %s (now a %s). A good cartoon would bring them back.",
                      g_mascot.name.c_str(), kStages[stage].name),
               kYellow);
}

// Newly released cartoons, by the back catalogue.
void CountReleases(float today)
{
    for (const game::Project& p : game::PlayerBackCatalogue()) {
        if (p.releaseDate <= 0 || !IsFinishedCartoon(p.title))
            continue;
        std::string title = Utf8(p.title);
        if (!g_mascot.counted.insert(title).second)
            continue;
        Released(title, p.quality, today);
    }
}

void Merchandise(float today)
{
    if (g_mascot.name.empty() || Year() < kMerchYear)
        return;
    if (g_mascot.lastMerch <= 0 || today < g_mascot.lastMerch) {
        g_mascot.lastMerch = today;
        return;
    }
    if (today - g_mascot.lastMerch < kDaysPerYear / 12)
        return;
    g_mascot.lastMerch = today;
    double income = kMerchMonthly1950 * g_mascot.appeal * g_mascot.appeal * EraFactor(Year());
    int year = static_cast<int>(Year());
    if (year != g_mascot.merchYearNo) {
        if (g_mascot.merchYearNo && g_mascot.merchYear >= 1000)
            Notify(Format("%s merchandise", g_mascot.name.c_str()),
                   Format("Toys, comics and lunchboxes earned %s in %d.", Dollars(g_mascot.merchYear).c_str(),
                          g_mascot.merchYearNo),
                   kGreen);
        g_mascot.merchYearNo = year;
        g_mascot.merchYear = 0;
    }
    if (income < 1)
        return;
    if (game::AddMoney(income))
        g_mascot.merchYear += income;
    else
        mod::Log("tv: mascot merchandise $%.0f NOT paid", income);
}

void Daily(float days, float today)
{
    CountReleases(today);
    if (g_mascot.name.empty())
        return;
    float years = days / kDaysPerYear;
    float fade = g_mascot.stage == kStageCount - 1 ? kIconFadeYears : kFadeYears;
    g_mascot.appeal *= std::pow(0.5f, years / fade);
    g_mascot.staleness *= std::pow(0.5f, years / kStaleYears);
    CheckStage();
    Merchandise(today);
}

void Frame()
{
    if (!game::Ready())
        return;
    float today = game::DateDays();
    if (today <= 0)
        return;
    if (g_lastDay < 0 || today < g_lastDay) {   // a load: start counting from here
        g_lastDay = today;
        return;
    }
    if (today - g_lastDay < 1)
        return;
    float days = std::min(today - g_lastDay, 7.0f);
    g_lastDay = today;
    Daily(days, today);
}

} // namespace

float MascotPull(uintptr_t project)
{
    if (g_mascot.name.empty() || !project || !CartoonStyle(project))
        return 1.0f;
    return 1.0f + kMaxPull * g_mascot.appeal;
}

std::string MascotName(bool create)
{
    if (g_mascot.name.empty() && create && game::Ready())
        Name("the first short", game::DateDays());
    return g_mascot.name;
}

float MascotAppeal() { return g_mascot.name.empty() ? 0.0f : g_mascot.appeal; }

void MascotShown(const std::string& shortTitle, float quality, float weight)
{
    if (weight <= 0 || !game::Ready())
        return;
    Released(shortTitle, quality, game::DateDays(), weight);
    CheckStage();
}

void InstallMascot()
{
    overlay::AddFrameCallback(&Frame);
    persist::Register(
        "mascot",
        [](persist::Section& out) {
            if (g_mascot.name.empty())
                return;
            out["name"] = g_mascot.name;
            out["born"] = Format("%.1f", g_mascot.born);
            out["appeal"] = Format("%.4f", g_mascot.appeal);
            out["staleness"] = Format("%.4f", g_mascot.staleness);
            out["last_release"] = Format("%.1f", g_mascot.lastRelease);
            out["stage"] = std::to_string(g_mascot.stage);
            out["cartoons"] = std::to_string(g_mascot.cartoons);
            out["merch_year"] = Format("%.0f", g_mascot.merchYear);
            out["merch_year_no"] = std::to_string(g_mascot.merchYearNo);
            out["last_merch"] = Format("%.1f", g_mascot.lastMerch);
            int i = 0;
            for (const std::string& title : g_mascot.counted)
                out[Format("counted%d", i++)] = title;
        },
        [](const persist::Section& in) {
            g_mascot = Mascot{};
            g_lastDay = -1;
            auto get = [&](const char* k) {
                auto it = in.find(k);
                return it == in.end() ? std::string() : it->second;
            };
            g_mascot.name = get("name");
            if (g_mascot.name.empty())
                return;
            g_mascot.born = static_cast<float>(atof(get("born").c_str()));
            g_mascot.appeal = std::clamp(static_cast<float>(atof(get("appeal").c_str())), 0.0f, 1.0f);
            g_mascot.staleness = std::clamp(static_cast<float>(atof(get("staleness").c_str())), 0.0f, 1.0f);
            g_mascot.lastRelease = static_cast<float>(atof(get("last_release").c_str()));
            g_mascot.stage = std::clamp(atoi(get("stage").c_str()), 0, kStageCount - 1);
            g_mascot.cartoons = atoi(get("cartoons").c_str());
            g_mascot.merchYear = atof(get("merch_year").c_str());
            g_mascot.merchYearNo = atoi(get("merch_year_no").c_str());
            g_mascot.lastMerch = static_cast<float>(atof(get("last_merch").c_str()));
            for (const auto& [key, value] : in)
                if (key.rfind("counted", 0) == 0)
                    g_mascot.counted.insert(value);
            mod::Log("tv: mascot %s restored - %s, appeal %.2f, staleness %.2f, %d cartoons", g_mascot.name.c_str(),
                     kStages[g_mascot.stage].name, g_mascot.appeal, g_mascot.staleness, g_mascot.cartoons);
        },
        [] {
            g_mascot = Mascot{};
            g_lastDay = -1;
        });
}

} // namespace tv
