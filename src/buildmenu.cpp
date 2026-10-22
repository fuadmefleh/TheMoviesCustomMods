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
// Settings: [BuildMenu] in MoviesMod.ini.

#include <windows.h>
#include <cstdint>
#include <cstring>
#include <string>

#include "MinHook.h"
#include "mod.h"
#include "nativeui.h"

namespace buildmenu {
namespace {

constexpr uintptr_t kGetMenuType  = 0x0095C9E0;   // int __fastcall(TMBlueprint*)
constexpr uintptr_t kOpenFacList  = 0x00642150;   // void __fastcall(WBuildButton*)
constexpr int kBpMenuType = 0x120, kBpName = 0xAC;
constexpr int kButtonList = 0x3C4;                 // WBuildButton -> WBuildButtonList
constexpr int kListMode = 0x348, kListCount = 0x540;   // mode 0 = buildings

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
void* g_list = nullptr;
float g_tabX = 6, g_tabY = -44, g_tabSize = 40;

bool IsTelevision(void* bp)
{
    __try {
        const char* text = *reinterpret_cast<const char* const*>(static_cast<char*>(bp) + kBpName);
        int len = *reinterpret_cast<const int*>(static_cast<char*>(bp) + kBpName + 4);
        return text && len > 11 && len < 128 && strstr(text, "facility_tv_") != nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

int CategoryOf(void* bp, int menuType)
{
    if (IsTelevision(bp))
        return Television;
    switch (menuType) {
    case 1: return Production;
    case 2: return Staff;
    case 3: return Amenities;
    default: return Production;
    }
}

int __fastcall HookGetMenuType(void* bp)
{
    int menuType = g_origGetMenuType(bp);
    if (!menuType || g_category == All)
        return menuType;
    return CategoryOf(bp, menuType) == g_category ? menuType : 0;
}

void Choose(int category)
{
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
    g_category = All;   // each time the list opens it starts with everything
    g_origOpen(button);
    g_list = ListOf(button);
    if (!g_list)
        return;
    int added = 0;
    for (int i = 0; i < CategoryCount; i++)
        added += nativeui::AddButton(g_list, kTabs[i].texture, kTabs[i].tooltip, g_tabX + (g_tabSize + 4) * i, g_tabY,
                                     g_tabSize, [i] { Choose(i); }) != nullptr;
    static bool logged = false;
    if (!logged) {
        logged = true;
        mod::Log("buildmenu: %d category buttons on the building list", added);
    }
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
    bool ok = MH_CreateHook(reinterpret_cast<LPVOID>(kGetMenuType), reinterpret_cast<LPVOID>(&HookGetMenuType),
                            reinterpret_cast<LPVOID*>(&g_origGetMenuType)) == MH_OK &&
              MH_CreateHook(reinterpret_cast<LPVOID>(kOpenFacList), reinterpret_cast<LPVOID>(&HookOpenFacList),
                            reinterpret_cast<LPVOID*>(&g_origOpen)) == MH_OK &&
              MH_EnableHook(reinterpret_cast<LPVOID>(kGetMenuType)) == MH_OK &&
              MH_EnableHook(reinterpret_cast<LPVOID>(kOpenFacList)) == MH_OK;
    mod::Log("buildmenu: category buttons %s", ok ? "on" : "FAILED");
}

} // namespace buildmenu
