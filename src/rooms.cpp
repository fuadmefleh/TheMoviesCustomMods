// The game's room factory (FUN_009405f0) gives zones with unrecognised names a plain
// TMRoom. TMRoom's main vtable is at 0x00D6E70C; slot 7 (0x00D6E728) is
//     void __thiscall TMRoom::OnDrop(TMMobile* who)
// (CHireRoom overrides it with the same shape). Subclasses have their own vtables, so
// patching this one slot only affects plain rooms.

#include "rooms.h"

#include <windows.h>
#include <cstring>
#include <map>
#include <set>
#include <vector>

#include "MinHook.h"
#include "game.h"
#include "mod.h"

namespace rooms {
namespace {

constexpr uintptr_t kTMRoomOnDropSlot = 0x00D6E728;

using OnDrop_t = void(__thiscall*)(uintptr_t room, uintptr_t who);
OnDrop_t    g_original = nullptr;
DropHandler g_handler = nullptr;
int         g_nameOffset = -1;   // where a room keeps its zone name, found on first drop

bool Readable(uintptr_t addr, size_t n)
{
    MEMORY_BASIC_INFORMATION mbi;
    if (addr < 0x10000 || !VirtualQuery(reinterpret_cast<void*>(addr), &mbi, sizeof(mbi)))
        return false;
    if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
        return false;
    return addr + n <= reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
}

// A narrow game String {char* text; int len; int cap} at addr, if it holds plain text.
std::string NarrowAt(uintptr_t addr)
{
    if (!Readable(addr, 12))
        return "";
    uintptr_t text = *reinterpret_cast<uintptr_t*>(addr);
    int len = *reinterpret_cast<int*>(addr + 4);
    int cap = *reinterpret_cast<int*>(addr + 8);
    if (len < 1 || len > 63 || cap < len || !Readable(text, len))
        return "";
    std::string s(reinterpret_cast<const char*>(text), len);
    for (char c : s)
        if (c < 32 || c > 126)
            return "";
    return s;
}

// A wide game String {wchar_t* text; int len; int cap} holding something name-like.
std::string WideNameAt(uintptr_t addr)
{
    if (!Readable(addr, 12))
        return "";
    uintptr_t text = *reinterpret_cast<uintptr_t*>(addr);
    int len = *reinterpret_cast<int*>(addr + 4);
    int cap = *reinterpret_cast<int*>(addr + 8);
    if (len < 2 || len > 40 || cap < len || !Readable(text, len * 2))
        return "";
    std::string s;
    const wchar_t* w = reinterpret_cast<const wchar_t*>(text);
    for (int i = 0; i < len; i++) {
        wchar_t c = w[i];
        if (!(iswalpha(c) || c == L' ' || c == L'\'' || c == L'-' || c == L'.'))
            return "";
        s += static_cast<char>(c < 128 ? c : '?');
    }
    return s;
}

std::string RoomName(uintptr_t room)
{
    if (g_nameOffset >= 0)
        return NarrowAt(room + g_nameOffset);
    for (int off = 4; off < 0x400; off += 4) {
        std::string s = NarrowAt(room + off);
        if (s.rfind("room_", 0) == 0) {
            g_nameOffset = off;
            mod::Log("rooms: zone name at room+0x%X", off);
            return s;
        }
    }
    return "";
}

std::string PersonName(uintptr_t person)
{
    // Names live in wide strings somewhere in the character; log the candidates once per
    // class so the exact field can be pinned down from a tester's log.
    static std::set<std::string> described;
    std::string cls = game::ClassName(person);
    bool describe = described.insert(cls).second;
    std::string best;
    for (int off = 4; off < 0xC00; off += 4) {
        std::string s = WideNameAt(person + off);
        if (s.empty())
            continue;
        if (describe)
            mod::Log("rooms:   %s +0x%X wide '%s'", cls.c_str(), off, s.c_str());
        if (best.empty() && s.find(' ') != std::string::npos)
            best = s;
    }
    return best;
}

void Report(uintptr_t room, uintptr_t who, const std::string& name, bool scriptRoom)
{
    if (!g_handler)
        return;
    Drop d;
    d.room = name;
    d.roomObject = room;
    d.scriptRoom = scriptRoom;
    d.person = who;
    d.personClass = game::ClassName(who);
    // Scripts and film canisters (CProjectObject) carry a CProject.
    int at = -1;
    d.project = d.personClass.rfind("CStar", 0) == 0 || d.personClass == "CStaff"
                    ? 0
                    : game::FindObjectOfClass(who, "CProject", 0x400, &at);
    d.personName = d.project ? "" : PersonName(who);
    if (d.project)
        mod::Log("rooms: %s carries a CProject at +0x%X (%08X)", d.personClass.c_str(), at, static_cast<unsigned>(d.project));
    mod::Log("rooms: %s '%s' dropped on %s%s", d.personClass.c_str(), d.personName.c_str(), name.c_str(),
             scriptRoom ? " (script room)" : "");
    // A script room handles its own script being dropped back on it.
    if (scriptRoom && d.project)
        return;
    g_handler(d);
}

void __fastcall HookOnDrop(uintptr_t room, void* /*edx*/, uintptr_t who)
{
    g_original(room, who);
    std::string name = RoomName(room);
    if (name.rfind("room_tv", 0) != 0 || !g_handler) {
        mod::Log("rooms: %s dropped on plain zone '%s' (not a TV zone)", game::ClassName(who).c_str(), name.c_str());
        return;
    }
    Report(room, who, name, false);
}

// --- Enabling TV zones -------------------------------------------------------------
// The room factory gives unrecognised zones a plain TMRoom and switches it off
// (BIsEnabled, room+0x1C6 = 0; it is saved with the game). Disabled zones ignore people
// dropped on them. TV zones are switched back on when created and whenever loaded.

constexpr uintptr_t kRoomFactory   = 0x009405F0;   // TMRoom* __cdecl(mesh, roomIndex, String* prefix, int sep)
constexpr uintptr_t kRoomSerialize = 0x0093F3F0;   // void __thiscall(serializable base = room + 0x64)
constexpr int       kRoomEnabled   = 0x1C6;
constexpr int       kRoomName      = 0x1A4;

using Factory_t   = uintptr_t(__cdecl*)(uintptr_t, int, void*, int);
using Serialize_t = void(__fastcall*)(uintptr_t, void*);
Factory_t   g_origFactory = nullptr;
Serialize_t g_origSerialize = nullptr;

void EnableIfTv(uintptr_t room, const char* when)
{
    if (!room)
        return;
    std::string name = NarrowAt(room + kRoomName);
    if (name.rfind("room_tv", 0) != 0 || !Readable(room + kRoomEnabled, 1))
        return;
    auto enabled = reinterpret_cast<unsigned char*>(room + kRoomEnabled);
    if (*enabled == 0) {
        *enabled = 1;
        mod::Log("rooms: enabled %s (%s)", name.c_str(), when);
    }
}

// --- The TV Writers' Office as a real script office -------------------------------------
// The film Script Office is a CFacilityScriptOffice whose desks (room_so_<genre>) are
// CScriptRooms: dropping a writer starts a CProject, the writer sits down and types, and the
// office puts the finished script outside. The TV office gets the same classes: its desks
// are built as CScriptRooms here and its name is added to the facility-class table, so the
// game does the writing and tv_writers.cpp turns each finished script into a TV script.
// Old saves keep their plain rooms (rooms are recreated by class on load).

constexpr uintptr_t kOperatorNew        = 0x00ACBC50;   // void* __cdecl(size)
constexpr uintptr_t kScriptRoomCtor     = 0x00935570;   // CScriptRoom* __thiscall(void* mem), 0x2AC bytes
constexpr uintptr_t kGenreLookup        = 0x0044A8D0;   // Genre* __cdecl(const String* "genre_comedy")
constexpr uintptr_t kScriptRoomGenre    = 0x00934770;   // void __thiscall(CScriptRoom*, Genre*)
constexpr uintptr_t kStringAssign       = 0x004015D0;   // void __thiscall String::Assign(const char*, int len)
constexpr int       kRoomScene          = 0x124;        // String: interaction scene name ("ai_room_so_hall")
constexpr uintptr_t kScriptOnDropSlot   = 0x00D6E1A0;   // CScriptRoom vtable 0x00D6E184 slot 7
constexpr uintptr_t kFacilityTableInit  = 0x00847210;   // fills the name -> creator map below
constexpr uintptr_t kFacilityTableReady = 0x0104ED0C;
constexpr uintptr_t kFacilityTable      = 0x0104ED5C;
constexpr uintptr_t kFacilityTableSlot  = 0x00846EC0;   // Creator* __thiscall(map*, const String* key)
constexpr uintptr_t kCreateScriptOffice = 0x0084AB00;   // CFacilityScriptOffice* __cdecl()
constexpr int       kRoomParent         = 0xC0;         // -> room group, whose +0x24C is the facility
constexpr int       kGroupRooms         = 0x228;        // {begin, end} of 0x18-byte entries, room at +0x14
constexpr int       kGroupFacility      = 0x24C;
constexpr int       kScriptRoomProject  = 0x270;
constexpr int       kScriptRoomHallRef  = 0x290;        // smart reference: vtbl[1] unlink, value at +0x14, vtbl[0] link
constexpr int       kScriptRoomHall     = 0x2A4;
constexpr int       kOfficeProject      = 0x528;
const char* const   kTvOfficeKey        = "facility/facility_tv_script_office";

struct NarrowString {
    const char* text;
    int         len;
    int         cap;
    char        inline_[20];
};

void SetString(NarrowString& n, const char* s)
{
    size_t len = strlen(s);
    n.len = static_cast<int>(len);
    if (len < sizeof(n.inline_)) {
        memcpy(n.inline_, s, len + 1);
        n.text = n.inline_;
        n.cap = 0x14;
    } else {
        n.text = s;   // ours and never freed: the game only copies keys
        n.cap = static_cast<int>(len);
    }
}

struct TvDesk {
    const char* suffix;
    const char* genre;   // nullptr: the office's hall
};
const TvDesk kTvDesks[] = {
    { "sitcom", "genre_comedy" }, { "crime", "genre_action" },  { "drama", "genre_romance" },
    { "soap", "genre_romance" },  { "scifi", "genre_sci-fi" }, { "variety", "genre_comedy" },
    { "office", nullptr },
};

bool        g_nativeWriters = true;   // [MoviesMod] NativeTvWriters
OnDrop_t    g_scriptOnDrop = nullptr;
using TableInit_t = void(__cdecl*)();
TableInit_t g_origTableInit = nullptr;

DWORD g_buildFault = 0;   // exception code from the last failed BuildScriptRoom

// What the room factory does for room_so_<genre> (and room_so_office, the hall).
uintptr_t BuildScriptRoom(const char* genre)
{
    NarrowString s;
    SetString(s, genre ? genre : "ai_room_tvw_hall");
    __try {
        uintptr_t mem = reinterpret_cast<uintptr_t(__cdecl*)(unsigned)>(kOperatorNew)(0x2AC);
        if (!mem)
            return 0;
        uintptr_t room = reinterpret_cast<uintptr_t(__thiscall*)(uintptr_t)>(kScriptRoomCtor)(mem);
        if (genre) {
            uintptr_t g = reinterpret_cast<uintptr_t(__cdecl*)(NarrowString*)>(kGenreLookup)(&s);
            reinterpret_cast<void(__thiscall*)(uintptr_t, uintptr_t)>(kScriptRoomGenre)(room, g);
            *reinterpret_cast<int*>(room + 0x1E0) = 0;
        } else {
            // The factory calls 0x0093C4B0 here, which also reloads the scene and faults on a
            // room that isn't in a building yet; setting the scene name is all that's needed.
            reinterpret_cast<void(__thiscall*)(uintptr_t, const char*, int)>(kStringAssign)(room + kRoomScene, s.text, s.len);
        }
        return room;
    } __except (g_buildFault = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

uintptr_t* TableSlot(NarrowString* key)
{
    __try {
        return reinterpret_cast<uintptr_t*(__thiscall*)(uintptr_t, NarrowString*)>(kFacilityTableSlot)(kFacilityTable, key);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

void RegisterTvOffice()
{
    static bool done = false;
    if (done || !g_nativeWriters || !*reinterpret_cast<const char*>(kFacilityTableReady))
        return;
    done = true;
    NarrowString key;
    SetString(key, kTvOfficeKey);
    uintptr_t* slot = TableSlot(&key);
    if (slot)
        *slot = kCreateScriptOffice;
    mod::Log("rooms: %s '%s' as a script office", slot ? "registered" : "FAILED to register", kTvOfficeKey);
}

void __cdecl HookTableInit()
{
    g_origTableInit();
    RegisterTvOffice();
}

bool SetHall(uintptr_t room, uintptr_t hall)
{
    using Fn = void(__thiscall*)(uintptr_t);
    uintptr_t ref = room + kScriptRoomHallRef;
    __try {
        uintptr_t* vtbl = *reinterpret_cast<uintptr_t**>(ref);
        reinterpret_cast<Fn>(vtbl[1])(ref);
        *reinterpret_cast<uintptr_t*>(room + kScriptRoomHall) = hall;
        reinterpret_cast<Fn>(vtbl[0])(ref);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// The game links a script office's desks to its hall only for "so" zones; do it for ours.
// The hall is optional (writers dropped there help the desk that's writing).
void LinkHall(uintptr_t room)
{
    if (!Readable(room + kScriptRoomHall, 4) || *reinterpret_cast<uintptr_t*>(room + kScriptRoomHall))
        return;
    uintptr_t group = *reinterpret_cast<uintptr_t*>(room + kRoomParent);
    if (!Readable(group + kGroupRooms, 8))
        return;
    uintptr_t begin = *reinterpret_cast<uintptr_t*>(group + kGroupRooms);
    uintptr_t end = *reinterpret_cast<uintptr_t*>(group + kGroupRooms + 4);
    if (!begin || end <= begin || (end - begin) % 0x18 || (end - begin) / 0x18 > 64 || !Readable(begin, end - begin))
        return;
    std::vector<uintptr_t> desks;
    uintptr_t hall = 0;
    for (uintptr_t e = begin; e < end; e += 0x18) {
        uintptr_t r = *reinterpret_cast<uintptr_t*>(e + 0x14);
        if (!r || game::ClassName(r) != "CScriptRoom")
            continue;
        std::string name = NarrowAt(r + kRoomName);
        if (name.rfind("room_tvw_", 0) != 0)
            continue;
        desks.push_back(r);
        if (name == "room_tvw_office")
            hall = r;
    }
    if (!hall)
        return;
    int linked = 0;
    for (uintptr_t r : desks)
        linked += SetHall(r, hall);
    mod::Log("rooms: linked %d TV writers' rooms to their hall", linked);
}

void __fastcall HookScriptOnDrop(uintptr_t room, void* /*edx*/, uintptr_t who)
{
    std::string name = NarrowAt(room + kRoomName);
    if (name.rfind("room_tvw_", 0) != 0) {
        g_scriptOnDrop(room, who);
        return;
    }
    if (!ScriptRoomOffice(room)) {
        // The building isn't a script office (its table entry didn't take), and the script
        // room's handler would call script-office methods on it: treat it as a plain room.
        mod::Log("rooms: %s is not in a script office - plain drop", name.c_str());
        g_original(room, who);
        Report(room, who, name, false);
        return;
    }
    LinkHall(room);
    g_scriptOnDrop(room, who);
    Report(room, who, name, true);
}

uintptr_t __cdecl HookFactory(uintptr_t mesh, int index, void* prefix, int sep)
{
    if (g_nativeWriters) {
        using Record_t = uintptr_t(__thiscall*)(uintptr_t, int);
        uintptr_t record = reinterpret_cast<Record_t>(0x0097EA00)(mesh, index);
        const char* name = record && Readable(record + 4, 24) ? reinterpret_cast<const char*>(record + 4) : "";
        if (strncmp(name, "room_tvw_", 9) == 0) {
            for (const TvDesk& d : kTvDesks) {
                if (strcmp(name + 9, d.suffix) != 0)
                    continue;
                if (uintptr_t room = BuildScriptRoom(d.genre)) {
                    mod::Log("rooms: created script room %s", name);
                    return room;
                }
                mod::Log("rooms: FAILED to build script room %s (exception %08X) - using a plain one", name,
                         static_cast<unsigned>(g_buildFault));
            }
        }
    }
    uintptr_t room = g_origFactory(mesh, index, prefix, sep);
    // The zone's name is filled in by the caller after the factory returns, so read it
    // from the mesh's room record instead: FUN_0097ea00(mesh, index) -> record, name at +4.
    if (room) {
        using Record_t = uintptr_t(__thiscall*)(uintptr_t, int);
        uintptr_t record = reinterpret_cast<Record_t>(0x0097EA00)(mesh, index);
        if (record && Readable(record + 4, 8) && strncmp(reinterpret_cast<const char*>(record + 4), "room_tv", 7) == 0) {
            *reinterpret_cast<unsigned char*>(room + kRoomEnabled) = 1;
            mod::Log("rooms: created enabled zone %s", reinterpret_cast<const char*>(record + 4));
        }
    }
    return room;
}

void __fastcall HookSerialize(uintptr_t base, void* edx)
{
    g_origSerialize(base, edx);
    EnableIfTv(base - 0x64, "loaded");
}

void HookFn(uintptr_t addr, void* detour, void** orig, const char* name)
{
    void* target = reinterpret_cast<void*>(addr);
    if (MH_CreateHook(target, detour, orig) == MH_OK && MH_EnableHook(target) == MH_OK)
        mod::Log("rooms: hooked %s", name);
    else
        mod::Log("rooms: FAILED to hook %s", name);
}

// ---------------------------------------------------------------------------------
// TV scripts on the lot
//
// A script on the lot is a CProjectObject (CProject+0x210 <-> object+0x210). Its 3D icon is
// chosen by name in CProjectObject::UpdateMesh (0x005D35F0): p_icon_write while it's being
// written, p_icon_done when finished, ... (+"_stunt" for stunt films). For TV scripts we
// load our own p_icon_*_tv models (tools/tv_script_icons.py) into the object's render
// Object (+0x11C, SetMesh = vtable +0x18).
//
// Dropping a carried object on a building goes through DropOnRoom (0x00938170), and every
// room class answers "can I take this?" in vtable slot +0x58 (also used for the hover
// prompt). A TV script is refused everywhere except the mod's TV zones (room_tv*), and falls
// to the ground like any refused drop.

constexpr uintptr_t kUpdateMesh  = 0x005D35F0;   // void __fastcall(CProjectObject*)
constexpr uintptr_t kLoadMesh    = 0x009DE1D0;   // Mesh* __cdecl(const char* name, int)
constexpr uintptr_t kReleaseMesh = 0x009DE3B0;   // void __thiscall(Mesh*)
constexpr uintptr_t kDropOnRoom  = 0x00938170;   // bool __thiscall(RoomController*, TMMobile* who)
constexpr uintptr_t kWStringAssign = 0x004036D0;   // WString::Assign(const wchar_t*, int)
constexpr int kObjProject = 0x210, kObjShown = 0x64, kObjRender = 0x11C, kObjStage = 0x24C, kObjSubState = 0x254;
constexpr int kCtrlRoom = 0x210, kRoomPrompt = 0x200, kSlotCanAccept = 0x58;
// Room classes whose CanAccept takes scripts (main vtables), plus TMRoom (accepts anything).
constexpr uintptr_t kScriptRoomVtables[] = { 0x00D6B51C, 0x00D6B604, 0x00D6B764, 0x00D6BDAC, 0x00D6C234, 0x00D6CA64,
                                             0x00D6CD14, 0x00D6CEE4, 0x00D6CFA4, 0x00D6D194, 0x00D6D3F4, 0x00D6D4C4,
                                             0x00D6D7C4, 0x00D6E184, 0x00D6E70C };

ProjectTest g_isTvScript = nullptr;
bool g_tvIcons = false;   // the _tv models are installed
using UpdateMesh_t = void(__fastcall*)(uintptr_t obj);
using DropOnRoom_t = bool(__fastcall*)(uintptr_t ctrl, void* edx, uintptr_t who);
using CanAccept_t = bool(__thiscall*)(uintptr_t room, uintptr_t who);
UpdateMesh_t g_origUpdateMesh = nullptr;
DropOnRoom_t g_origDropOnRoom = nullptr;
std::map<uintptr_t, CanAccept_t> g_origCanAccept;   // vtable -> original slot +0x58

uintptr_t TvScriptOf(uintptr_t who)
{
    if (!who || !g_isTvScript || game::ClassName(who) != "CProjectObject")
        return 0;
    uintptr_t project = *reinterpret_cast<uintptr_t*>(who + kObjProject);
    return project && g_isTvScript(project) ? project : 0;
}

bool IsTvZone(uintptr_t room) { return room && NarrowAt(room + kRoomName).rfind("room_tv", 0) == 0; }

void ApplyTvIcon(uintptr_t obj)
{
    __try {
        if (!*reinterpret_cast<uint8_t*>(obj + kObjShown))
            return;
        int stage = *reinterpret_cast<int*>(obj + kObjStage), sub = *reinterpret_cast<int*>(obj + kObjSubState);
        if (stage != 2 && stage != 3)
            return;   // only scripts: being written, or finished
        const char* name = sub == 0 || sub == 4 ? "p_icon_done_tv.msh" : "p_icon_write_tv.msh";
        uintptr_t mesh = reinterpret_cast<uintptr_t(__cdecl*)(const char*, int)>(kLoadMesh)(name, 1);
        if (!mesh)
            return;
        uintptr_t render = *reinterpret_cast<uintptr_t*>(obj + kObjRender);
        if (render)
            (*reinterpret_cast<void(__thiscall**)(uintptr_t, uintptr_t)>(*reinterpret_cast<uintptr_t*>(render) + 0x18))(
                render, mesh);
        reinterpret_cast<void(__thiscall*)(uintptr_t)>(kReleaseMesh)(mesh);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

void __fastcall HookUpdateMesh(uintptr_t obj)
{
    g_origUpdateMesh(obj);
    if (g_tvIcons && g_isTvScript) {
        uintptr_t project = *reinterpret_cast<uintptr_t*>(obj + kObjProject);
        if (project && g_isTvScript(project))
            ApplyTvIcon(obj);
    }
}

bool __fastcall HookDropOnRoom(uintptr_t ctrl, void* edx, uintptr_t who)
{
    uintptr_t room = ctrl ? *reinterpret_cast<uintptr_t*>(ctrl + kCtrlRoom) : 0;
    if (room && TvScriptOf(who) && !IsTvZone(room)) {
        static int logged = 0;
        if (logged++ < 5)
            mod::Log("rooms: a TV script can't go to %s - only TV buildings take it", NarrowAt(room + kRoomName).c_str());
        return false;   // refused: it falls where it was dropped
    }
    return g_origDropOnRoom(ctrl, edx, who);
}

bool __fastcall HookCanAccept(uintptr_t room, void*, uintptr_t who)
{
    auto it = g_origCanAccept.find(*reinterpret_cast<uintptr_t*>(room));
    if (it == g_origCanAccept.end())
        return false;
    bool ok = it->second(room, who);
    if (ok && TvScriptOf(who) && !IsTvZone(room)) {
        // No "release here" prompt over film buildings.
        reinterpret_cast<void(__thiscall*)(uintptr_t, const wchar_t*, int)>(kWStringAssign)(room + kRoomPrompt, L"", 0);
        return false;
    }
    return ok;
}

// A floor plan attaches to its building's model when a game loads (TMFloorPlan vtable
// slot +0x18, 0x00938250): child 0 of the building's render object (0x0097E350), then it
// waits for that mesh to load (0x009DC300) - with no null check. A building whose model has
// no child node crashed the game there; skip the attach instead and say which building.
constexpr uintptr_t kFloorPlanAttach = 0x00938250;   // bool __fastcall(TMFloorPlan*)
constexpr uintptr_t kRenderChild = 0x0097E350;       // Object* __thiscall(Object*, int index)
constexpr int kPlanOwner = 0x1D4;
using FloorPlanAttach_t = bool(__fastcall*)(uintptr_t plan);
FloorPlanAttach_t g_origFloorPlanAttach = nullptr;

uintptr_t FloorPlanModel(uintptr_t plan, uintptr_t* owner)
{
    __try {
        *owner = *reinterpret_cast<uintptr_t*>(plan + kPlanOwner);
        uintptr_t render = *owner ? *reinterpret_cast<uintptr_t*>(*owner + kObjRender) : 0;
        return render ? reinterpret_cast<uintptr_t(__thiscall*)(uintptr_t, int)>(kRenderChild)(render, 0) : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

bool __fastcall HookFloorPlanAttach(uintptr_t plan)
{
    uintptr_t owner = 0;
    if (FloorPlanModel(plan, &owner) || !owner)
        return g_origFloorPlanAttach(plan);
    std::string what = game::ClassName(owner);
    for (const game::Facility& f : game::Facilities())
        if (f.address == owner)
            what = f.type;
    static int logged = 0;
    if (logged++ < 10)
        mod::Log("rooms: a floor plan's building has no model loaded (%s @%08X) - not attached, game kept running",
                 what.c_str(), static_cast<unsigned>(owner));
    return false;
}

void InstallTvScriptHooks()
{
    HookFn(kFloorPlanAttach, reinterpret_cast<void*>(&HookFloorPlanAttach),
           reinterpret_cast<void**>(&g_origFloorPlanAttach), "floor plan attach (null model guard)");
    wchar_t path[MAX_PATH];
    swprintf_s(path, L"%s\\Data\\meshes\\p_icon_write_tv.msh", mod::GameDir());
    bool write = GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
    swprintf_s(path, L"%s\\Data\\meshes\\p_icon_done_tv.msh", mod::GameDir());
    g_tvIcons = write && GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
    if (g_tvIcons)
        HookFn(kUpdateMesh, reinterpret_cast<void*>(&HookUpdateMesh), reinterpret_cast<void**>(&g_origUpdateMesh),
               "script icon");
    else
        mod::Log("rooms: TV script icons not installed (Data\\meshes\\p_icon_*_tv.msh) - TV scripts look like film scripts");
    HookFn(kDropOnRoom, reinterpret_cast<void*>(&HookDropOnRoom), reinterpret_cast<void**>(&g_origDropOnRoom),
           "drop on building");
    int patched = 0;
    for (uintptr_t vt : kScriptRoomVtables) {
        auto slot = reinterpret_cast<uintptr_t*>(vt + kSlotCanAccept);
        DWORD old;
        if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &old))
            continue;
        g_origCanAccept[vt] = reinterpret_cast<CanAccept_t>(*slot);
        *slot = reinterpret_cast<uintptr_t>(&HookCanAccept);
        VirtualProtect(slot, sizeof(*slot), old, &old);
        patched++;
    }
    mod::Log("rooms: TV scripts refused by film buildings (%d room types)", patched);
}

} // namespace

void SetTvScriptTest(ProjectTest test) { g_isTvScript = test; }

namespace {
uintptr_t ProjectObjectOf(uintptr_t project)
{
    __try {
        return *reinterpret_cast<uintptr_t*>(project + kObjProject);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}
} // namespace

void RefreshScriptLook(uintptr_t project)
{
    if (!project || !g_origUpdateMesh)
        return;
    uintptr_t obj = ProjectObjectOf(project);
    if (obj && game::ClassName(obj) == "CProjectObject")
        HookUpdateMesh(obj);
}

namespace {

} // namespace

void SetHandler(DropHandler handler) { g_handler = handler; }

uintptr_t ScriptRoomOffice(uintptr_t room)
{
    if (!room || !Readable(room + kRoomParent, 4))
        return 0;
    uintptr_t group = *reinterpret_cast<uintptr_t*>(room + kRoomParent);
    if (!Readable(group + kGroupFacility, 4))
        return 0;
    uintptr_t facility = *reinterpret_cast<uintptr_t*>(group + kGroupFacility);
    return facility && game::ClassName(facility) == "CFacilityScriptOffice" ? facility : 0;
}

uintptr_t ScriptRoomProject(uintptr_t room)
{
    if (!room || game::ClassName(room) != "CScriptRoom" || !Readable(room + kScriptRoomProject, 4))
        return 0;
    uintptr_t p = *reinterpret_cast<uintptr_t*>(room + kScriptRoomProject);
    return p && game::ClassName(p) == "CProject" ? p : 0;
}

uintptr_t OfficeProject(uintptr_t office)
{
    if (!office || !Readable(office + kOfficeProject, 4))
        return 0;
    uintptr_t p = *reinterpret_cast<uintptr_t*>(office + kOfficeProject);
    return p && game::ClassName(p) == "CProject" ? p : 0;
}

void Install()
{
    auto slot = reinterpret_cast<uintptr_t*>(kTMRoomOnDropSlot);
    DWORD old;
    if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &old)) {
        mod::Log("rooms: could not unprotect the TMRoom vtable");
        return;
    }
    g_original = reinterpret_cast<OnDrop_t>(*slot);
    *slot = reinterpret_cast<uintptr_t>(&HookOnDrop);
    VirtualProtect(slot, sizeof(*slot), old, &old);
    mod::Log("rooms: plain-room drop handler hooked (original %08X)", reinterpret_cast<unsigned>(g_original));
    HookFn(kRoomFactory, reinterpret_cast<void*>(&HookFactory), reinterpret_cast<void**>(&g_origFactory), "room factory");
    HookFn(kRoomSerialize, reinterpret_cast<void*>(&HookSerialize), reinterpret_cast<void**>(&g_origSerialize), "room load/save");
    InstallTvScriptHooks();

    wchar_t ini[MAX_PATH];
    swprintf_s(ini, L"%s\\MoviesMod.ini", mod::GameDir());
    g_nativeWriters = GetPrivateProfileIntW(L"MoviesMod", L"NativeTvWriters", 1, ini) != 0;
    if (!g_nativeWriters) {
        mod::Log("rooms: NativeTvWriters = 0 - TV writers' desks stay plain rooms");
        return;
    }
    slot = reinterpret_cast<uintptr_t*>(kScriptOnDropSlot);
    if (VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &old)) {
        g_scriptOnDrop = reinterpret_cast<OnDrop_t>(*slot);
        *slot = reinterpret_cast<uintptr_t>(&HookScriptOnDrop);
        VirtualProtect(slot, sizeof(*slot), old, &old);
        mod::Log("rooms: script-room drop handler hooked (original %08X)", reinterpret_cast<unsigned>(g_scriptOnDrop));
    }
    HookFn(kFacilityTableInit, reinterpret_cast<void*>(&HookTableInit), reinterpret_cast<void**>(&g_origTableInit),
           "facility class table");
    RegisterTvOffice();   // in case the table was filled before we got here
}

} // namespace rooms
