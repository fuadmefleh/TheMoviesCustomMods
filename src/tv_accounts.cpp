// Accountants: a new kind of employee, hired at the Accounting Office.
//
// A career of their own, 17 (CStaff +0x814; the game's are 0-16, careers.cpp adds 17 and their
// page on the HUD's people card). The Accounting Office's hiring zone (room_tvk_hire) is a real
// CHireRoom for career 17 (built in rooms.cpp); the job title is SITT_DETAILS_CAREER_ACCOUNTANT_*
// ([text]) and data/tasks.ini [accountant] dresses them from costume_accountant. The career is
// saved with the person. Jobseekers and the studio's own staff can be hired there, exactly
// like the Employment Office's builder and janitor desks, and fired the usual way. On the
// Accounts Floor they sit at the desks and type (its scene: rooms.cpp).
//
// The gate's jobseekers don't know the job exists, so every so often one walks over and
// waits in front of the Accounting Office (room_tvk_wait); the player hires them by dropping
// them on the hiring desk.
//
// What they do: the share paperwork. With no accountant the Accounting Office can't trade the
// studio's shares; each one also cuts the fees on a sale or a buy-back (tv_shares.cpp).

#include <windows.h>
#include <cstdint>
#include <map>

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
constexpr DWORD kSeekerEveryMs = 30000;     // how often another jobseeker may walk to the desk
constexpr DWORD kSeekerWalkMs = 60000;      // how long one sent has to get there
constexpr int kMaxAccountants = 64;
constexpr DWORD kBackToWorkMs = 45000;   // outside that long (not on a break in a building): back to the desk
const char* const kBooks = "room_tvk_books";
const char* const kHireDesk = "room_tvk_hire";
const char* const kWaitSpot = "room_tvk_wait";   // out front, where a jobseeker stands and waits
constexpr int kAccountantCareer = careers::kAccountant;

using Hire_t = bool(__fastcall*)(uintptr_t room, void* edx, uintptr_t who);
Hire_t g_origHire = nullptr;
int g_count = 0;            // the player's accountants, counted every few seconds
DWORD g_counted = 0;
bool g_checked = false;     // the career census logged for this game
uintptr_t g_staff[kMaxAccountants];   // the player's accountants at the last census
int g_staffCount = 0;
std::map<uintptr_t, DWORD> g_outside;   // accountant -> when they were first seen outside, idle
DWORD g_lastTick = 0;
DWORD g_lastSeeker = 0;
uintptr_t g_waiting = 0;    // the jobseeker last sent to wait out front
bool g_arrivalLogged = false;
DWORD g_sentAt = 0;
bool g_seekerNoticed = false;

template <typename T> T Rd(uintptr_t a) { return *reinterpret_cast<const T*>(a); }

// The player's accountants, and (for the log) anyone else in the career.
bool CensusOf(uintptr_t studio, int* ours, int* others)
{
    *ours = *others = 0;
    g_staffCount = 0;
    __try {
        uintptr_t sentinel = kAllStaff + 0x14;
        uintptr_t node = Rd<uintptr_t>(kAllStaff + 8);
        for (int i = 0; node && node != sentinel && i < 4000; i++, node = Rd<uintptr_t>(node + 4)) {
            uintptr_t s = Rd<uintptr_t>(node + 8);
            if (!s || Rd<int>(s + kStaffCareer) != kAccountantCareer)
                continue;
            if (studio && Rd<uintptr_t>(s + kStaffEmployer) == studio) {
                if (g_staffCount < kMaxAccountants)
                    g_staff[g_staffCount++] = s;
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

bool Census(int* ours, int* others) { return CensusOf(game::ReadPlayerStudio().address, ours, others); }

bool IsAccountantOf(uintptr_t studio, uintptr_t person)
{
    __try {
        return Rd<int>(person + kStaffCareer) == kAccountantCareer &&
               studio && Rd<uintptr_t>(person + kStaffEmployer) == studio;
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

void JobseekerToDesk();

// Accountants with nothing to do go back to their desks. Someone in any building (the
// canteen, the toilets...) is left alone; only one standing about outside for a while is sent.
void BackToWork()
{
    if (!game::Ready() || GetTickCount() - g_lastTick < 2000)
        return;
    g_lastTick = GetTickCount();
    Accountants();   // refreshes g_staff
    uintptr_t books = 0;
    std::map<uintptr_t, DWORD> seen;
    for (int i = 0; i < g_staffCount; i++) {
        uintptr_t s = g_staff[i];
        if (RoomOf(s)) {
            continue;
        }
        auto it = g_outside.find(s);
        DWORD since = it == g_outside.end() ? GetTickCount() : it->second;
        if (GetTickCount() - since < kBackToWorkMs) {
            seen[s] = since;
            continue;
        }
        if (!books)
            books = rooms::FindWorkRoom(kBooks);
        if (!books)
            break;   // no Accounting Office: nowhere to go
        bool sent = rooms::SendToRoom(books, s);
        static int logged = 0;
        if (logged++ < 10)
            mod::Log("tv: accountant %08X %s back to the Accounts Floor", static_cast<unsigned>(s), sent ? "sent" : "FAILED to send");
    }
    g_outside.swap(seen);
    JobseekerToDesk();
}

// Someone looking for work: no career and no employer (the lot's jobseekers; +0x784 turned
// out not to mark them).
bool IsJobseeker(uintptr_t person)
{
    __try {
        return Rd<int>(person + kStaffCareer) == 0 && Rd<uintptr_t>(person + kStaffEmployer) == 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

uintptr_t FindJobseeker()
{
    __try {
        uintptr_t sentinel = kAllStaff + 0x14;
        uintptr_t node = Rd<uintptr_t>(kAllStaff + 8);
        for (int i = 0; node && node != sentinel && i < 4000; i++, node = Rd<uintptr_t>(node + 4)) {
            uintptr_t s = Rd<uintptr_t>(node + 8);
            if (s && IsJobseeker(s))
                return s;
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
    mod::Log("tv: no jobseeker for the hiring desk - %d unemployed, %d in the jobseekers' list; at:%s", idle, listed,
             text.c_str());
}

// One jobseeker at a time waits in front of the Accounting Office.
void JobseekerToDesk()
{
    DWORD now = GetTickCount();
    if (now - g_lastSeeker < kSeekerEveryMs)
        return;
    g_lastSeeker = now;
    uintptr_t spot = rooms::FindWorkRoom(kWaitSpot);
    static bool noSpotLogged = false;
    if (!spot) {
        if (!noSpotLogged && Accountants() == 0 && game::Ready()) {
            noSpotLogged = true;
            mod::Log("tv: jobseekers - no Accounting Office waiting spot found yet");
        }
        return;
    }
    if (g_waiting && IsJobseeker(g_waiting)) {
        if (RoomOf(g_waiting) == spot) {
            if (!g_arrivalLogged) {
                g_arrivalLogged = true;
                mod::Log("tv: jobseeker %08X is waiting in front of the Accounting Office", static_cast<unsigned>(g_waiting));
            }
            return;   // still waiting there
        }
        if (now - g_sentAt < kSeekerWalkMs)
            return;   // on the way
        mod::Log("tv: jobseeker %08X didn't get there in %u s (now in %s)", static_cast<unsigned>(g_waiting),
                 static_cast<unsigned>(kSeekerWalkMs / 1000),
                 RoomOf(g_waiting) ? rooms::ZoneName(RoomOf(g_waiting)).c_str() : "no room");
    }
    uintptr_t seeker = FindJobseeker();
    static int noSeekerLogged = 0;
    if (!seeker) {
        if (noSeekerLogged++ < 3)
            DescribeUnemployed();
        return;
    }
    uintptr_t from = RoomOf(seeker);
    bool sent = rooms::SendToRoom(spot, seeker);
    g_waiting = sent ? seeker : 0;
    g_sentAt = now;
    g_arrivalLogged = false;
    static int logged = 0;
    if (logged++ < 20)
        mod::Log("tv: jobseeker %08X (%s, in %s) %s the Accounting Office", static_cast<unsigned>(seeker),
                 game::ClassName(seeker).c_str(), from ? rooms::ZoneName(from).c_str() : "no room",
                 sent ? "sent to wait in front of" : "FAILED to send to wait in front of");
    if (sent && !g_seekerNoticed) {
        g_seekerNoticed = true;
        Notify("Looking for work", "A jobseeker is on the way to wait in front of the Accounting Office. Drop them on its "
                                   "hiring desk to hire them as an accountant.",
               kYellow);
    }
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
    // The desk hires accountants whatever a save restored it with.
    bool desk = rooms::ZoneName(room) == kHireDesk;
    if (desk)
        rooms::RememberRoom(room);
    if (HireCareer(room) != kAccountantCareer && desk) {
        mod::Log("tv: the accountants' hiring desk had career %d - set back to accountants", HireCareer(room));
        SetHireCareer(room, kAccountantCareer);
    }
    bool hired = g_origHire(room, edx, who);
    if (hired && HireCareer(room) == kAccountantCareer) {
        g_counted = 0;   // recount now
        int ours = 0, others = 0;
        Census(&ours, &others);
        g_count = ours;
        mod::Log("tv: hired an accountant (%d now)", ours);
        Notify("A new accountant", ours == 1 ? "Your first accountant has joined. The Accounting Office can now trade the "
                                               "studio's shares, and each accountant cuts the fees."
                                             : Format("You have %d accountants. Each one cuts the fees on share deals.", ours),
               kGreen);
    }
    return hired;
}

} // namespace

bool AccountsDrop(const std::string& zone, uintptr_t room, uintptr_t person)
{
    if (zone != kBooks)
        return false;
    if (!IsAccountantOf(game::ReadPlayerStudio().address, person)) {
        Notify("The Accounts Floor", "Only accountants work here. Hire one first: drop a jobseeker on the Accounting "
                                     "Office's hiring desk.",
               kYellow);
        return true;
    }
    bool seated = rooms::SeatInScene(room, person);
    mod::Log("tv: accountant %08X %s at a desk", static_cast<unsigned>(person), seated ? "seated" : "FAILED to seat");
    return true;
}

int Accountants()
{
    if (GetTickCount() - g_counted > 3000) {
        g_counted = GetTickCount();
        int ours = 0, others = 0;
        if (game::Ready() && Census(&ours, &others)) {
            g_count = ours;
            if (!g_checked) {
                                g_checked = true;
                mod::Log("tv: accountants - %d yours, %d other studios'", ours, others);
            }
        }
    }
    return g_count;
}

void AccountsReset()
{
    g_checked = false;
    g_counted = 0;
    g_count = 0;
    g_staffCount = 0;
    g_outside.clear();
    g_waiting = 0;
    g_seekerNoticed = false;
}

void InstallAccounts()
{
    overlay::AddFrameCallback(&BackToWork);
    void* target = reinterpret_cast<void*>(kHire);
    if (MH_CreateHook(target, reinterpret_cast<void*>(&HookHire), reinterpret_cast<void**>(&g_origHire)) == MH_OK &&
        MH_EnableHook(target) == MH_OK)
        mod::Log("tv: accountants - hooked hiring");
    else
        mod::Log("tv: accountants - FAILED to hook hiring (they can still be hired, but aren't announced)");
}

} // namespace tv
