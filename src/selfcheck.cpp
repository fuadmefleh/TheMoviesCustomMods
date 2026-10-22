// Self-check: once a game is running, verifies the game structures the newer features
// rely on (found by static analysis only) and logs PASS / FAIL lines, so one play session
// shows which of them hold. Read-only. [MoviesMod] SelfCheck = 0 turns it off.

#include <windows.h>
#include <cstdarg>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <string>
#include <vector>

#include "game.h"
#include "mod.h"
#include "overlay.h"

namespace selfcheck {
namespace {

template <typename T> T Rd(uintptr_t a) { return *reinterpret_cast<const T*>(a); }

int g_pass = 0, g_fail = 0;

void Check(bool ok, const char* what, const std::string& detail)
{
    (ok ? g_pass : g_fail)++;
    mod::Log("selfcheck: %s %s - %s", ok ? "PASS" : "FAIL", what, detail.c_str());
}

std::string Fmt(const char* fmt, ...)
{
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    return buf;
}

std::vector<uintptr_t> Items(uintptr_t list, int limit = 2000)
{
    std::vector<uintptr_t> out;
    uintptr_t sentinel = list + 0x14;
    uintptr_t node = Rd<uintptr_t>(list + 8);
    for (int i = 0; node && node != sentinel && i < limit; i++, node = Rd<uintptr_t>(node + 4))
        if (uintptr_t item = Rd<uintptr_t>(node + 8))
            out.push_back(item);
    return out;
}

std::string Narrow(uintptr_t at)
{
    const char* text = Rd<const char*>(at);
    int len = Rd<int>(at + 4);
    if (!text || len <= 0 || len > 200 || IsBadReadPtr(text, len))
        return "";
    return std::string(text, len);
}

bool Unit(float v) { return v == v && v >= 0 && v <= 1.0001f; }

// ---------------------------------------------------------------------------------

void RivalFilms()
{
    auto films = Items(0x0104D628);
    std::string cls = films.empty() ? "" : game::ClassName(films[0]);
    Check(!films.empty() && cls == "CProjectAI", "rival films list",
          Fmt("%d films, first is %s", static_cast<int>(films.size()), cls.c_str()));
    std::vector<game::Project> read = game::RivalFilms();
    if (!read.empty()) {
        const game::Project& p = read.front();
        Check(Unit(p.quality) && p.releaseDate > 0, "rival film fields",
              Fmt("'%ls' by %ls, quality %.2f, released day %.0f, stars %.2f, box office %.0f", p.title.c_str(),
                  p.studio.c_str(), p.quality, p.releaseDate, p.starRating, p.incomeInitial - p.incomeRemaining));
    }
    uintptr_t league = Rd<uintptr_t>(0x00F88F90);
    Check(league && game::ClassName(league) == "CLeagueTable", "Top Movies league table",
          league ? game::ClassName(league) : "null");
    mod::Log("selfcheck:   rival film cap %d, live count %d", Rd<int>(0x00E543D8), Rd<int>(0x0104D624));
}

void People(uintptr_t studio)
{
    auto stars = Items(0x0104D054);
    int mine = 0;
    uintptr_t sample = 0;
    for (uintptr_t s : stars)
        if (Rd<uintptr_t>(s + 0x934) == studio && ++mine == 1)
            sample = s;
    Check(!stars.empty(), "stars list", Fmt("%d stars, %d yours, first is %s", static_cast<int>(stars.size()), mine,
                                            stars.empty() ? "" : game::ClassName(stars[0]).c_str()));
    if (sample) {
        uintptr_t mood = Rd<uintptr_t>(sample + 0xC98);
        uintptr_t wb = mood ? Rd<uintptr_t>(mood + 0x128) : 0;
        Check(wb && game::ClassName(mood) == "CMood" && game::ClassName(wb) == "CWorkBar" && Unit(Rd<float>(wb + 0x8C)),
              "star stress (rest breaks)",
              Fmt("mood %s, workbar %s, stress %.2f/%.2f, boredom %.2f/%.2f", game::ClassName(mood).c_str(),
                  wb ? game::ClassName(wb).c_str() : "-", wb ? Rd<float>(wb + 0x8C) : -1, wb ? Rd<float>(wb + 0xA4) : -1,
                  wb ? Rd<float>(wb + 0x90) : -1, wb ? Rd<float>(wb + 0xA8) : -1));
    }
    auto staff = Items(0x0104CFC0);
    int handymen = 0, janitors = 0, ours = 0;
    for (uintptr_t s : staff) {
        if (Rd<uintptr_t>(s + 0x934) != studio)
            continue;
        ours++;
        handymen += Rd<int>(s + 0x814) == 5;
        janitors += Rd<int>(s + 0x814) == 6;
    }
    Check(ours > 0, "staff list", Fmt("%d people, %d yours: %d handymen, %d janitors", static_cast<int>(staff.size()),
                                      ours, handymen, janitors));
}

void Explainers()
{
    auto all = Items(0x01050254, 5000);
    int repair = 0;
    uintptr_t sample = 0;
    for (uintptr_t e : all)
        if (Rd<uintptr_t>(e) == 0x00D65504 && ++repair == 1)
            sample = e;
    std::string cls = sample ? game::ClassName(sample) : "";
    uintptr_t asset = sample ? Rd<uintptr_t>(sample + 0x1D4) : 0;
    Check(repair > 0 && cls == "CRepairForceExplainer", "repair explainers (handyman dispatch)",
          Fmt("%d explainers, %d repair, class %s, asset %s", static_cast<int>(all.size()), repair, cls.c_str(),
              asset ? game::ClassName(asset).c_str() : "-"));
}

void Technology()
{
    int techs = 0;
    std::string first;
    uintptr_t node = Rd<uintptr_t>(0x010509A4);
    for (int i = 0; node && node != 0x010509A4 && i < 300; i++, node = Rd<uintptr_t>(node)) {
        uintptr_t tech = Rd<uintptr_t>(node + 8);
        if (tech && techs++ == 0)
            first = game::ClassName(tech) + " " + Narrow(tech + 0x104) + " (" + Narrow(tech + 0x148) + ")";
    }
    Check(techs > 10 && first.rfind("CTech", 0) == 0, "technology list (outdated kit)",
          Fmt("%d technologies, first %s", techs, first.c_str()));
    int packs = 0;
    std::string pack;
    node = Rd<uintptr_t>(0x0104A740);
    for (int i = 0; node && node != 0x0104A74C && i < 400; i++, node = Rd<uintptr_t>(node + 4))
        if (uintptr_t p = Rd<uintptr_t>(node + 8))
            if (packs++ == 0)
                pack = game::ClassName(p) + " " + Narrow(p + 0x80);
    Check(packs > 10, "research packs", Fmt("%d packs, first %s", packs, pack.c_str()));
}

void PlayerFilm()
{
    std::vector<game::Project> films = game::PlayerProductions();
    for (const game::Project& p : game::PlayerBackCatalogue())
        films.push_back(p);
    for (const game::Project& f : films) {
        uintptr_t cast = Rd<uintptr_t>(f.address + 0x1F8);
        if (!cast)
            continue;
        std::string costumes;
        uintptr_t begin = Rd<uintptr_t>(cast + 0x64), end = Rd<uintptr_t>(cast + 0x68);
        int roles = 0;
        for (uintptr_t e = begin; e && e < end && roles < 20; e += 0x18, roles++) {
            uintptr_t role = Rd<uintptr_t>(e + 0x14);
            uintptr_t costume = role ? Rd<uintptr_t>(role + 0xDC) : 0;
            if (costume)
                costumes += " " + game::ClassName(costume) + ":" + Narrow(costume + 0x78);
        }
        Check(game::ClassName(cast) == "CCast" && !costumes.empty(), "film cast and costumes",
              Fmt("'%ls': %s, %d roles,%s", f.title.c_str(), game::ClassName(cast).c_str(), roles, costumes.c_str()));
        uintptr_t equipment = Rd<uintptr_t>(f.address + 0x258);
        std::string techs;
        for (int i = 0; equipment && i < 5; i++)
            techs += " " + Narrow(equipment + 0x60 + i * 0x20);
        Check(equipment && game::ClassName(equipment) == "CEquipment", "film equipment",
              Fmt("%s:%s", equipment ? game::ClassName(equipment).c_str() : "none", techs.c_str()));
        return;
    }
    mod::Log("selfcheck:   no film with a cast yet - costumes and equipment not checked");
}

void Timeline()
{
    auto nodules = Items(0x0104E7BC);
    int mine = 0;
    for (uintptr_t n : nodules)
        mine += game::ClassName(n) == "CNoduleEvent";
    Check(!nodules.empty(), "timeline", Fmt("%d entries, %d world events", static_cast<int>(nodules.size()), mine));
}

void Blueprints()
{
    struct Str { const char* t; int len; int cap; char inl[20]; };
    for (const char* name : { "facility_script_5star", "trailer_7", "trailer_9" }) {
        Str key{ name, static_cast<int>(strlen(name)), 0x14, {} };
        void* bp = reinterpret_cast<void*(__cdecl*)(Str*, int)>(0x009623A0)(&key, 1);
        bool available = bp && reinterpret_cast<bool(__thiscall*)(void*)>(0x00960F30)(bp);
        Check(bp != nullptr, "blueprint", Fmt("%s %s%s", name, bp ? "loaded" : "MISSING", available ? ", unlocked" : ", locked"));
    }
}

using Step = void (*)();
bool Run(const char* name, Step step)
{
    __try {
        step();
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        mod::Log("selfcheck: FAIL %s - crashed reading it (wrong offsets)", name);
        g_fail++;
        return false;
    }
}

uintptr_t g_studio = 0;
void PeopleStep() { People(g_studio); }

uintptr_t g_checkedStudio = 0;
DWORD g_readySince = 0;

void Frame()
{
    if (!game::Ready()) {
        g_readySince = 0;
        return;
    }
    uintptr_t studio = game::ReadPlayerStudio().address;
    if (studio == g_checkedStudio)
        return;
    if (!g_readySince)
        g_readySince = GetTickCount();
    if (GetTickCount() - g_readySince < 15000)   // let the game settle after loading
        return;
    g_checkedStudio = studio;
    g_studio = studio;
    g_pass = g_fail = 0;
    mod::Log("selfcheck: ---- checking game structures (day %.0f) ----", game::DateDays());
    Run("rival films", &RivalFilms);
    Run("people", &PeopleStep);
    Run("explainers", &Explainers);
    Run("technology", &Technology);
    Run("player film", &PlayerFilm);
    Run("timeline", &Timeline);
    Run("blueprints", &Blueprints);
    mod::Log("selfcheck: ---- %d passed, %d failed ----", g_pass, g_fail);
}

} // namespace

void Install()
{
    wchar_t ini[MAX_PATH];
    swprintf_s(ini, L"%s\\MoviesMod.ini", mod::GameDir());
    if (GetPrivateProfileIntW(L"MoviesMod", L"SelfCheck", 1, ini))
        overlay::AddFrameCallback(&Frame);
}

} // namespace selfcheck
