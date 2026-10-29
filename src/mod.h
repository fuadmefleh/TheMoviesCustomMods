#pragma once

#include <cstdint>

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

namespace production {
void Install();   // production.cpp: continuity makeup and the second unit
// Toon Studio spike: finishes a film (script done, stages 3-5) through the game's debug "finish film"
bool FinishWithoutShoot(uintptr_t project);
int  SceneCount(uintptr_t project);   // the film's scenes (its script's shots), -1 if unreadable
int  RoleCount(uintptr_t project);    // the film's acting roles (not extras), -1 if unreadable
}

namespace tv {
// Toon Studio: the performance part of a cartoon's quality (CQuality+0xA4), from its voice
// cast; false for any other film.
bool VoicePerformance(uintptr_t project, float* performance);
// The look a film plays with: 0 not a cartoon, 1 hand-drawn, 2 CGI.
int CartoonStyle(uintptr_t project);
// The studio mascot's pull at the box office for one of the player's films (1 = none).
float MascotPull(uintptr_t project);
}

namespace toon {
void Install();   // toon.cpp: the cel-shading pass for animated films (Toon Studio spike)
}

namespace buildmenu {
void Install();   // buildmenu.cpp: category buttons on the build menu's building list
}

namespace lists {
void MergeAll();   // lists.cpp: merge our entries into shared list files in Data (before the game reads them)
}

namespace moviemaker {
void Install();   // moviemaker.cpp: HD/30 fps export, sharper presets, character texture size, instant shoot
}

namespace sandbox {
void Install();   // sandbox.cpp: Instant Script-writing in the sandbox game's options
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
