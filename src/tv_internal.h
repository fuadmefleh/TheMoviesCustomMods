#pragma once
// Shared state and helpers of the Television & Streaming system. Only tv*.cpp include this.
//
//   tv.cpp          rules tables, series (pitch, verdict, seasons), daily simulation, save data
//   tv_network.cpp  networks, prime-time schedules, rival shows, audience model, your own network
//   tv_story.cpp    long-running shows: season plans, finales, star contracts, renewals, spinoffs
//   tv_ui.cpp       the Television window and the news ticker
//   tv_paper.cpp    the Season Finale newspaper
//   tv_library.cpp  the library: syndication, residuals, the Media Archive, your cable channel
//   tv_streaming.cpp your streaming service, rival streamers, originals
//   tv_events.cpp   the Golden Antenna Awards, TV history and scandals, Media Empire Value
//   tv_research.cpp research, challenges, live episodes
//   tv_writers.cpp  the TV Writers' Office: format desks, TV scripts, the script shelf
//   tv_staff.cpp    new professions: accountants and animators
//   tv_animation.cpp Toon Studio: the Animation Studio, cartoons drawn and painted by animators

#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <vector>

#include "game.h"
#include "imgui.h"

namespace tv {

// ---------------------------------------------------------------------------------
// Rules

constexpr float kDaysPerYear     = 365.0f;
constexpr int   kTelevisionYear  = 1938;    // regular broadcasts begin
constexpr int   kOwnNetworkYear  = 1946;    // independent networks can be founded
constexpr float kVerdictDelay    = 21.0f;   // days a network takes to screen a pilot
constexpr float kPremiereDelay   = 14.0f;   // days from order to the first airing
constexpr float kSummerHiatus    = 60.0f;   // days between a renewal and the next season
constexpr int   kLimitedOrder    = 8;
constexpr int   kShowsPerOffice  = 2;
constexpr int   kShowsPerStage   = 1;
constexpr float kMinFreshness    = 0.3f;    // how tired an audience can get of a show
constexpr float kAnimatedCost    = 0.7f;    // a TV cartoon's episode costs, against a live-action one's
constexpr float kAnimatedReruns  = 1.25f;   // ...and how much better it reruns: cartoons don't date

// Building file names (they must start with "facility" or the build menu ignores them).
extern const char* const kOfficeType;
extern const char* const kStageType;
extern const char* const kHqType;
extern const char* const kControlRoomType;
extern const char* const kArchiveType;
extern const char* const kDatacenterType;

enum FormatId { Sitcom, Procedural, Serialized, Soap, SciFi, TalkShow, GameShow, Reality, Documentary, Miniseries, FormatCount };

struct FormatInfo {
    const char* name;
    int         fromYear;     // when the format becomes available
    float       cost1950;     // per-episode production cost in 1950 dollars
    float       fee1950;      // per-episode license fee in 1950 dollars
    float       appeal1950;   // audience appeal then...
    float       appeal2000;   // ...and later (interpolated)
    bool        multiCamera;  // benefits from a Multi-Camera Soundstage
    int         bestHour;     // preferred prime-time hour (0 = 8 PM, 2 = 10 PM)
    const char* blurb;
};
extern const FormatInfo kFormats[FormatCount];

// Rival networks are 0..kRivalCount-1; the player's own network is kPlayerNetwork.
constexpr int kRivalCount    = 3;
constexpr int kPlayerNetwork = 3;
constexpr int kStreamingNetwork = 4;   // originals on your streaming service (no schedule)
constexpr int kNetworkSlots  = 4;

struct NetworkInfo {
    const char* name;
    const char* shortName;
    const char* identity;
    float       bar;          // pilot score needed for an order
    float       feeMul;       // how well they pay
    float       strength;     // audience pull of the brand
    float       showQuality;  // average quality of their own shows
    float       reachCap;     // share of TV homes they reach once fully grown
    float       taste[FormatCount];
};
extern const NetworkInfo kNetworks[kRivalCount];

enum Budget { BudgetLow, BudgetStandard, BudgetPremium, BudgetCount };
extern const char* const kBudgetNames[BudgetCount];
extern const float kBudgetCost[BudgetCount];
extern const float kBudgetQuality[BudgetCount];
extern const float kBudgetPitch[BudgetCount];

enum Deal { DealLicense, DealAdShare, DealNetworkOwned, DealCount };
extern const char* const kDealNames[DealCount];

enum Level { Casual, StudioBoss, MediaMogul, LevelCount };
extern const char* const kLevelNames[LevelCount];
extern const char* const kLevelHelp[LevelCount];

enum Status { Pitching, Rework, PassedOn, Ordered, OnAir, OnHold, RenewalOffered, Cancelled, Ended, StatusCount };
extern const char* const kStatusNames[StatusCount];

// Prime time: 7 nights x 3 hours.
constexpr int kNights = 7;
constexpr int kHours  = 3;
constexpr int kSlots  = kNights * kHours;
extern const char* const kNightNames[kNights];
extern const char* const kHourNames[kHours];
inline int SlotIndex(int night, int hour) { return night * kHours + hour; }

// Own network reach levels.
enum Reach { ReachLocal, ReachRegional, ReachNational, ReachCount };
extern const char* const kReachNames[ReachCount];
extern const float kReachShare[ReachCount];      // share of TV homes
extern const float kReachCost1950[ReachCount];   // one-off cost to reach this level
extern const float kReachUpkeep1950[ReachCount]; // weekly station costs

// Long-running shows (Phase 3).
enum SeasonPlan { PlanStandalone, PlanArc, PlanEvent, PlanCount };
extern const char* const kPlanNames[PlanCount];
extern const char* const kPlanHelp[PlanCount];
enum Finale { FinaleClosure, FinaleCliffhanger, FinaleCount };
extern const char* const kFinaleNames[FinaleCount];

// ---------------------------------------------------------------------------------
// State

struct Series {
    int         id = 0;
    std::string title;
    int         format = Sitcom;
    int         status = Pitching;
    int         network = 0;       // 0..2 rival, kPlayerNetwork = your own network
    int         budget = BudgetStandard;
    int         deal = DealLicense;
    std::string pilotKey;          // identifies the pilot film: title + start date
    std::string pilotTitle;
    float       pilotQuality = 0;  // 0..1, from the game when pitched
    float       pilotStars = 0;
    float       pitchScore = 0;
    int         passedMask = 0;    // networks that turned it down
    int         reworkMask = 0;    // networks that asked for a rework of the current pilot
    int         attempts = 0;
    int         season = 0;
    int         order = 0;         // episodes ordered this season
    int         aired = 0;         // episodes aired this season
    int         totalAired = 0;
    int         slot = -1;         // prime-time slot on its network, -1 = none yet
    float       verdictDay = 0;
    float       nextAirDay = 0;    // earliest day of the next airing
    float       freshness = 1.0f;
    float       lastRating = 0;
    float       lastShare = 0;
    float       bestRating = 0;
    float       ratingSum = 0;     // this season
    float       shareSum = 0;      // this season
    double      income = 0;        // all seasons
    double      cost = 0;
    std::vector<float> ratings;    // this season, one per episode
    // People assigned by dropping them on the TV Production Office's zones.
    std::string lead;              // Cast zone: the star of the show
    std::string showrunner;        // Showrunner zone
    std::vector<std::string> writers;   // Writers' Room zone, up to kMaxWriters
    // Phase 3: the season's plan and finale, contracts, renewals and spinoffs.
    int         plan = PlanStandalone;
    int         finale = FinaleClosure;
    bool        cliffhanger = false;   // the last season ended on one; the next premiere is an event
    float       qualitySum = 0;        // this season
    float       premiereRating = 0;    // this season
    float       lastQuality = 0;       // average episode quality of the last finished season
    float       leadPay = 1.0f;        // the star's pay, as a multiple of the standard rate
    int         leadSeasons = 0;       // seasons the current star has made
    int         demand = 0;            // a raise (percent) the star wants before the next season
    std::string formerLead;            // a star who walked out and hasn't been replaced
    float       feeMul = 1.0f;         // license fee or ad share, raised in renewal talks
    bool        askedForRaise = false; // already negotiated this renewal
    int         parentId = 0;          // spun off from this series
    int         lastSpinoffSeason = -9;
    // Phase 4: the show as a library title.
    double      ratingTotal = 0;       // every episode's rating, all seasons
    float       wear = 0;              // 0 = fresh, rises while reruns air, recovers in the vault
    double      syndIncome = 0;        // rerun money earned (after residuals)
    double      residuals = 0;         // paid to cast and writers
    // Phase 5: streaming originals.
    bool        binge = false;         // every episode at once instead of weekly
    float       lastReleaseDay = 0;    // last time an episode aired or streamed
    // Phase 6.
    int         awards = 0;            // Golden Antennas won
    bool        scandal = false;       // the star is in trouble, awaiting your decision
    bool        livePending = false;   // the next episode goes out live
    int         liveSeason = -1;       // season of the last live episode
    // Toon Studio: a TV cartoon, its pilot drawn at the Animation Studio (tv_writers.cpp) and its
    // episodes made there: no soundstage, cheaper, better with more animators.
    bool        animated = false;
};

constexpr int kMaxWriters = 4;

// A person dropped on a TV zone, waiting for the player to pick a show.
struct PendingDrop {
    bool        active = false;
    std::string room;
    std::string person;      // display name
    std::string personClass;
};
extern PendingDrop g_pendingDrop;
extern std::vector<uintptr_t> g_droppedProjects;   // scripts and films dropped on TV buildings
void OpenPitchFor(uintptr_t project);              // the Pitch tab, with this script or film picked
void OpenPitchForKey(const std::string& pilotKey); // ... or this pilot
void AssignPerson(Series& s, const std::string& room, const std::string& person);

struct AiShow {
    int         id = 0;
    std::string title;
    int         network = 0;
    int         format = Sitcom;
    float       quality = 0.4f;
    float       freshness = 1.0f;
    int         season = 1;
    int         weeks = 0;         // airings this season
    float       shareSum = 0;
    float       lastRating = 0;
};

// What occupies a schedule slot.
struct SlotEntry {
    int seriesId = 0;   // a player series, or
    int aiShowId = 0;   // a rival network's own show; both 0 = filler
};

struct NetworkState {
    bool        exists = false;
    std::string name;            // player network only (rivals use kNetworks)
    std::string shortName;
    int         reach = ReachLocal;
    bool        director = false; // a Programming Director is on staff
    SlotEntry   slots[kSlots];
    float       lastRating[kSlots] = {};
    float       lastShare[kSlots] = {};
    double      weekIncome = 0, weekCost = 0;     // player network, current week
    double      lastWeekIncome = 0, lastWeekCost = 0;
};

struct ChartEntry {
    std::string title;
    int         network = 0;
    int         format = 0;
    int         slot = 0;
    float       rating = 0;
    float       share = 0;
    bool        player = false;
};

// One edition of the Season Finale newspaper.
struct Newspaper {
    float       day = 0;
    std::string paper;       // masthead
    std::string kicker;      // small line above the headline
    std::string headline;
    std::string subhead;
    std::string body;
    std::string verdict;     // renewal news
    std::string quote;       // the critics
    std::string sidebar;     // the week's other top shows, one per line
    int         seriesId = 0;
};
extern std::deque<Newspaper> g_papers;   // newest last
extern int g_paperOpen;
// Files a front page in the Press tab and puts it on the game's timeline as news.
void PrintPaper(const Newspaper& p);                  // index into g_papers, -1 = closed
extern bool g_paperAuto;                 // open the paper at every finale

struct Toast {
    std::string title;
    std::string body;
    ImVec4      colour;
    double      born = -1;
};

extern std::vector<Series> g_series;
extern int   g_level;
extern int   g_nextId;
extern float g_incomeScale;
extern float g_costScale;
extern int   g_offices, g_stages, g_hqs, g_controlRooms, g_archives, g_datacenters;
extern bool  g_detectionWorks;
extern bool  g_devTools;
extern std::deque<Toast> g_toasts;

extern NetworkState g_networks[kNetworkSlots];
extern std::vector<AiShow> g_aiShows;
extern std::vector<ChartEntry> g_lastWeek;   // the most recent complete week, best first

// ---------------------------------------------------------------------------------
// Helpers (tv.cpp)

float Today();
float Year();
float EraFactor(float year);
float Households(float year);   // homes with a TV, millions
float Appeal(int format, float year);
int   FullOrder(int format, float year);
float Roll(uint32_t a, uint32_t b, uint32_t c, float lo, float hi);
uint32_t HashText(const std::string& s);

std::string Utf8(const std::wstring& w);
std::string Dollars(double v);
std::string Format(const char* fmt, ...);
std::string DateText(float days);

extern const ImVec4 kGold, kGreen, kRed, kYellow, kGrey;
void Notify(const std::string& title, const std::string& body, ImVec4 colour);

std::string PilotKey(const game::Project& p);
std::vector<game::Project> AvailablePilots();   // released films not yet used as a pilot

bool Unlocked();
void InstallNative();   // tv_native.cpp: the Television screen in the game's own interface
// Studio shares (tv_shares.cpp).
void SharesDaily(float day);
bool SharesOusted();
bool SharesWarned();   // the board will vote at the next review
double SharePrice();
std::string SellShares(double fraction);     // "" on success, else why not
std::string BuyBackShares(double fraction);
std::string PayDividend();
// What a sale or buy-back of 'fraction' of the studio would do now (shares 0 = not possible).
struct ShareQuote {
    double shares = 0;
    double cash = 0;          // raised by a sale, paid for a buy-back
    double ownedBefore = 0;   // fractions of the studio
    double ownedAfter = 0;
};
ShareQuote QuoteSell(double fraction);
ShareQuote QuoteBuyBack(double fraction);
std::wstring SharesPage();
// The bag of shares and the investors' briefcase at the Accounting Office (tv_shares.cpp).
bool IsShareObject(uintptr_t project);
const char* ShareObjectMesh(uintptr_t project);
bool SharesDrop(const std::string& zone, uintptr_t project);   // true if it was one of them
void PlaceShareObjects();             // put them out at the Accounting Office if missing
// New professions (tv_staff.cpp): accountants, hired at the Accounting Office, and animators,
// hired at the Animation Studio (careers 17 and 18).
int  Accountants();
int  Animators();
int  StaffWorkingIn(int career, const char* zone);   // the player's people of a career in a zone now
std::string ZoneOf(uintptr_t person);                // the zone someone is in ("" outside)
void StaffReset();
void InstallStaff();
// Someone dropped on a profession's work zone (the Accounts Floor, the animators' desks, Ink &
// Paint); true if it was handled there.
bool StaffDrop(const std::string& zone, uintptr_t room, uintptr_t person);
// Toon Studio (tv_animation.cpp): the Animation Studio's zones (room_tva_*); true if handled.
// Stars dropped in the Voice Booth voice the cartoon's roles.
bool AnimationDrop(const std::string& zone, uintptr_t room, uintptr_t project, uintptr_t person,
                   const std::string& personClass, const std::string& personName, float starRating);
// The cartoon's bubble on the lot: how many scenes are drawn and painted, how many roles voiced.
bool CartoonLabel(uintptr_t project, std::wstring* kind, std::wstring* phase, std::wstring* status);
bool IsCartoon(uintptr_t project);   // the film the animators are working on
bool IsFinishedCartoon(const std::wstring& title);   // a film the animators finished
void InstallAnimation();
bool CartoonInProgress();   // the animators have a feature cartoon
constexpr const char* kAnimationStudioType = "facility_animation_studio";
// The studio mascot (tv_mascot.cpp): born with the first short or cartoon, grows with each one.
void  InstallMascot();
std::string MascotName(bool create);   // "" before it's born; create: born now, for a short
float MascotAppeal();                  // 0..1
void  MascotShown(const std::string& shortTitle, float quality, float weight);   // a short played
// Cartoon shorts (tv_shorts.cpp): made between features, shown before the player's films.
void  InstallShorts();
bool  ShortsVoiceDrop(uintptr_t room, uintptr_t person, const std::string& personName, float rating);
std::string ShortsStatus();   // a sentence on the short in the works and the shelf
// The vault (tv_vault.cpp): cartoons re-released every 7 years. A film can on the lot, dropped
// on the Animation Studio.
void  InstallVault();
bool  IsVaultObject(uintptr_t project);
const char* VaultMesh(uintptr_t project);
const char* VaultPicture(uintptr_t project);
bool  VaultLabel(uintptr_t project, std::wstring* kind, std::wstring* phase, std::wstring* status);
bool  VaultDrop(const std::string& zone, uintptr_t project);   // true if it was the vault
void  VaultInfo();   // its card on the HUD list was clicked
// Voice fame (tv_voices.cpp): voice work as its own track, lifting a star's rating.
void  InstallVoices();
void  VoiceSeen(const std::string& name, uintptr_t person);   // a star dropped in the Voice Booth
float VoiceRating(const std::string& name, float rating);     // what their voice is worth: at least their fame
void  VoiceCast(const std::string& title, const std::vector<std::string>& names);   // credited at release
void  VoiceCredit(const std::string& name, float amount, const std::string& what);
void  VoiceSeason(const std::string& lead, const std::string& title, float success);   // an animated season ended
void  VoiceShort(const std::string& voice, const std::string& title, float weight);    // a short played
void  VoiceReissue(const std::string& title);   // a cartoon came out of the vault
// The lines of their hover bubble (kind, phase, status); false if it isn't one of them.
// The right-hand HUD list's picture for one (ui/shares_*.dds), and the Studio shares screen.
const char* ShareObjectPicture(uintptr_t project);
void OpenSharesScreen();
bool ShareObjectLabel(uintptr_t project, std::wstring* kind, std::wstring* phase, std::wstring* status);
void AskSellShares(double fraction);  // the game's yes/no dialog, then sell (tv_native.cpp)
void ReportShares(const std::string& error);

// Rich text for the game's own screens (tv_native.cpp): its colours and building blocks.
// The renderer lays everything out inline: <p> only breaks the line before itself, and
// headings and tables flow on after the text before them. So every block here ends with
// <br>, and pages put Line() round loose text instead of <p>.
namespace html {
extern const wchar_t* const Accent; // yours, the current tab
extern const wchar_t* const Dim;    // notes and secondary figures
extern const wchar_t* const Good;
extern const wchar_t* const Warn;
extern const wchar_t* const Bad;
std::wstring Text(const std::string& utf8);   // escaped for the renderer
std::wstring Colour(const std::wstring& text, const wchar_t* colour);
std::wstring Heading(const std::wstring& text);
std::wstring Note(const std::wstring& text);  // a dim line
std::wstring Line(const std::wstring& text);  // text, then a line break
std::wstring EndTable();
// A horizontal bar 'width' pixels long at value == max.
std::wstring Bar(double value, double max, const wchar_t* colour, int width = 160);
// A strip of figures, each a dim label over a bold value.
std::wstring Stats(const std::vector<std::pair<std::wstring, std::wstring>>& cells);
}
void SharesSave(std::map<std::string, std::string>& out);
void SharesLoad(const std::map<std::string, std::string>& in);
void SharesReset();
bool HasOffice();
int  Capacity();
bool InProduction(const Series& s);
std::string FormatLabel(const Series& s);   // "Sitcom", or "Animated Sitcom" for a TV cartoon
Series* FindSeries(int id);

// Series actions (tv.cpp)
float PitchScore(const Series& s, int network);
int   BestNetwork(const Series& s);
void  StartPitch(Series& s, int network);
void  OrderSeason(Series& s, int episodes, float firstAirDay);
float EpisodeQuality(const Series& s, int episode);
void  RecordAiring(Series& s, float quality, float rating, float share);   // money, stats, notifications
void  EndSeason(Series& s);
float RenewalNeeded(const Series& s);   // share a rival network wants to see
double StaffPay1950(const Series& s);

// Long-running shows (tv_story.cpp)
double LeadPay1950(const Series& s);
float  PlanQuality(const Series& s, int episode);
float  PlanCostMul(const Series& s);
float  EventBoost(const Series& s);            // audience pull of tonight's episode: premieres, finales, recasts
float  FreshnessLoss(const Series& s);         // per episode
void   SeasonFinale(Series& s, float avgShare, float needed);   // after the network's decision: contracts, newspaper
void   ResolveDemand(Series& s, bool pay);
void   AskForRaise(Series& s);                 // renewal talks with a rival network
void   RenewSeries(Series& s);                 // accept a renewal offer (or order the next season)
void   EndSeries(Series& s);                   // decline it: the show ends
void   AfterAiring(Series& s, float quality, float rating);
bool   CanSpinOff(const Series& s, std::string* why);
std::string SpinoffTitle(const Series& parent, int format, int variant);
Series& CreateSpinoff(const Series& parent, const std::string& title, int format);
void   StorySave(std::map<std::string, std::string>& out);
void   StoryLoad(const std::map<std::string, std::string>& in);
void   StoryReset();

// Library (tv_library.cpp)
enum RerunDeal { RerunLocal, RerunExclusive, RerunCable, RerunStream, RerunStreamLicense, RerunDealCount };
struct LibraryDeal {
    int    seriesId = 0;
    int    kind = RerunLocal;
    int    buyer = 0;          // network, for exclusive deals
    float  start = 0, end = 0; // days
    double weekly = 0;         // local stations: paid every week
    double upfront = 0;        // exclusive: paid on signing
};
extern std::vector<LibraryDeal> g_deals;
constexpr int kMinPackage = 22;          // episodes before reruns can be sold
constexpr int kCableYear  = 1976;
bool   InLibrary(const Series& s);
bool   OwnsReruns(const Series& s);
LibraryDeal* ActiveDeal(int seriesId);
double LocalWeekly(const Series& s);              // current money, per week
double ExclusiveOffer(const Series& s, int network, int years);
float  ResidualRate(float year);
void   PayReruns(Series& s, double gross);   // rerun money in, residuals out
void   SellLocal(Series& s, int years);
void   SellExclusive(Series& s, int network, int years);
void   SetOnCable(Series& s, bool on);
bool   CanLaunchCable(std::string* why);
void   LaunchCable(const std::string& name);
void   LibraryWeekly(float day);
void   LibrarySave(std::map<std::string, std::string>& out);
void   LibraryLoad(const std::map<std::string, std::string>& in);
void   LibraryReset();
double CableValue();
void   DrawLibrary();

// Streaming (tv_streaming.cpp)
constexpr int kStreamingYear = 2007;
struct StreamerInfo { const char* name; const char* shortName; int launch; float strength; };
constexpr int kStreamerCount = 3;
extern const StreamerInfo kStreamers[kStreamerCount];
struct RegionInfo { const char* name; float potential; float priceFactor; float cost1950; };
constexpr int kRegionCount = 5;
extern const RegionInfo kRegions[kRegionCount];
bool   StreamingExists();
const char* StreamingName();
const char* StreamingShort();
float  StreamingSubscribers();   // millions
bool   StreamerActive(int streamer);
double StreamLicenseOffer(const Series& s, int streamer, int years);
void   LicenseToStreamer(Series& s, int streamer, int years);
void   SetOnStreaming(Series& s, bool on);
void   StreamingDaily(float day);
void   DrawStreaming();
void   StreamingSave(std::map<std::string, std::string>& out);
void   StreamingLoad(const std::map<std::string, std::string>& in);
void   StreamingReset();
double OutletValue();            // cable channel and streaming service, for the empire value

// Events, awards, empire (tv_events.cpp)
float  EventAppeal(int format, float year);
float  StandardsBonus();          // extra pitch bar while networks are under fire
void   EventsDaily(float day);
bool   WritersOnStrike();
void   ResolveScandal(Series& s, bool fire);
double MediaEmpireValue(std::vector<std::pair<std::string, double>>* parts);
void   DrawEmpire();
void   EventsSave(std::map<std::string, std::string>& out);
void   EventsLoad(const std::map<std::string, std::string>& in);
void   EventsReset();

void   RecordHistory(const std::string& text);   // the Empire tab's timeline

// The Golden Antenna ceremony on the game's own awards stage (tv_ceremony.cpp).
struct CeremonyNominee {
    std::string title;      // as the papers print it: 'Title', or Name, 'Title'
    bool        player = false;
    int         network = 0;
    float       score = 0;
};
struct CeremonyCategory {
    std::string name;       // "Outstanding Comedy Series"
    std::string blurb;      // what it's for (the presenter explains)
    std::vector<CeremonyNominee> nominees;   // the winner first
};
bool CeremonyEnabled();
// Holds the show at the next quiet moment; 'after' runs when it's over (or skipped).
// A live show (the awards night) is announced on the lot's tannoy first; a replay isn't.
bool QueueCeremony(int edition, int year, std::vector<CeremonyCategory> categories, bool live, void (*after)());
void AnnounceAwardsSoon();   // the tannoy: "An Awards Ceremony will be held soon"
void CeremonyFrame();
void InstallCeremony(bool enabled);
// The latest Golden Antenna winners (tv_events.cpp); they count toward the studio rankings
// like the movie awards (tv_ceremony.cpp hooks the studio's awards factor).
std::vector<CeremonyNominee> LatestAntennaWinners();
bool WatchAntennas(int year);        // replays that year's ceremony; false if there's none
std::vector<int> AntennaYears();     // the years a ceremony was held
float AntennaDay(int year);          // its date (days), and the next one's
bool AntennaYearHeld(int year);
int NextAntennaYear();               // 0 before television
// The Golden Antennas on the game's timeline: icons and the countdown (tv_timeline.cpp).
void AwardsTimelineFrame();
void InstallAwardsTimeline();
void RefreshStudioRatings();

// Research, challenges, live episodes (tv_research.cpp)
enum TechId { TechLaughTrack, TechAudienceResearch, TechMultiCamera, TechRemoteVans, TechSalesTeam, TechVideotape,
              TechColour, TechSatellite, TechStereo, TechHD, TechCDN, TechRecAI, TechCount };
struct TechInfo { const char* name; int year; float cost1950; int weeks; const char* blurb; const char* effect; };
extern const TechInfo kTechs[TechCount];
bool   HasTech(int tech);
float  TechQualityBonus(int format);
float  TechCostMul();
float  TechAppeal(int format);
bool   CanGoLive(const Series& s, std::string* why);
void   PlanLive(Series& s);
void   LiveOutcome(Series& s);
void   ResearchWeekly(float day);
void   DrawResearch();
void   ResearchSave(std::map<std::string, std::string>& out);
void   ResearchLoad(const std::map<std::string, std::string>& in);
void   ResearchReset();

// TV Writers' Office (tv_writers.cpp)
extern const char* const kWritersType;
extern int g_writerOffices;
bool   IsWritersZone(const std::string& zone);
bool   IsShelfScriptObject(uintptr_t project);
bool   IsTvScriptProject(uintptr_t project);    // a TV script: on the shelf or being written   // a script the Writers' Office put on the lot
void   AssignWriterToDesk(const std::string& zone, const std::string& person, uintptr_t scriptRoom = 0);
bool   AdoptIfWriting(uintptr_t project);   // a script still being written, dropped on a TV building
void   WritersDaily(float day);
void   AddShelfPilots(std::vector<game::Project>& out, std::vector<std::string>& seen);
// Pilots (tv_writers.cpp): a TV script is shot as a pilot episode on a soundstage before
// it can be pitched (unless [tv] FilmPilots = 1, which also allows films and bare scripts).
extern bool g_filmPilots;
void   ApplyPilot(Series& s, const game::Project& pilot);   // pilot fields (and the pilot's star as lead)
bool   StartPilotForProject(uintptr_t project);   // a shelf script object dropped on a TV building
bool   IsUnshotScript(uintptr_t project);
bool   IsPilotReel(uintptr_t project);      // a shot pilot not yet pitched: a reel on the lot
const char* PilotReelMesh(uintptr_t project);
bool   DropReel(const std::string& zone, uintptr_t project);   // true if it was a reel
// The hover bubble of a finished TV script or reel (kind, phase, status); false to keep the game's.
bool   TvScriptLabel(uintptr_t project, std::wstring* kind, std::wstring* phase, std::wstring* status);
bool   CastPilot(const std::string& name, float stars, bool director);   // a person dropped on a soundstage
// TV cartoons: a TV script dropped in the Animation Studio's Story Room has its pilot drawn by
// the animators (after any feature cartoon) and its lead voiced in the Voice Booth.
bool   AnimatePilot(uintptr_t project);           // false if it isn't a TV script
bool   AnimatedPilotInProgress();
bool   PilotVoiceDrop(uintptr_t room, uintptr_t person, const std::string& personName, float rating);
void   AutoPilots(bool castStandIns);             // shoot the best scripts on free soundstages
// The script shelf for display (native screen): one entry per script not yet pitched.
struct ShelfEntry {
    std::string title, lead, director;
    int         format = 0;
    int         pilot = 0;          // 0 script, 1 shooting, 2 pilot ready
    float       quality = 0;        // the script
    float       pilotQuality = 0;
    float       wrapDay = 0;        // shooting: 0 = waiting for a lead
    bool        animated = false;   // the pilot is drawn at the Animation Studio
};
std::vector<ShelfEntry> ShelfSummary();
int    FreeSoundstages();
std::string PitchBestPilot();   // pitches the best finished pilot to the likeliest network; its title, or ""
int    ShelfScriptFormat(const game::Project& p);   // -1 unless it's a TV script from the shelf
void   DrawWriters();
void   WritersSave(std::map<std::string, std::string>& out);
void   WritersLoad(const std::map<std::string, std::string>& in);
void   WritersReset();
std::string ShowTitle(int format, uint32_t seed);   // a made-up show title (tv_network.cpp)

inline bool OwnOutlet(int network) { return network == kPlayerNetwork || network == kStreamingNetwork; }

// Networks (tv_network.cpp)
const char* NetworkName(int network);
const char* NetworkShort(int network);
float NetworkReach(int network, float year);
std::string SlotName(int network, int slot);
AiShow* FindAiShow(int id);
void  AssignSlot(Series& s);          // the series' network gives it a prime-time slot
void  ReleaseSlot(Series& s);         // the series leaves the schedule
void  AirNight(float day, int night); // everything on prime time tonight
void  NetworkDaily(float day);        // fall season, weekly accounts, Programming Director
void  AutoSchedule();                 // fill your network's empty slots with your shows
bool  CanFoundNetwork(std::string* why);
void  FoundNetwork(const std::string& name);
bool  CanUpgradeReach(std::string* why);
void  UpgradeReach();
void  NetworksReset();
void  NetworksSave(std::map<std::string, std::string>& out);
void  NetworksLoad(const std::map<std::string, std::string>& in);
void  EnsureRivalSchedules();

// UI (tv_ui.cpp, tv_paper.cpp)
void DrawWindow();
void DrawToasts();
void DrawNewspaper();
void HelpMarker(const char* text);
void RankBadge(int rank);   // a position in a rosette, like the ranking screen
void DrawPressTab();
int  TabTotal();          // the TV screen's tabs (for the simulator)
void SelectTab(int tab);

} // namespace tv
