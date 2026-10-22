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

// A yes/no prompt in the game's style. body is the game's rich text (<br>, <b>,
// <font color=#RRGGBB>, <table>...). Either callback may be empty.
void Confirm(const std::wstring& title, const std::wstring& body, std::function<void()> onYes,
             std::function<void()> onNo = nullptr);

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

// Text for the game's rich-text renderer: escapes the characters it treats as markup.
std::wstring Escape(const std::string& utf8);

void Install();   // after overlay::Install

} // namespace nativeui
