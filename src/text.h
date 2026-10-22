#pragma once
// Adds or replaces entries in the game's string database (building names, descriptions).
// Entries come from the [text] section of MoviesMod.ini: KEY = text (UTF-8).

#include <string>

namespace text {

// Hooks the game's string lookup. Call from OnAttach after MH_Initialize.
void Install();

// Adds or replaces an entry at run time (e.g. a news story's headline). Main thread only.
void Set(const std::string& key, const std::wstring& value);

} // namespace text
