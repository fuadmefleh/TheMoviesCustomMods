#pragma once

namespace mod {

void Log(const char* fmt, ...);
const wchar_t* GameDir();

// features.cpp
void LoadConfig();
void InstallHooks();

// inspector.cpp
void RegisterInspector();
void ShowInspector(bool show);   // hidden by default; toggled from the Television window

} // namespace mod

namespace buildmenu {
void Install();   // buildmenu.cpp: category buttons on the build menu's building list
}

namespace lists {
void MergeAll();   // lists.cpp: merge our entries into shared list files in Data (before the game reads them)
}

namespace moviemaker {
void Install();   // moviemaker.cpp: HD/30 fps export, sharper presets, character texture size, instant shoot
}

namespace selfcheck {
void Install();   // selfcheck.cpp: logs PASS/FAIL for the game structures the mod relies on
}

namespace manager {
void Install();   // manager.cpp: rest breaks for idle stars, handymen sent to run-down buildings
}

namespace ratings {
void Install();   // ratings.cpp: fairer film quality, success and box office
}
