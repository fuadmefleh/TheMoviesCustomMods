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
// Other mod objects on the lot (a shot pilot's reel, a bag of shares): the model to show
// for a project, e.g. "p_shares_bag.msh", or nullptr for the usual one.
using ProjectMesh = const char* (*)(uintptr_t project);
void SetMeshChooser(ProjectMesh chooser);
// The bubble shown over an object on the lot: the three lines under its title (the game's
// genre, phase and status, e.g. "Action / Script Complete / Ready For Casting"). Fill them
// and return true to replace the game's lines for that project.
struct ObjectLabel {
    std::wstring kind, phase, status;
};
using ProjectLabel = bool (*)(uintptr_t project, ObjectLabel* label);
void SetLabeler(ProjectLabel labeler);

// A zone's name (room+0x1A4), e.g. "room_tvk_hire", or "" if unreadable.
std::string ZoneName(uintptr_t room);
// Puts someone already in a room to work in its AI scene (TMRoom::Admit), e.g. an accountant
// at a desk on the Accounts Floor. SendToRoom walks them there first, as a player's drop does.
bool SeatInScene(uintptr_t room, uintptr_t who);
bool SendToRoom(uintptr_t room, uintptr_t who);
// A zone with a work scene (e.g. "room_tvk_books"), or the accountants' hiring desk
// ("room_tvk_hire"), in a building on the lot, or 0.
uintptr_t FindWorkRoom(const char* zone);
void RememberRoom(uintptr_t room);   // makes a room FindWorkRoom can find (one seen in use)

// Replaces the plain room's drop handler. Call from the game thread after MH_Initialize.
void Install();
void SetHandler(DropHandler handler);

} // namespace rooms
