#pragma once

#include <windows.h>

struct ImFont;

namespace overlay {

// Installs the Direct3D 9 and DirectInput hooks. Call once from the game thread,
// outside DllMain.
void Install();

// Called by the proxy after the game creates its IDirectInput8 object, so we can
// filter device input while an overlay window has focus.
void OnDirectInputCreated(void* directInput8);

// Panels register a draw callback; it runs every frame while the overlay is open.
using DrawFn = void (*)();
void AddPanel(const char* name, DrawFn draw);

// HUD elements draw every frame, also while the overlay is closed. They must not take
// input (use ImGuiWindowFlags_NoInputs when the overlay is closed).
void AddHud(DrawFn draw);

// Runs every frame, whether or not the overlay is open (game thread, before Present).
void AddFrameCallback(void (*fn)());
// F8 with the overlay closed calls this first (Shift+F8 skips it); returning true means it
// was handled (e.g. the native TV screen opened) and the overlay stays closed.
void SetToggleHandler(bool (*fn)());

bool IsOpen();
void Open();   // shows the overlay (e.g. when the game hands the player a decision)
void Close();  // hides it, as F8 does

// Larger sizes of the interface font (Arial Rounded MT Bold), or nullptr if unavailable.
ImFont* TitleFont();     // screen titles
ImFont* HeadingFont();   // section headings and big buttons

// One of the game's interface textures, copied by tools\extract_ui.py to
// <game>\MoviesMod\ui\<name>.tex, ready for ImGui::Image; nullptr if it isn't there.
void* Texture(const char* name, int* width = nullptr, int* height = nullptr);

// Saves the next finished frame to <game>\MoviesMod\shots\<name>.bmp (half size), so the
// mod's screens can be checked after a test session.
void RequestScreenshot(const char* name);

} // namespace overlay
