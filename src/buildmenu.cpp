// Category buttons for the build menu's building list.
//
// In v1.2 every building is in one long alphabetical list (the HUD's Flash movie has a
// single "facilities" button; `path = group_x` and `grouporder` are never read by the menu).
// The list is filled by WBuildButtonList (0x00654D50), which shows a blueprint when
// GetMenuType (0x0095C9E0, `return bp->menutype`) is non-zero. The designers' menutype values
// still sort the buildings - 1 production, 2 staff and talent (and trailers), 3 amenities - so
// we hook GetMenuType to hide everything outside the chosen category, and add icon buttons
// to the list when it opens (0x00642150). A click sets the category and resets the list's
// item count (+0x540), so its own update rebuilds it (0x0065D620 recounts and refills).
//
// The categories live in the game's own build menu (bottom left): our rewritten Flash movie
// data/ui/flash/buildmenu4.mfl (tools/buildmenu_flash.py) gives the Facilities button five
// sub-buttons, $fac_sub1..5, that pop out like the props ones and call cb_fac_sub1..5. We
// register those callbacks with the movie (0x0087DE50, after WBuildButton's own registration
// 0x00646950), and hook the click dispatcher (0x00643B60) so "$fac_subN" opens the building
// list (0x00642150) filtered to that category. Without the movie, icon buttons beside the list
// do the same job.
// Settings: [BuildMenu] in MoviesMod.ini.

#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "MinHook.h"
#include "mod.h"
#include "nativeui.h"
#include "text.h"

namespace buildmenu {
namespace {

constexpr uintptr_t kGetMenuType  = 0x0095C9E0;   // int __fastcall(TMBlueprint*)
constexpr uintptr_t kOpenFacList  = 0x00642150;   // void __fastcall(WBuildButton*)
constexpr int kBpMenuType = 0x120, kBpName = 0xAC;
constexpr int kButtonList = 0x3C4;                 // WBuildButton -> WBuildButtonList
constexpr int kListMode = 0x348, kListCount = 0x540;   // mode 0 = buildings
constexpr uintptr_t kRegisterCallbacks = 0x00646950;   // void __fastcall(WBuildButton*)
constexpr uintptr_t kAddFlashCallback  = 0x0087DE50;   // void __thiscall(registry, String* name, ctx, fn, 0)
constexpr uintptr_t kDispatch          = 0x00643B60;   // void __thiscall(WBuildButton*, String* node)
constexpr uintptr_t kClickNode         = 0x00645F60;   // void __thiscall(WBuildButton*, String* node)
constexpr uintptr_t kFindElement       = 0x008828C0;   // void* __thiscall(player, const char* name)
constexpr uintptr_t kSetTooltip        = 0x00872180;   // void __thiscall(elem, String* key, int, int)
constexpr int kButtonMovie = 0x364, kMoviePlayer = 0x358, kPlayerRegistry = 0x178, kPropSub = 0x3DC;

struct GameStr {   // the game's narrow String; cap 0x14 = inline, never freed by the callee
    const char* text;
    int         len;
    int         cap;
    char        inline_[20];
};
GameStr Str(const char* s) { return GameStr{ s, static_cast<int>(strlen(s)), 0x14, {} }; }

enum Category { All, Production, Staff, Amenities, Television, CategoryCount };
struct Tab {
    const char*    texture;
    const wchar_t* tooltip;
};
const Tab kTabs[CategoryCount] = {
    { "button_fac.",      L"All buildings" },
    { "button_clapperb.", L"Production: offices, research and post-production" },
    { "button_star.",     L"Staff and talent: schools, crew and trailers" },
    { "button_litter.",   L"Amenities: food, toilets, bars and health" },
    { "button_video.",    L"Television" },
};

using GetMenuType_t = int(__fastcall*)(void* bp);
using OpenFacList_t = void(__fastcall*)(void* button);
GetMenuType_t g_origGetMenuType = nullptr;
OpenFacList_t g_origOpen = nullptr;
int   g_category = All;
int   g_pending = -1;        // category chosen in the Flash menu, applied when the list opens
bool  g_listButtons = true;
bool  g_facOpensList = false;   // [BuildMenu] FacilitiesOpensList: the Facilities button itself opens the full list  // icon buttons beside the list (when our Flash movie isn't installed)
void* g_list = nullptr;
// The list window is a full-screen 1024x768 layer; the visible Facilities panel spans about
// x 92..442, y 28..512 of it, so the buttons go in a column just right of the panel.
float g_tabX = 450, g_tabY = 70, g_tabSize = 44;
bool  g_vertical = true;

// The blueprint's file stem ("facility_bar", "trailer_3"...), read from bp+0xAC.
bool BlueprintName(void* bp, char* out, int size)
{
    __try {
        const char* text = *reinterpret_cast<const char* const*>(static_cast<char*>(bp) + kBpName);
        int len = *reinterpret_cast<const int*>(static_cast<char*>(bp) + kBpName + 4);
        if (!text || len <= 0 || len >= size)
            return false;
        memcpy(out, text, len);
        out[len] = 0;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Which category a building belongs to, by its file name (the blueprint's menutype field
// doesn't carry the ini's 1/2/3).
int CategoryOf(void* bp)
{
    char name[128];
    if (!BlueprintName(bp, name, sizeof(name)))
        return Production;
    const char* n = strrchr(name, '/') ? strrchr(name, '/') + 1 : name;
    auto has = [n](const char* part) { return strstr(n, part) != nullptr; };
    if (has("facility_tv_"))
        return Television;
    if (has("trailer") || has("stage") || has("stunt") || has("crew") || has("wardrobe") || has("cosmetic") ||
        has("fightroom") || has("fullbodyburn") || has("spartan"))
        return Staff;
    if (has("bar") || has("catering") || has("toilet") || has("hospital") || has("detox") || has("restaurant") ||
        has("snack") || has("cafe"))
        return Amenities;
    return Production;   // offices, script offices, research, publicity, sales...
}

int __fastcall HookGetMenuType(void* bp)
{
    int menuType = g_origGetMenuType(bp);
    if (!menuType || g_category == All)
        return menuType;
    static int logged = 0;
    if (logged < 60) {
        char name[128] = "?";
        BlueprintName(bp, name, sizeof(name));
        logged++;
        mod::Log("buildmenu: %s menutype %d -> category %d", name, menuType, CategoryOf(bp));
    }
    return CategoryOf(bp) == g_category ? menuType : 0;
}

void Choose(int category)
{
    mod::Log("buildmenu: category %d chosen", category);
    g_category = category;
    __try {
        if (g_list && *reinterpret_cast<int*>(static_cast<char*>(g_list) + kListMode) == 0)
            *reinterpret_cast<int*>(static_cast<char*>(g_list) + kListCount) = -1;   // rebuilt on its next update
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

void* ListOf(void* button)
{
    __try {
        return *reinterpret_cast<void**>(static_cast<char*>(button) + kButtonList);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

void __fastcall HookOpenFacList(void* button)
{
    g_category = g_pending >= 0 ? g_pending : All;   // the Flash sub-button's choice, else everything
    g_pending = -1;
    static const wchar_t* const kTitles[CategoryCount] = { L"", L"Production", L"Staff & Talent", L"Amenities",
                                                           L"Television" };
    if (g_category == All)
        text::Unset("BB_FACILITIES");   // the game's own "Facilities"
    else
        text::Set("BB_FACILITIES", kTitles[g_category]);
    g_origOpen(button);
    g_list = ListOf(button);
    if (!g_list || !g_listButtons)
        return;
    int added = 0;
    void* first = nullptr;
    for (int i = 0; i < CategoryCount; i++) {
        float x = g_vertical ? g_tabX : g_tabX + (g_tabSize + 6) * i;
        float y = g_vertical ? g_tabY + (g_tabSize + 6) * i : g_tabY;
        void* b = nativeui::AddButton(g_list, kTabs[i].texture, kTabs[i].tooltip, x, y, g_tabSize, [i] { Choose(i); });
        added += b != nullptr;
        if (!first)
            first = b;
    }
    static bool shown = false;
    if (!shown && first) {
        shown = true;
        nativeui::LogRect("first category button", first);
    }
    static bool logged = false;
    if (!logged) {
        logged = true;
        mod::Log("buildmenu: %d category buttons on the building list", added);
        nativeui::LogRect("build list", g_list);
        nativeui::LogRect("build button", button);
        nativeui::ShotOnce("buildmenu", 700);
    }
}

// --- The Flash sub-buttons ------------------------------------------------------------

using RegisterCallbacks_t = void(__fastcall*)(void* button);
using Dispatch_t = void(__fastcall*)(void* button, void* edx, GameStr* node);
RegisterCallbacks_t g_origRegister = nullptr;
Dispatch_t g_origDispatch = nullptr;

// cb_fac_subN: called by the movie with ECX = the WBuildButton (the ctx we registered).
template <int N> bool __fastcall OnFacSub(void* button, void*)
{
    char node[16];
    sprintf_s(node, "$fac_sub%d", N);
    GameStr s = Str(node);
    __try {
        reinterpret_cast<void(__thiscall*)(void*, GameStr*)>(kClickNode)(button, &s);   // animate, then dispatch
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return true;
}
using FlashCallback_t = bool(__fastcall*)(void*, void*);
const FlashCallback_t kSubCallbacks[CategoryCount] = { &OnFacSub<1>, &OnFacSub<2>, &OnFacSub<3>, &OnFacSub<4>, &OnFacSub<5> };

void* Player(void* button)
{
    void* movie = *reinterpret_cast<void**>(static_cast<char*>(button) + kButtonMovie);
    return movie ? *reinterpret_cast<void**>(static_cast<char*>(movie) + kMoviePlayer) : nullptr;
}

int RegisterSubs(void* button)
{
    __try {
        void* player = Player(button);
        void* registry = player ? *reinterpret_cast<void**>(static_cast<char*>(player) + kPlayerRegistry) : nullptr;
        if (!registry)
            return 0;
        int n = 0;
        for (int i = 0; i < CategoryCount; i++) {
            char name[16], element[16];
            sprintf_s(name, "cb_fac_sub%d", i + 1);
            GameStr s = Str(name);
            reinterpret_cast<void(__thiscall*)(void*, GameStr*, void*, FlashCallback_t, int)>(kAddFlashCallback)(
                registry, &s, button, kSubCallbacks[i], 0);
            n++;
            // Tooltip: a key the string database lacks is shown as it is, so pass the text.
            sprintf_s(element, "facbutt_sub%d", i + 1);
            void* elem = reinterpret_cast<void*(__thiscall*)(void*, const char*)>(kFindElement)(player, element);
            if (elem) {
                static const char* const kTips[CategoryCount] = { "All buildings", "Production", "Staff and talent",
                                                                  "Amenities", "Television" };
                GameStr tip = Str(kTips[i]);
                reinterpret_cast<void(__thiscall*)(void*, GameStr*, int, int)>(kSetTooltip)(elem, &tip, 1, 1);
            }
        }
        return n;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

void __fastcall HookRegister(void* button)
{
    g_origRegister(button);
    int n = RegisterSubs(button);
    mod::Log("buildmenu: %d Facilities sub-buttons registered with the build menu movie%s", n < 0 ? 0 : n,
             n < 0 ? " (FAILED)" : "");
}

void __fastcall HookDispatch(void* button, void* edx, GameStr* node)
{
    const char* name = node && node->text ? node->text : "";
    if (strncmp(name, "$fac_sub", 8) == 0 && name[8] >= '1' && name[8] < '1' + CategoryCount) {
        g_pending = name[8] - '1';
        mod::Log("buildmenu: Facilities sub-button %d chosen", g_pending + 1);
        __try {
            *reinterpret_cast<int*>(static_cast<char*>(button) + kPropSub) = 0;
            HookOpenFacList(button);   // opens the building list, filtered
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
        return;
    }
    if (strcmp(name, "$fac") == 0) {
        g_pending = All;
        if (!g_facOpensList)
            return;   // the sub-buttons open the list
    }
    g_origDispatch(button, edx, node);
}

float IniFloat(const wchar_t* ini, const wchar_t* key, float def)
{
    wchar_t buf[32];
    GetPrivateProfileStringW(L"BuildMenu", key, L"", buf, 32, ini);
    return buf[0] ? static_cast<float>(_wtof(buf)) : def;
}

} // namespace

void Install()
{
    wchar_t ini[MAX_PATH];
    swprintf_s(ini, L"%s\\MoviesMod.ini", mod::GameDir());
    if (!GetPrivateProfileIntW(L"BuildMenu", L"Categories", 1, ini) || !nativeui::Enabled())
        return;
    g_tabX = IniFloat(ini, L"ButtonsX", g_tabX);
    g_tabY = IniFloat(ini, L"ButtonsY", g_tabY);
    g_tabSize = IniFloat(ini, L"ButtonSize", g_tabSize);
    g_vertical = GetPrivateProfileIntW(L"BuildMenu", L"Column", 1, ini) != 0;
    wchar_t movie[MAX_PATH];
    swprintf_s(movie, L"%s\\Data\\ui\\flash\\buildmenu4.mfl", mod::GameDir());
    bool flash = GetFileAttributesW(movie) != INVALID_FILE_ATTRIBUTES;
    g_listButtons = GetPrivateProfileIntW(L"BuildMenu", L"ListButtons", flash ? 0 : 1, ini) != 0;
    g_facOpensList = !flash || GetPrivateProfileIntW(L"BuildMenu", L"FacilitiesOpensList", 0, ini) != 0;
    bool ok = MH_CreateHook(reinterpret_cast<LPVOID>(kGetMenuType), reinterpret_cast<LPVOID>(&HookGetMenuType),
                            reinterpret_cast<LPVOID*>(&g_origGetMenuType)) == MH_OK &&
              MH_CreateHook(reinterpret_cast<LPVOID>(kOpenFacList), reinterpret_cast<LPVOID>(&HookOpenFacList),
                            reinterpret_cast<LPVOID*>(&g_origOpen)) == MH_OK &&
              MH_EnableHook(reinterpret_cast<LPVOID>(kGetMenuType)) == MH_OK &&
              MH_EnableHook(reinterpret_cast<LPVOID>(kOpenFacList)) == MH_OK;
    if (ok && flash)
        ok = MH_CreateHook(reinterpret_cast<LPVOID>(kRegisterCallbacks), reinterpret_cast<LPVOID>(&HookRegister),
                           reinterpret_cast<LPVOID*>(&g_origRegister)) == MH_OK &&
             MH_CreateHook(reinterpret_cast<LPVOID>(kDispatch), reinterpret_cast<LPVOID>(&HookDispatch),
                           reinterpret_cast<LPVOID*>(&g_origDispatch)) == MH_OK &&
             MH_EnableHook(reinterpret_cast<LPVOID>(kRegisterCallbacks)) == MH_OK &&
             MH_EnableHook(reinterpret_cast<LPVOID>(kDispatch)) == MH_OK;
    mod::Log("buildmenu: Facilities sub-buttons in the build menu %s, buttons beside the list %s",
             flash ? "on" : "off (buildmenu4.mfl not installed)", g_listButtons ? "on" : "off");
    mod::Log("buildmenu: category buttons %s", ok ? "on" : "FAILED");
}

} // namespace buildmenu
