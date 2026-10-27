#pragma once
// The HUD's right-hand project list: the mod's own projects (the bag of shares, the
// investors' briefcase) in a group of their own above the movies (projectlist.cpp).

#include <cstdint>

namespace projectlist {

using ProjectTest = bool (*)(uintptr_t project);              // one of the mod's
using ProjectPicture = const char* (*)(uintptr_t project);    // its card picture, e.g. "ui/shares_bag.dds"
using ProjectInfo = void (*)(uintptr_t project);              // a click, if the camera can't go to its object

void SetCategory(ProjectTest isOurs, ProjectPicture picture, ProjectInfo info);

// Patches the list. Call once from the game thread after MH_Initialize and careers::Install.
void Install();

} // namespace projectlist
