#pragma once
// Drop zones ("rooms") on buildings. Zones with names the game doesn't know become plain
// TMRoom objects; this module intercepts people being dropped on them so mod systems can
// react (e.g. casting a star in a TV show).

#include <cstdint>
#include <string>

namespace rooms {

struct Drop {
    std::string room;        // zone name, e.g. "room_tv_cast"
    uintptr_t   person = 0;  // the game object that was dropped
    std::string personClass; // RTTI class, e.g. CStar
    std::string personName;  // best guess from the object's strings, may be empty
    uintptr_t   project = 0; // a script or film (CProject) carried by the dropped object
    uintptr_t   roomObject = 0;
    bool        scriptRoom = false; // a TV writers' desk built as a real CScriptRoom
};

// The project a script room is writing (CScriptRoom +0x270), or 0.
uintptr_t ScriptRoomProject(uintptr_t room);
// The script office a script room belongs to, if it is one (CFacilityScriptOffice).
uintptr_t ScriptRoomOffice(uintptr_t room);
// The project a script office is writing right now (+0x528), or 0.
uintptr_t OfficeProject(uintptr_t office);

using DropHandler = bool (*)(const Drop& drop);   // return true if handled

// TV scripts: the mod tells this module which projects are TV scripts, so their 3D icon on
// the lot looks different (purple pages with a TV set) and film buildings refuse them.
using ProjectTest = bool (*)(uintptr_t project);
void SetTvScriptTest(ProjectTest test);
void RefreshScriptLook(uintptr_t project);   // re-pick the icon now (after tagging a project)

// Replaces the plain room's drop handler. Call from the game thread after MH_Initialize.
void Install();
void SetHandler(DropHandler handler);

} // namespace rooms
