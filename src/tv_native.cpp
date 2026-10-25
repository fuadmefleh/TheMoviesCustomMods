// The Television screen in the game's own interface (nativeui.cpp): a game dialog with
// icon tabs over rich-text pages, opened from a button on the HUD. Decisions that need
// many controls (pitching, the schedule grid, research) still open the overlay screen
// through the "Full controls" button until they are ported.

#include <windows.h>
#include <algorithm>
#include <set>

#include "game.h"
#include "mod.h"
#include "nativeui.h"
#include "overlay.h"
#include "tv_internal.h"

namespace tv {

namespace html {

// Dark inks that read on the dialog's pale cyan panel (its default text is black).
const wchar_t* const Accent = L"#1F4FB0";
const wchar_t* const Dim    = L"#4C6178";
const wchar_t* const Good   = L"#1D7A1D";
const wchar_t* const Warn   = L"#B25F00";
const wchar_t* const Bad    = L"#C01818";

std::wstring Text(const std::string& utf8) { return nativeui::Escape(utf8); }

std::wstring Colour(const std::wstring& text, const wchar_t* colour)
{
    if (!colour)
        return text;
    return std::wstring(L"<font color=") + colour + L">" + text + L"</font>";
}

std::wstring Heading(const std::wstring& text) { return L"<br><h2>" + text + L"</h2><br>"; }

std::wstring Note(const std::wstring& text) { return Colour(text, Dim) + L"<br>"; }

std::wstring Line(const std::wstring& text) { return text + L"<br>"; }

std::wstring EndTable() { return L"</table><br>"; }

// A one-cell table of the right width: the renderer's way to draw a filled box.
std::wstring Bar(double value, double max, const wchar_t* colour, int width)
{
    int w = std::clamp(static_cast<int>(value / (max > 0 ? max : 1) * width + 0.5), 2, width);
    return L"<table width=" + std::to_wstring(w) + L" cellspacing=0><tr><td bgcolor=#FF" + (colour + 1) +
           L" height=10> </td></tr></table>";
}

std::wstring Stats(const std::vector<std::pair<std::wstring, std::wstring>>& cells)
{
    std::wstring h = L"<table width=100% cellspacing=2><tr>";
    for (const auto& [label, value] : cells)
        h += L"<td>" + Colour(label, Dim) + L"<br><b>" + value + L"</b></td>";
    return h + L"</tr></table><hr>";
}

} // namespace html

namespace {

enum NativeTab { NShows, NPilots, NRatings, NNetworks, NBoxOffice, NPress, NTabCount };
const wchar_t* const kTabNames[NTabCount] = { L"Your shows", L"Scripts and pilots", L"Ratings", L"Networks",
                                              L"Box office", L"The press" };

std::wstring W(const std::string& s) { return html::Text(s); }

std::wstring Row(std::initializer_list<std::wstring> cells, bool header = false, const wchar_t* colour = nullptr)
{
    std::wstring r = L"<tr>";
    for (const std::wstring& c : cells)
        r += header ? L"<td><b>" + html::Colour(c, html::Dim) + L"</b></td>" : L"<td>" + html::Colour(c, colour) + L"</td>";
    return r + L"</tr>";
}

std::wstring Bold(const std::wstring& text, bool on = true) { return on ? L"<b>" + text + L"</b>" : text; }

std::wstring Money(double v) { return html::Colour(W(Dollars(v)), v >= 0 ? html::Good : html::Bad); }

// The tab names in a line, the current one highlighted: the icons carry no labels.
std::wstring TabLine(int tab)
{
    std::wstring h;
    for (int t = 0; t < NTabCount; t++) {
        if (t)
            h += html::Colour(L"  |  ", html::Dim);
        h += t == tab ? Bold(html::Colour(kTabNames[t], html::Accent)) : html::Colour(kTabNames[t], html::Dim);
    }
    return html::Line(h);
}

// The business at a glance, over every tab.
std::wstring Summary()
{
    int running = 0, pitched = 0, ready = 0;
    double profit = 0;
    for (const Series& s : g_series) {
        if (s.status == OnAir || s.status == Ordered || s.status == OnHold || s.status == RenewalOffered)
            running++;
        else if (s.status == Pitching || s.status == Rework)
            pitched++;
        profit += s.income - s.cost;
    }
    for (const ShelfEntry& e : ShelfSummary())
        if (e.pilot == 2)
            ready++;
    return html::Stats({ { L"Date", W(DateText(Today())) },
                         { L"Series running", std::to_wstring(running) },
                         { L"With the networks", std::to_wstring(pitched) },
                         { L"Pilots ready", ready ? html::Colour(std::to_wstring(ready), html::Accent) : L"0" },
                         { L"TV profit, all time", Money(profit) },
                         { L"Homes with a TV", W(Format("%.1f million", Households(Year()))) } });
}

const wchar_t* StatusColour(const Series& s)
{
    if (s.scandal)
        return html::Bad;
    if (s.demand)
        return html::Warn;
    switch (s.status) {
    case OnAir: case Ordered:          return html::Good;
    case Rework: case RenewalOffered:  return html::Warn;
    case PassedOn: case Cancelled:     return html::Bad;
    case Ended:                        return html::Dim;
    }
    return nullptr;
}

std::wstring StatusText(const Series& s)
{
    std::wstring t = W(kStatusNames[s.status]);
    if (s.scandal)
        t += L" - scandal!";
    else if (s.demand)
        t += L" - pay demand";
    return html::Colour(t, StatusColour(s));
}

std::wstring ShowsPage()
{
    std::wstring h = html::Heading(L"Your shows");
    int listed = 0;
    std::wstring table = L"<table width=100% cellspacing=1>" +
                         Row({ L"Show", L"Network", L"Format", L"Season", L"Status", L"Rating", L"Share", L"Freshness",
                               L"Profit" }, true);
    for (const Series& s : g_series) {
        if (s.status == Cancelled || s.status == Ended || s.status == PassedOn)
            continue;
        listed++;
        const wchar_t* fresh = s.freshness < 0.3f ? html::Bad : s.freshness < 0.5f ? html::Warn : nullptr;
        table += Row({ Bold(W(s.title)), W(NetworkShort(s.network)), W(kFormats[s.format].name),
                       s.season ? W(Format("%d  (%d of %d)", s.season, s.aired, s.order)) : L"-", StatusText(s),
                       s.lastRating > 0 ? W(Format("%.1f", s.lastRating)) : L"-",
                       s.lastShare > 0 ? W(Format("%.0f%%", s.lastShare * 100)) : L"-",
                       html::Colour(W(Format("%.0f%%", s.freshness * 100)), fresh),
                       s.totalAired ? Money(s.income - s.cost) : L"-" });
    }
    table += html::EndTable();
    if (!listed)
        h += html::Line(L"No shows in development or on the air.") +
             html::Note(L"Put writers on the desks of a TV Writers' Office, shoot a pilot on a soundstage and pitch it - "
                        L"or use Full controls to pitch one of your released films to a network.");
    else
        h += table;

    int ended = 0;
    std::wstring past = L"<table width=100% cellspacing=1>" +
                        Row({ L"Show", L"Network", L"Seasons", L"Episodes", L"Best rating", L"Profit" }, true);
    for (const Series& s : g_series)
        if (s.status == Cancelled || s.status == Ended) {
            ended++;
            past += Row({ W(s.title), W(NetworkShort(s.network)), std::to_wstring(s.season), std::to_wstring(s.totalAired),
                          W(Format("%.1f", s.bestRating)), Money(s.income - s.cost) });
        }
    if (ended)
        h += html::Heading(L"Finished") + past + html::EndTable();
    return h;
}

std::wstring PilotsPage()
{
    std::vector<ShelfEntry> shelf = ShelfSummary();
    std::wstring h = html::Heading(L"Scripts and pilots");
    if (shelf.empty())
        return h + html::Line(L"No scripts waiting.") +
               html::Note(L"Put writers on the desks of a TV Writers' Office: finished scripts come out of its door.");
    std::sort(shelf.begin(), shelf.end(), [](const ShelfEntry& a, const ShelfEntry& b) {
        return a.pilot != b.pilot ? a.pilot > b.pilot : a.quality > b.quality;
    });
    h += L"<table width=100% cellspacing=1>" + Row({ L"Title", L"Format", L"Script", L"Pilot" }, true);
    for (const ShelfEntry& e : shelf) {
        std::wstring state;
        if (e.pilot == 2)
            state = html::Colour(W(Format("Ready: %.0f%%, starring %s", e.pilotQuality * 100, e.lead.c_str())), html::Accent);
        else if (e.pilot == 1 && e.wrapDay == 0)
            state = html::Colour(L"Shooting - drop a star on the soundstage", html::Warn);
        else if (e.pilot == 1)
            state = W(Format("Shooting with %s, wraps %s", e.lead.c_str(), DateText(e.wrapDay).c_str()));
        else
            state = html::Colour(L"Script only", html::Dim);
        const wchar_t* script = e.quality >= 0.7f ? html::Good : e.quality < 0.4f ? html::Warn : nullptr;
        h += Row({ Bold(W(e.title)), W(kFormats[e.format].name), html::Colour(W(Format("%.0f%%", e.quality * 100)), script),
                   state });
    }
    h += html::EndTable();
    int free = FreeSoundstages();
    h += html::Note((free > 0 ? W(Format("%d soundstage%s free.", free, free == 1 ? "" : "s"))
                              : std::wstring(L"No soundstage free: every one is shooting, or none is built.")) +
                    L" The camera button shoots a pilot from the best waiting script; the tick pitches the best "
                    L"finished pilot to the network likeliest to buy it. Full controls pick the network, budget and deal.");
    return h;
}

std::wstring RatingsPage()
{
    if (g_lastWeek.empty())
        return html::Heading(L"Ratings") + html::Note(L"The first week's ratings arrive after Sunday night.");
    float top = 0;
    for (const ChartEntry& c : g_lastWeek)
        top = (std::max)(top, c.rating);
    std::wstring h = html::Heading(L"Last week's top shows");
    h += L"<table width=100% cellspacing=1>" + Row({ L"#", L"Show", L"Network", L"Rating", L"Share", L"" }, true);
    for (int i = 0; i < static_cast<int>(g_lastWeek.size()) && i < 15; i++) {
        const ChartEntry& c = g_lastWeek[i];
        h += Row({ std::to_wstring(i + 1), Bold(W(c.title), c.player), W(NetworkShort(c.network)),
                   W(Format("%.1f", c.rating)), W(Format("%.0f%%", c.share * 100)),
                   html::Bar(c.rating, top, c.player ? html::Accent : html::Dim) },
                 false, c.player ? html::Accent : nullptr);
    }
    h += html::EndTable();
    for (int i = 15; i < static_cast<int>(g_lastWeek.size()); i++)
        if (g_lastWeek[i].player)
            h += html::Line(html::Colour(L"#" + std::to_wstring(i + 1) + L"  " + W(g_lastWeek[i].title) + L"  " +
                                             W(Format("%.1f", g_lastWeek[i].rating)), html::Accent));
    return h + html::Note(L"Your shows in blue.");
}

std::wstring NetworksPage()
{
    float sum[kNetworkSlots] = {};
    int count[kNetworkSlots] = {};
    for (const ChartEntry& c : g_lastWeek)
        if (c.network >= 0 && c.network < kNetworkSlots) {
            sum[c.network] += c.share;
            count[c.network]++;
        }
    std::wstring h = html::Heading(L"The networks");
    h += L"<table width=100% cellspacing=1>" + Row({ L"Network", L"Shows last week", L"Average share", L"" }, true);
    int listed = 0;
    for (int n = 0; n < kNetworkSlots; n++) {
        if (!count[n])
            continue;
        listed++;
        float avg = sum[n] / count[n];
        bool own = OwnOutlet(n);
        h += Row({ Bold(W(NetworkName(n)), own) + (own ? L" (yours)" : L""), std::to_wstring(count[n]),
                   W(Format("%.0f%%", avg * 100)), html::Bar(avg, 0.5f, own ? html::Accent : html::Dim) },
                 false, own ? html::Accent : nullptr);
    }
    h += html::EndTable();
    if (!listed)
        h += html::Note(L"The networks' shares arrive with the first week's ratings.");
    return h;
}

std::wstring BoxOfficePage()
{
    std::vector<game::Project> films = game::RivalFilms();
    for (const std::vector<game::Project>& list : { game::PlayerBackCatalogue(), game::PlayerProductions() })
        for (const game::Project& p : list)
            if (p.stage >= 7 && std::none_of(films.begin(), films.end(), [&](const game::Project& f) { return f.address == p.address; }))
                films.push_back(p);
    auto gross = [](const game::Project& p) { return p.hasIncome ? p.incomeInitial - p.incomeRemaining : 0.0; };
    float today = Today();
    std::vector<const game::Project*> year;
    for (const game::Project& p : films)
        if (p.releaseDate > 0 && today - p.releaseDate <= 365)
            year.push_back(&p);
    std::sort(year.begin(), year.end(), [&](auto a, auto b) { return gross(*a) > gross(*b); });

    std::wstring h = html::Heading(L"This year's biggest films");
    if (year.empty())
        return h + html::Note(L"No films released in the last twelve months.");
    h += L"<table width=100% cellspacing=1>" + Row({ L"#", L"Film", L"Studio", L"Stars", L"Box office" }, true);
    for (size_t i = 0; i < year.size() && i < 15; i++) {
        const game::Project& p = *year[i];
        h += Row({ std::to_wstring(i + 1), Bold(W(Utf8(p.title)), !p.rival), p.rival ? W(Utf8(p.studio)) : L"Your studio",
                   p.starRating >= 0 ? W(Format("%.1f", p.starRating * 5)) : L"-", W(Dollars(gross(p))) },
                 false, p.rival ? nullptr : html::Accent);
    }
    return h + html::EndTable() + html::Note(L"Your films in blue. Stars out of five, from the critics.");
}

std::wstring PressPage()
{
    if (g_papers.empty())
        return html::Heading(L"The press") +
               html::Note(L"No front pages yet: they come with your shows' season finales, the awards and big milestones.");
    const Newspaper& p = g_papers.back();
    std::wstring h = L"<p align=center>" + html::Colour(W(p.paper) + L"  -  " + W(DateText(p.day)), html::Dim) + L"</p><br>";
    if (!p.kicker.empty())
        h += L"<p align=center>" + W(p.kicker) + L"</p><br>";
    h += L"<h1>" + W(p.headline) + L"</h1><br>";
    if (!p.subhead.empty())
        h += L"<p align=center><b>" + W(p.subhead) + L"</b></p><br>";
    std::wstring body = W(p.body);
    for (size_t at; (at = body.find(L'\n')) != std::wstring::npos;)
        body.replace(at, 1, L"<br>");
    h += L"<hr>" + html::Line(body);
    if (!p.verdict.empty())
        h += html::Line(L"<b>" + W(p.verdict) + L"</b>");
    if (!p.quote.empty())
        h += html::Note(W(p.quote));
    if (g_papers.size() > 1)
        h += html::Note(std::to_wstring(g_papers.size() - 1) + L" earlier editions in Full controls, Press.");
    return h;
}

std::wstring Page(int tab)
{
    if (!Unlocked())
        return html::Heading(L"Television") + html::Line(L"Television arrives in your world in 1949.") +
               html::Note(L"Build a TV Production Office to start making shows.");
    std::wstring h = TabLine(tab) + Summary();
    if (tab >= 0 && tab < NTabCount)   // one picture of each tab per session, for checking the layout
        nativeui::ShotOnce(Utf8(std::wstring(L"native_tv_") + kTabNames[tab]).c_str(), 250);
    switch (tab) {
    case NShows:     return h + ShowsPage();
    case NPilots:    return h + PilotsPage();
    case NRatings:   return h + RatingsPage();
    case NNetworks:  return h + NetworksPage();
    case NBoxOffice: return h + BoxOfficePage();
    case NPress:     return h + PressPage();
    }
    return h;
}

// The HUD buttons share one dialog: each toggles only its own screen and replaces the other.
enum Which { WhichTv, WhichShares };
Which g_which = WhichTv;

bool Showing(Which w) { return nativeui::IsOpen() && g_which == w; }

void ShootNextPilot()
{
    AutoPilots(false);
    nativeui::Refresh();
}

void PitchNextPilot()
{
    if (PitchBestPilot().empty())
        Notify("No pilot to pitch", "Shoot a pilot first: drop a TV script on a TV building, then a star on the soundstage.",
               kYellow);
    nativeui::Refresh();
}

void OpenFullControls()
{
    nativeui::Close();
    overlay::Open();
}

void OpenNative()
{
    if (Showing(WhichTv)) {
        nativeui::Close();
        return;
    }
    nativeui::Screen s;
    s.title = L"Television";
    // Flat icons for the tabs, round ones for the actions (after a gap).
    s.buttons = {
        { "button_starbig.",  L"Your shows",            NShows,     nullptr },
        { "button_scripts.",  L"Scripts and pilots",    NPilots,    nullptr },
        { "button_view.",     L"Ratings",               NRatings,   nullptr },
        { "button_online.",   L"Networks",              NNetworks,  nullptr },
        { "button_trl.",      L"Box office",            NBoxOffice, nullptr },
        { "button_reviews2.", L"The press",             NPress,     nullptr },
        { "button_shooting.", L"Shoot a pilot from the best waiting script", -1, &ShootNextPilot },
        { "button_ok.",       L"Pitch the best finished pilot",           -1, &PitchNextPilot },
        { "button_sliders.",  L"Full controls: pitch, schedule, research", -1, &OpenFullControls },
    };
    s.html = &Page;
    s.width = 900;
    s.height = 680;
    nativeui::Open(s, NShows);
    g_which = WhichTv;
}

// ---------------------------------------------------------------------------------
// Decisions as the game's own yes/no dialog: scandals, stars' pay demands and renewals,
// one at a time, each asked once (the overlay keeps every option, e.g. ending a series).

std::set<std::string> g_asked;
bool  g_promptOpen = false;
DWORD g_promptSince = 0;
DWORD g_lastPromptCheck = 0;

void Answered() { g_promptOpen = false; }

void Ask(const std::string& key, const std::wstring& title, const std::wstring& body, std::function<void()> yes,
         std::function<void()> no)
{
    g_asked.insert(key);
    g_promptOpen = true;
    g_promptSince = GetTickCount();
    nativeui::Confirm(title, body,
                      [yes] { Answered(); if (yes) yes(); },
                      [no] { Answered(); if (no) no(); });
}

std::wstring Small(const std::string& text)
{
    return L"<br><br>" + html::Colour(W(text), html::Dim);
}

void Prompts()
{
    if (GetTickCount() - g_lastPromptCheck < 1000)
        return;
    g_lastPromptCheck = GetTickCount();
    if (g_promptOpen && GetTickCount() - g_promptSince > 120000)
        g_promptOpen = false;   // the dialog was replaced by another one: move on
    if (g_promptOpen || overlay::IsOpen() || !nativeui::Ready() || !Unlocked())
        return;

    for (Series& s : g_series) {
        int id = s.id;
        std::string base = std::to_string(id) + ":" + std::to_string(s.season) + ":";
        if (s.scandal && !s.lead.empty() && !g_asked.count(base + "scandal")) {
            Ask(base + "scandal", L"Scandal!",
                L"<b>" + W(s.lead) + L"</b>, the star of <b>" + W(s.title) + L"</b>, is caught up in a scandal." +
                    Small("Tick: stand by them. Cross: let them go."),
                [id] { if (Series* x = FindSeries(id); x && x->scandal) ResolveScandal(*x, false); },
                [id] { if (Series* x = FindSeries(id); x && x->scandal) ResolveScandal(*x, true); });
            return;
        }
        if (s.demand && !s.lead.empty() && !g_asked.count(base + "demand")) {
            double now = LeadPay1950(s) * EraFactor(Year()) * g_costScale;
            Ask(base + "demand", L"A pay demand",
                L"<b>" + W(s.lead) + L"</b> wants a " + std::to_wstring(s.demand) + L"% raise to keep starring in <b>" +
                    W(s.title) + L"</b>: " + W(Dollars(now * s.demand / 100.0)) + L" more per episode." +
                    Small("Tick: pay it. Cross: refuse - the star may walk out until you recast the role."),
                [id] { if (Series* x = FindSeries(id); x && x->demand) ResolveDemand(*x, true); },
                [id] { if (Series* x = FindSeries(id); x && x->demand) ResolveDemand(*x, false); });
            return;
        }
        if (s.status == RenewalOffered && !g_asked.count(base + "renew")) {
            std::wstring offer = OwnOutlet(s.network)
                                     ? L"Order season " + std::to_wstring(s.season + 1) + L" of <b>" + W(s.title) +
                                           L"</b> (" + std::to_wstring(s.order) + L" episodes)?"
                                     : W(NetworkName(s.network)) + L" offers <b>" + W(s.title) + L"</b> a season " +
                                           std::to_wstring(s.season + 1) + L": " + std::to_wstring(s.order) + L" episodes.";
            Ask(base + "renew", L"Renewal", offer +
                    Small(Format("Last season averaged a %.1f rating; the show is %.0f%% fresh. Tick: renew. Cross: not "
                                 "now (end it or ask for more money in Full controls).",
                                 s.aired ? s.ratingSum / s.aired : s.lastRating, s.freshness * 100)),
                [id] { if (Series* x = FindSeries(id); x && x->status == RenewalOffered) RenewSeries(*x); },
                nullptr);
            return;
        }
    }
}

// --- Studio shares ---------------------------------------------------------------

void OpenShares();

void Report(const std::string& error)
{
    if (!error.empty())
        Notify("Studio shares", error, kYellow);
    nativeui::Refresh();
}

void AskSell(double fraction)
{
    ShareQuote q = QuoteSell(fraction);
    if (q.shares < 1) {
        Report(SellShares(fraction));   // says why not
        return;
    }
    std::string risk = q.ownedBefore >= 0.5 && q.ownedAfter < 0.5     ? " You'd lose the majority: the board could vote you out."
                       : q.ownedBefore >= 0.25 && q.ownedAfter < 0.25 ? " Below 25%, rival studios can bid to take the company over."
                                                                      : "";
    nativeui::Confirm(L"Sell shares",
                      W(Format("Sell %.0f%% of your studio for about %s? You'd own %.0f%%.%s", q.shares / 10000.0,
                               Dollars(q.cash).c_str(), q.ownedAfter * 100, risk.c_str())),
                      [fraction] { Report(SellShares(fraction)); });
}

void OpenShares()
{
    if (Showing(WhichShares)) {
        nativeui::Close();
        return;
    }
    nativeui::Screen s;
    s.title = L"Studio shares";
    // In the order of the page's "Your options" table.
    s.buttons = {
        { "button_minus.",     L"Sell 5% of the studio",              -1, [] { AskSell(0.05); } },
        { "button_reduce.",    L"Sell 10% of the studio",             -1, [] { AskSell(0.10); } },
        { "button_plus.",      L"Buy back 5% (market price + 5%)",    -1, [] { Report(BuyBackShares(0.05)); } },
        { "button_marketing.", L"Pay a dividend to the shareholders", -1, [] { Report(PayDividend()); } },
    };
    s.html = [](int) {
        nativeui::ShotOnce("native_shares", 250);
        return SharesPage();
    };
    s.width = 760;
    s.height = 680;
    nativeui::Open(s, 0);
    g_which = WhichShares;
}

bool OnToggleKey()
{
    if (!nativeui::Ready())
        return false;   // no game running: the overlay's own screens
    OpenNative();       // opens, or closes it if it is open
    return true;
}

} // namespace

void InstallNative()
{
    nativeui::AddHudButton("button_video.", L"Television", &OpenNative);
    nativeui::AddHudButton("button_finance.", L"Studio shares", &OpenShares);
    if (nativeui::Enabled())
        overlay::SetToggleHandler(&OnToggleKey);
    overlay::AddFrameCallback(&Prompts);
}

} // namespace tv
