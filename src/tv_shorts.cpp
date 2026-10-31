// Toon Studio: cartoon shorts, shown before the studio's films.
//
// While the animators have no feature cartoon (tv_animation.cpp), they make seven-minute
// shorts starring the studio mascot (tv_mascot.cpp; the first short brings the mascot to life):
// the animators at the desks draw a short's kShortScenes scenes, the ones in Ink & Paint colour
// them, at the same speeds as a feature. Nothing to drop: animators at the desks with no
// cartoon start the next short by themselves.
//
// A finished short goes on the shelf (up to kShelf) and plays before the player's next film:
// when a film of the player's is past its shoot, the oldest short on the shelf is booked with
// it, and its box office gets a bump (ShortPull, used by the success hook in ratings.cpp), more
// for a better short and a better-known mascot. Each short shown also adds to the mascot's
// appeal, less than a released cartoon does.
//
// From 1928 shorts have sound: a star dropped in the Voice Booth while there's no feature
// cartoon becomes the mascot's voice (recorded in kVoiceDays, kept for every short after);
// a short without one is silent and worth less. Cinemas showed shorts until the 1950s: the
// bump fades from kFadeFrom to nothing at kLastYear, after which no new shorts are started.

#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <cstdio>
#include <set>
#include <vector>

#include "careers.h"
#include "mod.h"
#include "overlay.h"
#include "persist.h"
#include "rooms.h"
#include "tv_internal.h"

namespace tv {
namespace {

const char* const kDesks = "room_tva_desks";
const char* const kInk = "room_tva_ink";
const char* const kVoice = "room_tva_voice";
constexpr float kDrawDays = 20.0f, kPaintDays = 8.0f;   // as a feature's scenes
constexpr float kVoiceDays = 3.0f;
constexpr int kShortScenes = 3;   // seven minutes
constexpr int kShelf = 3;
constexpr int kSoundYear = 1928, kFadeFrom = 1955, kLastYear = 1970;
constexpr int kStageShoot = 5;
constexpr float kMaxBump = 0.12f;   // a perfect short with an unknown mascot; up to twice that for an icon
constexpr float kMascotWeight = 0.5f;   // a short shown, for the mascot, against a released cartoon

const char* const kTitles[] = {
    "The Barn Dance",   "Plane Crazy",      "Haunted House",     "The Picnic",       "Moving Day",
    "Clock Cleaners",   "Boat Builders",    "The Band Concert",  "Lonesome Ghosts",  "Fire Brigade",
    "Hawaiian Holiday", "The Mad Doctor",   "Alpine Climbers",   "Trailer Trouble",  "The Big Bad Wolf",
    "Mail Pilot",       "Puppy Love",       "Camping Out",       "Gulliver's Day",   "Shanghaied",
    "The Dognapper",    "Two-Gun Trouble",  "Moose Hunters",     "Magician Mischief", "Society Dog Show",
    "Cactus Kid",       "The Castaway",     "Jungle Rhythm",     "Spooks Ahoy",      "Ye Olden Days",
};
constexpr int kTitleCount = sizeof(kTitles) / sizeof(kTitles[0]);

struct Short {
    std::string title;
    float       quality = 0;
};

struct Shorts {
    std::string        current;           // the short being made, "" for none
    float              drawn = 0, painted = 0;
    std::vector<Short> shelf;             // finished, oldest first
    std::map<std::string, Short> shown;   // film title -> the short shown before it
    int                made = 0;          // shorts finished, for the next title
    // The mascot's voice
    std::string voice;
    uintptr_t   voicePerson = 0;   // 0 after a load: their recording time still counts
    float       voiceRating = 0, voiceRecorded = 0;
    bool        fullNoticed = false, silentNoticed = false, endNoticed = false;
};
Shorts g_shorts;
float g_lastDay = -1;
std::set<std::string> g_oldFilms;   // films already past their shoot when the game was loaded
bool g_oldFilmsKnown = false;
std::map<uintptr_t, std::string> g_booked;   // this session: film -> its key in shown (titles can change)

std::string Narrow(const std::wstring& w)
{
    std::string s;
    for (wchar_t c : w)
        s += static_cast<char>(c < 128 ? c : '?');
    return s;
}

bool Voiced() { return !g_shorts.voice.empty() && g_shorts.voiceRecorded >= kVoiceDays; }

// How much cinemas still want shorts: 1 until kFadeFrom, nothing from kLastYear.
float Showing(float year)
{
    if (year <= kFadeFrom)
        return 1.0f;
    return std::clamp((kLastYear - year) / (kLastYear - kFadeFrom), 0.0f, 1.0f);
}

void Begin()
{
    std::string mascot = MascotName(true);
    bool first = g_shorts.made == 0;
    g_shorts.current = Format("%s in %s", mascot.c_str(), kTitles[g_shorts.made % kTitleCount]);
    g_shorts.drawn = g_shorts.painted = 0;
    mod::Log("tv: short '%s' started", g_shorts.current.c_str());
    if (first)
        Notify(Format("Meet %s", mascot.c_str()),
               Format("With no feature to work on, the animators have started a short: \"%s\". Shorts play before "
                      "your films and draw a bigger crowd to them, and they make %s, your studio's mascot, famous.%s",
                      g_shorts.current.c_str(), mascot.c_str(),
                      Year() >= kSoundYear ? " Drop a star in the Voice Booth to give the mascot a voice." : ""),
               kGold);
}

void Done()
{
    float q = 0.35f + 0.25f * (static_cast<float>(rand()) / RAND_MAX);
    bool silent = Year() >= kSoundYear && !Voiced();
    if (Voiced())
        q += 0.4f * g_shorts.voiceRating;
    else if (silent)
        q *= 0.7f;
    q = std::clamp(q, 0.0f, 1.0f);
    g_shorts.shelf.push_back({ g_shorts.current, q });
    g_shorts.made++;
    mod::Log("tv: short '%s' finished (quality %.2f%s) - %d on the shelf", g_shorts.current.c_str(), q,
             silent ? ", silent" : "", static_cast<int>(g_shorts.shelf.size()));
    std::string body = Format("\"%s\" is on the shelf, and plays before your next film.", g_shorts.current.c_str());
    if (silent && !g_shorts.silentNoticed) {
        g_shorts.silentNoticed = true;
        body += Format(" Audiences expect sound now: drop a star in the Voice Booth to give %s a voice.",
                       MascotName(false).c_str());
    }
    Notify("A short is finished", body, kGreen);
    g_shorts.current.clear();
    g_shorts.drawn = g_shorts.painted = 0;
}

void Record(float days)
{
    if (g_shorts.voice.empty() || g_shorts.voiceRecorded >= kVoiceDays)
        return;
    if (g_shorts.voicePerson && ZoneOf(g_shorts.voicePerson) != kVoice)
        return;   // away from the booth
    g_shorts.voiceRecorded = std::min(kVoiceDays, g_shorts.voiceRecorded + days);
    if (g_shorts.voiceRecorded < kVoiceDays)
        return;
    std::string mascot = MascotName(true);
    mod::Log("tv: %s is the voice of %s (rating %.2f)", g_shorts.voice.c_str(), mascot.c_str(), g_shorts.voiceRating);
    Notify(Format("The voice of %s", mascot.c_str()),
           Format("%s has recorded %s's voice for the shorts.", g_shorts.voice.c_str(), mascot.c_str()), kGreen);
}

// The films past their shoot when the game was loaded already had their chance at a short.
void KnowOldFilms()
{
    if (g_oldFilmsKnown)
        return;
    g_oldFilmsKnown = true;
    for (const game::Project& p : game::PlayerProductions())
        if (p.stage > kStageShoot)
            g_oldFilms.insert(Narrow(p.title));
    for (const game::Project& p : game::PlayerBackCatalogue())
        g_oldFilms.insert(Narrow(p.title));
    mod::Log("tv: shorts - %d films already made, %d shorts on the shelf, %d shown", static_cast<int>(g_oldFilms.size()),
             static_cast<int>(g_shorts.shelf.size()), static_cast<int>(g_shorts.shown.size()));
}

void Daily(float days)
{
    KnowOldFilms();
    if (CartoonInProgress() || AnimatedPilotInProgress())
        return;   // the animators are on the feature or a TV pilot, and so is the Voice Booth
    Record(days);
    int drawing = StaffWorkingIn(careers::kAnimator, kDesks);
    int painting = StaffWorkingIn(careers::kAnimator, kInk);
    float year = Year();
    if (g_shorts.current.empty()) {
        if (drawing <= 0)
            return;
        if (year >= kLastYear) {
            if (!g_shorts.endNoticed) {
                g_shorts.endNoticed = true;
                Notify("No more shorts", "Cinemas don't show cartoon shorts any more. Give the animators a feature: "
                                         "drop a film's script in the Story Room.",
                       kYellow);
            }
            return;
        }
        if (static_cast<int>(g_shorts.shelf.size()) >= kShelf) {
            if (!g_shorts.fullNoticed) {
                g_shorts.fullNoticed = true;
                Notify("The shelf is full", Format("%d shorts are waiting to be shown. Each one plays before your next "
                                                   "film; the animators start another when one goes out.",
                                                   kShelf),
                       kYellow);
            }
            return;
        }
        g_shorts.fullNoticed = false;
        Begin();
    }
    float scenes = static_cast<float>(kShortScenes);
    g_shorts.drawn = std::min(scenes, g_shorts.drawn + drawing * days / kDrawDays);
    g_shorts.painted = std::min(g_shorts.drawn, g_shorts.painted + painting * days / kPaintDays);
    if (g_shorts.painted >= scenes - 0.001f)
        Done();
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
    Daily(days);
}

// The bump a short gives the film it plays before.
float Bump(const Short& s, float year) { return kMaxBump * s.quality * (1 + MascotAppeal()) * Showing(year); }

} // namespace

bool ShortsVoiceDrop(uintptr_t room, uintptr_t person, const std::string& personName, float rating)
{
    std::string name = personName.empty() ? Format("Star #%04X", static_cast<unsigned>(person & 0xFFFF)) : personName;
    std::string mascot = MascotName(true);
    if (Year() < kSoundYear) {
        Notify("The Voice Booth", "Cartoons are silent until sound comes in, in 1928. Drop a film's script in the "
                                  "Story Room to make a feature cartoon.",
               kYellow);
        return true;
    }
    if (name == g_shorts.voice && Voiced()) {
        Notify("The Voice Booth", Format("%s is already %s's voice.", name.c_str(), mascot.c_str()), kYellow);
        return true;
    }
    bool again = name == g_shorts.voice;
    if (!again) {
        g_shorts.voice = name;
        g_shorts.voiceRating = rating;
        g_shorts.voiceRecorded = 0;
    }
    g_shorts.voicePerson = person;
    bool seated = rooms::SeatInScene(room, person);
    mod::Log("tv: %s %s as the voice of %s (rating %.2f, %s)", name.c_str(), again ? "back" : "records",
             mascot.c_str(), rating, seated ? "seated" : "FAILED to seat");
    Notify(Format("%s voices %s", name.c_str(), mascot.c_str()),
           Format("Recording the mascot's voice for the shorts takes %.0f days in the booth.", kVoiceDays), kGold);
    return true;
}

std::string MascotVoice(float* rating)
{
    if (!Voiced())
        return "";
    *rating = VoiceRating(g_shorts.voice, g_shorts.voiceRating);
    return g_shorts.voice;
}

std::string ShortsStatus()
{
    std::string s;
    if (!g_shorts.current.empty())
        s = Format("The animators are making \"%s\": %d of %d scenes drawn, %d painted.", g_shorts.current.c_str(),
                   static_cast<int>(g_shorts.drawn), kShortScenes, static_cast<int>(g_shorts.painted));
    else
        s = "With no feature to work on, animators at the desks make shorts.";
    if (!g_shorts.shelf.empty())
        s += Format(" %d short%s on the shelf.", static_cast<int>(g_shorts.shelf.size()),
                    g_shorts.shelf.size() == 1 ? "" : "s");
    return s;
}

float ShortPull(uintptr_t project)
{
    if (!project || !g_oldFilmsKnown)
        return 1.0f;
    game::Project p = game::ReadProjectAt(project);
    if (p.rival || p.stage <= kStageShoot)
        return 1.0f;
    std::string title = Narrow(p.title);
    if (auto b = g_booked.find(project); b != g_booked.end())
        title = b->second;
    if (title.empty())
        return 1.0f;
    float year = Year();
    if (auto it = g_shorts.shown.find(title); it != g_shorts.shown.end())
        return 1.0f + Bump(it->second, year);
    if (g_shorts.shelf.empty() || g_oldFilms.count(title) || IsTvScriptProject(project) || IsShareObject(project) || IsVaultObject(project) ||
        IsPilotReel(project))
        return 1.0f;
    // A new film of the player's, past its shoot: the oldest short on the shelf plays before it.
    Short s = g_shorts.shelf.front();
    g_shorts.shelf.erase(g_shorts.shelf.begin());
    g_shorts.shown[title] = s;
    g_booked[project] = title;
    float bump = Bump(s, year);
    mod::Log("tv: short '%s' (quality %.2f) plays before '%s' - box office x%.2f", s.title.c_str(), s.quality,
             title.c_str(), 1 + bump);
    MascotShown(s.title, s.quality, kMascotWeight * Showing(year));
    if (Voiced())
        VoiceShort(g_shorts.voice, s.title, s.quality * Showing(year) * (0.5f + MascotAppeal()));
    Notify("A short before the film",
           Format("\"%s\" plays before \"%s\"%s", s.title.c_str(), title.c_str(),
                  bump >= 0.005f ? Format(": %d%% more people come to see it.", static_cast<int>(bump * 100 + 0.5f)).c_str()
                                 : "."),
           kGreen);
    return 1.0f + bump;
}

void InstallShorts()
{
    overlay::AddFrameCallback(&Frame);
    persist::Register(
        "shorts",
        [](persist::Section& out) {
            out["made"] = std::to_string(g_shorts.made);
            out["noticed"] = Format("%d%d%d", g_shorts.fullNoticed, g_shorts.silentNoticed, g_shorts.endNoticed);
            if (!g_shorts.current.empty()) {
                out["current"] = g_shorts.current;
                out["drawn"] = Format("%.3f", g_shorts.drawn);
                out["painted"] = Format("%.3f", g_shorts.painted);
            }
            if (!g_shorts.voice.empty())
                out["voice"] = Format("%.3f|%.3f|%s", g_shorts.voiceRating, g_shorts.voiceRecorded,
                                      g_shorts.voice.c_str());
            int i = 0;
            for (const Short& s : g_shorts.shelf)
                out[Format("shelf%d", i++)] = Format("%.3f|%s", s.quality, s.title.c_str());
            i = 0;
            // "shown<n>" = quality|short|film (a short's title has no '|')
            for (const auto& [film, s] : g_shorts.shown)
                out[Format("shown%d", i++)] = Format("%.3f|%s|%s", s.quality, s.title.c_str(), film.c_str());
        },
        [](const persist::Section& in) {
            g_shorts = Shorts{};
            g_lastDay = -1;
            g_oldFilms.clear();
            g_oldFilmsKnown = false;
            g_booked.clear();
            auto get = [&](const char* k) {
                auto it = in.find(k);
                return it == in.end() ? std::string() : it->second;
            };
            g_shorts.made = atoi(get("made").c_str());
            std::string noticed = get("noticed");
            g_shorts.fullNoticed = noticed.size() > 0 && noticed[0] == '1';
            g_shorts.silentNoticed = noticed.size() > 1 && noticed[1] == '1';
            g_shorts.endNoticed = noticed.size() > 2 && noticed[2] == '1';
            g_shorts.current = get("current");
            g_shorts.drawn = static_cast<float>(atof(get("drawn").c_str()));
            g_shorts.painted = static_cast<float>(atof(get("painted").c_str()));
            float a = 0, b = 0;
            int used = 0;
            std::string voice = get("voice");
            if (sscanf_s(voice.c_str(), "%f|%f|%n", &a, &b, &used) == 2 && used > 0) {
                g_shorts.voice = voice.substr(used);
                g_shorts.voiceRating = a;
                g_shorts.voiceRecorded = b;
            }
            std::map<int, Short> shelf;   // in order
            for (const auto& [key, value] : in) {
                used = 0;
                if (key.rfind("shelf", 0) == 0) {
                    if (sscanf_s(value.c_str(), "%f|%n", &a, &used) == 1 && used > 0)
                        shelf[atoi(key.c_str() + 5)] = { value.substr(used), a };
                } else if (key.rfind("shown", 0) == 0) {
                    if (sscanf_s(value.c_str(), "%f|%n", &a, &used) != 1 || used <= 0)
                        continue;
                    std::string rest = value.substr(used);
                    size_t bar = rest.find('|');
                    if (bar != std::string::npos)
                        g_shorts.shown[rest.substr(bar + 1)] = { rest.substr(0, bar), a };
                }
            }
            for (const auto& [n, s] : shelf)
                g_shorts.shelf.push_back(s);
            mod::Log("tv: shorts restored - %d made, '%s' in the works, %d on the shelf, %d shown, voice '%s'",
                     g_shorts.made, g_shorts.current.c_str(), static_cast<int>(g_shorts.shelf.size()),
                     static_cast<int>(g_shorts.shown.size()), g_shorts.voice.c_str());
        },
        [] {
            g_shorts = Shorts{};
            g_lastDay = -1;
            g_oldFilms.clear();
            g_oldFilmsKnown = false;
            g_booked.clear();
        });
}

} // namespace tv
