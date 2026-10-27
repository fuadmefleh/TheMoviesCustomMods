// Studio shares: sell part of the studio to fund it, and risk losing it.
//
// The studio is 1,000,000 shares, all the player's at first. Selling blocks raises cash at
// the market price; the price follows what the studio is worth (cash, the TV business, the
// films still earning, the studio's standing and its lot) with momentum, noise and the
// era's crashes. Once outsiders own part of the studio a board judges it every quarter:
// share price, the critics, debt, dividends. Below 50% the board can vote the player out;
// below 25% rival studios can bid to take the company over. Either way the game is over for
// that studio. The mod's HUD button "Studio shares" opens the screen.
//
// Shares change hands at the studio's Accounting Office, a building on the lot. The player's
// stake is a bag of share certificates waiting at its door, the outside investors' stake a
// briefcase. Drop the bag on the share desk to sell 5% of the studio; the briefcase on the
// share desk to buy 5% back, or in the boardroom to pay the investors a dividend.

#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <deque>

#include "game.h"
#include "mod.h"
#include "nativeui.h"
#include "news.h"
#include "overlay.h"
#include "persist.h"
#include "rooms.h"
#include "tv_internal.h"

namespace tv {
namespace {

constexpr double kTotalShares = 1000000;
constexpr double kMinOwned = 0.10;          // the player can't sell below 10%
constexpr float  kQuarter = 91.25f;
// Dealing costs: 7% on a sale and 5% over the market on a buy-back, one point less for
// each accountant (down to 2% and 1%).
double SaleFee() { return std::max(0.02, 0.07 - 0.01 * Accountants()); }
double BuyPremium() { return std::max(0.01, 0.05 - 0.01 * Accountants()); }

struct Shares {
    double owned = kTotalShares;   // the player's shares
    double price = 0;              // per share, 0 = not valued yet
    double marketMood = 1.0;       // crashes and booms
    float  confidence = 60;        // the board, 0..100 (only once outsiders own shares)
    float  lastReview = 0;
    double priceAtReview = 0;
    bool   warned = false;         // the board's last warning: one quarter to turn it around
    bool   ousted = false;         // game over for this studio
    double dividendsThisQuarter = 0;
    double raised = 0;             // cash raised by selling, all time
    std::deque<double> history;    // price at each quarterly review, oldest first
    std::string fate;              // why the player lost the studio
};
Shares g_sh;
float g_lastDay = 0;
uint32_t g_noise = 12345;

float Noise()   // -1..1
{
    g_noise = g_noise * 1664525u + 1013904223u;
    return ((g_noise >> 8) % 20001) / 10000.0f - 1.0f;
}

double OutsideFraction() { return 1.0 - g_sh.owned / kTotalShares; }
double Ownership() { return g_sh.owned / kTotalShares; }

const char* Mood(float confidence)
{
    return confidence > 70 ? "delighted" : confidence > 45 ? "content" : confidence > 25 ? "restless" : "furious";
}

double DividendCost() { return g_sh.price * kTotalShares * OutsideFraction() * 0.02; }

// What the studio is worth.
double Valuation()
{
    double v = MediaEmpireValue(nullptr);   // cash, the TV business, awards
    float today = Today();
    for (const game::Project& p : game::PlayerBackCatalogue())
        if (p.hasIncome && p.releaseDate > 0 && today - p.releaseDate < 3 * kDaysPerYear)
            v += p.incomeRemaining + 0.25 * (p.incomeInitial - p.incomeRemaining);
    for (const game::Project& p : game::PlayerProductions())
        if (p.stage >= 7 && p.hasIncome && today - p.releaseDate < 3 * kDaysPerYear)
            v += p.incomeRemaining + 0.25 * (p.incomeInitial - p.incomeRemaining);
    game::Studio st = game::ReadPlayerStudio();
    double era = EraFactor(Year());
    if (st.starRating > 0)
        v += st.starRating * 2000000.0 * era;
    v += static_cast<double>(game::Facilities().size()) * 40000.0 * era;
    return std::max(v, 50000.0 * era);
}

// The era's market: crashes and the long recoveries after them.
void MarketDaily(float year)
{
    struct Crash { float year; double hit; const char* headline; };
    static const Crash kCrashes[] = {
        { 1929.8f, 0.55, "WALL STREET CRASHES" }, { 1973.9f, 0.75, "OIL SHOCK SINKS SHARES" },
        { 1987.8f, 0.72, "BLACK MONDAY" },        { 2000.25f, 0.70, "DOT-COM BUBBLE BURSTS" },
        { 2008.7f, 0.62, "MARKETS IN FREEFALL" },
    };
    static float lastYear = 0;
    for (const Crash& c : kCrashes)
        if (lastYear > 0 && lastYear < c.year && year >= c.year) {
            g_sh.marketMood *= c.hit;
            if (Ownership() < 1.0)
                Notify(c.headline, Format("Your studio's shares fall with the market (%.0f%%).", (c.hit - 1) * 100), kRed);
        }
    lastYear = year;
    g_sh.marketMood += (1.0 - g_sh.marketMood) * 0.0015;   // recovers over a few years
}

bool g_fateShown = false;

// Game over: the game's own quit-to-menu sits behind both buttons.
void ShowFate()
{
    g_fateShown = true;
    nativeui::GameOverDialog(L"Game over", L"<h2>You have lost the studio</h2><p>" + nativeui::Escape(g_sh.fate) +
                                               L"</p><p>This studio's story ends here: back to the main menu. Load an "
                                               L"earlier save or start a new game.</p>");
}

void Ousted(const std::string& why)
{
    if (g_sh.ousted)
        return;
    g_sh.ousted = true;
    g_sh.fate = why;
    mod::Log("tv: shares - GAME OVER: %s", why.c_str());
    news::Post("STUDIO BOSS OUSTED", why, "The new owners thanked the founder for their service and changed the locks.");
    Notify("You have lost the studio", why, kRed);
    ShowFate();
}

void QuarterlyReview(float today)
{
    double change = g_sh.priceAtReview > 0 ? g_sh.price / g_sh.priceAtReview - 1.0 : 0;
    // Investors expect about 2% growth a quarter: beat it and they're pleased.
    float score = static_cast<float>((change - 0.02) * 120.0);   // +7% = +6, flat = -2.4, -10% = -14.4
    int films = 0;
    float critics = 0;
    for (const std::vector<game::Project>& list : { game::PlayerBackCatalogue(), game::PlayerProductions() })
        for (const game::Project& p : list)
            if (p.releaseDate > today - kQuarter && p.releaseDate <= today && p.starRating >= 0) {
                films++;
                critics += p.starRating;
            }
    if (films) {
        float avg = critics / films;
        score += (avg - 0.5f) * 30.0f;   // a 4-star slate +9, a 1-star one -9
    } else {
        score -= 4;                      // nothing in cinemas this quarter
    }
    if (game::ReadMoney().capital < 0)
        score -= 10;
    if (g_sh.dividendsThisQuarter > 0)
        score += 6;
    score += Noise() * 4;
    // Moods fade: a board doesn't stay delighted (or furious) without new reasons.
    g_sh.confidence = std::clamp(50.0f + (g_sh.confidence + score - 50.0f) * 0.85f, 0.0f, 100.0f);
    g_sh.dividendsThisQuarter = 0;
    g_sh.history.push_back(g_sh.price);
    while (g_sh.history.size() > 40)
        g_sh.history.pop_front();
    g_sh.priceAtReview = g_sh.price;
    g_sh.lastReview = today;

    std::string mood = Mood(g_sh.confidence);
    mod::Log("tv: shares - quarter: price %.2f (%+.0f%%), %d film(s), confidence %.0f, you own %.0f%%", g_sh.price,
             change * 100, films, g_sh.confidence, Ownership() * 100);
    Notify(Format("Shareholders' report: the board is %s", mood.c_str()),
           Format("Shares %s (%+.0f%% this quarter). You own %.0f%% of the studio.", Dollars(g_sh.price).c_str(),
                  change * 100, Ownership() * 100),
           g_sh.confidence > 45 ? kGreen : g_sh.confidence > 25 ? kYellow : kRed);

    // Losing control.
    if (Ownership() < 0.5 && g_sh.confidence < 25) {
        if (g_sh.warned) {
            Ousted(Format("With %.0f%% of the shares, the board outvoted you and removed you as head of the studio.",
                          OutsideFraction() * 100));
            return;
        }
        g_sh.warned = true;
        news::Post("BOARD LOSES PATIENCE", "Shareholders demand results",
                   "Directors say the studio's founder has one quarter to turn things around.");
        nativeui::Confirm(L"The board's warning",
                          L"<p>The board is furious. With outside investors holding most of the studio, they will "
                          L"<b>vote you out at the next quarterly review</b> unless confidence recovers.</p>"
                          L"<p>Release a hit, pay a dividend, or buy back shares to hold the majority.</p>",
                          nullptr);
    } else if (g_sh.confidence >= 35 || Ownership() >= 0.5) {
        g_sh.warned = false;
    }

    // Takeover bids for a studio its founder barely owns.
    if (Ownership() < 0.25 && Noise() > (g_sh.confidence > 50 ? 0.5f : 0.0f)) {
        std::vector<game::Project> rivals = game::RivalFilms();
        std::wstring bidder = rivals.empty() ? L"A rival studio" : rivals[static_cast<size_t>(g_noise) % rivals.size()].studio;
        double defence = g_sh.price * kTotalShares * 0.3;
        std::string who = Utf8(bidder);
        mod::Log("tv: shares - takeover bid by %s (defence %.0f)", who.c_str(), defence);
        nativeui::Confirm(
            L"Takeover bid",
            L"<p><b>" + nativeui::Escape(who) + L"</b> has bid for your studio and the outside shareholders are "
            L"tempted.</p><p>Fight it off with a counter-offer to the shareholders: <b>" +
                nativeui::Escape(Dollars(defence)) + L"</b>.</p><p>Tick: fight. Cross: let the bid go through - "
                L"you lose the studio.</p>",
            [defence, who] {
                if (game::ReadMoney().capital >= defence) {
                    game::AddMoney(-defence);
                    g_sh.confidence = std::max(0.0f, g_sh.confidence - 5);
                    Notify("Takeover defeated", Format("You saw off %s's bid.", who.c_str()), kGreen);
                } else {
                    Ousted(Format("%s took over the studio: you couldn't raise %s to fight the bid.", who.c_str(),
                                  Dollars(defence).c_str()));
                }
            },
            [who] { Ousted(Format("%s bought the studio from its shareholders.", who.c_str())); });
    }
}

}   // namespace

// ---------------------------------------------------------------------------------

void SharesDaily(float day)
{
    if (g_sh.ousted) {
        if (!g_fateShown)
            ShowFate();   // a save of a lost studio
        return;
    }
    if (day <= g_lastDay)
        return;
    float days = g_lastDay > 0 ? std::min(day - g_lastDay, 30.0f) : 1;
    g_lastDay = day;
    float year = day / kDaysPerYear;
    MarketDaily(year);
    double fair = Valuation() * g_sh.marketMood / kTotalShares;
    if (g_sh.price <= 0) {
        g_sh.price = fair;
        g_sh.priceAtReview = fair;
        g_sh.lastReview = day;
    }
    // The price drifts toward what the studio is worth, with some daily noise.
    double rate = 1.0 - std::pow(0.97, days);
    g_sh.price += (fair - g_sh.price) * rate;
    g_sh.price *= 1.0 + Noise() * 0.006 * std::sqrt(days);
    g_sh.price = std::max(g_sh.price, 0.01);
    PlaceShareObjects();
    if (Ownership() < 1.0 && day - g_sh.lastReview >= kQuarter)
        QuarterlyReview(day);
    else if (Ownership() >= 1.0)
        g_sh.lastReview = day;
}

bool SharesOusted() { return g_sh.ousted; }
bool SharesWarned() { return g_sh.warned; }



double SharePrice() { return g_sh.price; }

// ---------------------------------------------------------------------------------
// The bag and the briefcase: script objects on the lot (the research-pack spawn) with our
// own models, found again by title after a load. One of each, at the Accounting Office's door
// while there is something to trade; if the game takes one away when it is dropped, a new
// one is put out.

namespace {

const wchar_t* const kBagTitle  = L"Your shares";
const wchar_t* const kCaseTitle = L"Investors' shares";
const char* const    kAccountsType = "facility_tv_accounting";

bool IsShareOffice(const std::string& type) { return type == kAccountsType; }
bool IsShareDesk(const std::string& zone) { return zone == "room_tvk_shares"; }
bool IsBoardroom(const std::string& zone) { return zone == "room_tvk_board"; }
uintptr_t g_bag = 0, g_case = 0;

bool WantBag() { return !g_sh.ousted && QuoteSell(0.05).shares >= 1; }
bool WantCase() { return !g_sh.ousted && OutsideFraction() > 1e-6; }

} // namespace

bool IsShareObject(uintptr_t project) { return project && (project == g_bag || project == g_case); }

const char* ShareObjectMesh(uintptr_t project)
{
    return !project ? nullptr : project == g_bag ? "p_shares_bag.msh" : project == g_case ? "p_shares_case.msh" : nullptr;
}

void PlaceShareObjects()
{
    g_bag = g_case = 0;
    for (const game::Project& p : game::PlayerProductions()) {
        if (p.stage >= 7)
            continue;
        if (p.title == kBagTitle)
            g_bag = p.address;
        else if (p.title == kCaseTitle)
            g_case = p.address;
    }
    rooms::RefreshScriptLook(g_bag);
    rooms::RefreshScriptLook(g_case);
    if ((g_bag || !WantBag()) && (g_case || !WantCase()))
        return;
    for (const game::Facility& f : game::Facilities()) {
        if (!IsShareOffice(f.type))
            continue;
        float pos[3], angle;
        if (!game::FacilityPlacement(f.address, pos, &angle))
            continue;
        // In front of the door (the model faces -y), the bag on its left, the case on its right.
        float fx = std::sin(angle), fy = -std::cos(angle);
        auto place = [&](const wchar_t* title, float side) {
            uintptr_t p = game::SpawnScript(title, pos[0] + fx * 11.0f + fy * side, pos[1] + fy * 11.0f - fx * side);
            mod::Log("tv: shares - '%ls' put out at the Accounting Office (%08X)", title, static_cast<unsigned>(p));
            return p;
        };
        if (!g_bag && WantBag())
            g_bag = place(kBagTitle, -2.5f);
        if (!g_case && WantCase())
            g_case = place(kCaseTitle, 2.5f);
        rooms::RefreshScriptLook(g_bag);
        rooms::RefreshScriptLook(g_case);
        return;
    }
}

const char* ShareObjectPicture(uintptr_t project)
{
    return !project ? nullptr : project == g_bag ? "ui/shares_bag.dds" : project == g_case ? "ui/shares_case.dds" : nullptr;
}

bool ShareObjectLabel(uintptr_t project, std::wstring* kind, std::wstring* phase, std::wstring* status)
{
    if (!IsShareObject(project))
        return false;
    if (project == g_bag) {
        ShareQuote q = QuoteSell(0.05);
        *kind = html::Text(Format("Studio shares - you own %.0f%%", Ownership() * 100));
        *phase = q.shares >= 1 ? html::Text(Format("Sell 5%% for %s", Dollars(q.cash).c_str())) : L"Nothing left to sell";
        *status = L"Drop on the Share Desk";
    } else {
        ShareQuote q = QuoteBuyBack(0.05);
        *kind = html::Text(Format("Investors own %.0f%%", OutsideFraction() * 100));
        *phase = html::Text(Format("Share Desk: buy back %s", Dollars(q.cash).c_str()));
        *status = html::Text(Format("Boardroom: dividend %s", Dollars(DividendCost()).c_str()));
    }
    return true;
}

bool SharesDrop(const std::string& zone, uintptr_t project)
{
    if (!IsShareObject(project))
        return false;
    bool bag = project == g_bag;
    if ((IsShareDesk(zone) || IsBoardroom(zone)) && Accountants() == 0) {
        Notify("No accountants", "Someone has to do the paperwork. Hire an accountant: drop a jobseeker (or one of "
                                 "your staff) on the Accounting Office's hiring desk.",
               kYellow);
        return true;
    }
    if (IsShareDesk(zone)) {
        if (bag)
            AskSellShares(0.05);
        else
            ReportShares(BuyBackShares(0.05));
    } else if (IsBoardroom(zone) && !bag) {
        ReportShares(PayDividend());
    } else {
        Notify(bag ? "Your shares" : "The investors' shares",
               bag ? "Drop your shares on the Accounting Office's share desk to sell 5% of the studio."
                   : "Drop the investors' shares on the Accounting Office's share desk to buy 5% back, or in its "
                     "boardroom to pay them a dividend.",
               kYellow);
    }
    PlaceShareObjects();
    return true;
}

ShareQuote QuoteSell(double fraction)
{
    ShareQuote q;
    q.ownedBefore = q.ownedAfter = Ownership();
    if (g_sh.ousted || g_sh.price <= 0)
        return q;
    double n = std::min(fraction * kTotalShares, g_sh.owned - kMinOwned * kTotalShares);
    if (n < 1)
        return q;
    q.shares = n;
    q.cash = n * g_sh.price * (1.0 - SaleFee());
    q.ownedAfter = (g_sh.owned - n) / kTotalShares;
    return q;
}

ShareQuote QuoteBuyBack(double fraction)
{
    ShareQuote q;
    q.ownedBefore = q.ownedAfter = Ownership();
    double n = std::min(fraction * kTotalShares, kTotalShares - g_sh.owned);
    if (g_sh.ousted || n < 1)
        return q;
    q.shares = n;
    q.cash = n * g_sh.price * (1.0 + BuyPremium());
    q.ownedAfter = (g_sh.owned + n) / kTotalShares;
    return q;
}

// Sell 'fraction' of the whole studio (0.05 = 5%).
std::string SellShares(double fraction)
{
    if (g_sh.ousted)
        return "The studio is no longer yours.";
    ShareQuote q = QuoteSell(fraction);
    if (q.shares < 1)
        return Format("You can't sell below %.0f%% of the studio.", kMinOwned * 100);
    double n = q.shares, cash = q.cash;
    game::AddMoney(cash);
    g_sh.owned -= n;
    g_sh.raised += cash;
    g_sh.price *= 1.0 - fraction * 0.25;   // a big sale moves the market
    if (g_sh.priceAtReview <= 0)
        g_sh.priceAtReview = g_sh.price;
    mod::Log("tv: shares - sold %.0f shares for %.0f; you own %.0f%%", n, cash, Ownership() * 100);
    Notify(Format("You sold %.0f%% of the studio", n / kTotalShares * 100),
           Format("%s raised. You now own %.0f%%%s", Dollars(cash).c_str(), Ownership() * 100,
                  Ownership() < 0.5 ? " - the board can now vote you out." : "."),
           Ownership() < 0.5 ? kYellow : kGreen);
    return "";
}

std::string BuyBackShares(double fraction)
{
    if (g_sh.ousted)
        return "The studio is no longer yours.";
    ShareQuote q = QuoteBuyBack(fraction);
    if (q.shares < 1)
        return "You already own the whole studio.";
    double n = q.shares, cost = q.cash;
    if (game::ReadMoney().capital < cost)
        return Format("Buying back %.0f%% costs %s.", n / kTotalShares * 100, Dollars(cost).c_str());
    game::AddMoney(-cost);
    g_sh.owned += n;
    g_sh.price *= 1.0 + fraction * 0.2;
    g_sh.confidence = std::min(100.0f, g_sh.confidence + 2);
    if (Ownership() >= 0.5)
        g_sh.warned = false;
    mod::Log("tv: shares - bought back %.0f shares for %.0f; you own %.0f%%", n, cost, Ownership() * 100);
    Notify("Shares bought back", Format("You now own %.0f%% of the studio.", Ownership() * 100), kGreen);
    return "";
}

std::string PayDividend()
{
    if (g_sh.ousted || Ownership() >= 1.0)
        return "There are no outside shareholders to pay.";
    double cost = DividendCost();
    if (game::ReadMoney().capital < cost)
        return Format("A dividend costs %s.", Dollars(cost).c_str());
    game::AddMoney(-cost);
    g_sh.dividendsThisQuarter += cost;
    g_sh.confidence = std::min(100.0f, g_sh.confidence + 4);
    Notify("Dividend paid", Format("%s to the shareholders. The board approves.", Dollars(cost).c_str()), kGreen);
    return "";
}

namespace {

std::wstring T(const std::string& s) { return html::Text(s); }

// Who owns the studio, as one bar: the player's part in blue, the investors' in slate.
std::wstring OwnershipBar()
{
    constexpr int kWidth = 600;
    int mine = std::clamp(static_cast<int>(Ownership() * kWidth + 0.5), 0, kWidth);
    std::wstring h = L"<table width=" + std::to_wstring(kWidth) + L" cellspacing=0><tr>";
    if (mine > 0)
        h += L"<td width=" + std::to_wstring(mine) + L" bgcolor=#FF1F4FB0 height=14> </td>";
    if (mine < kWidth)
        h += L"<td width=" + std::to_wstring(kWidth - mine) + L" bgcolor=#FF8AA0B8 height=14> </td>";
    return h + L"</tr></table><br>";
}

// One line of the Accounting Office table: a drop, and what it would do right now.
std::wstring Option(const wchar_t* name, const std::wstring& now)
{
    return L"<tr><td><b>" + std::wstring(name) + L"</b></td><td>" + now + L"</td></tr>";
}

} // namespace

std::wstring SharesPage()
{
    using html::Colour;
    if (g_sh.ousted)
        return html::Heading(L"Studio shares") + html::Line(Colour(L"<b>You have lost the studio.</b>", html::Bad)) +
               html::Line(T(g_sh.fate));
    double own = Ownership();
    double capital = game::ReadMoney().capital;
    double change = g_sh.priceAtReview > 0 ? g_sh.price / g_sh.priceAtReview - 1.0 : 0;
    std::wstring trend = own < 1.0 ? L"  " + Colour(T(Format("%+.1f%%", change * 100)), change >= 0 ? html::Good : html::Bad) : L"";

    std::wstring h = html::Stats({ { L"Share price", T(Dollars(g_sh.price)) + trend },
                                   { L"Studio value", T(Dollars(g_sh.price * kTotalShares)) },
                                   { L"Your stake", T(Format("%.0f%%", own * 100)) },
                                   { L"Your stake is worth", T(Dollars(g_sh.price * g_sh.owned)) },
                                   { L"Raised by selling", T(Dollars(g_sh.raised)) } });

    // Ownership, and what it puts at risk.
    h += html::Heading(L"Who owns the studio") + OwnershipBar();
    h += html::Line(Colour(T(Format("You  %.0f%%", own * 100)), html::Accent) + Colour(L"   |   ", html::Dim) +
                    T(Format("Outside investors  %.0f%%", OutsideFraction() * 100)));
    if (own >= 1.0)
        h += html::Note(L"You own the whole studio. Selling shares raises cash, but brings in a board that judges the "
                        L"studio every quarter.");
    else if (own >= 0.5)
        h += html::Line(Colour(L"You hold the majority.", html::Good) + L" " +
                        Colour(L"Below 50% the board can vote you out; below 25% rivals can bid for the studio.", html::Dim));
    else if (own >= 0.25)
        h += html::Line(Colour(L"<b>You no longer hold the majority:</b> if the board loses confidence it can vote you out.",
                               html::Warn));
    else
        h += html::Line(Colour(L"<b>You own under a quarter:</b> the board can vote you out, and rival studios may bid "
                               L"to take the company over.", html::Bad));

    // The board, once there is one.
    if (own < 1.0) {
        const wchar_t* moodColour = g_sh.confidence > 45 ? html::Good : g_sh.confidence > 25 ? html::Warn : html::Bad;
        int days = std::max(0, static_cast<int>(kQuarter - (Today() - g_sh.lastReview) + 0.5f));
        h += html::Heading(L"The board");
        h += L"<table cellspacing=2><tr><td>" + Colour(L"<b>" + T(Mood(g_sh.confidence)) + L"</b>", moodColour) +
             L"   </td><td>" + html::Bar(g_sh.confidence, 100, moodColour, 300) + L"</td><td>   " +
             T(Format("confidence %.0f / 100", g_sh.confidence)) + L"</td></tr>" + html::EndTable();
        h += html::Note(T(Format("Next quarterly review in %d day%s. The board wants the share price up about 2%% a "
                                 "quarter and well-reviewed films in cinemas.", days, days == 1 ? "" : "s")));
        if (g_sh.dividendsThisQuarter > 0)
            h += html::Line(Colour(T(Format("Dividend paid this quarter: %s. The board will remember it.",
                                            Dollars(g_sh.dividendsThisQuarter).c_str())), html::Good));
        if (g_sh.warned)
            h += html::Line(Colour(L"<b>The board will vote you out at the next review unless confidence recovers.</b>",
                                   html::Bad));
    }

    // The price at each review, then today's in blue.
    if (!g_sh.history.empty()) {
        std::vector<double> prices(g_sh.history.begin(), g_sh.history.end());
        prices.push_back(g_sh.price);
        double top = std::max(*std::max_element(prices.begin(), prices.end()), 0.01);
        double low = *std::min_element(prices.begin(), prices.end());
        h += html::Heading(L"Share price by quarter") + L"<table cellspacing=2><tr>";
        for (size_t i = 0; i < prices.size(); i++) {
            const wchar_t* c = i + 1 == prices.size() ? html::Accent : i > 0 && prices[i] < prices[i - 1] ? html::Bad : html::Good;
            int height = std::max(2, static_cast<int>(prices[i] / top * 90));
            h += L"<td valign=bottom><table width=12 cellspacing=0><tr><td bgcolor=#FF" + std::wstring(c + 1) +
                 L" height=" + std::to_wstring(height) + L"> </td></tr></table></td>";
        }
        h += L"</tr>" + html::EndTable();
        h += html::Note(T(Format("High %s   |   Low %s   |   %d quarter%s. Green: up on the quarter before, red: down, "
                                 "blue: today.", Dollars(top).c_str(), Dollars(low).c_str(),
                                 static_cast<int>(g_sh.history.size()), g_sh.history.size() == 1 ? "" : "s")));
    }

    // What each drop at the Accounting Office would do now.
    h += html::Heading(L"At the Accounting Office") + L"<table width=100% cellspacing=2>";
    {
        ShareQuote q = QuoteSell(0.05);
        std::wstring now;
        if (q.shares < 1) {
            now = Colour(T(Format("Nothing to sell: you keep at least %.0f%% of the studio.", kMinOwned * 100)), html::Dim);
        } else {
            now = L"Sell 5%: " + Colour(L"+" + T(Dollars(q.cash)), html::Good) +
                  T(Format(" after %.0f%% fees. You'd own %.0f%%.", SaleFee() * 100, q.ownedAfter * 100));
            if (q.ownedBefore >= 0.5 && q.ownedAfter < 0.5)
                now += L" " + Colour(L"You'd lose the majority.", html::Warn);
            else if (q.ownedBefore >= 0.25 && q.ownedAfter < 0.25)
                now += L" " + Colour(L"Rivals could bid for the studio.", html::Bad);
        }
        h += Option(L"Your shares on the share desk", now);
    }
    if (own >= 1.0) {
        h += Option(L"The investors' briefcase", Colour(L"There are no outside investors yet.", html::Dim));
    } else {
        ShareQuote q = QuoteBuyBack(0.05);
        h += Option(L"Their briefcase on the share desk",
                    L"Buy back 5%: " + Colour(L"-" + T(Dollars(q.cash)), q.cash <= capital ? nullptr : html::Bad) +
                        T(Format(" (market price + %.0f%%). You'd own %.0f%%.", BuyPremium() * 100, q.ownedAfter * 100)) +
                        (q.cash <= capital ? L"" : L" " + Colour(L"Not enough cash.", html::Bad)));
        double cost = DividendCost();
        h += Option(L"Their briefcase in the boardroom",
                    L"Pay a dividend: " + Colour(L"-" + T(Dollars(cost)), cost <= capital ? nullptr : html::Bad) +
                        L" (2% of their stake). Raises the board's confidence." +
                        (cost <= capital ? L"" : L" " + Colour(L"Not enough cash.", html::Bad)));
    }
    h += html::EndTable();
    int accountants = Accountants();
    h += html::Note(accountants == 0
                        ? Colour(L"No accountants: hire one at the Accounting Office's hiring desk before you can trade.",
                                 html::Warn)
                        : T(Format("%d accountant%s: fees cut to %.0f%% on a sale, %.0f%% over the market on a buy-back.",
                                   accountants, accountants == 1 ? "" : "s", SaleFee() * 100, BuyPremium() * 100)));
    bool office = game::CountFacilities(kAccountsType) > 0;
    h += html::Note(T(office ? Format("Cash on hand: %s. Pick up the bag or the briefcase at the Accounting Office's door "
                                        "and drop it in the building.", Dollars(capital).c_str())
                               : Format("Cash on hand: %s. Build an Accounting Office (Television buildings) to trade your "
                                        "shares.", Dollars(capital).c_str())));
    return h;
}

void SharesSave(persist::Section& out)
{
    out["owned"] = Format("%.0f", g_sh.owned);
    out["price"] = Format("%.6f", g_sh.price);
    out["mood"] = Format("%.6f", g_sh.marketMood);
    out["confidence"] = Format("%.3f", g_sh.confidence);
    out["review"] = Format("%.3f", g_sh.lastReview);
    out["priceAtReview"] = Format("%.6f", g_sh.priceAtReview);
    out["warned"] = g_sh.warned ? "1" : "0";
    out["ousted"] = g_sh.ousted ? "1" : "0";
    out["fate"] = g_sh.fate;
    out["dividends"] = Format("%.2f", g_sh.dividendsThisQuarter);
    out["raised"] = Format("%.2f", g_sh.raised);
    std::string hist;
    for (double p : g_sh.history)
        hist += Format(hist.empty() ? "%.4f" : "|%.4f", p);
    out["history"] = hist;
}

void SharesReset()
{
    g_fateShown = false;
    g_bag = g_case = 0;
    AccountsReset();   // count the new game's accountants afresh
    g_sh = Shares{};
    g_lastDay = 0;
}

void SharesLoad(const persist::Section& in)
{
    SharesReset();
    auto num = [&](const char* k, double def) {
        auto it = in.find(k);
        return it == in.end() ? def : atof(it->second.c_str());
    };
    g_sh.owned = num("owned", kTotalShares);
    g_sh.price = num("price", 0);
    g_sh.marketMood = num("mood", 1);
    g_sh.confidence = static_cast<float>(num("confidence", 60));
    g_sh.lastReview = static_cast<float>(num("review", 0));
    g_sh.priceAtReview = num("priceAtReview", 0);
    g_sh.warned = num("warned", 0) != 0;
    g_sh.ousted = num("ousted", 0) != 0;
    g_sh.dividendsThisQuarter = num("dividends", 0);
    g_sh.raised = num("raised", 0);
    if (auto it = in.find("fate"); it != in.end())
        g_sh.fate = it->second;
    if (auto it = in.find("history"); it != in.end()) {
        const std::string& s = it->second;
        for (size_t pos = 0; pos < s.size();) {
            size_t bar = s.find('|', pos);
            g_sh.history.push_back(atof(s.substr(pos, bar == std::string::npos ? std::string::npos : bar - pos).c_str()));
            if (bar == std::string::npos)
                break;
            pos = bar + 1;
        }
    }
}

} // namespace tv
