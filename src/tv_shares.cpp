// Studio shares: sell part of the studio to fund it, and risk losing it.
//
// The studio is 1,000,000 shares, all the player's at first. Selling blocks raises cash at
// the market price; the price follows what the studio is worth (cash, the TV business, the
// films still earning, the studio's standing and its lot) with momentum, noise and the
// era's crashes. Once outsiders own part of the studio a board judges it every quarter:
// share price, the critics, debt, dividends. Below 50% the board can vote the player out;
// below 25% rival studios can bid to take the company over. Either way the game is over for
// that studio. The mod's HUD button "Studio shares" opens the screen.

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
#include "tv_internal.h"

namespace tv {
namespace {

constexpr double kTotalShares = 1000000;
constexpr double kMinOwned = 0.10;          // the player can't sell below 10%
constexpr float  kQuarter = 91.25f;
constexpr double kSaleFee = 0.07, kBuyPremium = 0.05;

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

    std::string mood = g_sh.confidence > 70 ? "delighted" : g_sh.confidence > 45 ? "content" : g_sh.confidence > 25 ? "restless" : "furious";
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
    if (Ownership() < 1.0 && day - g_sh.lastReview >= kQuarter)
        QuarterlyReview(day);
    else if (Ownership() >= 1.0)
        g_sh.lastReview = day;
}

bool SharesOusted() { return g_sh.ousted; }
bool SharesWarned() { return g_sh.warned; }



double SharePrice() { return g_sh.price; }

// Sell 'fraction' of the whole studio (0.05 = 5%).
std::string SellShares(double fraction)
{
    if (g_sh.ousted)
        return "The studio is no longer yours.";
    double n = std::min(fraction * kTotalShares, g_sh.owned - kMinOwned * kTotalShares);
    if (n < 1 || g_sh.price <= 0)
        return Format("You can't sell below %.0f%% of the studio.", kMinOwned * 100);
    double cash = n * g_sh.price * (1.0 - kSaleFee);
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
    double n = std::min(fraction * kTotalShares, kTotalShares - g_sh.owned);
    if (n < 1)
        return "You already own the whole studio.";
    double cost = n * g_sh.price * (1.0 + kBuyPremium);
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
    double cost = g_sh.price * kTotalShares * OutsideFraction() * 0.02;
    if (game::ReadMoney().capital < cost)
        return Format("A dividend costs %s.", Dollars(cost).c_str());
    game::AddMoney(-cost);
    g_sh.dividendsThisQuarter += cost;
    g_sh.confidence = std::min(100.0f, g_sh.confidence + 4);
    Notify("Dividend paid", Format("%s to the shareholders. The board approves.", Dollars(cost).c_str()), kGreen);
    return "";
}

std::wstring SharesPage()
{
    std::wstring h = L"<h2>Studio shares</h2>";
    if (g_sh.ousted)
        return h + L"<p><font color=#FF8080><b>You have lost the studio.</b></font></p><p>" + nativeui::Escape(g_sh.fate) + L"</p>";
    double cap = g_sh.price * kTotalShares;
    h += L"<table width=100% cellspacing=1>";
    auto row = [&](const std::wstring& k, const std::wstring& v) { h += L"<tr><td>" + k + L"</td><td><b>" + v + L"</b></td></tr>"; };
    row(L"Share price", nativeui::Escape(Dollars(g_sh.price)));
    row(L"Studio market value", nativeui::Escape(Dollars(cap)));
    row(L"You own", nativeui::Escape(Format("%.0f%% (%.0f shares)", Ownership() * 100, g_sh.owned)));
    row(L"Raised by selling", nativeui::Escape(Dollars(g_sh.raised)));
    if (Ownership() < 1.0) {
        const char* mood = g_sh.confidence > 70 ? "delighted" : g_sh.confidence > 45 ? "content" : g_sh.confidence > 25 ? "restless" : "furious";
        row(L"The board", nativeui::Escape(Format("%s (confidence %.0f/100)", mood, g_sh.confidence)));
    }
    h += L"</table>";
    if (Ownership() < 0.5)
        h += L"<p><font color=#FFB060><b>You no longer hold the majority:</b> if the board loses confidence it can vote you out.</font></p>";
    if (Ownership() < 0.25)
        h += L"<p><font color=#FF8080>Rival studios may bid to take the company over.</font></p>";
    if (g_sh.warned)
        h += L"<p><font color=#FF6060><b>The board will vote you out at the next review unless confidence recovers.</b></font></p>";
    if (!g_sh.history.empty()) {
        h += L"<h2>Share price by quarter</h2><table cellspacing=2><tr>";
        double top = *std::max_element(g_sh.history.begin(), g_sh.history.end());
        for (double p : g_sh.history) {
            int height = std::max(2, static_cast<int>(p / std::max(top, 0.01) * 80));
            h += L"<td valign=bottom><table width=10 cellspacing=0><tr><td bgcolor=#FFF9E81E height=" + std::to_wstring(height) +
                 L"> </td></tr></table></td>";
        }
        h += L"</tr></table>";
    }
    h += L"<p><font color=#9FB4D8>Sell 5% or 10%: cash now at the market price (less 7% fees). Buy back 5%: at the "
         L"market price plus 5%. Dividend: 2% of the outside shareholders' value, which pleases the board.</font></p>";
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
