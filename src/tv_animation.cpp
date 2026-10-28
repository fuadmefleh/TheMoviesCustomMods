// Toon Studio: the Animation Studio and its animators.
//
// Drop a film's script in the Story Room and it becomes the studio's cartoon: it is never shot.
// Animators (career 18, hired at the studio's own desk: tv_staff.cpp) draw its scenes at the
// desks, and colour the drawings in Ink & Paint; from 1995 computers do the painting. Each
// animator draws a scene in kDrawDays and paints one in kPaintDays, so ten animators get
// through a thirty-scene film in a few months. The script's bubble on the lot shows how far
// they are. When every scene is painted the film is finished through the game's own "finish
// film" (production::FinishWithoutShoot) and released like any other.
//
// One cartoon at a time. A film already being cast or shot can't be animated.

#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <cmath>

#include "careers.h"
#include "mod.h"
#include "overlay.h"
#include "persist.h"
#include "tv_internal.h"

namespace tv {
namespace {

const char* const kDesks = "room_tva_desks";
const char* const kInk = "room_tva_ink";
const char* const kStory = "room_tva_story";
constexpr float kDrawDays = 20.0f;   // one animator, one scene
constexpr float kPaintDays = 8.0f;
constexpr int kComputerPaintYear = 1995;
constexpr int kStageScriptDone = 3, kStageShoot = 5;

struct Cartoon {
    std::string title;      // empty = none
    uintptr_t   project = 0;
    int         scenes = 0;
    float       drawn = 0, painted = 0;
    float       started = 0;
};
Cartoon g_cartoon;
float g_lastDay = -1;

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

bool PaintedByComputer() { return Year() >= kComputerPaintYear; }

void Finish()
{
    std::string title = g_cartoon.title;
    bool ok = production::FinishWithoutShoot(g_cartoon.project);
    mod::Log("tv: cartoon '%s' %s after %.0f days", title.c_str(), ok ? "finished" : "FAILED to finish",
             game::DateDays() - g_cartoon.started);
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
    int drawing = StaffWorkingIn(careers::kAnimator, kDesks);
    int painting = StaffWorkingIn(careers::kAnimator, kInk);
    float scenes = static_cast<float>(g_cartoon.scenes);
    g_cartoon.drawn = std::min(scenes, g_cartoon.drawn + drawing * days / kDrawDays);
    if (PaintedByComputer())
        g_cartoon.painted = g_cartoon.drawn;
    else
        g_cartoon.painted = std::min(g_cartoon.drawn, g_cartoon.painted + painting * days / kPaintDays);
    static int logged = 0;
    if (logged++ < 40)
        mod::Log("tv: cartoon '%s' - %d drawing, %d painting: %.1f drawn, %.1f painted of %d", g_cartoon.title.c_str(),
                 drawing, painting, g_cartoon.drawn, g_cartoon.painted, g_cartoon.scenes);
    if (g_cartoon.painted >= scenes - 0.001f)
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

bool StartCartoon(uintptr_t project)
{
    game::Project p = game::ReadProjectAt(project);
    std::string title = Narrow(p.title);
    if (IsTvScriptProject(project) || IsShareObject(project) || IsPilotReel(project)) {
        Notify("The Story Room", "Only a film's script can be made into a cartoon here.", kYellow);
        return true;
    }
    if (p.stage < kStageScriptDone) {
        Notify("The Story Room", "That script isn't finished yet. Bring it back when the writers are done.", kYellow);
        return true;
    }
    if (p.stage > kStageScriptDone) {
        Notify("The Story Room",
               Format("\"%s\" is already being made with actors. Only a script that hasn't been cast can be animated.",
                      title.c_str()),
               kYellow);
        return true;
    }
    if (!g_cartoon.title.empty()) {
        Notify("The Story Room",
               Format("The animators are busy with \"%s\". One cartoon at a time.", g_cartoon.title.c_str()), kYellow);
        return true;
    }
    int scenes = production::SceneCount(project);
    if (scenes <= 0) {
        mod::Log("tv: cartoon '%s' has no scenes (%d) - refused", title.c_str(), scenes);
        Notify("The Story Room", "That script has no scenes to draw.", kYellow);
        return true;
    }
    g_cartoon.title = title;
    g_cartoon.project = project;
    g_cartoon.scenes = scenes;
    g_cartoon.started = game::DateDays();
    mod::Log("tv: cartoon '%s' started - %d scenes", title.c_str(), scenes);
    int animators = Animators();
    Notify("A new cartoon",
           Format("\"%s\" will be animated: %d scenes to draw%s. %s", title.c_str(), scenes,
                  PaintedByComputer() ? "" : " and paint",
                  animators ? "Put animators at the desks to draw and in Ink & Paint to colour the drawings in."
                            : "Hire animators at the Animation Studio's desk to start."),
           kGreen);
    return true;
}

} // namespace

bool AnimationDrop(const std::string& zone, uintptr_t project, uintptr_t person)
{
    if (zone.rfind("room_tva_", 0) != 0)
        return false;
    if (project) {
        if (zone == kStory)
            return StartCartoon(project);
        Notify("The Animation Studio", "Drop a film's script in the Story Room to make it into a cartoon.", kYellow);
        return true;
    }
    if (person && zone == kStory)
        Notify("The Animation Studio", "Animators work at the desks and in Ink & Paint. The Story Room takes scripts.",
               kYellow);
    return person != 0;
}

bool CartoonLabel(uintptr_t project, std::wstring* kind, std::wstring* phase, std::wstring* status)
{
    Relink();
    if (g_cartoon.title.empty() || project != g_cartoon.project)
        return false;
    *kind = L"Cartoon";
    *phase = L"Being Animated";
    std::string s = PaintedByComputer()
                        ? Format("%d of %d scenes drawn", static_cast<int>(g_cartoon.drawn), g_cartoon.scenes)
                        : Format("%d of %d scenes drawn, %d painted", static_cast<int>(g_cartoon.drawn), g_cartoon.scenes,
                                 static_cast<int>(g_cartoon.painted));
    *status = std::wstring(s.begin(), s.end());
    return true;
}

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
            if (g_cartoon.title.empty())
                return;
            out["title"] = g_cartoon.title;
            out["scenes"] = std::to_string(g_cartoon.scenes);
            out["drawn"] = Format("%.3f", g_cartoon.drawn);
            out["painted"] = Format("%.3f", g_cartoon.painted);
            out["started"] = Format("%.1f", g_cartoon.started);
        },
        [](const persist::Section& in) {
            Clear();
            g_lastDay = -1;
            auto get = [&](const char* k) {
                auto it = in.find(k);
                return it == in.end() ? std::string() : it->second;
            };
            g_cartoon.title = get("title");
            g_cartoon.scenes = atoi(get("scenes").c_str());
            g_cartoon.drawn = static_cast<float>(atof(get("drawn").c_str()));
            g_cartoon.painted = static_cast<float>(atof(get("painted").c_str()));
            g_cartoon.started = static_cast<float>(atof(get("started").c_str()));
            if (!g_cartoon.title.empty())
                mod::Log("tv: cartoon '%s' restored - %.1f drawn, %.1f painted of %d", g_cartoon.title.c_str(),
                         g_cartoon.drawn, g_cartoon.painted, g_cartoon.scenes);
        },
        [] {
            Clear();
            g_lastDay = -1;
        });
}

} // namespace tv
