// New professions: accountants (the Accounting Office) and animators (the Animation Studio).
//
// Each is a career of its own (CStaff +0x814; the game's are 0-16, careers.cpp adds 17 and 18,
// and the accountants' page on the HUD's people card). A building's hiring zone (room_tvk_hire,
// room_tva_hire) is a real CHireRoom for the career (built in rooms.cpp); the job title is
// SITT_DETAILS_CAREER_<NAME>_* ([text]) and data/tasks.ini [accountant]/[animator] dresses
// them. The career is saved with the person. Jobseekers and the studio's own staff can be
// hired there, exactly like the Employment Office's builder and janitor desks, and fired the
// usual way. In their work zones they sit at the desks and type (the scenes: rooms.cpp).
//
// The gate's jobseekers don't know the jobs exist, so every so often one walks over and waits
// in front of each building (its wait zone); the player hires them by dropping them on the
// hiring desk. People who stand about outside for a while go back to their desks.
//
// What they do: accountants the share paperwork (with none the Accounting Office can't trade
// the studio's shares; each one also cuts the fees, tv_shares.cpp). Animators draw and paint
// animated films (tv_animation.cpp).

#include <windows.h>
#include <cstdint>
#include <map>
#include <vector>

#include "MinHook.h"
#include "careers.h"
#include "game.h"
#include "mod.h"
#include "overlay.h"
#include "rooms.h"
#include "tv_internal.h"

namespace tv {
namespace {

constexpr uintptr_t kHire = 0x009246E0;       // bool __thiscall CHireRoom::Hire(TMCharacter*)
constexpr uintptr_t kAllStaff = 0x0104CFC0;   // InList<CStaff>: first node +8, sentinel +0x14
constexpr int kHireCareer = 0x258, kStaffCareer = 0x814, kStaffEmployer = 0x934, kPersonRoom = 0x198;
constexpr int kSeekerLink = 0x784;          // prev/next in the jobseekers' list (CStudio::Employ 0x0050A7D0 unlinks it)
constexpr DWORD kSeekerEveryMs = 30000;     // how often another jobseeker may walk to a building
constexpr DWORD kSeekerWalkMs = 60000;      // how long one sent has to get there
constexpr int kMaxStaff = 64;
constexpr DWORD kBackToWorkMs = 45000;   // outside that long (not on a break in a building): back to the desk

struct Profession {
    int         career;
    const char* work[2];    // zones they sit and work in; the first is where idle ones are sent
    const char* hire;       // the hiring desk
    const char* wait;       // out front, where a jobseeker stands and waits
    const char* building;   // "the Accounting Office"
    const char* noun;       // "accountant"
    const char* workPlace;  // "the Accounts Floor"
    const char* firstHired; // the first one's news
    const char* moreHired;  // "%d" of them
};

const Profession kProfessions[] = {
    { careers::kAccountant, { "room_tvk_books", nullptr }, "room_tvk_hire", "room_tvk_wait", "the Accounting Office",
      "accountant", "the Accounts Floor",
      "Your first accountant has joined. The Accounting Office can now trade the studio's shares, and each accountant "
      "cuts the fees.",
      "You have %d accountants. Each one cuts the fees on share deals." },
    { careers::kAnimator, { "room_tva_desks", "room_tva_ink" }, "room_tva_hire", "room_tva_wait", "the Animation Studio",
      "animator", "the animators' desks",
      "Your first animator has joined. Drop them on the Animation Studio's desks to draw, or in Ink & Paint to colour "
      "the drawings in.",
      "You have %d animators. More animators finish a cartoon sooner." },
};
constexpr int kProfessionCount = sizeof(kProfessions) / sizeof(kProfessions[0]);

// What the mod knows about each profession's people this session.
struct Staffing {
    int       count = 0;            // the player's, counted every few seconds
    DWORD     counted = 0;
    bool      checked = false;      // the career census logged for this game
    uintptr_t staff[kMaxStaff];     // the player's, at the last census
    int       staffCount = 0;
    std::map<uintptr_t, DWORD> outside;   // person -> when first seen outside, idle
    uintptr_t waiting = 0;          // the jobseeker last sent to wait out front
    bool      arrivalLogged = false;
    DWORD     sentAt = 0;
    DWORD     lastSeeker = 0;
    bool      seekerNoticed = false;
    bool      noSpotLogged = false;
};
Staffing g_staffing[kProfessionCount];

using Hire_t = bool(__fastcall*)(uintptr_t room, void* edx, uintptr_t who);
Hire_t g_origHire = nullptr;
DWORD g_lastTick = 0;

template <typename T> T Rd(uintptr_t a) { return *reinterpret_cast<const T*>(a); }

int IndexOfCareer(int career)
{
    for (int i = 0; i < kProfessionCount; i++)
        if (kProfessions[i].career == career)
            return i;
    return -1;
}

// The player's people in a career, and (for the log) anyone else in it.
bool CensusOf(int p, uintptr_t studio, int* ours, int* others)
{
    Staffing& s = g_staffing[p];
    int career = kProfessions[p].career;
    *ours = *others = 0;
    s.staffCount = 0;
    __try {
        uintptr_t sentinel = kAllStaff + 0x14;
        uintptr_t node = Rd<uintptr_t>(kAllStaff + 8);
        for (int i = 0; node && node != sentinel && i < 4000; i++, node = Rd<uintptr_t>(node + 4)) {
            uintptr_t person = Rd<uintptr_t>(node + 8);
            if (!person || Rd<int>(person + kStaffCareer) != career)
                continue;
            if (studio && Rd<uintptr_t>(person + kStaffEmployer) == studio) {
                if (s.staffCount < kMaxStaff)
                    s.staff[s.staffCount++] = person;
                ++*ours;
            }
            else
                ++*others;
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool Census(int p, int* ours, int* others) { return CensusOf(p, game::ReadPlayerStudio().address, ours, others); }

// The player's people in profession p, recounted every few seconds.
int Count(int p)
{
    Staffing& s = g_staffing[p];
    if (GetTickCount() - s.counted > 3000) {
        s.counted = GetTickCount();
        int ours = 0, others = 0;
        if (game::Ready() && Census(p, &ours, &others)) {
            s.count = ours;
            if (!s.checked) {
                s.checked = true;
                mod::Log("tv: %ss - %d yours, %d other studios'", kProfessions[p].noun, ours, others);
            }
        }
    }
    return s.count;
}

bool IsStaffOf(uintptr_t studio, uintptr_t person, int career)
{
    __try {
        return Rd<int>(person + kStaffCareer) == career && studio && Rd<uintptr_t>(person + kStaffEmployer) == studio;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

uintptr_t RoomOf(uintptr_t person)
{
    __try {
        return Rd<uintptr_t>(person + kPersonRoom);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 1;   // unreadable: leave them be
    }
}

// Someone looking for work: no career and no employer (the lot's jobseekers; +0x784 turned
// out not to mark them). Only staff jobseekers (CStaff itself): the would-be actors
// (CWannabe) are unemployed too, but they come with acting experience.
bool IsUnemployed(uintptr_t person)
{
    __try {
        return Rd<int>(person + kStaffCareer) == 0 && Rd<uintptr_t>(person + kStaffEmployer) == 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool IsJobseeker(uintptr_t person) { return IsUnemployed(person) && game::ClassName(person) == "CStaff"; }

// A jobseeker not already waiting in front of another building.
uintptr_t FindJobseeker()
{
    __try {
        uintptr_t sentinel = kAllStaff + 0x14;
        uintptr_t node = Rd<uintptr_t>(kAllStaff + 8);
        for (int i = 0; node && node != sentinel && i < 4000; i++, node = Rd<uintptr_t>(node + 4)) {
            uintptr_t person = Rd<uintptr_t>(node + 8);
            if (!person || !IsJobseeker(person))
                continue;
            bool taken = false;
            for (const Staffing& s : g_staffing)
                taken |= s.waiting == person;
            if (!taken)
                return person;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return 0;
}

// The unemployed: their rooms (up to max) and how many are in the jobseekers' list.
int CollectUnemployed(uintptr_t* where, int max, int* listed)
{
    int idle = 0;
    *listed = 0;
    __try {
        uintptr_t sentinel = kAllStaff + 0x14;
        uintptr_t node = Rd<uintptr_t>(kAllStaff + 8);
        for (int i = 0; node && node != sentinel && i < 4000; i++, node = Rd<uintptr_t>(node + 4)) {
            uintptr_t s = Rd<uintptr_t>(node + 8);
            if (!s || Rd<int>(s + kStaffCareer) != 0 || Rd<uintptr_t>(s + kStaffEmployer))
                continue;
            if (Rd<uintptr_t>(s + kSeekerLink) || Rd<uintptr_t>(s + kSeekerLink + 4))
                ++*listed;
            if (idle < max)
                where[idle] = Rd<uintptr_t>(s + kPersonRoom);
            ++idle;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return idle;
}

// For the log: who is out of work, and where.
void DescribeUnemployed()
{
    uintptr_t where[256];
    int listed = 0;
    int idle = CollectUnemployed(where, 256, &listed);
    std::map<std::string, int> places;
    for (int i = 0; i < idle && i < 256; i++)
        places[where[i] ? rooms::ZoneName(where[i]) : "(outside)"]++;
    std::string text;
    for (auto& w : places)
        text += Format(" %s x%d", w.first.c_str(), w.second);
    mod::Log("tv: no jobseeker for a hiring desk - %d unemployed, %d in the jobseekers' list; at:%s", idle, listed,
             text.c_str());
}

// One jobseeker at a time waits in front of each building.
void JobseekerToDesk(int p)
{
    const Profession& prof = kProfessions[p];
    Staffing& st = g_staffing[p];
    DWORD now = GetTickCount();
    if (now - st.lastSeeker < kSeekerEveryMs)
        return;
    st.lastSeeker = now;
    uintptr_t spot = rooms::FindWorkRoom(prof.wait);
    if (!spot) {
        if (!st.noSpotLogged && Count(p) == 0 && game::Ready()) {
            st.noSpotLogged = true;
            mod::Log("tv: jobseekers - no waiting spot at %s found yet", prof.building);
        }
        return;
    }
    if (st.waiting && IsJobseeker(st.waiting)) {
        if (RoomOf(st.waiting) == spot) {
            if (!st.arrivalLogged) {
                st.arrivalLogged = true;
                mod::Log("tv: jobseeker %08X is waiting in front of %s", static_cast<unsigned>(st.waiting), prof.building);
            }
            return;   // still waiting there
        }
        if (now - st.sentAt < kSeekerWalkMs)
            return;   // on the way
        mod::Log("tv: jobseeker %08X didn't get to %s in %u s (now in %s)", static_cast<unsigned>(st.waiting),
                 prof.building, static_cast<unsigned>(kSeekerWalkMs / 1000),
                 RoomOf(st.waiting) ? rooms::ZoneName(RoomOf(st.waiting)).c_str() : "no room");
    }
    st.waiting = 0;
    uintptr_t seeker = FindJobseeker();
    static int noSeekerLogged = 0;
    if (!seeker) {
        if (noSeekerLogged++ < 3)
            DescribeUnemployed();
        return;
    }
    uintptr_t from = RoomOf(seeker);
    bool sent = rooms::SendToRoom(spot, seeker);
    st.waiting = sent ? seeker : 0;
    st.sentAt = now;
    st.arrivalLogged = false;
    static int logged = 0;
    if (logged++ < 20)
        mod::Log("tv: jobseeker %08X (%s, in %s) %s %s", static_cast<unsigned>(seeker), game::ClassName(seeker).c_str(),
                 from ? rooms::ZoneName(from).c_str() : "no room",
                 sent ? "sent to wait in front of" : "FAILED to send to wait in front of", prof.building);
    if (sent && !st.seekerNoticed) {
        st.seekerNoticed = true;
        Notify("Looking for work",
               Format("A jobseeker is on the way to wait in front of %s. Drop them on its hiring desk to hire them as "
                      "an %s.",
                      prof.building, prof.noun),
               kYellow);
    }
}

// People with nothing to do go back to their desks. Someone in any building (the canteen,
// the toilets...) is left alone; only one standing about outside for a while is sent.
void BackToWork(int p)
{
    const Profession& prof = kProfessions[p];
    Staffing& st = g_staffing[p];
    Count(p);   // refreshes st.staff
    uintptr_t desk = 0;
    std::map<uintptr_t, DWORD> seen;
    for (int i = 0; i < st.staffCount; i++) {
        uintptr_t person = st.staff[i];
        if (RoomOf(person))
            continue;
        auto it = st.outside.find(person);
        DWORD since = it == st.outside.end() ? GetTickCount() : it->second;
        if (GetTickCount() - since < kBackToWorkMs) {
            seen[person] = since;
            continue;
        }
        if (!desk)
            desk = rooms::FindWorkRoom(prof.work[0]);
        if (!desk)
            break;   // no building: nowhere to go
        bool sent = rooms::SendToRoom(desk, person);
        static int logged = 0;
        if (logged++ < 10)
            mod::Log("tv: %s %08X %s back to %s", prof.noun, static_cast<unsigned>(person),
                     sent ? "sent" : "FAILED to send", prof.workPlace);
    }
    st.outside.swap(seen);
    JobseekerToDesk(p);
}

void Frame()
{
    if (!game::Ready() || GetTickCount() - g_lastTick < 2000)
        return;
    g_lastTick = GetTickCount();
    for (int p = 0; p < kProfessionCount; p++)
        BackToWork(p);
}

int HireCareer(uintptr_t room)
{
    __try {
        return Rd<int>(room + kHireCareer);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

void SetHireCareer(uintptr_t room, int career)
{
    __try {
        *reinterpret_cast<int*>(room + kHireCareer) = career;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

bool __fastcall HookHire(uintptr_t room, void* edx, uintptr_t who)
{
    // Each desk hires its own profession whatever a save restored it with.
    std::string zone = rooms::ZoneName(room);
    int desk = -1;
    for (int p = 0; p < kProfessionCount; p++)
        if (zone == kProfessions[p].hire)
            desk = p;
    if (desk >= 0) {
        rooms::RememberRoom(room);
        if (HireCareer(room) != kProfessions[desk].career) {
            mod::Log("tv: the %ss' hiring desk had career %d - set back", kProfessions[desk].noun, HireCareer(room));
            SetHireCareer(room, kProfessions[desk].career);
        }
    }
    bool hired = g_origHire(room, edx, who);
    int p = hired ? IndexOfCareer(HireCareer(room)) : -1;
    if (p >= 0) {
        g_staffing[p].counted = 0;   // recount now
        int ours = 0, others = 0;
        Census(p, &ours, &others);
        g_staffing[p].count = ours;
        mod::Log("tv: hired an %s (%d now)", kProfessions[p].noun, ours);
        Notify(Format("A new %s", kProfessions[p].noun),
               ours == 1 ? std::string(kProfessions[p].firstHired) : Format(kProfessions[p].moreHired, ours), kGreen);
    }
    return hired;
}

} // namespace

bool StaffDrop(const std::string& zone, uintptr_t room, uintptr_t person)
{
    for (const Profession& prof : kProfessions) {
        if (zone != (prof.work[0] ? prof.work[0] : "") && zone != (prof.work[1] ? prof.work[1] : ""))
            continue;
        if (!IsStaffOf(game::ReadPlayerStudio().address, person, prof.career)) {
            Notify("Staff only",
                   Format("Only %ss work here. Hire one first: drop a jobseeker on the hiring desk at %s.", prof.noun,
                          prof.building),
                   kYellow);
            return true;
        }
        bool seated = rooms::SeatInScene(room, person);
        mod::Log("tv: %s %08X %s in %s", prof.noun, static_cast<unsigned>(person), seated ? "seated" : "FAILED to seat",
                 zone.c_str());
        return true;
    }
    return false;
}

int Accountants() { return Count(IndexOfCareer(careers::kAccountant)); }
int Animators() { return Count(IndexOfCareer(careers::kAnimator)); }

int StaffWorkingIn(int career, const char* zone)
{
    int p = IndexOfCareer(career);
    if (p < 0)
        return 0;
    Count(p);
    int n = 0;
    for (int i = 0; i < g_staffing[p].staffCount; i++) {
        uintptr_t room = RoomOf(g_staffing[p].staff[i]);
        n += room > 1 && rooms::ZoneName(room) == zone;
    }
    return n;
}

void StaffReset()
{
    for (Staffing& s : g_staffing) {
        s.checked = false;
        s.counted = 0;
        s.count = 0;
        s.staffCount = 0;
        s.outside.clear();
        s.waiting = 0;
        s.seekerNoticed = false;
        s.noSpotLogged = false;
    }
}

void InstallStaff()
{
    overlay::AddFrameCallback(&Frame);
    void* target = reinterpret_cast<void*>(kHire);
    if (MH_CreateHook(target, reinterpret_cast<void*>(&HookHire), reinterpret_cast<void**>(&g_origHire)) == MH_OK &&
        MH_EnableHook(target) == MH_OK)
        mod::Log("tv: staff - hooked hiring");
    else
        mod::Log("tv: staff - FAILED to hook hiring (they can still be hired, but aren't announced)");
}

} // namespace tv
