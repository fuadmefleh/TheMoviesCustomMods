// Toon Studio: the vault. Animated films don't age, so they can go back into cinemas.
//
// kYears after a cartoon's release (or its last re-release) it can come out of the vault. A
// film canister, the vault, then waits in front of the Animation Studio (a script object
// like the shares bag, with the game's finished-film model; found again by title after a
// load, and left out once it has appeared). Drop it anywhere on the Animation Studio and the
// cartoon that would earn the most goes back into cinemas.
//
// A re-release earns a share of the film's first box office (kShare, less each time it comes
// back), at today's prices, more with a well-known mascot, paid as the run goes on: most in the
// first weeks, all of it within kRunDays. One re-release at a time. Each one also counts for
// the mascot, half as much as a new cartoon.

#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

#include "mod.h"
#include "overlay.h"
#include "persist.h"
#include "rooms.h"
#include "tv_internal.h"

namespace tv {
namespace {

const wchar_t* const kVaultTitle = L"The Vault";
constexpr float kYears = 7.0f;
constexpr float kShare = 0.45f, kAgain = 0.85f;   // of the first box office; kept each time it's back
constexpr float kRunDays = 60.0f;
constexpr float kDailyShare = 0.05f;   // of what's left of the run's money, each day
constexpr float kMascotWeight = 0.5f;
constexpr double kFallback1950 = 1500000;   // a first box office we can't read, for a 100% film

struct Vaulted {
    int   reissues = 0;
    float last = 0;   // day of the last re-release, 0 = never
    bool  noticed = false;   // "it can come out of the vault" was shown for this window
};
std::map<std::string, Vaulted> g_films;   // by title

struct Run {
    std::string title;   // "" = none
    double total = 0, paid = 0;
    float  start = 0;
};
Run g_run;
uintptr_t g_vault = 0;
bool g_vaultEver = false;   // the vault has been put out (it stays)
float g_lastDay = -1;

// A cartoon in the back catalogue, and what it would earn now.
struct Candidate {
    std::string title;
    float       due = 0;   // day it can come out
    double      take = 0;
    float       quality = 0;
};

double Take(const game::Project& p, int reissues)
{
    float released = p.releaseDate / kDaysPerYear, now = Year();
    double first = p.incomeInitial > 0 ? p.incomeInitial
                                       : kFallback1950 * std::max(0.1f, p.quality) * EraFactor(released);
    double prices = EraFactor(now) / std::max(0.01f, EraFactor(released));
    return first * prices * kShare * std::pow(kAgain, reissues) * (1 + 0.3 * MascotAppeal());
}

std::vector<Candidate> Cartoons()
{
    std::vector<Candidate> out;
    for (const game::Project& p : game::PlayerBackCatalogue()) {
        if (p.releaseDate <= 0 || !IsFinishedCartoon(p.title))
            continue;
        Candidate c;
        c.title = Utf8(p.title);
        const Vaulted& v = g_films[c.title];
        c.due = std::max(p.releaseDate, v.last) + kYears * kDaysPerYear;
        c.take = Take(p, v.reissues);
        c.quality = p.quality;
        out.push_back(c);
    }
    return out;
}

// The cartoon to re-release now: the one that would earn the most, of those due.
bool BestDue(Candidate* best)
{
    bool found = false;
    float today = Today();
    for (const Candidate& c : Cartoons())
        if (c.due <= today && c.title != g_run.title && (!found || c.take > best->take)) {
            *best = c;
            found = true;
        }
    return found;
}

void PlaceVault()
{
    g_vault = 0;
    for (const game::Project& p : game::PlayerProductions())
        if (p.stage < 7 && p.title == kVaultTitle)
            g_vault = p.address;
    rooms::RefreshScriptLook(g_vault);
    if (g_vault || !g_vaultEver)
        return;
    for (const game::Facility& f : game::Facilities()) {
        if (f.type != kAnimationStudioType)
            continue;
        float pos[3], angle;
        if (!game::FacilityPlacement(f.address, pos, &angle))
            continue;
        float fx = std::sin(angle), fy = -std::cos(angle);   // in front of the door (models face -y)
        g_vault = game::SpawnScript(kVaultTitle, pos[0] + fx * 12.0f + fy * 3.0f, pos[1] + fy * 12.0f - fx * 3.0f);
        mod::Log("tv: the vault put out at the Animation Studio (%08X)", static_cast<unsigned>(g_vault));
        rooms::RefreshScriptLook(g_vault);
        return;
    }
}

void Pay(float days)
{
    if (g_run.title.empty())
        return;
    double left = g_run.total - g_run.paid;
    bool over = Today() - g_run.start >= kRunDays;
    double pay = over ? left : left * (1 - std::pow(1 - kDailyShare, days));
    if (pay >= 1 && game::AddMoney(pay))
        g_run.paid += pay;
    if (!over)
        return;
    mod::Log("tv: re-release of '%s' over - %.0f earned", g_run.title.c_str(), g_run.paid);
    Notify(Format("\"%s\" goes back in the vault", g_run.title.c_str()),
           Format("Its re-release earned %s. It can come out again in %.0f years.", Dollars(g_run.paid).c_str(), kYears),
           kGreen);
    g_run = Run{};
}

void Daily(float days)
{
    Pay(days);
    float today = Today();
    for (const Candidate& c : Cartoons()) {
        Vaulted& v = g_films[c.title];
        if (c.due > today || v.noticed || c.title == g_run.title)
            continue;
        v.noticed = true;
        g_vaultEver = true;
        mod::Log("tv: '%s' can come out of the vault (about %.0f)", c.title.c_str(), c.take);
        Notify(Format("\"%s\" can come out of the vault", c.title.c_str()),
               Format("Cartoons don't age: a new generation is waiting for it. Drop the vault (the film can in front of "
                      "the Animation Studio) on the studio to re-release it, for about %s.",
                      Dollars(c.take).c_str()),
               kGold);
    }
    PlaceVault();
}

void Frame()
{
    if (!game::Ready())
        return;
    float today = Today();
    if (today <= 0)
        return;
    if (g_lastDay < 0 || today < g_lastDay) {   // a load: start counting from here
        g_lastDay = today;
        PlaceVault();
        return;
    }
    if (today - g_lastDay < 1)
        return;
    float days = std::min(today - g_lastDay, 7.0f);
    g_lastDay = today;
    Daily(days);
}

std::string NextDue()
{
    const Candidate* next = nullptr;
    std::vector<Candidate> all = Cartoons();
    for (const Candidate& c : all)
        if (c.title != g_run.title && (!next || c.due < next->due))
            next = &c;
    if (!next)
        return "No cartoons in the vault";
    return Format("\"%s\" can come out in %s", next->title.c_str(), DateText(next->due).c_str());
}

} // namespace

bool IsVaultObject(uintptr_t project) { return project && project == g_vault; }

const char* VaultMesh(uintptr_t project) { return IsVaultObject(project) ? "p_icon_finished.msh" : nullptr; }

const char* VaultPicture(uintptr_t project) { return IsVaultObject(project) ? "ui/vault.dds" : nullptr; }

bool VaultLabel(uintptr_t project, std::wstring* kind, std::wstring* phase, std::wstring* status)
{
    if (!IsVaultObject(project))
        return false;
    *kind = L"The Vault";
    Candidate c;
    if (BestDue(&c))
        *phase = html::Text(Format("Re-release \"%s\": about %s", c.title.c_str(), Dollars(c.take).c_str()));
    else
        *phase = html::Text(NextDue());
    *status = g_run.title.empty() ? L"Drop on the Animation Studio"
                                  : html::Text(Format("In cinemas: \"%s\"", g_run.title.c_str()));
    return true;
}

void VaultInfo()
{
    Candidate c;
    Notify("The Vault", BestDue(&c) ? Format("\"%s\" is ready to go back into cinemas, for about %s. Drop the vault on "
                                             "the Animation Studio.",
                                             c.title.c_str(), Dollars(c.take).c_str())
                                    : NextDue() + ".",
           kYellow);
}

bool VaultDrop(const std::string& zone, uintptr_t project)
{
    if (!IsVaultObject(project))
        return false;
    if (zone.rfind("room_tva_", 0) != 0) {
        Notify("The Vault", "Drop the vault on the Animation Studio to re-release a cartoon.", kYellow);
        return true;
    }
    if (!g_run.title.empty()) {
        Notify("The Vault", Format("\"%s\" is in cinemas now. One re-release at a time.", g_run.title.c_str()), kYellow);
        return true;
    }
    Candidate c;
    if (!BestDue(&c)) {
        Notify("The Vault", NextDue() + ".", kYellow);
        return true;
    }
    Vaulted& v = g_films[c.title];
    v.reissues++;
    v.last = Today();
    v.noticed = false;
    g_run.title = c.title;
    g_run.total = c.take;
    g_run.paid = 0;
    g_run.start = Today();
    mod::Log("tv: '%s' re-released (time %d) - expected %.0f", c.title.c_str(), v.reissues, c.take);
    MascotShown(c.title, c.quality, kMascotWeight);
    Notify(Format("\"%s\" is back in cinemas", c.title.c_str()),
           Format("Re-released for a new generation: about %s over the next two months.", Dollars(c.take).c_str()),
           kGold);
    return true;
}

void InstallVault()
{
    overlay::AddFrameCallback(&Frame);
    persist::Register(
        "vault",
        [](persist::Section& out) {
            out["ever"] = g_vaultEver ? "1" : "0";
            int i = 0;
            for (const auto& [title, v] : g_films)
                if (v.reissues || v.noticed)
                    out[Format("film%d", i++)] = Format("%d|%.1f|%d|%s", v.reissues, v.last, v.noticed ? 1 : 0,
                                                        title.c_str());
            if (!g_run.title.empty())
                out["run"] = Format("%.0f|%.0f|%.1f|%s", g_run.total, g_run.paid, g_run.start, g_run.title.c_str());
        },
        [](const persist::Section& in) {
            g_films.clear();
            g_run = Run{};
            g_vault = 0;
            g_lastDay = -1;
            auto it = in.find("ever");
            g_vaultEver = it != in.end() && it->second == "1";
            for (const auto& [key, value] : in) {
                int used = 0, n = 0, noticed = 0;
                float last = 0;
                double a = 0, b = 0;
                if (key.rfind("film", 0) == 0) {
                    if (sscanf_s(value.c_str(), "%d|%f|%d|%n", &n, &last, &noticed, &used) == 3 && used > 0)
                        g_films[value.substr(used)] = { n, last, noticed == 1 };
                } else if (key == "run") {
                    if (sscanf_s(value.c_str(), "%lf|%lf|%f|%n", &a, &b, &last, &used) == 3 && used > 0)
                        g_run = { value.substr(used), a, b, last };
                }
            }
            mod::Log("tv: vault restored - %d films tracked, run '%s'", static_cast<int>(g_films.size()),
                     g_run.title.c_str());
        },
        [] {
            g_films.clear();
            g_run = Run{};
            g_vault = 0;
            g_vaultEver = false;
            g_lastDay = -1;
        });
}

} // namespace tv
