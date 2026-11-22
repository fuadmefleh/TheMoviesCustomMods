# Costumed mascot walkabout: RE notes (static, not run-time tested)

Dumps: re/mas_i1..i9.txt (decompiles), mas_vt1 (CStaff vtable 0x00D267CC), mas_vt2..vt5 (desire/explainer vtables),
mas_s1..s4 (string refs), mas_d1..d4 (disasm), mas_r1 (RetSize).

## Classes
TMMobile -> TMCharacter (TMCharacter.cpp) -> CPerson -> CStaff (vtable 0x00D267CC) -> CExtra 0x00D24A0C / CStar 0x00D27A5C / CWannabe 0x00D28B44.
Secondary vtables at +0x78 (the saveable base for TMCharacter fields: save offset + 0x78) and +0xA0.

## Costume
| what | address / offset | conv |
|---|---|---|
| Build CCostume from name | `0x004335F0 CCostume** __cdecl(CCostume** out, String* name, int gender /*person+0x4A0*/, 0, 0, 0)` | cdecl, ret ref held in *out (refcount +0x48; release = dec, if 0 call vtbl[0](1)) |
|  - name first char F/G/M/f/g/m = one costume (new 0xE0 CCostume, ctor 0x004330C0); anything else = a `costume/<list>` list (costume_janitor...), random pick via 0x004331D0 | | |
| Blueprint lookup | `0x00959A40(String*, ?)` -> CCostumeBlueprint | |
| CCostume name String | `+0x78` ; compare 0x00430D70 thiscall(a, b) -> bool same name | |
| Person Current costume ("PCurrentCostume") | `+0x568` | |
| Person Desired costume ("PDesiredCostume") | `+0x56C`, get 0x0059BB90 (fastcall), set 0x0059BB60 thiscall(person, CCostume*) RET 4 (addrefs) | |
| Person Normal costume ("PNormalCostume") = own clothes | `+0x570`, set 0x0059BBA0 thiscall RET 4; GetNormal 0x0059BBD0 fastcall (builds from vfunc +0x124 list name if null, falls back to m_none/f_none, also fills Desired if null) | |
| SetCostume (vtable slot 74, +0x128) | `0x0059BAC0 void __thiscall(person, CCostume*, char rebuild)` RET 8; addrefs, stores +0x568; if person has a model (+0x6FC) and rebuild: calls vfunc +0x12C | |
| Rebuild look (slot 75, +0x12C) | `0x0059C740 __thiscall(person, char)` RET 4: re-composites texture via 0x004319B0 (loads .cos) + 0x009D2C50 | |
| GetCurrentCostume | `0x0059C6E0 CCostume* __thiscall(person, char)` RET 4 (dresses in Normal if none) | |
| Career dresser | `0x00598540 __cdecl(CStaff*, career)`: build from tasks.ini list -> if != current: SetCostume(c,1), SetDesired(c), SetNormal(c) | |

DesireUpdateCostume (vtable 0x00D5F9FC): slot 9 0x00841BE0 fires when Desired != null and Current != Desired;
slot 12 0x00841BB0 does SetCostume(person, Desired). So the AI will change a person back to Desired.
DesireGetChanged (ctor 0x00836770, "changecostume") is the wardrobe trip.

Recipe: c = Build("g_mascot"); save = AddRef(+0x568), saveDesired = AddRef(+0x56C);
SetCostume(p, c, 1); SetDesired(p, c); release c.
Restore: SetDesired(p, saveDesired or +0x570) and either SetCostume(p, same, 1) or let DesireUpdateCostume do it
(on-spot change scene ai_cos_change_on_spot.flm from 0x008B4B40). Don't touch +0x570.

## Walking: activities
Person activity queue: head +0x4F8 (node+8 = CActivity*), sentinel +0x504. Desires list +0x53C..+0x548 (name String at desire+0x64).
| what | address | conv |
|---|---|---|
| Scene-at-point (CFreeActivityManager, 0x2E0 bytes) | ctor `0x00445F60 __thiscall(mem, float pos[3], float angle)` RET 8; snaps z to ground; then `vfunc +0xB0(String* "ai_xxx.flm")` loads the scene | |
| CActivity (0x2B4) | ctor `0x00402380 __thiscall(mem, person, scene)` RET 8 (addrefs scene) | |
| Activity set desire | `0x00401A00 __thiscall(act, desire)` RET 4 (+0x23C) | |
| Activity set explainer | `0x004016F0 __thiscall(act, explainer)` RET 4 (+0x274) | |
| Activity interruptible flag | `+0x225` (ctor = 1). 0x00401050(act, 0) clears it | |
| FindDesire by name | `0x0059C530 __thiscall(person, const char*)` RET 4 | |
| Current activity | `0x005998E0 __fastcall(person)` | |
| Cancel/remove activity | `0x0059ADD0 __thiscall(person, act)` RET 4 | |
| AddAction | `0x0059C020 __thiscall(person, act)` RET 4 (appends; cancels current if interruptible) | |
| Random walkable lot point near person | `0x004EEBE0 float* __cdecl(float out[2], person, float minDist)` | |
| Position of any asset/person | vfunc +0x34: `float* __thiscall(obj, float out[3])` ; facility pos +0x100, angle +0xC4 | |

Templates: CWanderExplainer slot 2 `0x008BE1B0` (random point, ai_donothing.flm, the person walks there);
forced janitor tidy `0x00462BE0` (desire FindDesire("task_janitor"), +0x225=0, cancel current, AddAction, +0x292=1, +0x240=2).

## Reactions
AweExplainer (vtable 0x00D64214) slot 2 0x008AE490: builds ai_awed_react_to_star.flm at the person facing the star (+0x110).
Its condition (slot 1) reads star-only data (0x00585FF0), so don't attach it to an extra; instead queue
ai_awed_react_to_star.flm / ai_watchfight.flm / ai_autograph.flm activities on nearby people yourself with the recipe above.
Mood: CStar mood +0xC98 -> work bar, AddStress 0x00475130 / AddBoredom 0x00475040 (see src/manager.cpp).
