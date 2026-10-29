#pragma once
// Stores mod data alongside the game's save files.
//
// Each "Saved Games\<name>.jad" gets a sibling "<name>.tvs" text file. Systems register
// a named section with save/load/reset callbacks; sections are plain key=value maps.

#include <map>
#include <string>

namespace persist {

using Section = std::map<std::string, std::string>;
using SaveFn  = void (*)(Section& out);
using LoadFn  = void (*)(const Section& in);
using ResetFn = void (*)();   // new game, or a save without mod data

void Register(const char* name, SaveFn save, LoadFn load, ResetFn reset);

// Hooks the game's file access. Call once from the game thread.
void Install();

// Call every frame: detects when a different game has been loaded and applies its data.
void Tick();

// A new game is being started from the menus: forget the saves the menus read (the main
// menu reads the autosave), so the game that follows is not taken for a load.
void NewGameStarting();

// The save file the current game came from ("" for a new game).
const std::wstring& CurrentSave();

} // namespace persist
