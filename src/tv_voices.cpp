// Toon Studio: voice fame. Voice work is its own track for a star, apart from their films.
//
// A star earns voice fame from the cartoons they voice: a feature cartoon when it is released
// (more for one the critics like, and again when it comes back out of the vault), an animated
// TV series at the end of each season, the mascot's voice each time a short plays. Fame gets
// harder to add as it grows, and fades slowly without voice work (kHalfLife).
//
// It counts twice:
// - In the Voice Booth a known voice is worth at least its fame, whatever the star's rating.
// - It lifts the star's rating, through a bonus the game adds to it (game::SetStarBonus), more
//   for a faded star than a big one: a has-been can come back as a voice.
// Stars are known by name; the person is found again after a load by name among all stars.

#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>

#include "game.h"
#include "mod.h"
#include "overlay.h"
#include "persist.h"
#include "rooms.h"
#include "tv_internal.h"

namespace tv {
namespace {

constexpr float kHalfLife = 4.0f;   // years for voice fame to halve without voice work
constexpr float kBonus = 0.35f;     // to the star rating at full fame, for a 0-star screen career
constexpr float kFeature = 0.10f, kFeatureCritics = 0.30f;   // a released feature cartoon, per voice
constexpr float kReissue = 0.35f;   // a re-release, of what the first release gave
constexpr float kSeason = 0.05f, kSeasonHit = 0.10f;   // an animated series' season, its lead
constexpr float kShort = 0.05f;     // a short shown, the mascot's voice
constexpr float kLevels[] = { 0.20f, 0.45f, 0.75f };   // a known voice, a voice star, a legend
constexpr float kFaded = 0.30f;     // a star rating (1.5 stars) under which a rise is a comeback
constexpr float kFindEvery = 30;    // days between searches for stars not found

struct VoiceStar {
    float fame = 0;
    int   level = 0;     // highest of kLevels reached (noticed)
    int   roles = 0;     // cartoons, seasons and shorts credited
    float last = 0;      // day of the last credit
    float base = -1;     // star rating without the bonus, when last seen (-1 = not yet)
};
std::map<std::string, VoiceStar> g_stars;   // by name

struct Cast {
    std::vector<std::string> names;
    bool  released = false;
    float credit = 0;   // per voice, at release (for the re-releases)
};
std::map<std::string, Cast> g_casts;   // feature cartoons, by title

std::map<std::string, uintptr_t> g_people;   // name -> CStar, this session
float g_lastFind = -1e9f;
float g_lastDay = -1;

const char* LevelName(int level)
{
    static const char* const names[] = { "a known voice", "a voice star", "a legend of animation" };
    return names[std::clamp(level, 1, 3) - 1];
}

float Stars(float rating) { return std::clamp(rating, 0.0f, 1.0f) * 5; }

// The star with this name, or 0. Pointers from this session are checked against the list of stars.
uintptr_t Person(const std::string& name, const std::set<uintptr_t>& all)
{
    auto it = g_people.find(name);
    if (it != g_people.end() && all.count(it->second))
        return it->second;
    return 0;
}

// Finds the voice stars not seen this session, by name (at most every kFindEvery days).
void FindMissing(const std::set<uintptr_t>& all)
{
    bool missing = false;
    for (const auto& [name, v] : g_stars)
        if (v.fame > 0.005f && !Person(name, all))
            missing = true;
    if (!missing || Today() - g_lastFind < kFindEvery)
        return;
    g_lastFind = Today();
    int found = 0;
    for (uintptr_t star : all) {
        std::string name = rooms::NameOf(star);
        auto it = g_stars.find(name);
        if (it != g_stars.end() && !Person(name, all)) {
            g_people[name] = star;
            found++;
        }
    }
    mod::Log("tv: voice stars - %d found again among %d stars", found, static_cast<int>(all.size()));
}

std::set<uintptr_t> AllStars()
{
    std::vector<uintptr_t> list = game::AllStars();
    return std::set<uintptr_t>(list.begin(), list.end());
}

// Sets the star's bonus from their fame; returns their rating before and after.
void Apply(const std::string& name, VoiceStar& v, uintptr_t star, float* before = nullptr, float* after = nullptr)
{
    if (!star)
        return;
    float old = game::StarRating(star);
    float bonus = game::StarBonus(star);
    if (bonus != 0 && !game::SetStarBonus(star, 0))
        return;
    v.base = std::clamp(game::StarRating(star), 0.0f, 1.0f);
    float want = v.fame > 0.005f ? kBonus * v.fame * (1 - 0.5f * v.base) : 0.0f;
    if (want != 0 && !game::SetStarBonus(star, want))
        mod::Log("tv: %s's voice bonus NOT set", name.c_str());
    if (before)
        *before = old;
    if (after)
        *after = game::StarRating(star);
}

void Credit(const std::string& name, float amount, const std::string& what)
{
    if (name.empty() || amount <= 0 || name == "a voice actor")
        return;
    VoiceStar& v = g_stars[name];
    float was = v.fame;
    v.fame = std::min(1.0f, v.fame + amount * (1 - v.fame));
    v.roles++;
    v.last = Today();
    std::set<uintptr_t> all = AllStars();
    uintptr_t star = Person(name, all);
    float before = -1, after = -1;
    Apply(name, v, star, &before, &after);
    mod::Log("tv: %s's voice fame %.2f -> %.2f (%s), rating %.2f -> %.2f", name.c_str(), was, v.fame, what.c_str(),
             before, after);
    int level = 0;
    for (float l : kLevels)
        level += v.fame >= l;
    if (level <= v.level)
        return;
    v.level = level;
    bool comeback = level < 3 && v.base >= 0 && v.base < kFaded;
    std::string rise = star && after > before + 0.01f
                           ? Format(" Their star rating goes from %.1f to %.1f stars.", Stars(before), Stars(after))
                           : "";
    if (comeback)
        Notify(Format("%s's comeback", name.c_str()),
               Format("Their films are forgotten, but audiences love the voice from \"%s\": %s is %s now.%s",
                      what.c_str(), name.c_str(), LevelName(level), rise.c_str()),
               kGold);
    else
        Notify(Format("%s is %s", name.c_str(), LevelName(level)),
               Format("After \"%s\", people know the voice.%s In the Voice Booth it counts as %.1f stars or more.",
                      what.c_str(), rise.c_str(), Stars(v.fame)),
               kGold);
}

void Daily(float days)
{
    float keep = std::pow(0.5f, days / (kHalfLife * kDaysPerYear));
    for (auto& [name, v] : g_stars)
        v.fame *= keep;
    // Feature cartoons released since: their voices are credited by what the critics said.
    for (const game::Project& p : game::PlayerBackCatalogue()) {
        if (p.releaseDate <= 0 || p.starRating < 0)
            continue;
        auto it = g_casts.find(Utf8(p.title));
        if (it == g_casts.end() || it->second.released)
            continue;
        Cast& c = it->second;
        c.released = true;
        c.credit = kFeature + kFeatureCritics * std::clamp(p.starRating, 0.0f, 1.0f);
        for (const std::string& name : c.names)
            Credit(name, c.credit, it->first);
    }
    std::set<uintptr_t> all = AllStars();
    FindMissing(all);
    for (auto& [name, v] : g_stars)
        if (uintptr_t star = Person(name, all))
            Apply(name, v, star);
}

void Frame()
{
    if (!game::Ready())
        return;
    float today = Today();
    if (today <= 0)
        return;
    if (g_lastDay < 0 || today < g_lastDay) {   // a load: the bonuses are put back on the first day
        g_lastDay = today - 1;
        g_people.clear();
        g_lastFind = -1e9f;
    }
    if (today - g_lastDay < 1)
        return;
    float days = std::min(today - g_lastDay, 7.0f);
    g_lastDay = today;
    Daily(days);
}

} // namespace

void VoiceSeen(const std::string& name, uintptr_t person)
{
    if (!name.empty() && person)
        g_people[name] = person;
}

float VoiceRating(const std::string& name, float rating)
{
    auto it = g_stars.find(name);
    return it == g_stars.end() ? rating : std::max(rating, std::min(1.0f, it->second.fame));
}

void VoiceCast(const std::string& title, const std::vector<std::string>& names)
{
    if (title.empty() || names.empty())
        return;
    g_casts[title] = { names, false, 0 };
    mod::Log("tv: '%s' has %d voices, credited when it's released", title.c_str(), static_cast<int>(names.size()));
}

void VoiceCredit(const std::string& name, float amount, const std::string& what) { Credit(name, amount, what); }

void VoiceSeason(const std::string& lead, const std::string& title, float success)
{
    Credit(lead, kSeason + kSeasonHit * std::clamp(success, 0.0f, 1.5f), title);
}

void VoiceShort(const std::string& voice, const std::string& title, float weight)
{
    Credit(voice, kShort * weight, title);
}

void VoiceReissue(const std::string& title)
{
    auto it = g_casts.find(title);
    if (it == g_casts.end() || !it->second.released)
        return;
    for (const std::string& name : it->second.names)
        Credit(name, kReissue * it->second.credit, title);
}

void InstallVoices()
{
    overlay::AddFrameCallback(&Frame);
    persist::Register(
        "voices",
        [](persist::Section& out) {
            int i = 0;
            for (const auto& [name, v] : g_stars)
                out[Format("star%d", i++)] =
                    Format("%.4f|%d|%d|%.1f|%.3f|%s", v.fame, v.level, v.roles, v.last, v.base, name.c_str());
            i = 0;
            for (const auto& [title, c] : g_casts) {
                std::string names;
                for (const std::string& n : c.names)
                    names += (names.empty() ? "" : "\t") + n;
                out[Format("cast%d", i++)] = Format("%d|%.3f|%s\n%s", c.released ? 1 : 0, c.credit, title.c_str(),
                                                    names.c_str());
            }
        },
        [](const persist::Section& in) {
            g_stars.clear();
            g_casts.clear();
            g_people.clear();
            g_lastDay = -1;
            g_lastFind = -1e9f;
            for (const auto& [key, value] : in) {
                int used = 0, level = 0, roles = 0, released = 0;
                float fame = 0, last = 0, base = -1;
                if (key.rfind("star", 0) == 0) {
                    if (sscanf_s(value.c_str(), "%f|%d|%d|%f|%f|%n", &fame, &level, &roles, &last, &base, &used) == 5 &&
                        used > 0)
                        g_stars[value.substr(used)] = { fame, level, roles, last, base };
                } else if (key.rfind("cast", 0) == 0) {
                    if (sscanf_s(value.c_str(), "%d|%f|%n", &released, &fame, &used) != 2 || used <= 0)
                        continue;
                    size_t nl = value.find('\n', used);
                    Cast c;
                    c.released = released == 1;
                    c.credit = fame;
                    std::string names = nl == std::string::npos ? "" : value.substr(nl + 1);
                    for (size_t at = 0; at < names.size();) {
                        size_t tab = names.find('\t', at);
                        if (tab == std::string::npos)
                            tab = names.size();
                        if (tab > at)
                            c.names.push_back(names.substr(at, tab - at));
                        at = tab + 1;
                    }
                    g_casts[value.substr(used, nl == std::string::npos ? std::string::npos : nl - used)] = c;
                }
            }
            mod::Log("tv: voice fame restored - %d stars, %d cartoon casts", static_cast<int>(g_stars.size()),
                     static_cast<int>(g_casts.size()));
        },
        [] {
            g_stars.clear();
            g_casts.clear();
            g_people.clear();
            g_lastDay = -1;
            g_lastFind = -1e9f;
        });
}

} // namespace tv
