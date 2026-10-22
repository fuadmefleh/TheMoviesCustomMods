// The Television screen in the game's own interface (nativeui.cpp): a game dialog with
// icon tabs over rich-text pages, opened from a button on the HUD. Decisions that need
// many controls (pitching, the schedule grid, research) still open the overlay screen
// through the "Full controls" button until they are ported.

#include <windows.h>
#include <algorithm>

#include "game.h"
#include "mod.h"
#include "nativeui.h"
#include "overlay.h"
#include "tv_internal.h"

namespace tv {
namespace {

enum NativeTab { NShows, NRatings, NNetworks, NBoxOffice, NPress, NTabCount };

const wchar_t* kGold = L"#F9E81E";
const wchar_t* kDim  = L"#9FB4D8";

std::wstring W(const std::string& s) { return nativeui::Escape(s); }

std::wstring Heading(const std::wstring& text) { return L"<h2>" + text + L"</h2>"; }

std::wstring Row(std::initializer_list<std::wstring> cells, bool header = false, bool gold = false)
{
    std::wstring r = L"<tr>";
    for (const std::wstring& c : cells) {
        r += header ? L"<td><b>" : L"<td>";
        if (gold)
            r += std::wstring(L"<font color=") + kGold + L">";
        r += c;
        if (gold)
            r += L"</font>";
        r += header ? L"</b></td>" : L"</td>";
    }
    return r + L"</tr>";
}

// A rating as a coloured bar: a one-cell table of the right width.
std::wstring Bar(float value, float max, const wchar_t* colour)
{
    int width = std::clamp(static_cast<int>(value / max * 160 + 0.5f), 2, 160);
    std::wstring argb = std::wstring(L"#FF") + (colour + 1);
    return L"<table width=" + std::to_wstring(width) + L" cellspacing=0><tr><td bgcolor=" + argb +
           L" height=10> </td></tr></table>";
}

std::wstring ShowsPage()
{
    std::wstring h = Heading(L"Your shows");
    int listed = 0;
    std::wstring table = L"<table width=100% cellspacing=1>" +
                         Row({ L"Show", L"Network", L"Format", L"Season", L"Status", L"Last rating", L"Share", L"Freshness" }, true);
    for (const Series& s : g_series) {
        if (s.status == Cancelled || s.status == Ended || s.status == PassedOn)
            continue;
        listed++;
        table += Row({ W(s.title), W(NetworkShort(s.network)), W(kFormats[s.format].name),
                       s.season ? std::to_wstring(s.season) : L"-", W(kStatusNames[s.status]),
                       s.lastRating > 0 ? W(Format("%.1f", s.lastRating)) : L"-",
                       s.lastShare > 0 ? W(Format("%.0f%%", s.lastShare * 100)) : L"-",
                       W(Format("%.0f%%", s.freshness * 100)) },
                     false, s.status == OnAir);
    }
    table += L"</table>";
    if (!listed)
        h += L"<p>No shows in development or on the air. Use <b>Full controls</b> to pitch one of your films to a network.</p>";
    else
        h += table;

    int ended = 0;
    std::wstring past = L"<table width=100% cellspacing=1>" + Row({ L"Show", L"Network", L"Seasons", L"Episodes", L"Best rating" }, true);
    for (const Series& s : g_series)
        if (s.status == Cancelled || s.status == Ended) {
            ended++;
            past += Row({ W(s.title), W(NetworkShort(s.network)), std::to_wstring(s.season), std::to_wstring(s.totalAired),
                          W(Format("%.1f", s.bestRating)) });
        }
    if (ended)
        h += Heading(L"Finished") + past + L"</table>";
    return h;
}

std::wstring RatingsPage()
{
    if (g_lastWeek.empty())
        return Heading(L"Ratings") + L"<p>The first week's ratings arrive after Sunday night.</p>";
    float top = 0;
    for (const ChartEntry& c : g_lastWeek)
        top = (std::max)(top, c.rating);
    std::wstring h = Heading(L"Last week's top shows");
    h += L"<table width=100% cellspacing=1>" + Row({ L"#", L"Show", L"Network", L"Rating", L"" }, true);
    for (int i = 0; i < static_cast<int>(g_lastWeek.size()) && i < 15; i++) {
        const ChartEntry& c = g_lastWeek[i];
        h += Row({ std::to_wstring(i + 1), W(c.title), W(NetworkShort(c.network)), W(Format("%.1f", c.rating)),
                   Bar(c.rating, top > 0 ? top : 1, c.player ? kGold : kDim) },
                 false, c.player);
    }
    h += L"</table>";
    for (int i = 15; i < static_cast<int>(g_lastWeek.size()); i++)
        if (g_lastWeek[i].player)
            h += std::wstring(L"<p><font color=") + kGold + L">#" + std::to_wstring(i + 1) + L"  " + W(g_lastWeek[i].title) +
                 L"  " + W(Format("%.1f", g_lastWeek[i].rating)) + L"</font></p>";
    return h;
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
    std::wstring h = Heading(L"The networks");
    h += L"<table width=100% cellspacing=1>" + Row({ L"Network", L"Shows last week", L"Average share", L"" }, true);
    for (int n = 0; n < kNetworkSlots; n++) {
        if (!count[n])
            continue;
        float avg = sum[n] / count[n];
        h += Row({ W(NetworkName(n)), std::to_wstring(count[n]), W(Format("%.0f%%", avg * 100)),
                   Bar(avg, 0.5f, OwnOutlet(n) ? kGold : kDim) },
                 false, OwnOutlet(n));
    }
    return h + L"</table>";
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

    std::wstring h = Heading(L"This year's biggest films");
    if (year.empty())
        return h + L"<p>No films released in the last twelve months.</p>";
    h += L"<table width=100% cellspacing=1>" + Row({ L"#", L"Film", L"Studio", L"Stars", L"Box office" }, true);
    for (size_t i = 0; i < year.size() && i < 15; i++) {
        const game::Project& p = *year[i];
        h += Row({ std::to_wstring(i + 1), W(Utf8(p.title)), p.rival ? W(Utf8(p.studio)) : L"Your studio",
                   p.starRating >= 0 ? W(Format("%.1f", p.starRating * 5)) : L"-", W(Dollars(gross(p))) },
                 false, !p.rival);
    }
    return h + L"</table>";
}

std::wstring PressPage()
{
    if (g_papers.empty())
        return Heading(L"The press") + L"<p>No front pages yet: they come with your shows' season finales, the awards and big milestones.</p>";
    const Newspaper& p = g_papers.back();
    std::wstring h = std::wstring(L"<p align=center><font color=") + kDim + L">" + W(p.paper) + L"  -  " +
                     W(DateText(p.day)) + L"</font></p>";
    if (!p.kicker.empty())
        h += L"<p align=center>" + W(p.kicker) + L"</p>";
    h += L"<h1>" + W(p.headline) + L"</h1>";
    if (!p.subhead.empty())
        h += L"<p align=center><b>" + W(p.subhead) + L"</b></p>";
    std::wstring body = W(p.body);
    for (size_t at; (at = body.find(L'\n')) != std::wstring::npos;)
        body.replace(at, 1, L"<br>");
    h += L"<hr><p>" + body + L"</p>";
    if (!p.verdict.empty())
        h += L"<p><b>" + W(p.verdict) + L"</b></p>";
    if (!p.quote.empty())
        h += std::wstring(L"<p><font color=") + kDim + L">" + W(p.quote) + L"</font></p>";
    if (g_papers.size() > 1)
        h += std::wstring(L"<p><font color=") + kDim + L">" + std::to_wstring(g_papers.size() - 1) +
             L" earlier editions in Full controls, Press.</font></p>";
    return h;
}

std::wstring Page(int tab)
{
    if (!Unlocked())
        return Heading(L"Television") +
               L"<p>Television arrives in your world in 1949. Build a TV Production Office to start making shows.</p>";
    switch (tab) {
    case NShows:     return ShowsPage();
    case NRatings:   return RatingsPage();
    case NNetworks:  return NetworksPage();
    case NBoxOffice: return BoxOfficePage();
    case NPress:     return PressPage();
    }
    return L"";
}

void OpenFullControls()
{
    nativeui::Close();
    overlay::Open();
}

void OpenNative()
{
    if (nativeui::IsOpen()) {
        nativeui::Close();
        return;
    }
    nativeui::Screen s;
    s.title = L"Television";
    s.buttons = {
        { "button_star.",    L"Your shows",            NShows,     nullptr },
        { "button_interest.", L"Ratings",              NRatings,   nullptr },
        { "button_marketing.", L"Networks",            NNetworks,  nullptr },
        { "button_finance.", L"Box office",            NBoxOffice, nullptr },
        { "button_reviews.", L"The press",             NPress,     nullptr },
        { "button_sliders.", L"Full controls: pitch, schedule, research", -1, &OpenFullControls },
    };
    s.html = &Page;
    nativeui::Open(s, NShows);
}

} // namespace

void InstallNative()
{
    nativeui::AddHudButton("button_video.", L"Television", &OpenNative);
}

} // namespace tv
