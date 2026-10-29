#pragma once
// The game's own interface widgets, built with the game's constructors (see nativeui.cpp):
// a framed dialog with tab buttons and a rich-text panel, yes/no prompts, and a HUD button.
// Main thread only (the overlay's frame callbacks and game UI handlers).

#include <functional>
#include <string>
#include <vector>

namespace nativeui {

// True while a game is running and its interface exists.
bool Ready();
// [MoviesMod] NativeUI = 1 (the default).
bool Enabled();

// A yes/no prompt in the game's style. body is the game's rich text (<br>, <b>,
// <font color=#RRGGBB>, <table>...). Either callback may be empty.
void Confirm(const std::wstring& title, const std::wstring& body, std::function<void()> onYes,
             std::function<void()> onNo = nullptr);

// The game's own "quit to the main menu without saving" (the pause menu's handler 0x006D7BA0)
// behind both buttons of a dialog: for game over. The game quits in its own UI phase.
void GameOverDialog(const std::wstring& title, const std::wstring& body);

// A screen: a game dialog with a row of icon buttons (tabs and actions) over a rich-text
// panel. html(tab) is asked for the page whenever a tab is chosen and every couple of
// seconds while the screen is open.
struct Button {
    const char*  texture;   // game button texture base, e.g. "button_star." (with the dot)
    std::wstring tooltip;
    int          tab;       // >= 0: switches to this tab; < 0: calls action
    std::function<void()> action;
};
struct Screen {
    std::wstring              title;
    std::vector<Button>       buttons;
    std::function<std::wstring(int tab)> html;
    float                     width = 820, height = 580;
};
void Open(const Screen& screen, int tab = 0);
void Close();
bool IsOpen();
void Refresh();   // rebuild the page now

// A round button on the game's HUD (right edge), re-added after every load.
void AddHudButton(const char* texture, const std::wstring& tooltip, std::function<void()> onClick);

// An icon button added to an existing game window (e.g. the build menu's list), placed at
// (x, y) from its top-left; it goes when the window goes. Returns the widget or nullptr.
void* AddButton(void* parent, const char* texture, const std::wstring& tooltip, float x, float y, float size,
                std::function<void()> onClick);

// Pieces for building into a game window while its constructor runs (sandbox.cpp lays them
// out itself). Each guards the game's calls and returns nullptr / false if one faults.
void* NewContainer();                                   // an empty WWindow (0x344, ctor 0x007432F0)
void* NewText(const wchar_t* html);                     // a WHTML with this rich text
bool  SetText(void* html, const wchar_t* text);         // the way the game's own windows do it
void* NewIconButton(const char* texture, const wchar_t* tooltip, float size);
void* NewPicture(const char* path);                     // a WImage (0x360, ctor 0x0069D820)
bool  SetPicture(void* picture, const char* path);      // e.g. "ui/button_toggleon.dds"
bool  OnClick(void* widget, const char* name, std::function<void()> fn);

// Logs a game window's on-screen rectangle (for placing mod widgets).
void LogRect(const char* what, void* window);
// Saves one screenshot of the game (MoviesMod\shots\<name>.bmp) delayMs from now, once per session.
void ShotOnce(const char* name, unsigned delayMs);

// Text for the game's rich-text renderer: escapes the characters it treats as markup.
std::wstring Escape(const std::string& utf8);

void Install();   // after overlay::Install

} // namespace nativeui
