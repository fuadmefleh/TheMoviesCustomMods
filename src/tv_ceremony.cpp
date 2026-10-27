// The Golden Antenna Awards as a ceremony: the game's own 3D awards show (the two presenters,
// the envelope, the studios' results board) held for the TV awards each May.
//
// The game's ceremony plays every award (CAward) dated with the ceremony's date. So the mod
// makes one CAward per TV category, dated today, with type ids past the game's 70 (0x46 is the
// award iterator's "any"), no winner object (so winning changes nothing in the game) and
// category 2 (not a ceremony or lifetime award, which the studio rankings count), then opens
// the ceremony screen for today. Hooks give the new types a name for the dialogue keys, a
// title, an icon and a place in the programme, and add them to the ceremony's list. The
// presenters' lines come from [text] keys set while the show runs; their voices are kept
// only where the recording fits ("And the winner is...", the studio names, the goodbye).
// When the screen closes the awards are deleted again. See re/awards.md.
//
// Pacing: a silent line would be over as soon as it's shown (the states wait for the voice),
// so the "voice playing" check says yes for as long as the line takes to read.
//
// The Golden Antennas count toward the studio rankings like the movie awards: the studio's
// awards factor (CStudio vslot +0x98, 0.169 a win from the last movie ceremony, at most 1)
// gets the same for each Golden Antenna its shows won at the latest TV awards.

#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <cstring>

#include "MinHook.h"
#include "game.h"
#include "mod.h"
#include "overlay.h"
#include "text.h"
#include "tv_internal.h"

namespace tv {
namespace {

template <typename T> T Rd(uintptr_t a) { return *reinterpret_cast<const T*>(a); }

constexpr int kFirstType = 0x47;      // the game's award types are 0-0x45; 0x46 means "any"
constexpr int kMaxCategories = 16;
constexpr int kTvCategory = 2;        // CAward +0x68: 0 ceremony, 1 lifetime

constexpr uintptr_t kTypeName      = 0x00860970;   // const String* __cdecl(int type): the <NAME> in text keys
constexpr uintptr_t kTypeTitle     = 0x00861DE0;   // WString* __cdecl(WString* out, int type)
constexpr uintptr_t kTypeIcon      = 0x00861FB0;   // const String* __cdecl(int type); NULL for unknown types
constexpr uintptr_t kTypeAvailable = 0x0085CEA0;   // int __cdecl(int type, float when)
constexpr uintptr_t kCollect       = 0x007D9270;   // void __fastcall(WStandardAwards*): the awards to present
constexpr uintptr_t kPushAward     = 0x007D7A00;   // void __thiscall(vector*, CAward** where, int n, CAward* const*)
constexpr uintptr_t kVoiceMale     = 0x007C9F40;   // void __stdcall(record*): plays [record+0] (a key)
constexpr uintptr_t kVoiceFemale   = 0x007C9F80;
constexpr uintptr_t kVoiceHandle   = 0x00E5AFF4;   // the line playing; -1 = none (the states wait for it)
constexpr uintptr_t kVoicePlaying  = 0x007C9690;   // int __cdecl(): the presenter is still talking
constexpr uintptr_t kTannoyManager = 0x0104B0E4;   // the lot's public address
constexpr uintptr_t kTannoyPlay    = 0x004F8A00;   // void __thiscall(mgr, int 2, const String* key, int priority, int 0)
constexpr uintptr_t kNewAward      = 0x00858930;   // CAward* __cdecl(int type, const float* when, int category)
constexpr uintptr_t kSetWinner     = 0x00857110;   // void __thiscall(CAward*, const WString* name, obj, float score, CStudio*)
constexpr uintptr_t kOpenCeremony  = 0x007C8760;   // void __cdecl(const CeremonyInfo*)
constexpr uintptr_t kCeremonyOpen  = 0x007C88F0;   // bool __cdecl(): one is pending or showing
constexpr uintptr_t kUiFree        = 0x0053D1D0;   // bool __cdecl(): no modal dialog
constexpr uintptr_t kMovieAwardsDate = 0x0104F0E8; // float: the movie ceremony's date
constexpr uintptr_t kAwardsFactor = 0x0050D8E0;   // float* __thiscall(CStudio*, float* out)
constexpr uintptr_t kStudioRating = 0x0050AB70;   // void __fastcall(CStudio*): recomputes its rating
constexpr float kPerAward = 0.16949153f;          // the game's own weight for a movie award
constexpr int kAwardWinners = 0x8C, kWinnerSize = 0x34, kWinnerStudio = 0x18, kWinnerScore = 0x30;
constexpr int kAwardRunnersUp = 0x128, kCollected = 0x408;

struct GameString {   // inline buffer up to 19 characters
    char* ptr;
    int   len;
    int   cap;
    char  buf[0x14];
};
struct GameWString {
    wchar_t* text;
    int      len;
    int      cap;
    wchar_t  inline_[10];
};
struct WideRef {      // all SetWinner reads of a WString
    const wchar_t* text;
    int            len;
};
struct CeremonyInfo {
    float   when;
    uint8_t ceremony, rankUp, stuntRankUp, pad;
    int     ranks[4];
};

using TypeName_t = const GameString*(__cdecl*)(int);
using TypeTitle_t = GameWString*(__cdecl*)(GameWString*, int);
using TypeIcon_t = const GameString*(__cdecl*)(int);
using TypeAvailable_t = int(__cdecl*)(int, float);
using Collect_t = void(__fastcall*)(uintptr_t);
using Voice_t = void(__fastcall*)(void*, void*, uintptr_t);
using AwardsFactor_t = float*(__fastcall*)(uintptr_t, void*, float*);
using VoicePlaying_t = int(__cdecl*)();
VoicePlaying_t g_origPlaying = nullptr;
DWORD g_holdUntil = 0;   // a silent line is on screen until then
DWORD g_openAt = 0;      // a live show opens after the tannoy has had its say
AwardsFactor_t g_origFactor = nullptr;
TypeName_t g_origName = nullptr;
TypeTitle_t g_origTitle = nullptr;
TypeIcon_t g_origIcon = nullptr;
TypeAvailable_t g_origAvailable = nullptr;
Collect_t g_origCollect = nullptr;
Voice_t g_origMale = nullptr, g_origFemale = nullptr;

bool g_enabled = true;
GameString g_names[kMaxCategories];   // TVAWARD_0.. : the <NAME> of each category's keys
GameString g_icon;                    // ui\awards_tv.dds: the Golden Antenna trophy
std::vector<CeremonyCategory> g_show; // the one queued or on stage
std::wstring g_titles[kMaxCategories];
int g_edition = 0, g_year = 0;
void (*g_after)() = nullptr;          // when the curtain falls (the newspaper)
enum State { Idle, Waiting, OnStage } g_state = Idle;
float g_queuedOn = 0;
bool g_seenOpen = false;
std::vector<uintptr_t> g_awards;
std::vector<std::string> g_keys;      // text set for the show, unset afterwards

bool Ours(int type) { return type >= kFirstType && type < kFirstType + static_cast<int>(g_show.size()); }

std::wstring Wide(const std::string& s)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

void Say(const std::string& key, const std::string& text)
{
    text::Set(key, Wide(text));
    g_keys.push_back(key);
}

// --- Hooks ---------------------------------------------------------------------------

const GameString* __cdecl HookName(int type) { return Ours(type) ? &g_names[type - kFirstType] : g_origName(type); }

GameWString* __cdecl HookTitle(GameWString* out, int type)
{
    if (!Ours(type))
        return g_origTitle(out, type);
    // Built as the game builds its own: an empty inline string, then assigned.
    using Assign_t = void(__thiscall*)(GameWString*, const wchar_t*, int);
    const std::wstring& t = g_titles[type - kFirstType];
    out->text = out->inline_;
    out->inline_[0] = 0;
    out->len = 0;
    out->cap = 10;
    reinterpret_cast<Assign_t>(0x004036D0)(out, t.c_str(), static_cast<int>(t.size()));
    return out;
}

const GameString* __cdecl HookIcon(int type) { return Ours(type) ? &g_icon : g_origIcon(type); }

int __cdecl HookAvailable(int type, float when) { return Ours(type) ? 1 : g_origAvailable(type, when); }

void __fastcall HookCollect(uintptr_t screen)
{
    g_origCollect(screen);
    if (g_state != OnStage)
        return;
    __try {
        uintptr_t list = screen + kCollected;
        for (uintptr_t award : g_awards) {
            uintptr_t end = Rd<uintptr_t>(list + 8);
            reinterpret_cast<void(__thiscall*)(uintptr_t, uintptr_t, int, const uintptr_t*)>(kPushAward)(list, end, 1, &award);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        mod::Log("tv: ceremony - adding the Golden Antennas to the programme faulted");
    }
}

// The presenters' recordings name movie categories and the "Motion Picture Awards". During
// the TV show only these fit: "And the winner is..." (for the envelope), the studio names
// and the generic close; the rest are silent (their lines are still shown).
const char* const kWinnerIs[2] = { "AWARDS_DIALOGUE_WINNERIS_MOVIES_FIRSTPLACED_10",    // "And the winner is..."
                                   "AWARDS_DIALOGUE_WINNERIS_MOVIES_FIRSTPLACED_30" };  // "The award goes to..."

bool Starts(const char* s, const char* prefix) { return strncmp(s, prefix, strlen(prefix)) == 0; }

const char* KeyOf(uintptr_t record)
{
    __try {
        const char* k = record ? Rd<const char*>(record) : nullptr;
        return k ? k : "";
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return "";
    }
}

void Voice(Voice_t orig, void* self, void* edx, uintptr_t record)
{
    if (g_state != OnStage)
        return orig(self, edx, record);
    const char* key = KeyOf(record);
    g_holdUntil = 0;
    if (Starts(key, "AWARDS_DIALOGUE_WINNERIS_TVAWARD_")) {
        const char* recording = kWinnerIs[atoi(key + strlen("AWARDS_DIALOGUE_WINNERIS_TVAWARD_")) & 1];
        return orig(self, edx, reinterpret_cast<uintptr_t>(&recording));   // a record starts with its key
    }
    if (Starts(key, "AWARDS_DIALOGUE_WINNERIS_") || Starts(key, "PROFILESCREEN_DIALOGUE_DEFAULTSTUDIONAME") ||
        Starts(key, "AWARDS_DIALOGUE_CONCLUDE_1"))
        return orig(self, edx, record);
    *reinterpret_cast<int*>(kVoiceHandle) = -1;   // silent: held on screen for as long as it takes to read
    int chars = 0;
    __try {
        chars = record ? Rd<int>(record + 0x44) : 0;   // record +0x40: the line's text (WString)
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    g_holdUntil = GetTickCount() + static_cast<DWORD>(std::clamp(1500 + 60 * chars, 2500, 12000));
}

int __cdecl HookPlaying()
{
    if (g_state == OnStage && static_cast<int>(g_holdUntil - GetTickCount()) > 0)
        return 1;
    return g_origPlaying();
}

void __fastcall HookMale(void* self, void* edx, uintptr_t record) { Voice(g_origMale, self, edx, record); }
void __fastcall HookFemale(void* self, void* edx, uintptr_t record) { Voice(g_origFemale, self, edx, record); }

// --- The show --------------------------------------------------------------------------

// One of the game's public-address announcements on the lot (they're about awards in general).
void PlayTannoy(const char* key, int priority)
{
    GameString s{ const_cast<char*>(key), static_cast<int>(strlen(key)), 0, {} };
    s.cap = std::max(s.len, 0x15);   // a long String: the caller owns the text, the game copies it
    __try {
        uintptr_t mgr = Rd<uintptr_t>(kTannoyManager);
        if (mgr)
            reinterpret_cast<void(__thiscall*)(uintptr_t, int, const GameString*, int, int)>(kTannoyPlay)(mgr, 2, &s, priority, 0);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        mod::Log("tv: ceremony - the tannoy faulted");
    }
}


std::string Ordinal(int n)
{
    const char* suffix = n % 100 / 10 == 1 ? "th" : n % 10 == 1 ? "st" : n % 10 == 2 ? "nd" : n % 10 == 3 ? "rd" : "th";
    return Format("%d%s", n, suffix);
}

std::string NomineeList(const CeremonyCategory& c)
{
    std::string out;
    for (size_t i = 0; i < c.nominees.size(); i++) {
        const CeremonyNominee& n = c.nominees[i];
        std::string one = n.player ? n.title + " from your studio" : Format("%s on %s", n.title.c_str(), NetworkShort(n.network));
        out += (i == 0 ? "" : i + 1 == c.nominees.size() ? " and " : ", ") + one;
    }
    return out;
}

void WriteScript()
{
    std::string show = Format("the %s Golden Antenna Awards", Ordinal(g_edition).c_str());
    std::string welcome = Format("Ladies and gentlemen, welcome to %s, television's biggest night!", show.c_str());
    Say("AWARDS_DIALOGUE_WELCOME_FIRST", welcome);
    Say("AWARDS_DIALOGUE_WELCOME_AFTER_2010", welcome);
    for (int y = 1920; y <= 2010; y += 5)
        Say(Format("AWARDS_DIALOGUE_WELCOME_%d", y), welcome);
    std::string reply = "Tonight we honour the finest shows on the air - the ones the critics loved and the whole country "
                        "tuned in for. So let's go straight to our first category.";
    Say("AWARDS_DIALOGUE_WELCOME_REPLY_FIRST", reply);
    Say("AWARDS_DIALOGUE_WELCOME_REPLY_GENERIC", reply);
    Say("AWARDS_DIALOGUE_CONCLUDE_2", "Congratulations to all of tonight's winners. Thank you for watching, and good night "
                                      "from the Golden Antennas!");
    Say("AWARDSNEW_BONUS_", "A Golden Antenna");
    Say("AWARDSNEW_BONUS_DESC_", "The winning show stays fresher, earns its network's best fees and draws a bigger crowd "
                                 "to its next premiere.");
    for (size_t i = 0; i < g_show.size(); i++) {
        const CeremonyCategory& c = g_show[i];
        std::string name = Format("TVAWARD_%zu", i);
        Say("AWARDS_DIALOGUE_NOMINEES_" + name, Format("The nominees for %s are %s.", c.name.c_str(), NomineeList(c).c_str()));
        Say("AWARDS_DIALOGUE_EXPLAIN_" + name, c.blurb);
        Say("AWARDS_DIALOGUE_WINNERIS_" + name, i & 1 ? "The award goes to..." : "And the winner is...");
        Say("AWARDSNEW_" + name, c.name);
        Say("AWARDSNEW_" + name + "_HINT", c.blurb);
        Say("AWARDSNEW_" + name + "_TOOLTIP", c.name + " - " + c.blurb);
    }
}

void Unscript()
{
    for (const std::string& k : g_keys)
        text::Unset(k);
    g_keys.clear();
}

// Rival studios, in the game's order (refreshed every few seconds: the list rarely changes).
const std::vector<uintptr_t>& RivalStudios()
{
    static std::vector<uintptr_t> rivals;
    static DWORD at = 0;
    DWORD now = GetTickCount();
    if (rivals.empty() || now - at > 10000) {
        at = now;
        rivals.clear();
        for (const game::StudioFilms& s : game::AllStudios())
            if (!s.isPlayer && s.address)
                rivals.push_back(s.address);
    }
    return rivals;
}

// The studio a nominee belongs to: the player's, or for a rival network's show a rival studio
// standing in as its producer (always the same one for a network).
uintptr_t StudioFor(bool player, int network)
{
    const std::vector<uintptr_t>& rivals = RivalStudios();
    if (player || rivals.empty())
        return game::ReadPlayerStudio().address;
    return rivals[static_cast<size_t>(std::max(0, network)) % rivals.size()];
}

uintptr_t StudioFor(const CeremonyNominee& n) { return StudioFor(n.player, n.network); }

float* __fastcall HookAwardsFactor(uintptr_t studio, void* edx, float* out)
{
    float* r = g_origFactor(studio, edx, out);
    if (!out || !game::Ready())
        return r;
    int wins = 0;
    for (const CeremonyNominee& w : LatestAntennaWinners())
        wins += StudioFor(w) == studio;
    if (wins)
        *out = std::min(1.0f, *out + kPerAward * wins);
    return r;
}

uintptr_t NewAward(int type, float now)
{
    __try {
        return reinterpret_cast<uintptr_t(__cdecl*)(int, const float*, int)>(kNewAward)(type, &now, kTvCategory);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

bool SetWinner(uintptr_t award, const WideRef& name, float score, uintptr_t studio)
{
    __try {
        reinterpret_cast<void(__thiscall*)(uintptr_t, const WideRef*, uintptr_t, float, uintptr_t)>(kSetWinner)(
            award, &name, 0, score, studio);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Runner-up 'slot' (1 or 2): only the studio and score; its studio lights up on the board.
bool SetRunnerUp(uintptr_t award, int slot, float score, uintptr_t studio)
{
    __try {
        uintptr_t entry = award + kAwardWinners + slot * kWinnerSize;
        uintptr_t cp = entry + kWinnerStudio;
        uintptr_t vt = Rd<uintptr_t>(cp);
        reinterpret_cast<void(__thiscall*)(uintptr_t)>(Rd<uintptr_t>(vt + 4))(cp);   // unlink
        *reinterpret_cast<uintptr_t*>(cp + 0x14) = studio;
        reinterpret_cast<void(__thiscall*)(uintptr_t)>(Rd<uintptr_t>(vt))(cp);       // link
        *reinterpret_cast<float*>(entry + kWinnerScore) = score;
        *reinterpret_cast<int*>(award + kAwardRunnersUp) = slot;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool MakeAwards(float now)
{
    if (!game::ReadPlayerStudio().address)
        return false;
    for (size_t i = 0; i < g_show.size(); i++) {
        const CeremonyCategory& c = g_show[i];
        uintptr_t award = NewAward(kFirstType + static_cast<int>(i), now);
        if (!award)
            return false;
        g_awards.push_back(award);
        const CeremonyNominee& w = c.nominees[0];
        std::wstring name = Wide(w.title);
        if (!SetWinner(award, WideRef{ name.c_str(), static_cast<int>(name.size()) }, w.score, StudioFor(w)))
            return false;
        for (int slot = 1; slot <= 2 && slot < static_cast<int>(c.nominees.size()); slot++)
            if (!SetRunnerUp(award, slot, c.nominees[slot].score, StudioFor(c.nominees[slot])))
                return false;
    }
    return true;
}

void DeleteAwards()
{
    for (uintptr_t award : g_awards) {
        __try {
            reinterpret_cast<void(__thiscall*)(uintptr_t, int)>(Rd<uintptr_t>(Rd<uintptr_t>(award)))(award, 1);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    g_awards.clear();
}

bool CeremonyShowing()
{
    __try {
        return reinterpret_cast<bool(__cdecl*)()>(kCeremonyOpen)();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return true;
    }
}

bool UiFree()
{
    __try {
        return reinterpret_cast<bool(__cdecl*)()>(kUiFree)();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void Curtain()
{
    DeleteAwards();
    Unscript();
    g_state = Idle;
    mod::Log("tv: ceremony - the %s Golden Antenna Awards are over", Ordinal(g_edition).c_str());
    if (g_after) {
        auto after = g_after;
        g_after = nullptr;
        after();
    }
}

bool OpenScreen(float when)
{
    CeremonyInfo info{ when, 1, 0, 0, 0, { -1, -1, -1, -1 } };
    __try {
        reinterpret_cast<void(__cdecl*)(const CeremonyInfo*)>(kOpenCeremony)(&info);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void Open()
{
    float now = game::DateDays();
    // Never on the movie ceremony's own date: its awards would join the show.
    if (now <= 0 || now == Rd<float>(kMovieAwardsDate))
        return;
    if (!MakeAwards(now)) {
        DeleteAwards();
        g_state = Idle;
        mod::Log("tv: ceremony - couldn't make the awards, no ceremony this year");
        return;
    }
    WriteScript();
    if (overlay::IsOpen())
        overlay::Close();
    g_state = OnStage;
    g_seenOpen = false;
    if (!OpenScreen(now)) {
        mod::Log("tv: ceremony - opening the awards screen faulted");
        Curtain();
        return;
    }
    mod::Log("tv: ceremony - the %s Golden Antenna Awards begin (%zu categories)", Ordinal(g_edition).c_str(), g_show.size());
}

bool Hook(uintptr_t addr, void* detour, void** orig, const char* what)
{
    if (MH_CreateHook(reinterpret_cast<void*>(addr), detour, orig) == MH_OK && MH_EnableHook(reinterpret_cast<void*>(addr)) == MH_OK)
        return true;
    mod::Log("tv: ceremony - FAILED to hook %s", what);
    return false;
}

void RecomputeRating(uintptr_t studio)
{
    __try {
        reinterpret_cast<void(__fastcall*)(uintptr_t)>(kStudioRating)(studio);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

} // namespace

// After the awards: every studio's rating takes its Golden Antennas in now, not at its next update.
void RefreshStudioRatings()
{
    if (!g_origFactor || !game::Ready())
        return;
    std::string won;
    for (const game::StudioFilms& s : game::AllStudios()) {
        if (!s.address)
            continue;
        RecomputeRating(s.address);
        int wins = 0;
        for (const CeremonyNominee& w : LatestAntennaWinners())
            wins += StudioFor(w) == s.address;
        if (wins)
            won += Format("%s%ls %d", won.empty() ? "" : ", ", s.name.c_str(), wins);
    }
    mod::Log("tv: studio ratings take in the Golden Antennas (%s)", won.empty() ? "none won" : won.c_str());
}

bool CeremonyEnabled() { return g_enabled && g_origCollect; }

void AnnounceAwardsSoon()
{
    if (CeremonyEnabled())
        PlayTannoy("TANNOY_AWARDS_COMINGSOON", 2);
}

bool QueueCeremony(int edition, int year, std::vector<CeremonyCategory> categories, bool live, void (*after)())
{
    categories.erase(std::remove_if(categories.begin(), categories.end(),
                                    [](const CeremonyCategory& c) { return c.nominees.empty(); }),
                     categories.end());
    if (!CeremonyEnabled() || g_state != Idle || categories.empty())
        return false;
    if (categories.size() > kMaxCategories)
        categories.resize(kMaxCategories);
    g_show = std::move(categories);
    for (size_t i = 0; i < g_show.size(); i++)
        g_titles[i] = Wide(g_show[i].name);
    g_edition = edition;
    g_year = year;
    g_after = after;
    g_state = Waiting;
    g_queuedOn = Today();
    g_openAt = GetTickCount();
    if (live) {   // "Today's the big awards day..." first
        PlayTannoy("TANNOY_CEREMONY_ABOUTTOSTART", 1);
        g_openAt += 9000;
    }
    mod::Log("tv: ceremony - the %s Golden Antenna Awards are next (%zu categories)", Ordinal(edition).c_str(), g_show.size());
    return true;
}

void CeremonyFrame()
{
    if (g_state == Idle)
        return;
    if (!game::Ready()) {   // the game was left or another loaded
        if (g_state == OnStage)
            Curtain();
        g_state = Idle;
        return;
    }
    if (g_state == Waiting) {
        if (Today() - g_queuedOn > 30) {   // never a quiet moment: the papers have it anyway
            g_state = Idle;
            if (g_after)
                g_after();
            g_after = nullptr;
            mod::Log("tv: ceremony - no chance to hold it, skipped");
            return;
        }
        if (static_cast<int>(g_openAt - GetTickCount()) <= 0 && !CeremonyShowing() && UiFree())
            Open();
        return;
    }
    bool showing = CeremonyShowing();
    if (showing)
        g_seenOpen = true;
    else
        Curtain();
}

void InstallCeremony(bool enabled)
{
    g_enabled = enabled;
    for (int i = 0; i < kMaxCategories; i++) {
        char name[20];
        sprintf_s(name, "TVAWARD_%d", i);
        GameString& s = g_names[i];
        s.ptr = s.buf;
        s.cap = 0x14;
        s.len = static_cast<int>(strlen(name));
        memcpy(s.buf, name, s.len + 1);
    }
    const char* icon = "ui\\awards_tv.dds";
    g_icon.ptr = g_icon.buf;
    g_icon.cap = 0x14;
    g_icon.len = static_cast<int>(strlen(icon));
    memcpy(g_icon.buf, icon, g_icon.len + 1);
    bool ok = Hook(kTypeName, reinterpret_cast<void*>(&HookName), reinterpret_cast<void**>(&g_origName), "award names") &&
              Hook(kTypeTitle, reinterpret_cast<void*>(&HookTitle), reinterpret_cast<void**>(&g_origTitle), "award titles") &&
              Hook(kTypeIcon, reinterpret_cast<void*>(&HookIcon), reinterpret_cast<void**>(&g_origIcon), "award icons") &&
              Hook(kTypeAvailable, reinterpret_cast<void*>(&HookAvailable), reinterpret_cast<void**>(&g_origAvailable),
                   "award availability") &&
              Hook(kVoiceMale, reinterpret_cast<void*>(&HookMale), reinterpret_cast<void**>(&g_origMale), "presenter voice") &&
              Hook(kVoiceFemale, reinterpret_cast<void*>(&HookFemale), reinterpret_cast<void**>(&g_origFemale),
                   "presenter voice 2") &&
              Hook(kVoicePlaying, reinterpret_cast<void*>(&HookPlaying), reinterpret_cast<void**>(&g_origPlaying),
                   "presenter talking") &&
              Hook(kAwardsFactor, reinterpret_cast<void*>(&HookAwardsFactor), reinterpret_cast<void**>(&g_origFactor),
                   "studio awards factor") &&
              // Last: with this one in place the ceremony is on (CeremonyEnabled).
              Hook(kCollect, reinterpret_cast<void*>(&HookCollect), reinterpret_cast<void**>(&g_origCollect), "award programme");
    if (!ok)
        g_origCollect = nullptr;
    mod::Log("tv: Golden Antenna ceremony %s", !ok ? "NOT installed" : enabled ? "on" : "off ([tv] AwardsCeremony = 0)");
}

} // namespace tv
