# Game internals

Reverse-engineered facts about **`MoviesSE.exe` v1.2** (The Movies: Stunts & Effects).

**Status column:**
- **Verified:** confirmed in a running game.
- **Static:** read from the disassembly only.
- **Built:** in use by code that hasn't run in game yet.

## Contents

- [The executable](#the-executable)
- [Core types](#core-types)
- [Global addresses](#global-addresses)
- [Functions](#functions)
- [Object layouts](#object-layouts)
- [Data files](#data-files)
- [Costumes](#costumes)
- [Reverse-engineering workflow](#reverse-engineering-workflow)

## The executable

| Property | Value |
|---|---|
| Format | PE32, 32-bit x86, Windows GUI |
| Image base | `0x00400000`, **no ASLR**, so every address below is absolute |
| Timestamp | `0x44500000` (26 Apr 2006). The mod refuses to hook any other build. |
| Compiler | MSVC 7.1 (Visual Studio .NET 2003) |
| RTTI | Present. Classes are in namespace `TM` (for example `.?AVCFacility@TM@@`). |
| Source path leaked in strings | `C:\movies\dev\TheMovies\` (for example `Facility.cpp`) |
| DirectInput | `DINPUT8.dll` is delay-loaded; only `DirectInput8Create` is imported |
| Large Address Aware | Not set by Lionhead; this project sets it (backups in `*.pre-laa.bak`) |

The project analyses a copy at `re\MoviesSE.exe`, because Ghidra's batch launcher fails on the `(x86)` in the install path.

## Core types

### `String`, `WString`

The game uses its own small-string types:

```
struct String  { char*    text; int len; int cap; char    inline[20]; };  // cap 0x14 when inline
struct WString { wchar_t* text; int len; int cap; wchar_t inline[10]; };  // cap 10 when inline
```

`text` points at `inline` while the string is short. A buffer is freed only when `cap` is larger than the inline size.

### `InList<T>`: intrusive linked list

```
+0x00  vtable
+0x08  first node*
+0x14  sentinel node (end marker)
node:  { node* prev; node* next; T* item; }
```

To iterate: start at `[list+8]` and follow `node+4` until you reach `list+0x14`; the item is at `node+8`.

### `CP<T>`: smart pointer
24 bytes, with the raw pointer at `+0x14`.

### Money
`int64` fixed point: **dollars × 2²³** (`8388608`).

### Date
A `float` counting **days since year 0**. The year is `days / 365`. For example, `704460.8` is early 1930, and `712197.25` is March 1951.

## Global addresses

| Address | Type | What | Status |
|---|---|---|---|
| `0x0104BCE4` | `CStudioPlayer*` | The player's studio; null until a game is loaded | Verified |
| `0x00F87ED8` | `CFinance*` | The player's finances | Verified |
| `0x00E4FA4C` | `float` | Current game date (days) | Verified |
| `0x0104D680` | `InList<CProject>` | All projects in the world. Showed only 1 in a 1951 game, so it may not include rivals. | Verified |
| `0x0104ED10` | `InList<CFacility>` | Every building on the lot (constructed by Facility.cpp's static initialiser `0x00D0C1F0`) | Built |
| `0x01050878` | list head | Facility blueprints, linked through blueprint `+0xD0` | Static |
| `0x0105CC70` | `CStringDatabase*` | Localised text | Built |
| `0x0104ED0C` | `bool` | "Facility menu table initialised" flag | Static |

## Functions

Calling conventions as used by the mod. A `this` parameter means `__thiscall` (`ecx`).

| Address | Signature | What | Status |
|---|---|---|---|
| `0x00558610` | `float TuningFile::GetFloat(this, String* key, float def, int flag)` | Every tuning value; `ret 0xC`. The file name is a `String` at `this+0xA8`. | Verified (hooked) |
| `0x00558750` | `int TuningFile::GetInt(this, String* key, int def, ...)` | Integer tuning values | Static |
| `0x005584E0` | `String* TuningFile::GetString(this, String* out, String* key, ...)` | String tuning values | Static |
| `0x00558A50` | `bool TuningFile::EnterSection(this, String* name)` | Selects an ini section | Static |
| `0x0055BE10` | | Loads a tuning file (`"data/" + name`) | Static |
| `0x004769E0` | | Loads the `mood` stress and boredom values | Static |
| `0x00442B60` | `void CFinance::Transact(this, uint32 lo, int32 hi, int category)` | Adds an `int64` amount. Category 8 means uncategorised. | Verified |
| `0x005B2350` | `void CProject::GetQuality(this, float* out)` | Film quality, 0 to 1 | Verified |
| `0x0098B490` | `bool FieldName(const char* name)` | The save serializer's named-field marker. Used to map save fields to object offsets. | Static |
| `0x009B5030` | `WString* __cdecl LookupText(WString* out, String* key)` | All display text. A missing key returns the key itself. | Built (hooked) |
| `0x00650E40` | `bool WBuildMenu::OnBuildButton(this, WWindow* button)` | Build-menu click: names starting `facility`/`trailer` → `CreateFacility("facility/"+name, 1)` (placement ghost) | Static |
| `0x008480B0` | `CFacility* __cdecl CreateFacility(String* path, char flag)` | Checks `data/<path>.ini` exists, creates the facility (default `CFacility` for names not in the `FUN_00847210` table) | Static |
| `0x009D3660` | `bool __cdecl FileExists(String* path, int* size)` | Pak archives first, then loose files | Verified (hooked) |
| `0x00A35EB0` | `int CStringDatabase::FindIndex(this, const char* key)` | 0 means missing | Built |
| `0x00A35E50` | `wchar_t* CStringDatabase::GetText(this, int index)` | | Static |
| `0x004036D0` | `void WString::Assign(this, const wchar_t* s, int len)` | | Built |
| `0x0095D630` | | Enumerates `data/facility/` | Static |
| `0x0095D010` | | Building factory: reads a facility `.ini` and creates a `CFacilityBlueprint` (`0x128` bytes) | Static |
| `0x0095CB30` | | `CFacilityBlueprint` constructor | Static |
| `0x00847210` | | Builds the name → menu handler table for script offices, trailers, stage, production, post, marketing, publicity, catering, bar, pre-production and stunt. **Names not in the table get default behaviour.** | Static |
| `0x00845170` | | `CFacility` save function; serializes `SubFacilities` at `+0x420` | Static |
| `0x00ACE790` | `__RTDynamicCast` | | Static |
| `0x005C43C0` | `CProject* __cdecl SpawnScript(String* stockName, float xy[2])` | A finished script on the lot with sparkles (research-pack sample scripts). An empty name makes a short one-scene script. | Built (called) |
| `0x005C1AE0` | `CProject* __cdecl GenerateScript(Genre* genre, float size, char stunts)` | The script office's random script: `size` 0–1 picks 1–3 acts (`+0x3A4`) | Static |
| `0x005C18B0` | `CProject* __cdecl NewProject()` | Empty player project with its `CProjectObject` | Static |
| `0x009362E0` | `CScriptRoom::OnDrop(this, TMMobile*)` | A writer dropped on a genre desk: stock script (quality near `room+0x274`) or `GenerateScript(room+0x28C, ...)` | Static |
| `0x00935570` | `CScriptRoom::CScriptRoom(this)` (`__thiscall`, 0x2AC bytes from `operator new` `0x00ACBC50`) | Then `0x00934770(this, genre)` sets the genre (`+0x28C`), `+0x1E0 = 0`; the hall instead gets `0x0093C4B0(this, String* "ai_room_so_hall")`. Room `+0x270` project, `+0x288` quality, `+0x2A4` hall (smart ref at `+0x290`) | Called |
| `0x00935030` | `CScriptRoom` accept/action (vtable slot at `0x00D6E1DC`) | Needs the building to be a `CFacilityScriptOffice`; one desk writes at a time (`SITT_ACTION_SCRIPTROOM_START` / `_ASSIST`) | Static |
| `0x00847210` | Facility-class table init | Map at `0x0104ED5C` (ready flag `0x0104ED0C`), `0x00846EC0` = `slot* __thiscall(map, String* key)`; keys like `facility/facility_script1` -> creator `0x0084AB00` (`CFacilityScriptOffice`, 0x550 bytes). Unknown names get a plain `CFacility` | Hooked |
| `0x0093A850` | Room-group setup | Links every `CScriptRoom` to the hall only when the zone prefix is `so` | Static |
| `0x0044A8D0` | `Genre* (String* "genre_comedy")` | Genre handle; script rooms keep theirs at `+0x28C` | Static |

## Object layouts

### Save-function offsets

The game's save functions call `FieldName("Name")`, then a typed read or write on the field's address. Field addresses in save functions are relative to the class's serializable base, which sits at **object +0x64**. So:

**object offset = save offset + 0x64**

Field maps produced this way are in `re\all_fields.txt` and `re\cproject_map.txt`.

### `CStudio` / `CStudioPlayer`

| Offset | Type | Field | Status |
|---|---|---|---|
| `+0x0C0` | `InList<CProject>` | BackCatalogue. Was empty with one released film in 1951; it may fill only after a film leaves cinemas. | Verified (read) |
| `+0x12C` | `float` | Prestige | Verified |
| `+0x130` | `float` | Star rating | Verified |
| `+0x18C` | `String` | Studio name | Verified |
| `+0x1E4` | `int` | Films released | Verified |
| `+0x1F0` | `InList<CProject>` | Player's projects, including released films (stage 7) | Verified |

### `CFinance`

| Offset | Type | Field | Status |
|---|---|---|---|
| `+0x90` | `int64` money | Capital (live balance) | Verified |
| `+0x98` | `int64` money | Balance shown in the UI (updated by the game a moment later) | Verified |

### `CProject` (a film)

| Offset | Type | Field | Status |
|---|---|---|---|
| `+0x170` | `float` | Star rating (about 0 to 1) | Verified |
| `+0x228` | `CProjectPhase*` | Phase. The stage number comes from virtual `+0x24`; 7 means released. | Verified |
| `+0x25C` | `String` | Title | Verified |
| `+0x288` | `float` days | Release date | Verified |
| `+0x2C8` | `float` days | Start date | Verified |
| `+0x360` | `float` | Raw quality | Verified |
| `+0x3A0` | `CProjectIncome*` | Box office | Verified |

### `CProjectIncome`

| Offset | Type | Field | Status |
|---|---|---|---|
| `+0x90` | `int64` money | Pool still to earn | Verified |
| `+0x98` | `int64` money | Pool at release (earned so far = initial − pool) | Verified |

### `CFacilityBlueprint` (`0x128` bytes)

| Offset | Field | Status |
|---|---|---|
| `+0x38` | maxinstances | Verified (live memory) |
| `+0x40` | `int64` money: purchase cost | Verified (live memory) |
| `+0x88` | Unknown. 0 for every building at load; later 1 on most and 4 on new ones. Forcing it to 1 changed nothing. | Verified (live memory) |
| `+0xAC` | `String` name (file stem), returned by virtual slot 2 | Verified (live memory) |
| `+0xD0` | Link in the global blueprint list | Static |
| `+0xE0` | `String` mesh name, `.msh` stripped | Static |
| `+0x100` | `WString` (probably the display name) | Static |
| `+0x120` | menutype | Static |
| `+0x124` | grouporder | Static |

**Virtual slots:**
- slot 2: the name String
- slot 3: loads the model `data/meshes/<mesh>` used for the placement ghost
- slot 9: can afford (cost vs money, instance limit)

### `CFacility`
- `+0x33C`: `String` holding the building's `facility/<name>` path (found at runtime). Verified.
- `+0x100`: position `float[3]`; `+0xC4`: facing angle in radians (`TMFixedAsset` virtuals `+0x34` / `+0x48`). Built.
- `CFacilityScriptOffice` `+0x528`: the `CProject` being written (cleared when it is put outside), `+0x52C`: the room writing it. A room's facility is `*(*(room+0xC0)+0x24C)`.

### RTTI

To get the class name of any polymorphic object:
1. `vtable = [obj]`
2. `col = [vtable − 4]` (CompleteObjectLocator; its signature is 0)
3. `td = [col + 0xC]` (TypeDescriptor)
4. The name string is at `td + 8`, for example `.?AVCFacility@TM@@`.

`tools\rtti.py` does this offline, and `game::ClassName` does it live.

Facility classes: `CFacility`, `CFacilityBar`, `CFacilityCanteen`, `CFacilityMarketing`, `CFacilityPost`, `CFacilityPreProduction`, `CFacilityProduction`, `CFacilityScriptOffice`, `CFacilityStageSchool`, `CFacilityStuntSchool`, `CFacilityTrailer`, `CSubFacility`, `CFacilityBlueprint`.

## Data files

### Pak archives (`Data\*.pak`, Addon paks)

All little-endian. `tools\pak.py` reads versions 4, 5 and 6, and all 81,935 files verify.

```
header      u32 version, u32 data_start, u32 0, u32 file_count, u32 0x100,
            u32 dir_count, u32 dir_names_len, then table offsets:
              v4:    u32 table, u32 hash_table, u32 dir_names
              v5/v6: u32 ?, u32 0, u32 0, u32 table, u32 hash_table, u32 dir_names
file entry  56 bytes: u64 name_hash, u32 offset, u32 stored_size, u32 size, u32 flags, char name[32]
            directory = dir_names + ((flags & 0x7FFF) >> 1)
dir_names   NUL-separated, e.g. "data\facility\"
file data   blocks of { u32 block_stored, u32 block_size, u32 zlib_len, u32 raw }
            followed by zlib data, or block_size raw bytes when raw == 1
            (.msh, .flm etc. carry their own "zcmp" compression and are stored raw)
```

**Loading order:**
- Addon paks (`Addon_2704_*`) override base paks.
- **Loose files** under `Data\` load too, which is how the mod adds buildings (verified).

### Facility `.ini` (`data\facility\*.ini`)

The file name (the "stem") is the building's identity. The file starts with top-level keys (`mesh`, `initialrotation`), followed by sections:

| Section | Keys |
|---|---|
| `[finance]` | `purchasecost`, `annualcost`, `dailyrate` |
| `[description]` | `attractiveness`, `quality`, `territory`, `ownable`, `capacity` (staff inside) |
| `[attributes]` | `Radio` |
| `[maintenance]` | `decaytime`, `repairwork`, `buildingwork`, `rebuildingwork` |
| `[particles]` | `position`, `type` (e.g. `chimneysmoke`), `partspersecond` |
| `[blueprint]` | `maxinstances`, `trickiness`, `given`, `researchable`, `ETA` (year), `path` (menu group, e.g. `group_trailers`), `grouporder`, `menutype`, `availableindebt` |
| `[blueprint/requires]` | `0=date_1930`. Known-valid tokens are `date_1930`, `date_1950`, `date_1960`, `date_1970` and `date_1980`. It's unclear whether a token that has already passed unlocks a newly added building; the TV buildings don't use it and gate eras in mod code instead. |
| `[subfacilities]`, `[extra_info]` | |

**The file name must start with `facility` or `trailer`.** The build-menu click handler (`FUN_00650e40`) passes only those prefixes to `CreateFacility` (`FUN_008480b0`, `cdecl(String* name, char ghost)`); `set…` names go to the set loader, and anything else is silently ignored. The building then appears in the menu but no placement ghost shows. Verified.

**Display name:** the stem in capitals is a string-database key, e.g. `facility_sell` → `FACILITY_SELL`. Trailer descriptions use `FAC_TRAILERn`.

**Cut content found in code and text, but with no data file:**
- `trailer_6` to `trailer_9`
- `facility_script_5star` ("Script Office: Ultimate")
- `facility_marketing` ("Press Office")
- `facility_post`

**Unused text keys:** `FACILITY_RELEASE` ("Release Office"), `FACILITY_GYM`, `FACILITY_KENNEL`, `FACILITY_STABLE`, `FACILITY_SCRIPT2/3`.

**Meshes:** `fac_casting_office`, `fac_catering0..2`, `fac_cosmeticsurgery`, `fac_crewfacility`, `fac_customscriptoffice`, `fac_detox`, `fac_fightroom`, `fac_fullbodyburn`, `fac_gatehouse`, `fac_hospital`, `fac_postproduction`, `fac_prod_office`, `fac_publicity_office`, `fac_research`, `fac_scriptoffice_1..4`, `fac_sell`, `fac_spartantd`, `fac_stageschool`, `fac_stuntschool`, `fac_toilet1..3`, `fac_trailer1..5`, `fac_wardrobe` (all `.msh`).

### Building models (`data\meshes\*.msh`)

Each building has three models: `fac_<name>` (outside), `fld_<name>` (the cut-away floor plan shown while carrying a person) and `blp_<name>` (the construction ghost, groups `sfi_hideoff_3`, `sfo_hideoff_2`, `blp_hideoff_1` hidden in stages).

- Files are wrapped as `zcmp`, u32 size-4, u32 raw size, u32 zlib size, u32 0, then zlib data. Inside is format version 10; `tools\msh.py` documents the layout and reads and writes it (all 206 building models rebuild byte-identical).
- Vertices are in lot coordinates (metres, Z up); group pivots don't move them. Compressed vertices are 8 x u16 scaled by 14 range floats (position min/max, UV min/max, lightmap UV min/max).
- Materials are 24 bytes: image indices for diffuse, reflection, lightmap, unused (0xFF = none), then flags and a BGRA colour.
- **Drop zones** are the model's rooms: a 32-byte name plus a flat triangle mesh, in both `fac_` and `fld_`. They are copied into the save when the building is placed.
- **Shapes** are box volumes: u32 0, a 3x3 rotation (rows are the box axes), the centre of the box's base, then width, depth, height.
- **Convex hull** (flagsA 0x80) is a memory image with pointers relative to an arbitrary base: u32 `0x10000 | faces`, ptr to the face pointer array, ptr to the footprint outline, centre and half size, then the face pointers. Each face or outline record is u32 n, 2D bounds, a ptr to n float3 points. Faces wind counter-clockwise seen from outside. `tools\hull.py` builds it.

### Building extra info (`data\meshes\extrainfoac_<name>.inf`)

u32 version (1), u32 section count, then sections of u32 type, u32 size (including these 8 bytes):

| Type | Contents |
|---|---|
| 2 | Points: u32 count, u32 0, then per point float x, y, facing angle and a 32-byte name (`_fp_door`, `_fp_dot`, `repair_NN`, `leanspot_NN`, `peerspot`, `build_N`, `moveasset`) |
| 7 | Thumbnail camera |
| 13 | Unknown (`[land above]`) |
| 11 | Zone label: u32 0, 32-byte zone name, then start x, y, z, angle, length and text height (1.5) of the line the zone's name is written along |

### String database (`Data\text\<lang>\stringdatabase*.lhts`)

- Header magic `LHTS`, then `TheMoviesAddOn`, `en-uk`, a count and an index.
- Each entry: a 2-byte id, an ASCII key, then `00 00 FF 00` and UTF-16LE text.
- Lookups are case-insensitive and hashed (`FindIndex`).

The mod doesn't edit these files. It adds text through the `LookupText` hook instead.

### Save games (`Documents\The Movies\Saved Games\*.jad`)

- The game opens them with `CreateFileA/W` and uses `MoveFileA/W` and `DeleteFileA/W`.
- The load menu opens every save for reading (to show its header).
- New games start from `Data\Scenarios\builtlot.jad` or `emptylot.jad`.
- File names look like `<Studio><day> <Month> <year><time><weekday><date>.jad`.

### UI

- The interface is Flash SWF 5 (`.mfl`, uncompressed `FWS`).
- The font is Arial Rounded MT Bold (`Data\fonts\*.fnt`).
- The palette is in `re\ui_palette.json` (from `tools\swfpalette.py`).

### Timing

- About 1.6 game days pass per real second at normal speed, so a year takes about 4 minutes.
- A film takes about one game year to make.

## Costumes

A costume is a blueprint (`TM::CCostumeBlueprint`) named after its file. `tools\costumes.py` builds new ones from existing ones; the format below is what it reads and writes (all 464 game `.cos` files rebuild byte-identical).

### Files

| File | Contents |
|---|---|
| `data\costume\<name>.ini` | Top-level `cost`, `start`, `end`, `glamour`, `boredom`; `[fashionable]` `start`/`peak`/`end`; `[age]` `min`/`max`; `[physique]` `thin`/`normal`/`fat`; `[genre]` `genre_horror`/`_action`/`_comedy`/`_romance`/`_sci-fi`; `[animation]` `name` (`cos_generic.flm`, `cos_wes_gunslinger.flm`...); `[blueprint]` `trickiness`, `given`, `researchable`, `ETA`, `path`, and `[blueprint/requires]` `0 = date_YYYY` (game costumes use all sorts of years, e.g. `date_1914`, `date_1949`). CRLF, tab-indented. The top-level `start`/`end` look unreliable in the game's own files (most men's costumes say 1985-2010); `[fashionable]` carries the era. |
| `data\costume\datas\<name>.cos` | Binary: mesh name, texture layers, accessory meshes (below). Loaded by `FUN_004319b0` as `data/costume/datas/<name>.cos`; a missing file falls back to `m_none.cos`/`f_none.cos` by the name's first letter. |
| `data\meshes\cos_<mesh>.msh` | The body mesh named in the `.cos`. Skinned (30-bone rig `0x1E449A00`, up to 4 influences, a neck seam joining the head), ~2,000 vertices. `tools\msh.py` can't read these; `tools\charmsh.py` can. |
| `data\meshes\extrainfo\cos_<mesh>.inf` | Per-mesh extra data (150-280 KB). Section type 1 is u32 vertex count + 72 bytes (18 floats) per vertex, probably the thin/fat physique morphs; type 4 unknown. Keyed by mesh, so costumes sharing a mesh share it. |
| `data\textures\costumes\<layer>.dds` | Texture layers: 512x512 DXT3 with 10 mips (2,833 files; 83 DXT5, 9 DXT1; a few 16x16 to 512x1024). Every layer is a full sheet in the body's UV layout, transparent except where its piece is. |
| `data\textures\cos_<mesh>.dds` | A pre-baked composite (layers + skin) for 295 of 384 meshes. Not needed: 89 meshes (incl. most S&E costumes) have none, and costumes sharing a mesh look different, so the game composites at run time (into one 512x512 texture per person, see `[MovieMaker] CharacterTextureSize`). |
| `data\textures\thumbs\costumes\<name>.dds` | Wardrobe thumbnail: DXT1 128x256, no mips (all 410). |
| `data\costume\datas\infotextures.fas` | u32 0, u32 count (390), u32 ?, then 52-byte records: char[32] layer texture, u32 year (e.g. 1920 or 0), u32, u32, u32, u32 0. Looks like style/colour data for the costume designer's variants; loaded by `FUN_00a71c60`. New layers don't need an entry. |

Names start with `m_`, `f_` (or `g_`) for the gender; the wardrobe compares a gender field (`+0x98`, read by `FUN_0042feb0`) with the person's, and 2 matches both. Static.

### `.cos` layout (`FUN_009ced70`; header `FUN_009cc940`, layers `FUN_009cd880`, accessories `FUN_009cd980`)

Little-endian. Every game file is version 7.

```
0x00 u32  version (7). <6: layers have no u32 at +0x50; <7: no 5 u32 at 0x90
0x04 u8   flags: 1 = ?, 2 = a u32 trailer count follows at 0xA4, 4 = don't add the
          automatic latex head
0x05 u8   bit 0 -> flag
0x06 u8   number of texture layers (5 bits)
0x07 u8   bit 0 -> flag 0x1000 (also stops the latex head being added)
0x08 u8   2 bits -> flags
0x09 u8   ? (0..7)
0x0A u16  unused
0x0C char[32] x 4  mesh names; only the first is used ("cos_m_wes_1.msh"); the others are
          empty (or tool garbage). Any costume's mesh can be named: 120 costumes reuse
          another's (all karate belts share cos_m_karate_1.msh)
0x8C u32  number of accessories (5 bits)
0x90 u32  start year, u32 end year (0 in some files), u32 x 3 unknown (small numbers)
0xA4 u32  number of 64-byte trailer records (only if flags & 2; garbage in the 25 files
          that have it, so write 0)
then      layers, accessories, trailer records
```

**Texture layer** (120 bytes; 0x54 + 36 when byte 0x50 has bit 8, which every file sets):

```
+0x00 char[32] texture file name (com_m_wes_shirt_1_v00.dds), searched in data/textures/
+0x20 u32 flags (1 = alpha/skin mask layer, e.g. com_f_20s_und_a.dds)
+0x24 f32 stacking order: composited ascending (shoes 0 ... shirt 0.67, waistcoat 0.81,
          dirt 1.0); the masks ending _a.dds use 0.5. Default when absent 0.5
+0x28 u32 1 (type), +0x2C u32 0
+0x30 char[32] slot: cos_shirt, cos_trousers, cos_shoes, cos_dirt... (text keys COS_*);
          empty for masks and accessory textures
+0x50 u8 flags (8 = 36-byte option block follows; 1 = ?), u8, u16
+0x54 36 bytes option block (zero for layers)
```

- A layer name ending `_vNN.dds` is one style of a family: the game derives `_v00` and counts the variants for the costume designer's next/previous style (`FUN_009ad890`). Names without the pattern have no variants.
- A layer whose name doesn't start with `com_` is an accessory's texture: it is paired with the accessory mesh of the same stem (`acc_holster_wes_1_v00.dds` + `.msh`).

**Accessory** (120 bytes in every file):

```
+0x00 char[32] mesh (hat_wes_v00.msh, acc_glasses_tz1_v00.msh, acc_m_ring_v00.msh ...)
+0x20 u8 attachment (1 = hat, 5 = glasses, 0 = body); acc_glasses_* with 0 become 5
+0x21 u8 flags: 2 = u32 at +0x50, 4 = 36-byte block follows, 0x10/0x01 force +0x2C = 1
+0x22 u16, +0x24 f32 (0.46-0.98; ring low, hat high), +0x28 u32 2 (type), +0x2C u32
+0x30 char[32] slot (cos_hat, cos_glasses, cos_holster ...)
+0x50 u32, then the 36-byte block (hats: floats 0, 0.27, 0.5, 20, 3, 0, 1.65, 0, 0, 1.65)
```

Four files (`m_dietician`, `m_patient`, `m_patient_2`, `m_priest`) carry one more 120-byte record than they count; the game never reads it.

### Wardrobe tabs (categories)

- `data\costume\category.ini` lists the tabs as sections: `[category_western]` `icon = pcgui_cos_wildwest`, `cat = category_western`. The menu (`FUN_00671680`) reads each section's `cat`, adds `category_premium` if missing, then `category_custom` (saved custom costumes), `category_extra` and `category_all`.
- A tab's members are the lines of `data\costume\<cat>.ini` (one name per line, tab-indented). `FUN_00685b00` looks each name up as a costume blueprint (`FUN_00959a40`) and shows it if it is available (`FUN_00960f30`), of the right gender, and passes a date check (`FUN_0043b710`/`FUN_0043b6a0`, skipped when the list's flag `+0x388` is set). Static.
- Every costume's `[blueprint] path` equals the category file that lists it (442 of 442); `path = NONE` (114, e.g. karate belts, crew outfits) means no tab.
- **Adding costumes to a tab:** ship a loose copy of the category file with every original name plus the new ones (`tools\costumes.py` does this from the pak copy).
- **A new tab** would be a loose `category.ini` with an extra section, named by text key `CATEGORY_<NAME>`; untested.
- `data\costume\costume_<role>.ini` (e.g. `costume_western.ini`) are different: lists of costumes the game dresses extras and staff in, with a `[fallback]`.
- Tab names are text keys: `CATEGORY_CASUAL`, `CATEGORY_SMART`, `CATEGORY_WORK`, `CATEGORY_UNDERWEAR`, `CATEGORY_MONSTER`, `CATEGORY_FUTURISTIC`, `CATEGORY_MILITARY`, `CATEGORY_WESTERN`, `CATEGORY_PREMIUM`.

### Display name

`cosname_<name>` through `LookupText` (`FUN_004317b0`, `FUN_0095a550`), i.e. `COSNAME_M_WES_1` = "Cowboy Costume 1"; the mod's `[text]` section supplies it for new costumes. There is no description key.

### Ini loader

`FUN_0095a600` (factory for `costume/<name>`) creates the blueprint only if the file has a `[blueprint]` section (so the category and `costume_*` lists in the same folder are skipped), then reads `[fashionable]` (start, end, peak at blueprint `+0xD8`), `[age]` (`+0xDC`/`+0xE0`) and `[physique]`.

## Reverse-engineering workflow

### Ghidra headless

```
set G=%USERPROFILE%\tools\ghidra_12.1.4_PUBLIC\support\analyzeHeadless.bat
set M=%USERPROFILE%\source\MoviesMod
"%G%" "%M%\ghidra" MoviesSE -process MoviesSE.exe -noanalysis -readOnly ^
      -scriptPath "%M%\ghidra_scripts" -postScript Inspect.java "%M%\re\out.txt" 0x00847210
```

Several `-postScript` options can be chained in one run.

| Script | Arguments | Output |
|---|---|---|
| `Inspect.java` | `out.txt addr...` | Decompiles the function containing each address, and lists its callers |
| `Disasm.java` | `out.txt addr:count...` | Raw disassembly; useful where Ghidra found no function |
| `StringRefs.java` | `out.txt string...` | Code referencing each string, decompiled |
| `FindSerializer.java` | `out.txt slots vtable...` | Which virtual slot of a class is its save function |
| `SaveFields.java` | `out.txt func...` | Named save fields and the operands that give their offsets |
| `Vtable.java` | | Lists a vtable's slots |
| `DumpTuningRefs.java` | | Every tuning key referenced in code |

### Python tools (`tools\`, run with `python -I`)

| Tool | Usage |
|---|---|
| `pak.py` | `list <pak> [filter]`, `verify <pak>...`, `extract <out_dir> <pak> [filter]` |
| `rtti.py` | `rtti.py <exe> <vtable>...` gives class names; `--find <text>` searches class names |
| `fieldmap.py` | `fieldmap.py <exe> <disasm.txt>` turns a save-function disassembly into field → offset |
| `swfpalette.py` | `swfpalette.py <out.json> <pak>...` extracts the UI colour palette |
| `msh.py` | `info`, `verify`, `obj`, `rename-rooms` for `.msh` models |
| `hull.py` | Convex hull blocks for models (library) |
| `building.py` | `export <donor> <dir>`, `build <dir> <name> [--install]`: buildings from Blender; see [BUILDINGS.md](BUILDINGS.md) |
| `costumes.py` | `info <costume>`, `verify`, `build [pack.json] [--only N] [--preview png] [--install]`, `install`, `uninstall`: recoloured/remixed costumes from `costume_pack.json`; see [Costumes](#costumes) |

### Techniques that worked

1. **Find functions from strings.** Tuning keys, ini keys and text keys are literal strings, so `StringRefs.java` leads straight to the code that uses them.
2. **Find field offsets from save functions.** `FindSerializer.java` gets a class's save function from its vtable. `SaveFields.java` and `fieldmap.py` then give named offsets (remember the +0x64 base).
3. **Find globals from static initialisers.** Find the vtable immediate (e.g. `InList<CFacility>`'s vtable `0x00D1A8D4`) with a byte search. Its constructor is called from a CRT initialiser stub (`mov ecx, <global>; call ctor`), which names the global.
4. **Confirm at runtime.** Unknown layouts are discovered live with RTTI and logged, then confirmed from a tester's log. Everything that reads memory is SEH-guarded, so a wrong guess is safe.

## How films are scored

Recovered from the code (details and decompiles in `re/rel_*.txt`). `lerp(a, b, t) = a + (b - a) t`.

**Quality** (`FUN_005DCB10`, `__thiscall(CQuality*, float* out)`, result also at `CQuality+0x90`):
`Q = clamp(script + shape(0.8 × ((sets × crew × relationships × performance − 1) − 0.2 × (script − 0.4))))`
- script: the script's quality (1-5 script stars = 0.2 .. 1.0, `releasing_movies` `1_star`..`5_star`)
- performance: cast average of genre experience `lerp(0.8,1.2)` × genre fit `lerp(0.9,1.1)` × mood `lerp(0.8,1.2)` (extras 0.8; stunt actors their own formula)
- relationships `lerp(0.8,1.2)`, set condition `lerp(0.9,1.0)`, crew skill `lerp(0.9,1.1)`
- `shape(x)`: below zero unchanged; above zero each 0.1 counts 0.8, 0.64, 0.512... (at most +0.4). Good production lifts a film by up to 0.4; bad production can sink it without limit.

**Success** (`FUN_005DEDC0`, `CProjectSuccess` at `CProject+0x388`, result `+0x90`, not capped at 1):
`S = Q + shape(0.8 × ((tech × boredom × genre × cast stars × public interest − 1) − 0.24 × (Q − 0.5)))`
- tech: 1.1 if pioneering, else 1. `technology_min/max` and `costume_boredom_bound` are read but never used.
- boredom: sets and stars, `bound + (1 − bound)(1 − boredom)`; genre popularity `lerp(0.9,1.2)` × genre boredom; cast star ratings `lerp(0.8,1.2)`; public interest/hype `lerp(1.0,1.4)`.

**Box office** (`FUN_005CDB80` → `CProjectIncome` at `CProject+0x3A0`): pool = `revenue_base + revenue_multiplier × S²`; weekly take = pool × rate (0.013 × (0.4 + 0.6 × interest)), rate decays by `1 − 0.004 × ...` a week.

**Rating** (`FUN_005B8C90`, fastcall `CProject*`, writes `CProject+0x170`): `clamp(starratingboxoffice × clamp(S) + starratingquality × Q + starratingawards × hasAward)`, game weights 0.73 / 0.33 / 0 (MoviesMod sets 0.45 / 0.60 / 0.08). Computed at release (`FUN_005B9050`, which also enters the film in the Top Movies league table with this value) and again when it wins an award. Rival films: same formula in `FUN_005A4060` (field `+0x110`).

**Studio rating** (`FUN_0050AB70`, `CStudio+0x130`, the Top Studios table): base + 0.239 × money + 0.239 × stars + 0.144 × awards factors (`starrating*factor`).

**MoviesMod changes** (`src/ratings.cpp`, `[Ratings]` and `[global]` in MoviesMod.ini):
- Quality: `FUN_005DCB10` is hooked; after the game fills in the parts (`CQuality+0xA4` performance, `+0xAC` script, `+0xB0` crew, `+0xB4` relationships, `+0xB8` sets) the mod recomputes `Q` with gain decay 0.9 instead of 0.8 and losses ×0.75. The first dozen films log both values, so the recomputation can be checked against the game's.
- Box office: both pool computations (`0x005CDC17`, `0x005CDE35`, `FLD qword [0x00D1D258]` = the 2.0 given to `pow`) load the exponent from the mod instead (1.5).
- Critics' weights 0.45 / 0.60 / 0.08; studio ranking factors reputation 0.30, money 0.16, stars 0.21, awards 0.20, prestige 0.14.
- Success: `FUN_005DEDC0` is hooked. For player films the mod multiplies the factor product by a staleness factor and recomputes `S` (the quality the game used is recovered by bisection, since `S` rises with `Q`).
  - Technology: `CEquipment* = *(project+0x258)`, Techs[5] Strings at `+0x60 + 0x20 i`. `CTech` list head `0x010509A4` (node = tech+0xF0, next +0, tech at node+8); rank `+0x100`, name `+0x104`, techtype `+0x148`. Research packs `InList` at `0x0104A738` (first node `[0x0104A740]`, sentinel `0x0104A74C`), name `+0x80`, DateCanStartResearch `+0xA8`. Modernity = rank / best rank of the same techtype invented by the film's start (`project+0x2C8`), mapped to [technology_min, technology_max].
  - Costumes: `CCast* = *(project+0x1F8)`, roles `[cast+0x64, cast+0x68)` stride 0x18, `CRole* = *(entry+0x14)`, type `+0x88` (1-3 acting), `CCostume* = *(role+0xDC)`, name String `+0x78`. Boredom per costume = Σ over the player's earlier releases of 0.15 × 2^(−years since release / boredomhalflife), clamped to 1; factor `(1−b)(1−bound)+bound`.
  - The game's set boredom: global `std::map<String, BoredomRec>` at `0x0104AD74` (saved with the studio; get-or-insert `0x004D12E0`), `BoredomRec {date, value, rate}`, read `0x004950C0`, boost `0x004950A0`. It has no costume entries.

## Native interface

The UI is C++ widgets (W* classes) built in code; Flash `.mfl` movies only animate small ornaments. There is no screen factory: every screen is `operator new` + constructor + `parent->AddChild`. The mod composes screens the same way (`src/nativeui.cpp`, dumps `re/ui_*.txt`).

| What | Address / offset | Notes |
|---|---|---|
| Root window | `[0x0104DF40]` | full-screen layer `0x0071B2B0()` |
| `WDialog` | 0x3A8 bytes, ctor `0x006889C0(this, char closeButton)` | content `+0x36C`, X button `+0x384`, title `+0x39C`; SetTitle `0x006888D0`, AddContent `0x006884E0(child, mode)` |
| `WHTML` | 0x3FC bytes, ctor `0x00833290` | wrap width `+0x354`; supports `<table> <tr> <td width= bgcolor=#AARRGGBB> <font color=> <h1> <h2> <b> <br> <hr> <img> <translate>` |
| `WImageButton` | 0x420 bytes, ctor `0x0069FB10(this, String base /* BY VALUE, 0x20 bytes; the ctor frees it */, WString* tip, float w, float h)` | base like `"button_star."` (`data/textures/ui/button_*`) |
| Yes/no | `ShowConfirm 0x00669B80(WString* title, cbOk, cbCancel, WString body by value)` | callbacks `void __stdcall(sender, dlg)`; singleton |
| Close + release | `bool __stdcall 0x0071B530(sender, window)` | |
| WWindow virtuals | `+0x0C` AddChild, `+0x10/+0x14` width/height, `+0x18` SetHandler(type, fn, ctx, name), `+0x50` Layout, `+0x54` SetText, `+0x5C/+0x60/+0x64/+0x68` anchors (mode, rel, offset), `+0x78/+0x7C` SetWidth/Height, `+0x8C` fit height, `+0xAC` AddPopup | handlers are `bool __stdcall(sender, ctx)`; type 0 = click, type 5 registered with `0x005F37F0` |

The Television screen (`src/tv_native.cpp`) opens from a HUD button (`button_video.`); `[MoviesMod] NativeUI = 0` turns the native interface off.

