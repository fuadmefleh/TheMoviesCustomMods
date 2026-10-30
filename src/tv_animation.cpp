// Toon Studio: the Animation Studio and its animators.
//
// Drop a film's script in the Story Room and it becomes the studio's cartoon: it is never shot.
// Animators (career 18, hired at the studio's own desk: tv_staff.cpp) draw its scenes at the
// desks, and colour the drawings in Ink & Paint. Each animator draws a scene in kDrawDays and
// paints one in kPaintDays, so ten animators get through a thirty-scene film in a few months.
// The script's bubble on the lot shows how far they are, and its phase line the next thing to
// do (animators at the desks, animators in Ink & Paint, a render farm, voices).
//
// From 1995 the CGI Render Farm (facility_render_farm) can be built. A script dropped on a farm
// instead of in the Story Room becomes a CGI cartoon, and the cartoon's own script dropped on a
// farm turns it into one: the animators at the desks animate its scenes as before, and there's
// no Ink & Paint: every farm on the lot renders an animated scene in kRenderDays, at a running
// cost per scene. Hand-drawn cartoons can still be made after 1995, through Ink & Paint.
//
// The roles are voiced in the Voice Booth: a star dropped there stands at a microphone and
// records one role in kVoiceDays (time spent away from the booth doesn't count). A cartoon
// has no actors on screen, so its voice cast gives the performance part of its quality
// (ratings.cpp asks VoicePerformance): the voices' star ratings, an unvoiced role counting
// less than the worst voice. When every scene is painted and every role voiced, the film is
// finished through the game's own "finish film" (production::FinishWithoutShoot) and
// released like any other.
//
// One cartoon at a time. A film already being cast or shot can't be animated. Between
// cartoons the animators draw TV cartoons' pilots (tv_writers.cpp), then shorts (tv_shorts.cpp),
// and the Voice Booth records the TV cartoon's lead, or else the mascot.

#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
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
const char* const kStory = "room_tva_story";
const char* const kVoice = "room_tva_voice";
const char* const kFarmZones = "room_tvr_";   // any zone of a render farm
const char* const kFarm = "facility_render_farm";
constexpr float kDrawDays = 20.0f;   // one animator, one scene
constexpr float kPaintDays = 8.0f;
constexpr float kVoiceDays = 3.0f;   // one star, one role
constexpr float kRenderDays = 3.0f;  // one farm, one scene
constexpr double kRenderCost1950 = 2500;   // per scene rendered, in 1950 dollars
constexpr int kStageScriptDone = 3, kStageShoot = 5;
constexpr int kDefaultRoles = 2, kMaxRoles = 12;   // when the script's roles can't be read; a cap
// The performance part of quality (about 0.8 to 1.2 for a cast on screen): 0-star voices give
// the low end, 5-star voices the high end, an unvoiced role kUnvoiced.
constexpr float kVoiceLow = 0.8f, kVoiceHigh = 1.2f, kUnvoiced = 0.7f;

struct Voice {
    std::string name;
    uintptr_t   person = 0;     // 0 after a load: their recording time still counts
    float       rating = 0.4f;  // star rating, 0..1
    float       recorded = 0;   // days; the role is voiced at kVoiceDays
};

struct Cartoon {
    std::string        title;   // empty = none
    uintptr_t          project = 0;
    int                scenes = 0;
    int                roles = 0;
    std::vector<Voice> voices;
    bool               cgi = false;   // rendered on the farms instead of painted
    float              drawn = 0, painted = 0;   // CGI: animated, rendered
    float              started = 0;
    bool               waitNoticed = false;    // "the drawings are done, the voices aren't" was shown
    bool               paintNoticed = false;   // "the drawings are waiting for painters" was shown
    int                drawing = -1, painting = -1;   // animators at work, as of the last day
};
Cartoon g_cartoon;
float g_lastDay = -1;
// Finished cartoons, by title: their voices' performance (for quality) and their look.
struct Done {
    float performance = kVoiceLow;
    bool  cgi = false;
};
std::map<std::string, Done> g_done;

std::string Narrow(const std::wstring& w)
{
    std::string s;
    for (wchar_t c : w)
        s += static_cast<char>(c < 128 ? c : '?');
    return s;
}

// After a load the film is back at a new address: find it by title.
void Relink()
{
    if (g_cartoon.title.empty() || g_cartoon.project)
        return;
    for (const game::Project& p : game::PlayerProductions())
        if (Narrow(p.title) == g_cartoon.title)
            g_cartoon.project = p.address;
}

void Clear() { g_cartoon = Cartoon{}; }

int Farms() { return game::CountFacilities(kFarm); }

// The words for the cartoon's two steps: drawn and painted, or animated and rendered.
const char* Drawn() { return g_cartoon.cgi ? "animated" : "drawn"; }
const char* Painted() { return g_cartoon.cgi ? "rendered" : "painted"; }

int VoicedRoles()
{
    int n = 0;
    for (const Voice& v : g_cartoon.voices)
        n += v.recorded >= kVoiceDays;
    return std::min(n, g_cartoon.roles);
}

// The performance from the voices recorded so far.
float Performance()
{
    if (g_cartoon.roles <= 0)
        return kVoiceLow;
    float sum = 0;
    int voiced = 0;
    for (const Voice& v : g_cartoon.voices)
        if (v.recorded >= kVoiceDays && voiced < g_cartoon.roles) {
            sum += kVoiceLow + (kVoiceHigh - kVoiceLow) * v.rating;
            voiced++;
        }
    sum += kUnvoiced * (g_cartoon.roles - voiced);
    return sum / g_cartoon.roles;
}

void Record(float days)
{
    for (Voice& v : g_cartoon.voices) {
        if (v.recorded >= kVoiceDays)
            continue;
        if (v.person && ZoneOf(v.person) != kVoice)
            continue;   // away from the booth
        v.recorded = std::min(kVoiceDays, v.recorded + days);
        if (v.recorded < kVoiceDays)
            continue;
        int left = g_cartoon.roles - VoicedRoles();
        mod::Log("tv: %s voiced a role in '%s' (rating %.2f) - %d of %d", v.name.c_str(), g_cartoon.title.c_str(),
                 v.rating, VoicedRoles(), g_cartoon.roles);
        Notify(Format("%s has voiced a role", v.name.c_str()),
               left > 0 ? Format("%d role%s in \"%s\" still need%s a voice.", left, left == 1 ? "" : "s",
                                 g_cartoon.title.c_str(), left == 1 ? "s" : "")
                        : Format("Every role in \"%s\" has a voice.", g_cartoon.title.c_str()),
               kGreen);
    }
}

// What the cartoon needs next, for its bubble and the notices: a short phase and a sentence.
const char* NextPhase()
{
    float scenes = static_cast<float>(g_cartoon.scenes);
    if (g_cartoon.drawn < scenes && g_cartoon.drawing == 0)
        return "Needs Animators";
    if (g_cartoon.cgi && g_cartoon.painted < scenes - 0.001f && Farms() == 0)
        return "Needs a Render Farm";
    if (!g_cartoon.cgi && g_cartoon.painted < g_cartoon.drawn - 0.999f && g_cartoon.painting == 0)
        return "Needs Painters";
    if (g_cartoon.painted >= scenes - 0.001f && VoicedRoles() < g_cartoon.roles)
        return "Needs Voices";
    return g_cartoon.cgi ? "Being Rendered" : "Being Animated";
}

std::string NextStep()
{
    std::string phase = NextPhase();
    if (phase == "Needs Animators")
        return Animators() ? Format("Drop animators on the Animators' Desks to %s its scenes.",
                                    g_cartoon.cgi ? "animate" : "draw")
                           : "Hire animators at the Animation Studio's desk, then drop them on the Animators' Desks.";
    if (phase == "Needs a Render Farm")
        return "Build a CGI Render Farm to render the animated scenes.";
    if (phase == "Needs Painters")
        return "The drawings are waiting to be coloured: drop an animator in Ink & Paint.";
    if (phase == "Needs Voices")
        return "Drop stars in the Voice Booth to voice the roles.";
    return g_cartoon.cgi ? "The animators and the render farms are at work." : "The animators are at work.";
}

// The farms render animated scenes, at a running cost.
void Render(float days)
{
    int farms = Farms();
    float before = g_cartoon.painted;
    g_cartoon.painted = std::min(g_cartoon.drawn, g_cartoon.painted + farms * days / kRenderDays);
    double cost = (g_cartoon.painted - before) * kRenderCost1950 * EraFactor(Year());
    if (cost > 0 && !game::AddMoney(-cost))
        mod::Log("tv: render farm cost $%.0f NOT charged", cost);
}

void Finish()
{
    std::string title = g_cartoon.title;
    g_done[title] = { Performance(), g_cartoon.cgi };
    bool ok = production::FinishWithoutShoot(g_cartoon.project);
    mod::Log("tv: cartoon '%s' %s after %.0f days (voice performance %.2f)", title.c_str(),
             ok ? "finished" : "FAILED to finish", game::DateDays() - g_cartoon.started, g_done[title].performance);
    if (ok)
        Notify("A cartoon is finished",
               Format("The animators have finished every scene of \"%s\". Release it like any other film.", title.c_str()),
               kGreen);
    else
        Notify("The cartoon is stuck",
               Format("\"%s\" couldn't be finished. Its scenes are drawn; tell the mod's author what happened.",
                      title.c_str()),
               kRed);
    Clear();
}

void Daily(float days)
{
    Relink();
    if (g_cartoon.title.empty())
        return;
    game::Project p = g_cartoon.project ? game::ReadProjectAt(g_cartoon.project) : game::Project{};
    if (!g_cartoon.project || Narrow(p.title) != g_cartoon.title || p.stage > kStageShoot) {
        mod::Log("tv: cartoon '%s' is gone (stage %d) - the animators stop", g_cartoon.title.c_str(), p.stage);
        Clear();
        return;
    }
    Record(days);
    int drawing = StaffWorkingIn(careers::kAnimator, kDesks);
    int painting = StaffWorkingIn(careers::kAnimator, kInk);
    g_cartoon.drawing = drawing;
    g_cartoon.painting = painting;
    float scenes = static_cast<float>(g_cartoon.scenes);
    g_cartoon.drawn = std::min(scenes, g_cartoon.drawn + drawing * days / kDrawDays);
    if (g_cartoon.cgi)
        Render(days);
    else
        g_cartoon.painted = std::min(g_cartoon.drawn, g_cartoon.painted + painting * days / kPaintDays);
    static int logged = 0;
    if (logged++ < 40)
        mod::Log("tv: cartoon '%s'%s - %d at the desks, %d %s: %.1f %s, %.1f %s of %d, %d of %d voiced",
                 g_cartoon.title.c_str(), g_cartoon.cgi ? " (CGI)" : "", drawing,
                 g_cartoon.cgi ? Farms() : painting, g_cartoon.cgi ? "farms" : "painting", g_cartoon.drawn, Drawn(),
                 g_cartoon.painted, Painted(), g_cartoon.scenes, VoicedRoles(), g_cartoon.roles);
    // Drawings piling up with nobody in Ink & Paint: say so once (again after someone painted).
    if (painting > 0)
        g_cartoon.paintNoticed = false;
    else if (!g_cartoon.paintNoticed && NextPhase() == std::string("Needs Painters")) {
        g_cartoon.paintNoticed = true;
        Notify("Drawings to colour in",
               Format("%d scene%s of \"%s\" %s drawn and waiting. %s", static_cast<int>(g_cartoon.drawn),
                      static_cast<int>(g_cartoon.drawn) == 1 ? "" : "s", g_cartoon.title.c_str(),
                      static_cast<int>(g_cartoon.drawn) == 1 ? "is" : "are", NextStep().c_str()),
               kYellow);
    }
    if (g_cartoon.painted < scenes - 0.001f)
        return;
    int left = g_cartoon.roles - VoicedRoles();
    if (left > 0) {
        if (!g_cartoon.waitNoticed) {
            g_cartoon.waitNoticed = true;
            Notify(g_cartoon.cgi ? "The rendering is done" : "The drawings are done",
                   Format("\"%s\" is waiting for its voices: %d role%s left. Drop stars in the Voice Booth.",
                          g_cartoon.title.c_str(), left, left == 1 ? "" : "s"),
                   kYellow);
        }
        return;
    }
    Finish();
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

bool StartCartoon(uintptr_t project, bool cgi)
{
    game::Project p = game::ReadProjectAt(project);
    std::string title = Narrow(p.title);
    const char* where = cgi ? "The Render Farm" : "The Story Room";
    if (IsTvScriptProject(project) || IsShareObject(project) || IsVaultObject(project) ||
        IsPilotReel(project)) {
        Notify(where, "Only a film's script can be made into a cartoon here.", kYellow);
        return true;
    }
    if (p.stage < kStageScriptDone) {
        Notify(where, "That script isn't finished yet. Bring it back when the writers are done.", kYellow);
        return true;
    }
    if (p.stage > kStageScriptDone) {
        Notify(where,
               Format("\"%s\" is already being made with actors. Only a script that hasn't been cast can be animated.",
                      title.c_str()),
               kYellow);
        return true;
    }
    if (!g_cartoon.title.empty()) {
        Notify(where, Format("The animators are busy with \"%s\". One cartoon at a time.", g_cartoon.title.c_str()),
               kYellow);
        return true;
    }
    int scenes = production::SceneCount(project);
    if (scenes <= 0) {
        mod::Log("tv: cartoon '%s' has no scenes (%d) - refused", title.c_str(), scenes);
        Notify(where, "That script has no scenes to draw.", kYellow);
        return true;
    }
    int roles = production::RoleCount(project);
    Clear();
    g_cartoon.title = title;
    g_cartoon.project = project;
    g_cartoon.scenes = scenes;
    g_cartoon.roles = roles > 0 ? std::min(roles, kMaxRoles) : kDefaultRoles;
    g_cartoon.cgi = cgi;
    g_cartoon.started = game::DateDays();
    mod::Log("tv: %s '%s' started - %d scenes, %d roles (the script has %d)", cgi ? "CGI cartoon" : "cartoon",
             title.c_str(), scenes, g_cartoon.roles, roles);
    int animators = Animators();
    std::string work = !animators ? "Hire animators at the Animation Studio's desk to start."
                       : cgi      ? "Put animators at the Animation Studio's desks to animate; the render farms do the rest."
                                  : "Put animators at the desks to draw and in Ink & Paint to colour the drawings in.";
    Notify(cgi ? "A new CGI cartoon" : "A new cartoon",
           Format("\"%s\" will be %s: %d scenes to %s, %d role%s to voice. %s Drop stars in the Voice Booth to "
                  "voice the roles.",
                  title.c_str(), cgi ? "made in CGI" : "animated", scenes, cgi ? "animate and render" : "draw and paint",
                  g_cartoon.roles, g_cartoon.roles == 1 ? "" : "s", work.c_str()),
           kGreen);
    return true;
}

// The cartoon's own script dropped on a render farm: it's finished in CGI from now on. The
// scenes already painted count as rendered; the drawn ones still to paint go to the farms.
void SwitchToCgi()
{
    if (g_cartoon.cgi) {
        Notify("The Render Farm", Format("\"%s\" is already being rendered here.", g_cartoon.title.c_str()), kYellow);
        return;
    }
    g_cartoon.cgi = true;
    g_cartoon.paintNoticed = false;
    mod::Log("tv: cartoon '%s' switched to CGI - %.1f drawn, %.1f painted", g_cartoon.title.c_str(), g_cartoon.drawn,
             g_cartoon.painted);
    Notify(Format("\"%s\" goes CGI", g_cartoon.title.c_str()),
           "Ink & Paint is done with it: the render farms render its scenes as the animators finish them.", kGreen);
}

bool VoiceDrop(uintptr_t room, uintptr_t person, const std::string& personClass, const std::string& personName,
               float rating)
{
    if (personClass != "CStar") {
        Notify("The Voice Booth", "Only actors voice cartoons.", kYellow);
        return true;
    }
    if (g_cartoon.title.empty())   // no feature: a TV cartoon's lead, or the mascot for the shorts
        return PilotVoiceDrop(room, person, personName, rating) || ShortsVoiceDrop(room, person, personName, rating);
    std::string name = personName.empty() ? Format("Star #%04X", static_cast<unsigned>(person & 0xFFFF)) : personName;
    for (Voice& v : g_cartoon.voices) {
        if (v.name != name)
            continue;
        if (v.recorded >= kVoiceDays) {
            Notify("The Voice Booth",
                   Format("%s has already voiced a role in \"%s\".", name.c_str(), g_cartoon.title.c_str()), kYellow);
            return true;
        }
        v.person = person;
        bool seated = rooms::SeatInScene(room, person);
        mod::Log("tv: %s back in the voice booth (%s)", name.c_str(), seated ? "seated" : "FAILED to seat");
        Notify("The Voice Booth", Format("%s goes back to the microphone.", name.c_str()), kGold);
        return true;
    }
    if (static_cast<int>(g_cartoon.voices.size()) >= g_cartoon.roles) {
        Notify("The Voice Booth", Format("Every role in \"%s\" has a voice.", g_cartoon.title.c_str()), kYellow);
        return true;
    }
    g_cartoon.voices.push_back({ name, person, rating, 0 });
    bool seated = rooms::SeatInScene(room, person);
    mod::Log("tv: %s %08X voices a role in '%s' (rating %.2f, %s)", name.c_str(), static_cast<unsigned>(person),
             g_cartoon.title.c_str(), rating, seated ? "seated" : "FAILED to seat");
    Notify(Format("%s voices a role", name.c_str()),
           Format("Recording for \"%s\" takes %.0f days in the booth.", g_cartoon.title.c_str(), kVoiceDays), kGold);
    return true;
}

} // namespace

bool AnimationDrop(const std::string& zone, uintptr_t room, uintptr_t project, uintptr_t person,
                   const std::string& personClass, const std::string& personName, float starRating)
{
    if (zone.rfind(kFarmZones, 0) == 0) {
        if (project && IsCartoon(project))
            SwitchToCgi();
        else if (project)
            StartCartoon(project, true);
        else if (person)
            Notify("The Render Farm", "The farm runs itself. Animators work at the Animation Studio's desks.", kYellow);
        return project || person;
    }
    if (zone.rfind("room_tva_", 0) != 0)
        return false;
    if (project) {
        if (zone == kStory && AnimatePilot(project))   // a TV script: a TV cartoon
            return true;
        if (zone == kStory && !IsCartoon(project))
            return StartCartoon(project, false);
        if (IsCartoon(project)) {   // the cartoon itself, dropped again: where it stands, what's next
            Notify(Format("\"%s\"", g_cartoon.title.c_str()),
                   Format("%d of %d scenes %s, %d %s, %d of %d roles voiced. %s", static_cast<int>(g_cartoon.drawn),
                          g_cartoon.scenes, Drawn(), static_cast<int>(g_cartoon.painted), Painted(), VoicedRoles(),
                          g_cartoon.roles, NextStep().c_str()),
                   kYellow);
            return true;
        }
        Notify("The Animation Studio", "Drop a film's script in the Story Room to make it into a cartoon.", kYellow);
        return true;
    }
    if (person && zone == kVoice)
        return VoiceDrop(room, person, personClass, personName, starRating);
    if (person && zone == kStory)
        Notify("The Animation Studio",
               "Animators work at the desks and in Ink & Paint, stars in the Voice Booth. The Story Room takes scripts. " +
                   (g_cartoon.title.empty() ? ShortsStatus() : std::string()),
               kYellow);
    return person != 0;
}

bool VoicePerformance(uintptr_t project, float* performance)
{
    if (!project)
        return false;
    Relink();
    if (project == g_cartoon.project && !g_cartoon.title.empty()) {
        *performance = Performance();
        return true;
    }
    if (g_done.empty())
        return false;
    auto it = g_done.find(Narrow(game::ReadProjectAt(project).title));
    if (it == g_done.end())
        return false;
    *performance = it->second.performance;
    return true;
}

bool IsFinishedCartoon(const std::wstring& title) { return g_done.count(Narrow(title)) != 0; }

int CartoonStyle(uintptr_t project)
{
    if (!project)
        return 0;
    Relink();
    if (project == g_cartoon.project && !g_cartoon.title.empty())
        return g_cartoon.cgi ? 2 : 1;
    if (g_done.empty())
        return 0;
    auto it = g_done.find(Narrow(game::ReadProjectAt(project).title));
    return it == g_done.end() ? 0 : it->second.cgi ? 2 : 1;
}

bool CartoonLabel(uintptr_t project, std::wstring* kind, std::wstring* phase, std::wstring* status)
{
    Relink();
    if (g_cartoon.title.empty() || project != g_cartoon.project)
        return false;
    *kind = g_cartoon.cgi ? L"CGI Cartoon" : L"Cartoon";
    std::string next = NextPhase();
    *phase = std::wstring(next.begin(), next.end());
    std::string s = Format("%d of %d scenes %s, %d %s", static_cast<int>(g_cartoon.drawn), g_cartoon.scenes, Drawn(),
                           static_cast<int>(g_cartoon.painted), Painted());
    s += Format(", %d of %d roles voiced", VoicedRoles(), g_cartoon.roles);
    *status = std::wstring(s.begin(), s.end());
    return true;
}

bool CartoonInProgress() { return !g_cartoon.title.empty(); }

bool IsCartoon(uintptr_t project)
{
    Relink();
    return project && project == g_cartoon.project;
}

void InstallAnimation()
{
    overlay::AddFrameCallback(&Frame);
    persist::Register(
        "toon",
        [](persist::Section& out) {
            int i = 0;
            for (const auto& [title, done] : g_done)
                out[Format("voiced%d", i++)] = Format("%.3f|%d|%s", done.performance, done.cgi ? 1 : 0, title.c_str());
            if (g_cartoon.title.empty())
                return;
            out["title"] = g_cartoon.title;
            out["scenes"] = std::to_string(g_cartoon.scenes);
            out["roles"] = std::to_string(g_cartoon.roles);
            out["drawn"] = Format("%.3f", g_cartoon.drawn);
            out["painted"] = Format("%.3f", g_cartoon.painted);
            out["started"] = Format("%.1f", g_cartoon.started);
            out["waited"] = g_cartoon.waitNoticed ? "1" : "0";
            out["cgi"] = g_cartoon.cgi ? "1" : "0";
            for (size_t v = 0; v < g_cartoon.voices.size(); v++) {
                const Voice& voice = g_cartoon.voices[v];
                out[Format("voice%d", static_cast<int>(v))] =
                    Format("%.3f|%.3f|%s", voice.rating, voice.recorded, voice.name.c_str());
            }
        },
        [](const persist::Section& in) {
            Clear();
            g_done.clear();
            g_lastDay = -1;
            auto get = [&](const char* k) {
                auto it = in.find(k);
                return it == in.end() ? std::string() : it->second;
            };
            // "voiced<n>" = performance|cgi|title (saved before CGI: performance|title),
            // "voice<n>" = rating|days|name
            for (const auto& [key, value] : in) {
                float a = 0, b = 0;
                int used = 0;
                if (key.rfind("voiced", 0) == 0) {
                    int cgi = 0;
                    if (sscanf_s(value.c_str(), "%f|%d|%n", &a, &cgi, &used) == 2 && used > 0 && (cgi == 0 || cgi == 1))
                        g_done[value.substr(used)] = { a, cgi == 1 };
                    else if (sscanf_s(value.c_str(), "%f|%n", &a, &used) == 1 && used > 0)
                        g_done[value.substr(used)] = { a, false };
                } else if (key.rfind("voice", 0) == 0) {
                    if (sscanf_s(value.c_str(), "%f|%f|%n", &a, &b, &used) == 2 && used > 0)
                        g_cartoon.voices.push_back({ value.substr(used), 0, a, b });
                }
            }
            g_cartoon.title = get("title");
            if (g_cartoon.title.empty()) {
                g_cartoon.voices.clear();
                return;
            }
            g_cartoon.scenes = atoi(get("scenes").c_str());
            g_cartoon.roles = atoi(get("roles").c_str());
            if (g_cartoon.roles <= 0)
                g_cartoon.roles = kDefaultRoles;   // saved before the Voice Booth
            g_cartoon.drawn = static_cast<float>(atof(get("drawn").c_str()));
            g_cartoon.painted = static_cast<float>(atof(get("painted").c_str()));
            g_cartoon.started = static_cast<float>(atof(get("started").c_str()));
            g_cartoon.waitNoticed = get("waited") == "1";
            g_cartoon.cgi = get("cgi") == "1";
            mod::Log("tv: %s '%s' restored - %.1f %s, %.1f %s of %d, %d of %d voiced",
                     g_cartoon.cgi ? "CGI cartoon" : "cartoon", g_cartoon.title.c_str(), g_cartoon.drawn, Drawn(),
                     g_cartoon.painted, Painted(), g_cartoon.scenes, VoicedRoles(), g_cartoon.roles);
        },
        [] {
            Clear();
            g_done.clear();
            g_lastDay = -1;
        });
}

} // namespace tv
