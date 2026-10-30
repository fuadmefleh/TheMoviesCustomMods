// The TV Writers' Office: a script office for television, separate from the film one.
//
// Its six desks each write one format. Drop writers on a desk and they write a TV script
// of that format; more writers write faster and better, and writers improve with every
// script they finish. Finished scripts go on the shelf and can be pitched as pilots in
// place of a film.
//
// A TV script dropped in the Animation Studio's Story Room instead becomes a TV cartoon: its
// pilot is drawn by the animators (kAnimPilotScenes scenes, at a feature's speeds, once the
// studio has no feature cartoon) and its lead voiced by a star dropped in the Voice Booth. The
// reel is pitched like any other, and the series is made at the Animation Studio.
//
// Offices built with this version are real script offices (see rooms.cpp): the game sits
// the writers down and writes the script, and this file renames it for TV as it starts and
// shelves it when the office puts it outside. Offices from older saves have plain rooms
// and still write "virtually" on a timer.

#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <cmath>

#include "careers.h"
#include "mod.h"
#include "rooms.h"
#include "tv_internal.h"

namespace tv {

const char* const kWritersType = "facility_tv_script_office";
int g_writerOffices = 0;

namespace {

struct DeskInfo {
    const char* zone;
    const char* name;
    int         format;
};
constexpr int kDeskCount = 6;
const DeskInfo kDesks[kDeskCount] = {
    { "room_tvw_sitcom", "Sitcom desk", Sitcom },
    { "room_tvw_crime", "Crime desk", Procedural },
    { "room_tvw_drama", "Drama desk", Serialized },
    { "room_tvw_soap", "Soap desk", Soap },
    { "room_tvw_scifi", "Science fiction desk", SciFi },
    { "room_tvw_variety", "Variety desk", TalkShow },
};

struct Desk {
    std::vector<std::string> writers;
    std::string title;      // the script being written, empty = none yet
    float       progress = 0;
    uint32_t    seed = 0;
    bool        native = false;   // a script room: the game does the writing
};

// A script the game is writing at a native desk.
struct Writing {
    uintptr_t project = 0;
    uintptr_t office = 0;
    uintptr_t room = 0;
    int       desk = 0;
    bool      seen = false;   // the office has been seen writing it
    std::string title;
};
std::vector<Writing> g_writing;

enum PilotState { PilotNone, PilotShooting, PilotShot };
constexpr float kPilotDays = 21;   // to shoot a pilot once the lead is cast

struct TvScript {
    int         id = 0;
    std::string title;
    int         format = Sitcom;
    float       quality = 0;
    float       written = 0;   // day finished
    std::string writers;
    uintptr_t   project = 0;   // the script object on the lot (found again by title after a load)
    // The pilot episode, shot on a TV Soundstage from this script.
    int         pilot = PilotNone;
    std::string lead;           // the star cast in it (stays on as the series' lead)
    std::string director;
    float       leadStars = 0;  // the star's rating, 0..1
    float       shootStart = 0, shootEnd = 0;   // shootEnd 0 = waiting for a lead
    float       pilotQuality = 0;
    // A TV cartoon: the pilot is drawn at the Animation Studio, the lead voiced in its booth.
    bool        animated = false;
    float       drawn = 0, painted = 0;   // scenes
    float       voiced = 0;               // days the lead has recorded
    uintptr_t   voicePerson = 0;          // this session: time away from the booth doesn't count
    bool        waitNoticed = false;      // "the pilot needs animators" was shown
    bool        voiceNoticed = false;     // "the pilot is waiting for its voice" was shown
};

Desk g_desks[kDeskCount];
std::vector<TvScript> g_shelf;
std::map<std::string, int> g_experience;   // scripts each writer has finished
int g_nextScript = 1;

constexpr float kDaysPerScript = 75.0f;    // one writer, one script
constexpr float kWriterWeekly1950 = 350.0f;
constexpr int   kShelfLimit = 4;           // unpitched scripts before the writers wait
constexpr int   kAnimPilotScenes = 2;
constexpr float kAnimDrawDays = 20.0f, kAnimPaintDays = 8.0f, kAnimVoiceDays = 3.0f;   // as a feature cartoon
const char* const kAnimDesks = "room_tva_desks";
const char* const kAnimInk = "room_tva_ink";
const char* const kAnimVoice = "room_tva_voice";
bool g_relinked = false;                    // shelf scripts matched to their objects after a load

bool Pitched(const TvScript& s);

// Unpitched script objects still on the lot (binned ones and pre-object scripts don't count).
int WaitingScripts()
{
    int n = 0;
    for (const TvScript& s : g_shelf)
        n += s.project && game::ClassName(s.project) == "CProject" && !Pitched(s);
    return n;
}

bool TitleTaken(const std::string& title)
{
    for (const TvScript& s : g_shelf)
        if (s.title == title)
            return true;
    for (const Series& s : g_series)
        if (s.title == title)
            return true;
    return false;
}

// Puts the finished script outside the office, fanned out so they don't pile up.
uintptr_t PlaceScript(const TvScript& s)
{
    for (const game::Facility& f : game::Facilities()) {
        if (f.type != kWritersType)
            continue;
        float pos[3], angle;
        if (!game::FacilityPlacement(f.address, pos, &angle))
            continue;
        // Models face -y; the front door is half the building's depth away along that axis.
        float fx = std::sin(angle), fy = -std::cos(angle);
        float side = ((s.id % 4) - 1.5f) * 1.6f;
        float x = pos[0] + fx * 9.0f + fy * side;
        float y = pos[1] + fy * 9.0f - fx * side;
        uintptr_t project = game::SpawnScript(std::wstring(s.title.begin(), s.title.end()), x, y);
        mod::Log("tv: script object for '%s' at (%.1f, %.1f) - office at (%.1f, %.1f) angle %.2f -> %08X",
                 s.title.c_str(), x, y, pos[0], pos[1], angle, static_cast<unsigned>(project));
        return project;
    }
    return 0;
}

bool Pitched(const TvScript& s)
{
    game::Project p;
    p.title = std::wstring(s.title.begin(), s.title.end());
    p.startDate = -1000.0f - s.id;
    std::string key = PilotKey(p);
    for (const Series& sr : g_series)
        if (sr.pilotKey == key)
            return true;
    return false;
}

// After a load the script objects are back, at new addresses: find them by title.
void Relink()
{
    g_relinked = true;
    std::vector<game::Project> mine = game::PlayerProductions();
    auto find = [&](const std::string& t) {
        std::wstring title(t.begin(), t.end());
        uintptr_t found = 0;
        for (const game::Project& p : mine)
            if (p.title == title && p.stage < 7)
                found = p.address;
        return found;
    };
    for (TvScript& s : g_shelf) {
        s.project = find(s.title);
        rooms::RefreshScriptLook(s.project);
    }
    for (Writing& w : g_writing) {
        w.project = find(w.title);
        rooms::RefreshScriptLook(w.project);
    }
}

int DeskOf(const std::string& zone)
{
    for (int i = 0; i < kDeskCount; i++)
        if (zone == kDesks[i].zone)
            return i;
    return -1;
}

float WriterSkill(const Desk& d)
{
    if (d.writers.empty())
        return 0;
    float sum = 0;
    for (const std::string& w : d.writers) {
        auto it = g_experience.find(w);
        sum += std::min(5, it == g_experience.end() ? 0 : it->second);
    }
    return sum / d.writers.size();
}

std::string UniqueTitle(int format, uint32_t seed)
{
    std::string title = ShowTitle(format, seed);
    for (uint32_t k = 1; TitleTaken(title) && k < 40; k++)
        title = ShowTitle(format, seed + k * 7919u);
    return title;
}

// A finished script from a desk's writers: quality from how many there are and how
// experienced, and one more script of experience each.
TvScript NewScript(const Desk& d, int deskIndex, const std::string& title)
{
    const DeskInfo& info = kDesks[deskIndex];
    int n = std::max(1, static_cast<int>(d.writers.size()));
    TvScript s;
    s.id = g_nextScript++;
    s.title = title;
    s.format = info.format;
    s.quality = std::clamp(0.28f + 0.07f * std::min(n, 5) + 0.025f * WriterSkill(d) + TechQualityBonus(info.format) +
                               Roll(s.id, deskIndex, 909, -0.08f, 0.08f),
                           0.1f, 0.95f);
    s.written = Today();
    for (const std::string& w : d.writers) {
        s.writers += (s.writers.empty() ? "" : ", ") + w;
        g_experience[w]++;
    }
    return s;
}

void Shelve(const TvScript& s)
{
    g_shelf.push_back(s);
    rooms::RefreshScriptLook(s.project);
    Notify(Format("New TV script: '%s'", s.title.c_str()),
           s.project ? Format("A %s script, quality %.0f%%, is waiting outside the TV Writers' Office. Drag it onto any "
                              "TV building to pitch it.", kFormats[s.format].name, s.quality * 100)
                     : Format("A %s script, quality %.0f%%. Pitch it from 'Pitch a Pilot'.", kFormats[s.format].name,
                              s.quality * 100),
           kGold);
    mod::Log("tv: script '%s' (%s) written by %s, quality %.2f, object %08X", s.title.c_str(), kFormats[s.format].name,
             s.writers.empty() ? "?" : s.writers.c_str(), s.quality, static_cast<unsigned>(s.project));
}

void FinishScript(Desk& d, int deskIndex)
{
    TvScript s = NewScript(d, deskIndex, TitleTaken(d.title) ? UniqueTitle(kDesks[deskIndex].format, d.seed + 1) : d.title);
    s.project = PlaceScript(s);
    Shelve(s);
    d.title.clear();
    d.progress = 0;
}

// --- Native desks: the game writes, we rename and shelve ---------------------------------

uintptr_t g_deskRoom[kDeskCount] = {};   // the script room behind each desk (this session)

// The TV office writing this project right now, or 0.
uintptr_t OfficeWriting(uintptr_t project)
{
    for (const game::Facility& f : game::Facilities())
        if (f.type == kWritersType && rooms::OfficeProject(f.address) == project)
            return f.address;
    return 0;
}

void StartWriting(int desk, uintptr_t project, uintptr_t room)
{
    for (const Writing& w : g_writing)
        if (w.project == project)
            return;
    Writing w;
    w.project = project;
    w.room = room;
    w.desk = desk;
    w.office = rooms::ScriptRoomOffice(room);
    w.title = UniqueTitle(kDesks[desk].format, static_cast<uint32_t>(Today()) * 2654435761u + desk * 97);
    for (const Writing& o : g_writing)
        if (o.title == w.title)
            w.title = UniqueTitle(kDesks[desk].format, static_cast<uint32_t>(project));
    bool renamed = game::SetProjectTitle(project, std::wstring(w.title.begin(), w.title.end()));
    g_writing.push_back(w);
    rooms::RefreshScriptLook(project);
    g_desks[desk].title = w.title;
    mod::Log("tv: %s starts '%s' (project %08X, office %08X writing %08X, renamed %d)", kDesks[desk].name,
             w.title.c_str(), static_cast<unsigned>(project), static_cast<unsigned>(w.office),
             static_cast<unsigned>(rooms::OfficeProject(w.office)), renamed);
}

// Called daily: new scripts at the desks, and scripts the office has finished.
void WatchNativeDesks()
{
    for (int i = 0; i < kDeskCount; i++)
        if (uintptr_t p = rooms::ScriptRoomProject(g_deskRoom[i]))
            StartWriting(i, p, g_deskRoom[i]);
    for (size_t k = 0; k < g_writing.size();) {
        Writing& w = g_writing[k];
        if (!w.project || game::ClassName(w.project) != "CProject") {
            mod::Log("tv: lost track of '%s' before it was finished", w.title.c_str());
            g_writing.erase(g_writing.begin() + k);
            continue;
        }
        if (OfficeWriting(w.project)) {
            if (!w.seen)
                mod::Log("tv: the office is writing '%s'", w.title.c_str());
            w.seen = true;
            k++;
            continue;
        }
        if (!w.seen) {   // not started yet: the writer may still be walking over
            k++;
            continue;
        }
        Desk& d = g_desks[w.desk];
        TvScript s = NewScript(d, w.desk, w.title);
        s.project = w.project;
        game::SetProjectTitle(w.project, std::wstring(s.title.begin(), s.title.end()));
        Shelve(s);
        if (d.title == w.title)
            d.title.clear();
        g_writing.erase(g_writing.begin() + k);
    }
}

// ---------------------------------------------------------------------------------
// Pilots: a TV script becomes a pilot episode on a Multi-Camera Soundstage. Drop the script
// on a TV building (or press Shoot the pilot), cast a star by dropping them on the
// soundstage, and three weeks later the pilot is ready to pitch.

game::Project ShelfProject(const TvScript& t)
{
    game::Project p;
    p.title = std::wstring(t.title.begin(), t.title.end());
    bool shot = t.pilot == PilotShot;
    p.quality = p.rawQuality = shot ? t.pilotQuality : t.quality;
    p.starRating = shot ? t.leadStars : 0.5f;
    p.startDate = -1000.0f - t.id;
    p.stage = -2;
    p.address = t.project;   // so dropping the script object picks this pilot
    return p;
}

int ShootingPilots()
{
    int n = 0;
    for (const TvScript& t : g_shelf)
        n += t.pilot == PilotShooting && !t.animated;   // animated ones are at the Animation Studio
    return n;
}

int FreeStages() { return (g_detectionWorks ? g_stages : 1) - ShootingPilots(); }

double PilotCost(const TvScript& t)
{
    return kFormats[t.format].cost1950 * 1.5 * EraFactor(Year()) * g_costScale;
}

bool StartPilot(TvScript& t)
{
    if (t.pilot != PilotNone || Pitched(t))
        return false;
    if (FreeStages() <= 0) {
        Notify(Format("No soundstage free for '%s'", t.title.c_str()),
               g_stages ? "Every Multi-Camera Soundstage is shooting a pilot. Build another, or wait."
                        : "Pilots are shot on a Multi-Camera Soundstage. Build one first.",
               kYellow);
        return false;
    }
    double cost = PilotCost(t);
    game::AddMoney(-cost);
    t.pilot = PilotShooting;
    t.shootStart = Today();
    t.shootEnd = 0;
    Notify(Format("Shooting the pilot of '%s'", t.title.c_str()),
           Format("%s for the pilot. Drop a star on the soundstage to play the lead (and a director in its control "
                  "room if you like); shooting takes three weeks once the lead is cast.", Dollars(cost).c_str()),
           kGreen);
    mod::Log("tv: pilot of '%s' started, cost %.0f", t.title.c_str(), cost);
    return true;
}

void CastLead(TvScript& t, const std::string& name, float stars)
{
    t.lead = name;
    t.leadStars = std::clamp(stars, 0.0f, 1.0f);
    t.shootEnd = Today() + kPilotDays;
}

void FinishPilot(TvScript& t)
{
    float stage = kFormats[t.format].multiCamera ? 0.04f : 0.0f;
    float noise = Roll(static_cast<uint32_t>(t.id), static_cast<uint32_t>(t.shootStart), 7u, -0.05f, 0.05f);
    t.pilotQuality = std::clamp(0.55f * t.quality + 0.30f * t.leadStars + (t.director.empty() ? 0.02f : 0.08f) + stage +
                                    noise, 0.05f, 1.0f);
    t.pilot = PilotShot;
    rooms::RefreshScriptLook(t.project);   // the script becomes a reel
    Notify(Format("The pilot of '%s' is in the can", t.title.c_str()),
           Format("Starring %s. Quality %.0f%%. Drag its reel to the Network HQ to pitch it to the networks.",
                  t.lead.c_str(), t.pilotQuality * 100),
           kGold);
    mod::Log("tv: pilot '%s' shot - script %.2f, %s %.2f, director '%s' -> %.2f", t.title.c_str(), t.quality,
             t.lead.c_str(), t.leadStars, t.director.c_str(), t.pilotQuality);
}

// --- Animated pilots ---------------------------------------------------------------------

TvScript* AnimatedPilot()
{
    for (TvScript& t : g_shelf)
        if (t.animated && t.pilot == PilotShooting)
            return &t;
    return nullptr;
}

bool LeadVoiced(const TvScript& t) { return !t.lead.empty() && t.voiced >= kAnimVoiceDays; }

void FinishAnimatedPilot(TvScript& t)
{
    int animators = Animators();
    float noise = Roll(static_cast<uint32_t>(t.id), static_cast<uint32_t>(t.shootStart), 7u, -0.05f, 0.05f);
    t.pilotQuality = std::clamp(0.55f * t.quality + 0.30f * t.leadStars + 0.02f * std::min(animators, 4) + noise, 0.05f,
                                1.0f);
    t.pilot = PilotShot;
    rooms::RefreshScriptLook(t.project);   // the script becomes a reel
    Notify(Format("The pilot of '%s' is drawn", t.title.c_str()),
           Format("%s voices the lead. Quality %.0f%%. Drag its reel to the Network HQ to pitch it to the networks.",
                  t.lead.c_str(), t.pilotQuality * 100),
           kGold);
    mod::Log("tv: animated pilot '%s' finished after %.0f days - script %.2f, %s %.2f, %d animators -> %.2f",
             t.title.c_str(), Today() - t.shootStart, t.quality, t.lead.c_str(), t.leadStars, animators,
             t.pilotQuality);
}

// One day of the animators' work on the TV pilot; a feature cartoon comes first.
void AnimatedPilotDaily()
{
    TvScript* t = AnimatedPilot();
    if (!t || CartoonInProgress())
        return;
    if (!t->lead.empty() && t->voiced < kAnimVoiceDays && (!t->voicePerson || ZoneOf(t->voicePerson) == kAnimVoice)) {
        t->voiced = std::min(kAnimVoiceDays, t->voiced + 1.0f);
        if (t->voiced >= kAnimVoiceDays)
            Notify(Format("%s has voiced '%s'", t->lead.c_str(), t->title.c_str()), "The pilot's lead is recorded.",
                   kGreen);
    }
    if (t->lead.empty() && g_level == Casual && Today() - t->shootStart > 7) {
        t->lead = "a voice actor";   // casual players get a stand-in
        t->leadStars = 0.35f;
        t->voiced = kAnimVoiceDays;
    }
    int drawing = StaffWorkingIn(careers::kAnimator, kAnimDesks);
    int painting = StaffWorkingIn(careers::kAnimator, kAnimInk);
    float scenes = static_cast<float>(kAnimPilotScenes);
    t->drawn = std::min(scenes, t->drawn + drawing / kAnimDrawDays);
    t->painted = std::min(t->drawn, t->painted + painting / kAnimPaintDays);
    if (drawing == 0 && t->drawn < scenes && !t->waitNoticed) {
        t->waitNoticed = true;
        Notify(Format("'%s' needs animators", t->title.c_str()),
               Animators() ? "Drop animators on the Animators' Desks to draw the pilot, and in Ink & Paint to colour it."
                           : "Hire animators at the Animation Studio's desk, then drop them on the Animators' Desks.",
               kYellow);
    }
    if (t->painted < scenes - 0.001f)
        return;
    if (!LeadVoiced(*t)) {
        if (!t->voiceNoticed) {
            t->voiceNoticed = true;
            Notify(Format("The pilot of '%s' is drawn", t->title.c_str()),
                   "It's waiting for its lead's voice: drop a star in the Animation Studio's Voice Booth.", kYellow);
        }
        return;
    }
    FinishAnimatedPilot(*t);
}

void PilotsDaily()
{
    AnimatedPilotDaily();
    for (TvScript& t : g_shelf) {
        if (t.pilot != PilotShooting || t.animated)
            continue;
        if (t.shootEnd == 0 && g_level == Casual && Today() - t.shootStart > 7)
            CastLead(t, "a TV actor", 0.35f);   // casual players get a stand-in
        if (t.shootEnd > 0 && Today() >= t.shootEnd)
            FinishPilot(t);
    }
}

// With nothing in development the studio's TV side sits idle. Casual players get their best
// waiting script pitched for them; everyone else gets a reminder now and then.
float g_lastNudge = 0;

void KeepTvBusy(float day)
{
    for (const Series& sr : g_series)
        if (InProduction(sr) || sr.status == Pitching || sr.status == Rework || sr.status == RenewalOffered)
            return;
    if (!HasOffice())
        return;
    if (g_level == Casual)
        AutoPilots(false);
    const TvScript* best = nullptr;
    for (const TvScript& t : g_shelf)
        if (!Pitched(t) && (g_filmPilots || t.pilot == PilotShot) && (!best || t.quality > best->quality))
            best = &t;
    if (g_level == Casual && best) {
        std::string title = PitchBestPilot();
        Notify(Format("Your TV team pitches '%s'", title.c_str()),
               "Nothing was in development, so they took the best pilot off the shelf.", kGreen);
        return;
    }
    if (day - g_lastNudge < kDaysPerYear)
        return;
    g_lastNudge = day;
    Notify("The networks want new shows",
           best ? Format("You have nothing in development. '%s' is waiting on the script shelf - pitch it from the TV "
                         "screen (F8).", best->title.c_str())
                : std::string("You have nothing in development. Put writers in a TV Writers' Office, or pitch a released "
                              "film from the TV screen (F8)."),
           kYellow);
}

} // namespace

bool IsWritersZone(const std::string& zone) { return DeskOf(zone) >= 0; }

bool IsTvScriptProject(uintptr_t project)
{
    if (!project)
        return false;
    for (const TvScript& s : g_shelf)
        if (s.project == project)
            return true;
    for (const Writing& w : g_writing)
        if (w.project == project)
            return true;
    return false;
}

bool IsShelfScriptObject(uintptr_t project)
{
    for (const TvScript& s : g_shelf)
        if (project && s.project == project)
            return true;
    return false;
}

bool AdoptIfWriting(uintptr_t project)
{
    for (size_t k = 0; k < g_writing.size(); k++) {
        if (g_writing[k].project != project)
            continue;
        Writing w = g_writing[k];
        g_writing.erase(g_writing.begin() + k);
        TvScript s = NewScript(g_desks[w.desk], w.desk, w.title);
        s.project = project;
        Shelve(s);
        mod::Log("tv: '%s' adopted when it was dropped on a TV building", w.title.c_str());
        return true;
    }
    return false;
}

void AssignWriterToDesk(const std::string& zone, const std::string& person, uintptr_t scriptRoom)
{
    int i = DeskOf(zone);
    if (i < 0 || person.empty())
        return;
    for (Desk& d : g_desks)
        d.writers.erase(std::remove(d.writers.begin(), d.writers.end(), person), d.writers.end());
    g_desks[i].writers.push_back(person);
    if (scriptRoom) {
        g_desks[i].native = true;
        g_deskRoom[i] = scriptRoom;
        if (uintptr_t p = rooms::ScriptRoomProject(scriptRoom))
            StartWriting(i, p, scriptRoom);
        else
            mod::Log("tv: %s has no project yet after the drop", kDesks[i].name);
    }
    Notify(Format("%s joins the %s", person.c_str(), kDesks[i].name),
           Format("%d writer%s on %s scripts.", static_cast<int>(g_desks[i].writers.size()),
                  g_desks[i].writers.size() == 1 ? "" : "s", kFormats[kDesks[i].format].name),
           kGreen);
}

void WritersDaily(float day)
{
    PilotsDaily();
    if (static_cast<int>(day) % kNights == 0)
        KeepTvBusy(day);
    if (g_writerOffices == 0 && g_detectionWorks)
        return;
    if (!g_relinked)
        Relink();
    WatchNativeDesks();
    bool idle = WritersOnStrike() || WaitingScripts() >= kShelfLimit;
    for (int i = 0; i < kDeskCount; i++) {
        Desk& d = g_desks[i];
        int n = static_cast<int>(d.writers.size());
        if (n == 0 || idle || d.native)
            continue;
        if (d.title.empty()) {
            d.seed = static_cast<uint32_t>(day) * 2654435761u + i * 97;
            d.title = ShowTitle(kDesks[i].format, d.seed);
            d.progress = 0;
        }
        d.progress += (1.0f + 0.6f * (n - 1)) / kDaysPerScript;
        if (d.progress >= 1.0f)
            FinishScript(d, i);
    }
    if (static_cast<int>(day) % kNights == 0) {
        int writers = 0;   // native desks' writers are studio staff, paid by the game
        for (const Desk& d : g_desks)
            writers += d.native ? 0 : static_cast<int>(d.writers.size());
        if (writers)
            game::AddMoney(-writers * kWriterWeekly1950 * EraFactor(Year()) * g_costScale);
    }
}

// Finished, unused scripts as pilot choices: a "project" with no address, a negative start
// date (so its pilot key is unique) and a star rating standing for "written for TV".
void AddShelfPilots(std::vector<game::Project>& out, std::vector<std::string>& seen)
{
    for (const TvScript& t : g_shelf) {
        if (!g_filmPilots && t.pilot != PilotShot)
            continue;   // the networks want to see a pilot, not a script
        game::Project p = ShelfProject(t);
        std::string key = PilotKey(p);
        bool used = false;
        for (const Series& sr : g_series)
            used |= sr.pilotKey == key;
        if (used || std::find(seen.begin(), seen.end(), key) != seen.end())
            continue;
        seen.push_back(key);
        out.push_back(p);
    }
}

void ApplyPilot(Series& s, const game::Project& p)
{
    s.pilotKey = PilotKey(p);
    s.pilotTitle = Utf8(p.title);
    s.pilotQuality = std::max(0.0f, p.quality);
    s.pilotStars = std::max(0.0f, p.starRating);
    if (p.stage != -2)
        return;
    int id = static_cast<int>(-p.startDate - 1000.0f + 0.5f);
    for (const TvScript& t : g_shelf) {
        if (t.id != id)
            continue;
        s.animated = t.animated;
        if (t.pilot == PilotShot && !t.lead.empty() && t.lead != "a TV actor" && t.lead != "a voice actor")
            s.lead = t.lead;   // the pilot's star stays on for the series
    }
}

bool StartPilotForProject(uintptr_t project)
{
    for (TvScript& t : g_shelf)
        if (project && t.project == project)
            return t.pilot == PilotNone && StartPilot(t);
    return false;
}

bool IsUnshotScript(uintptr_t project)
{
    for (const TvScript& t : g_shelf)
        if (project && t.project == project)
            return t.pilot != PilotShot;
    return false;
}

bool CastPilot(const std::string& name, float stars, bool director)
{
    for (TvScript& t : g_shelf) {
        if (t.pilot != PilotShooting || t.animated)
            continue;
        if (director) {
            if (!t.director.empty())
                continue;
            t.director = name;
            Notify(Format("%s directs the pilot of '%s'", name.c_str(), t.title.c_str()), "A director lifts the pilot.",
                   kGreen);
            return true;
        }
        if (t.shootEnd > 0)
            continue;
        CastLead(t, name, stars);
        Notify(Format("%s stars in the pilot of '%s'", name.c_str(), t.title.c_str()),
               Format("Shooting wraps around %s.", DateText(t.shootEnd).c_str()), kGreen);
        return true;
    }
    return false;
}

std::vector<ShelfEntry> ShelfSummary()
{
    std::vector<ShelfEntry> out;
    for (const TvScript& t : g_shelf) {
        if (Pitched(t))
            continue;
        ShelfEntry e;
        e.title = t.title;
        e.lead = t.lead;
        e.director = t.director;
        e.format = t.format;
        e.pilot = t.pilot;
        e.quality = t.quality;
        e.pilotQuality = t.pilotQuality;
        e.wrapDay = t.shootEnd;
        e.animated = t.animated;
        out.push_back(e);
    }
    return out;
}

int FreeSoundstages() { return FreeStages(); }

bool AnimatePilot(uintptr_t project)
{
    TvScript* t = nullptr;
    for (TvScript& s : g_shelf)
        if (project && s.project == project)
            t = &s;
    if (!t)
        return false;
    if (t->pilot == PilotShot || Pitched(*t)) {
        Notify("The Story Room", Format("The pilot of '%s' is finished: take its reel to the Network HQ.", t->title.c_str()),
               kYellow);
        return true;
    }
    if (t->pilot == PilotShooting && !t->animated) {
        Notify("The Story Room", Format("'%s' is being shot on a soundstage.", t->title.c_str()), kYellow);
        return true;
    }
    if (t->animated) {   // dropped again: where it stands
        Notify(Format("'%s'", t->title.c_str()),
               Format("%d of %d scenes drawn, %d painted. %s", static_cast<int>(t->drawn), kAnimPilotScenes,
                      static_cast<int>(t->painted),
                      t->lead.empty()          ? "The lead needs a voice: drop a star in the Voice Booth."
                      : LeadVoiced(*t)         ? Format("%s has voiced the lead.", t->lead.c_str()).c_str()
                                               : Format("%s is recording the lead.", t->lead.c_str()).c_str()),
               kYellow);
        return true;
    }
    if (TvScript* other = AnimatedPilot()) {
        Notify("The Story Room",
               Format("The animators are drawing the pilot of '%s'. One TV pilot at a time.", other->title.c_str()),
               kYellow);
        return true;
    }
    double cost = PilotCost(*t) * 0.75;
    game::AddMoney(-cost);
    t->pilot = PilotShooting;
    t->animated = true;
    t->shootStart = Today();
    t->shootEnd = 0;
    t->drawn = t->painted = t->voiced = 0;
    t->lead.clear();
    t->director.clear();
    rooms::RefreshScriptLook(t->project);
    mod::Log("tv: '%s' becomes a TV cartoon - pilot cost %.0f", t->title.c_str(), cost);
    Notify(Format("'%s' will be a cartoon", t->title.c_str()),
           Format("%s for the pilot. The animators draw its %d scenes %s, and a star dropped in the Voice Booth voices "
                  "the lead.",
                  Dollars(cost).c_str(), kAnimPilotScenes,
                  CartoonInProgress() ? "once the feature cartoon is finished" : "at the desks and in Ink & Paint"),
           kGreen);
    return true;
}

bool AnimatedPilotInProgress() { return AnimatedPilot() != nullptr; }

bool PilotVoiceDrop(uintptr_t room, uintptr_t person, const std::string& personName, float rating)
{
    TvScript* t = AnimatedPilot();
    if (!t)
        return false;
    std::string name = personName.empty() ? Format("Star #%04X", static_cast<unsigned>(person & 0xFFFF)) : personName;
    if (LeadVoiced(*t)) {
        Notify("The Voice Booth", Format("%s has already voiced the lead of '%s'.", t->lead.c_str(), t->title.c_str()),
               kYellow);
        return true;
    }
    if (t->lead != name) {
        t->lead = name;
        t->leadStars = std::clamp(rating, 0.0f, 1.0f);
        t->voiced = 0;
    }
    t->voicePerson = person;
    bool seated = rooms::SeatInScene(room, person);
    mod::Log("tv: %s voices the lead of '%s' (rating %.2f, %s)", name.c_str(), t->title.c_str(), rating,
             seated ? "seated" : "FAILED to seat");
    Notify(Format("%s voices '%s'", name.c_str(), t->title.c_str()),
           Format("Recording the lead takes %.0f days in the booth, and %s stays on as the voice of the series.",
                  kAnimVoiceDays, name.c_str()),
           kGold);
    return true;
}

namespace {

// A series from a shot pilot: pitched to the network that suits it best, or ordered
// straight onto your own network.
void PitchScript(const TvScript& t, bool ownNetwork)
{
    Series s;
    s.id = g_nextId++;
    s.title = t.title;
    s.format = t.format;
    ApplyPilot(s, ShelfProject(t));
    if (ownNetwork) {
        s.network = kPlayerNetwork;
        int episodes = s.format == Miniseries ? 6 : FullOrder(s.format, Year());
        OrderSeason(s, episodes, Today() + kPremiereDelay);
        g_series.push_back(s);
        AutoSchedule();
        Notify(Format("'%s' greenlit", s.title.c_str()),
               Format("%d episodes for %s, on the air in two weeks.", episodes, NetworkName(kPlayerNetwork)), kGold);
    } else {
        StartPitch(s, std::max(0, BestNetwork(s)));
        g_series.push_back(s);
    }
    mod::Log("tv: %s '%s' (pilot %.2f)", ownNetwork ? "greenlit" : "pitched", t.title.c_str(), s.pilotQuality);
}

TvScript* ReelScript(uintptr_t project)
{
    for (TvScript& t : g_shelf)
        if (project && t.project == project && t.pilot == PilotShot && !Pitched(t))
            return &t;
    return nullptr;
}

} // namespace

std::string PitchBestPilot()
{
    const TvScript* best = nullptr;
    for (const TvScript& t : g_shelf)
        if (!Pitched(t) && (g_filmPilots || t.pilot == PilotShot) &&
            (!best || (t.pilot == PilotShot ? t.pilotQuality : t.quality) >
                          (best->pilot == PilotShot ? best->pilotQuality : best->quality)))
            best = &t;
    if (!best)
        return "";
    PitchScript(*best, false);
    return best->title;
}

bool IsPilotReel(uintptr_t project) { return ReelScript(project) != nullptr; }

const char* PilotReelMesh(uintptr_t project)
{
    return ReelScript(project) ? "p_icon_finished_tv.msh" : nullptr;
}

bool TvScriptLabel(uintptr_t project, std::wstring* kind, std::wstring* phase, std::wstring* status)
{
    const TvScript* t = nullptr;
    for (const TvScript& s : g_shelf)
        if (project && s.project == project)
            t = &s;
    if (!t)
        return false;   // still being written: the game's "Scriptwriting" is right
    std::string format = Format("%s%s", t->animated ? "Animated " : "",
                                t->format >= 0 && t->format < FormatCount ? kFormats[t->format].name : "TV");
    if (t->animated && t->pilot == PilotShooting) {
        *kind = html::Text(Format("%s script", format.c_str()));
        *phase = L"Pilot being animated";
        std::string st = Format("%d of %d scenes drawn, %d painted, ", static_cast<int>(t->drawn), kAnimPilotScenes,
                                static_cast<int>(t->painted));
        st += LeadVoiced(*t) ? "lead voiced" : t->lead.empty() ? "lead needs a voice" : "lead recording";
        *status = html::Text(st);
        return true;
    }
    if (Pitched(*t)) {
        *kind = html::Text(Format("%s pilot", format.c_str()));
        *phase = L"Pitched";
        *status = L"See the TV screen";
    } else if (t->pilot == PilotShot) {
        *kind = html::Text(Format("%s pilot", format.c_str()));
        *phase = html::Text(Format("Pilot shot - %.0f%%", t->pilotQuality * 100));
        *status = L"Take it to the Network HQ";
    } else if (t->pilot == PilotShooting) {
        *kind = html::Text(Format("%s script", format.c_str()));
        *phase = L"Pilot in production";
        *status = t->shootEnd == 0 ? L"Needs a star on the soundstage" : L"Shooting on the soundstage";
    } else {
        *kind = html::Text(Format("%s script", format.c_str()));
        *phase = L"Script Complete";
        *status = L"Ready to shoot a pilot";
    }
    return true;
}

// A pilot's reel dropped on a TV building: the Network HQ (or the TV Production Office)
// pitches it to the networks, the Control Room puts it on your own network.
bool DropReel(const std::string& zone, uintptr_t project)
{
    TvScript* t = ReelScript(project);
    if (!t)
        return false;
    if (zone.rfind("room_tvc_", 0) == 0) {
        if (!g_networks[kPlayerNetwork].exists) {
            Notify(Format("Nowhere to air '%s'", t->title.c_str()),
                   "The Control Room runs your own network, and you haven't launched one. Take the reel to the Network "
                   "HQ to pitch it to the networks instead.",
                   kYellow);
            return true;
        }
        PitchScript(*t, true);
    } else if (zone.rfind("room_tvn_", 0) == 0 || zone.rfind("room_tv_", 0) == 0) {
        PitchScript(*t, false);
    } else {
        Notify(Format("The pilot of '%s' is finished", t->title.c_str()),
               "Take the reel to the Network HQ (or the TV Production Office) to pitch it to the networks, or to the "
               "Control Room to air it on your own network.",
               kYellow);
        return true;
    }
    rooms::RefreshScriptLook(project);
    return true;
}

void AutoPilots(bool castStandIns)
{
    for (TvScript& t : g_shelf)
        if (castStandIns && t.pilot == PilotShooting && t.shootEnd == 0 && !t.animated)
            CastLead(t, "a TV actor", 0.35f);
    while (FreeStages() > 0) {
        TvScript* best = nullptr;
        for (TvScript& t : g_shelf)
            if (t.pilot == PilotNone && !Pitched(t) && (!best || t.quality > best->quality))
                best = &t;
        if (!best || !StartPilot(*best))
            return;
        if (castStandIns)
            CastLead(*best, "a TV actor", 0.35f);
    }
}

int ShelfScriptFormat(const game::Project& p)
{
    if (p.stage != -2)
        return -1;
    int id = static_cast<int>(-p.startDate - 1000.0f + 0.5f);
    for (const TvScript& s : g_shelf)
        if (s.id == id)
            return s.format;
    return -1;
}

// ---------------------------------------------------------------------------------
// Writers tab

void DrawWriters()
{
    if (g_writerOffices == 0 && g_detectionWorks) {
        ImGui::TextWrapped("Build a TV Writers' Office to write scripts for television. Drop scriptwriters on its "
                           "desks - one desk per format - and they'll write pilots you can pitch without making a "
                           "film first.");
        ImGui::Spacing();
    }
    constexpr ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit;
    if (ImGui::BeginTable("desks", 4, flags)) {
        ImGui::TableSetupColumn("Desk");
        ImGui::TableSetupColumn("Writers", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Working on");
        ImGui::TableSetupColumn("Progress");
        ImGui::TableHeadersRow();
        for (int i = 0; i < kDeskCount; i++) {
            Desk& d = g_desks[i];
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(kDesks[i].name);
            ImGui::TableNextColumn();
            if (d.writers.empty()) {
                ImGui::TextColored(kGrey, "Drop writers on this desk");
            } else {
                std::string names;
                for (const std::string& w : d.writers)
                    names += (names.empty() ? "" : ", ") + w;
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextUnformatted(names.c_str());
                ImGui::PopTextWrapPos();
                if (ImGui::SmallButton(Format("Clear desk##d%d", i).c_str()))
                    d.writers.clear();
            }
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(d.title.empty() ? "-" : ("'" + d.title + "'").c_str());
            ImGui::TableNextColumn();
            if (d.native)
                ImGui::TextColored(kGrey, d.title.empty() ? "Waiting for writers" : "Writing at the desk");
            else if (!d.writers.empty())
                ImGui::ProgressBar(d.progress, ImVec2(160, 0));
        }
        ImGui::EndTable();
    }
    if (WritersOnStrike())
        ImGui::TextColored(kRed, "The writers are on strike. The desks are empty until it's settled.");
    else if (WaitingScripts() >= kShelfLimit)
        ImGui::TextColored(kRed, "The shelf is full (%d scripts). Pitch or bin some before the writers start another.",
                           kShelfLimit);
    bool timed = false;
    for (const Desk& d : g_desks)
        timed |= !d.native && !d.writers.empty();
    if (timed)
        ImGui::TextColored(kGrey, "Writers cost %s a week each. Experienced writers (and more of them) write better scripts.",
                           Dollars(kWriterWeekly1950 * EraFactor(Year()) * g_costScale).c_str());
    else
        ImGui::TextColored(kGrey, "Drop a writer on a desk to choose the format; they sit down in the office and write. "
                                  "Drop more writers on the same desk (or in the office) to help. More writers, and more "
                                  "experienced ones, write better scripts.");

    ImGui::SeparatorText("Scripts and pilots");
    std::vector<TvScript*> waiting;
    for (TvScript& t : g_shelf)
        if (!Pitched(t))
            waiting.push_back(&t);
    if (waiting.empty()) {
        ImGui::TextColored(kGrey, "No finished scripts waiting.");
        return;
    }
    std::sort(waiting.begin(), waiting.end(), [](const TvScript* a, const TvScript* b) {
        return a->pilot != b->pilot ? a->pilot > b->pilot : a->quality > b->quality;
    });
    int binId = 0;
    for (TvScript* t : waiting) {
        ImGui::PushID(t->id);
        if (t->pilot == PilotShot || g_filmPilots) {
            if (ImGui::SmallButton("Pitch"))
                OpenPitchForKey(PilotKey(ShelfProject(*t)));
            ImGui::SameLine();
        }
        if (t->pilot == PilotNone) {
            ImGui::BeginDisabled(FreeStages() <= 0);
            if (ImGui::SmallButton(Format("Shoot the pilot (%s)", Dollars(PilotCost(*t)).c_str()).c_str()))
                StartPilot(*t);
            ImGui::EndDisabled();
            if (FreeStages() <= 0 && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip(g_stages ? "Every soundstage is busy shooting a pilot."
                                           : "Pilots are shot on a Multi-Camera Soundstage: build one.");
            ImGui::SameLine();
        }
        if (t->pilot != PilotShooting) {
            if (ImGui::SmallButton("Bin"))
                binId = t->id;
            ImGui::SameLine();
        }
        ImGui::Text("'%s'  -  %s, script %.0f%%", t->title.c_str(), kFormats[t->format].name, t->quality * 100);
        if (t->pilot == PilotShooting) {
            if (t->shootEnd == 0)
                ImGui::TextColored(kYellow, "    Shooting the pilot: drop a star on the soundstage to play the lead%s.",
                                   t->director.empty() ? " (and a director in its control room)" : "");
            else
                ImGui::TextColored(kGreen, "    Shooting with %s%s - wraps %s.", t->lead.c_str(),
                                   t->director.empty() ? "" : (", directed by " + t->director).c_str(),
                                   DateText(t->shootEnd).c_str());
        } else if (t->pilot == PilotShot) {
            ImGui::TextColored(kGold, "    Pilot ready: starring %s, quality %.0f%%.", t->lead.c_str(), t->pilotQuality * 100);
        }
        ImGui::PopID();
    }
    if (binId)
        g_shelf.erase(std::remove_if(g_shelf.begin(), g_shelf.end(), [&](const TvScript& x) { return x.id == binId; }),
                      g_shelf.end());
    ImGui::TextColored(kGrey, "Finished scripts wait outside the office. Drag one onto any TV building (or press Shoot "
                              "the pilot) to shoot its pilot on a Multi-Camera Soundstage, then cast a star by dropping "
                              "them on the soundstage. Finished pilots are pitched to the networks.");
}

// ---------------------------------------------------------------------------------
// Save data

void WritersSave(std::map<std::string, std::string>& out)
{
    out["next"] = std::to_string(g_nextScript);
    for (int i = 0; i < kDeskCount; i++) {
        const Desk& d = g_desks[i];
        std::string w;
        for (const std::string& name : d.writers)
            w += (w.empty() ? "" : "|") + name;
        out[Format("desk%d", i)] = Format("%.4f|%u|%s", d.progress, d.seed, d.title.c_str());
        out[Format("desk%d.writers", i)] = w;
        out[Format("desk%d.native", i)] = d.native ? "1" : "0";
    }
    out["writing"] = std::to_string(g_writing.size());
    for (size_t i = 0; i < g_writing.size(); i++)
        out[Format("w%zu", i)] = Format("%d|%d|%s", g_writing[i].desk, g_writing[i].seen ? 1 : 0, g_writing[i].title.c_str());
    out["shelf"] = std::to_string(g_shelf.size());
    for (size_t i = 0; i < g_shelf.size(); i++) {
        const TvScript& s = g_shelf[i];
        out[Format("s%zu", i)] = Format("%d|%d|%.4f|%.3f|%s", s.id, s.format, s.quality, s.written, s.title.c_str());
        out[Format("s%zu.writers", i)] = s.writers;
        if (s.pilot != PilotNone) {
            out[Format("s%zu.pilot", i)] = Format("%d|%.4f|%.3f|%.3f|%.4f", s.pilot, s.leadStars, s.shootStart,
                                                  s.shootEnd, s.pilotQuality);
            out[Format("s%zu.lead", i)] = s.lead;
            out[Format("s%zu.director", i)] = s.director;
        }
        if (s.animated)
            out[Format("s%zu.anim", i)] = Format("%.3f|%.3f|%.3f", s.drawn, s.painted, s.voiced);
    }
    std::string xp;
    for (const auto& e : g_experience)
        xp += Format(xp.empty() ? "%s=%d" : "|%s=%d", e.first.c_str(), e.second);
    out["experience"] = xp;
}

void WritersReset()
{
    for (Desk& d : g_desks)
        d = Desk{};
    g_shelf.clear();
    g_writing.clear();
    for (uintptr_t& r : g_deskRoom)
        r = 0;
    g_experience.clear();
    g_nextScript = 1;
    g_relinked = false;
}

void WritersLoad(const std::map<std::string, std::string>& in)
{
    WritersReset();
    auto get = [&](const std::string& k) {
        auto it = in.find(k);
        return it == in.end() ? std::string() : it->second;
    };
    auto split = [](const std::string& s) {
        std::vector<std::string> parts;
        for (size_t pos = 0; pos <= s.size();) {
            size_t bar = s.find('|', pos);
            parts.push_back(s.substr(pos, bar == std::string::npos ? std::string::npos : bar - pos));
            if (bar == std::string::npos)
                break;
            pos = bar + 1;
        }
        return parts;
    };
    g_nextScript = std::max(1, atoi(get("next").c_str()));
    for (int i = 0; i < kDeskCount; i++) {
        std::vector<std::string> p = split(get(Format("desk%d", i)));
        if (p.size() >= 3) {
            g_desks[i].progress = static_cast<float>(atof(p[0].c_str()));
            g_desks[i].seed = static_cast<uint32_t>(strtoul(p[1].c_str(), nullptr, 10));
            g_desks[i].title = p[2];
            for (size_t k = 3; k < p.size(); k++)
                g_desks[i].title += "|" + p[k];
        }
        std::string w = get(Format("desk%d.writers", i));
        if (!w.empty())
            g_desks[i].writers = split(w);
        g_desks[i].native = get(Format("desk%d.native", i)) == "1";
    }
    int writing = atoi(get("writing").c_str());
    for (int i = 0; i < writing; i++) {
        std::vector<std::string> p = split(get(Format("w%d", i)));
        if (p.size() < 3)
            continue;
        Writing w;
        w.desk = std::clamp(atoi(p[0].c_str()), 0, kDeskCount - 1);
        w.seen = p[1] == "1";
        w.title = p[2];
        for (size_t k = 3; k < p.size(); k++)
            w.title += "|" + p[k];
        g_writing.push_back(w);
    }
    int n = atoi(get("shelf").c_str());
    for (int i = 0; i < n; i++) {
        std::vector<std::string> p = split(get(Format("s%d", i)));
        if (p.size() < 5)
            continue;
        TvScript s;
        s.id = atoi(p[0].c_str());
        s.format = std::clamp(atoi(p[1].c_str()), 0, FormatCount - 1);
        s.quality = static_cast<float>(atof(p[2].c_str()));
        s.written = static_cast<float>(atof(p[3].c_str()));
        s.title = p[4];
        s.writers = get(Format("s%d.writers", i));
        std::vector<std::string> pp = split(get(Format("s%d.pilot", i)));
        if (pp.size() >= 5) {
            s.pilot = std::clamp(atoi(pp[0].c_str()), 0, 2);
            s.leadStars = static_cast<float>(atof(pp[1].c_str()));
            s.shootStart = static_cast<float>(atof(pp[2].c_str()));
            s.shootEnd = static_cast<float>(atof(pp[3].c_str()));
            s.pilotQuality = static_cast<float>(atof(pp[4].c_str()));
            s.lead = get(Format("s%d.lead", i));
            s.director = get(Format("s%d.director", i));
        }
        std::vector<std::string> pa = split(get(Format("s%d.anim", i)));
        if (pa.size() >= 3) {
            s.animated = true;
            s.drawn = static_cast<float>(atof(pa[0].c_str()));
            s.painted = static_cast<float>(atof(pa[1].c_str()));
            s.voiced = static_cast<float>(atof(pa[2].c_str()));
        }
        g_shelf.push_back(s);
    }
    for (const std::string& e : split(get("experience"))) {
        size_t eq = e.rfind('=');
        if (eq != std::string::npos && eq > 0)
            g_experience[e.substr(0, eq)] = atoi(e.substr(eq + 1).c_str());
    }
}

} // namespace tv
