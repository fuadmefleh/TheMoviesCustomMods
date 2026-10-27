# Employees: careers, hiring, and adding an "Accountant"

Research notes from MoviesSE.exe (Ghidra headless). All addresses are VAs in MoviesSE.exe.
Supporting dumps are `re/emp1.txt` .. `re/emp23.txt` and `re/room_create.txt` (the room factory).
"Guess" marks anything not confirmed by code.

## 1. The career enum (`CStaff +0x814`, saved as `CurrentTask`)

The game calls it "task" internally (save field `(int&)(CurrentTask)`). The name table is a static
array of 17 game `String`s (stride 0x20) at **`0x00E54078`**, built by the CRT initialiser
`FUN_00D09220`:

| Value | Name (table) | Label key `SITT_DETAILS_CAREER_<NAME>_10/_20` (EN) | Notes |
|---|---|---|---|
| 0 | unemployed | Unemployed | jobseekers; cursor "wannabe" |
| 1 | staff | Staff | cursor "unhired"; no constant `SetCareer(1)` caller found (see section 5) |
| 2 | actor | Actor / Actress | |
| 3 | director | Director | |
| 4 | extra | Extra | |
| 5 | builder | Builder | Employment Office `hire_builder`; desires task_repairman + task_builder |
| 6 | janitor | Janitor | Employment Office `hire_janitor`; desires task_janitor + garden + watergrass |
| 7 | cameraman | Crew | 7-12 = crew (`IsCrew` `0x00576040`) |
| 8 | soundman | Crew | the value the crew facility's hire room gives |
| 9 | directorsassistant | (key `..._DIRECTORSASSISTANT` doesn't exist; the text DB has `DIRECTORASSISTANT`) | |
| 10 | dollyop | Crew | |
| 11 | setop | Crew | |
| 12 | clapperman | Crew | |
| 13 | assistant | Assistant / "Assistant to %star%" | star at `+0xA20` |
| 14 | writer | Scriptwriter | |
| 15 | scientist | Scientist | |
| 16 | stuntman | `ADDON1_SITT_DETAILS_CAREER_STUNTMAN` | Stunts addon |

Related fields: `+0x818` previous career (`LastTask`), `+0x4A0` gender (non-zero = male),
`+0x4C4` some person kind/state int (the CStaff ctor sets 3; `FUN_00598EE0` tests `== 0`; hire rooms
require `< 8`). What `+0x4C4` means is a guess.

### Accessors and setter
- `CStaff::SetCareer`: **`0x00577230`**, `void __thiscall(CStaff*, int career, int extra)`, virtual
  slot **+0x224** (`0x224/4` = slot 137) in the CStaff vtable `0x00D267CC`, and also in CExtra
  `0x00D24A0C` and CWannabe `0x00D28B44`. If the value changes, it does this:
  `+0x818 = old`, `+0x814 = new`, resets the activity at `+0x7B8`, and for 13 stores `extra` (the star)
  at `+0xA20`. Then **only if the employer (`+0x934`) is the player studio (`FUN_0050A830`)** it calls
  `FUN_00598A90(staff, career)` and `vfunc +0x1D4`. When the global `0x010583E4` is set (a guess: loading
  a save) it skips the side effects.
- `FUN_00598A90(staff, career)`, cdecl:
  - `FUN_00598540` (costume), called when `+0x4C4 != 0 && != 2`, or when the career is 8
  - `FUN_00598250` (task desires)
  - `FUN_005988C0` (home facility)
- Predicates: `0x00576040` IsCrew (7..12), `0x00576070` IsGroundStaff (5|6), `0x00576090` IsActorOrDirector (2|3).
  The SITT condition thunks at `0x00905740..0x009058A0` are `career == 2/3/4/5/6/...`. `0x008F36D0` is a
  generic "career == this+4" predicate used by the people filter `FUN_008F45F0`
  ("Researchers"→15, "Writers"→14, "Janitors"→6, "Builders"→5, "Extras"→4, "Directors"→3, "GroundStaff"→5|6).
- String→career: `FUN_005984B0` (linear search of the name table, returns 0..16, else 0). It is used
  only by 3 SITT condition constructors (`FUN_008DFC10/70/D0`).

### How many places read it, and how out-of-range values behave
- `RefsTo 0x814` finds about 118 instruction sites in about 78 functions (a few belong to other classes or
  are stack offsets). There are also the accessor and predicate helpers above, and about 50 `SetCareer`
  virtual calls (`re/emp21.txt`, pushed constants listed in `re/emp23.txt`: 2,3,4,5,6,7,8,0xA,0xD,0xE,0x10).
- Almost all reads are `CMP career, const`, so an unknown value just falls through.
- Switches use bounded jump tables: `0x004D7943` (`career-2 <= n`), `0x0091EEDB` (`<= 0xF`, the
  held-person cursor `FUN_0091EE70`), and `FUN_005988C0`.
- **Tables indexed by career with no bounds check (crash or garbage for a value >= 17):**
  1. Name table `0x00E54078` (17 × 0x20): `FUN_00907EA0` and `FUN_009086B0`, the career tooltip
     text ("SITT_DETAILS_CAREER_" + name).
  2. Costume tables `0x0104D0C8` "costume" and `0x0104D2E8` "vip" (17 × 0x20 each): read in
     `FUN_00598540`, filled by `FUN_00597F70`.
  3. `0x00F87C80`: 17 (`0x11`) × 16-byte `vector<String>` per career (initialiser at `0x00D06F20`),
     indexed `career<<4` in `FUN_0043B220` (credits/names list, called from `FUN_004EACE0`).
  This list is from tracing the known globals only; other per-career arrays may exist.

## 2. Hiring at the Employment Office

The Employment Office is **`facility_gatehouse`** (mesh `fac_gatehouse.msh`). Its zones are
`room_staff_hire_builder`, `room_staff_hire_janitor`, `room_staff_fire` and `room_`.

**Hire rooms are hard-coded by zone name** in the room factory `FUN_009405F0` (`re/room_create.txt`),
which splits `room_<building>_<room>`:

| Building / room | Object | Career |
|---|---|---|
| `staff` / `hire_builder` | `CHireRoom(5)`, AI anim `ai_room_staff_hire01` | builder |
| `staff` / `hire_janitor` | `CHireRoom(6)`, `ai_room_staff_hire02` | janitor |
| `staff` / `fire` | `CFireRoom` (`FUN_0091C070`, 0x278 bytes) | - |
| `cf` / `hire` (crew facility `room_cf_hire`) | `CHireRoom(8)` | crew |
| `cf` / `fire` | `CFireRoom` | - |
| research building, `hire` | `CHireRoom(0xF)` | scientist |

Writers are made by the script rooms: `CScriptRoom` calls `SetCareer(0xE)` at `0x00936911`.
The facility inis only decide which mesh is used. The zone names in the mesh pick the room
classes, and the factory's string compares decide the career.

**CHireRoom** (RTTI `.?AVCHireRoom@TM@@`, 0x25C bytes)
- ctor `FUN_00924230`, `__thiscall(this, int career)`. Career at **`+0x258`**.
  Main vtable `0x00D6C75C`, second vtable `0x00D6C73C` at `+0x64`.
- It overrides TMRoom slots 0 (dtor), 3, **7 OnDrop `0x009248D0`** and **22 (`+0x58`) `0x009244F0`**.
  Slot 22 is the accept/tooltip check: it refuses when the room is full or `+0x4C4 >= 8`, says
  "belongs to another studio" when `+0x934` is a rival, and otherwise shows `SITT_ACTION_HIREROOM_HIRE`.
- OnDrop `0x009248D0` `(this, TMMobile*)` → `FUN_009246E0(this, TMCharacter*)` = **Hire**:
  1. dynamic_cast to CStaff
  2. `playerStudio->vfunc[+0x30](staff)` = `CStudio::Employ` **`0x0050A7D0`** (`__thiscall(CStudio*, CStaff*)`):
     unlinks the staff from the list at `+0x784`, calls `studio vfunc +0xA0`, then
     `FUN_0050A760` (calls `staff vfunc +0x1A4(studio)`, links into `studio+0xA0` when `+0x4C4 == 0`)
  3. `staff->vfunc[+0x224](room+0x258, 0)` = SetCareer
  4. `FUN_00598540(staff, career)` = dress in the career costume
  5. career 8 only: walks the facility list `0x0104ED78` and sends the person to a free `CCrewRoom`
  Back in OnDrop it calls the base `TMRoom::OnDrop` `0x009436B0` and `FUN_0093CFD0(room, char, 0)`,
  then clears `+0x81C` and notifies the facility.

Data and tuning:
- **Salary appears career-independent.** `global.ini [staff]` has `basic_staff_salary = 1000`
  (loaded by `FUN_004FD240` into `0x0104B158`) and `staff_min/max_ability_salary_multiplier`
  (`0x00E51D84/88`), lerped by ability (`FUN_004FD0F0`). The same section has `spawn_age_*`,
  `staff_turnover_min/max`, `death_age_*` and `dismissal_*`. I didn't find a per-career salary.
- **Costume: data-driven per career name**, `data/tasks.ini`, a `[<careername>]` section with
  `costume =` and `vip =` (for example `[janitor] costume_janitor / costume_janitor_vip`, `[builder]
  costume_runner`, `[staff] costume_actor`). It is read at startup by `FUN_00597F70` for exactly the 17
  table names (an extra `[repairman]` section is ignored).
- How many jobseekers arrive: probably `staff_turnover_min/max` (guess). The gatehouse ini has
  `[subfacilities/staff/staff] staff = 24` (capacity of its subfacility).
- The SITT "stream" highlight hints are in `data/sitt.ini` `[actions/a_stream_roomhirebuilder]`
  `room=hire_builder` etc., and `data/sittsets/fixedasset.ini` (`gs_employ_staff`, `gs_hire_janitor`).

## 3. What the career drives

- **Desires** (`FUN_00598250(staff, career)`): always removes `task_janitor`, `task_repairman`,
  `task_builder`, `garden` and `watergrass` (`vfunc +0x104` RemoveDesire). It then adds:
  - 5: `DesireTaskRepairman` (`FUN_00840130`) and `DesireTaskBuilder` (`FUN_00840FD0`)
  - 6: `DesireTaskJanitor` (`FUN_00840640`), `garden` (`FUN_00838200`) and watergrass (`FUN_008424C0`)

  `FUN_0059A1C0` = AddDesire. **Other careers get no task desires here.** Writers and scientists work
  through their facility and rooms.
- **Home facility** (`FUN_005988C0(staff, career)`, bounded switch):
  - 2,3 → `facility_stage`
  - 5,6 → `facility_gatehouse`
  - 7,8,12 → `facility_crew`
  - 14 → `facility_script1`
  - 15 → `facility_research`

  It finds the nearest facility of that type (`FUN_00847DA0(type, vec)`) and binds the staff to its
  first subfacility with **`CStaff::SetSubFacility` `0x0057B4B0`** (`__thiscall(CStaff*, CSubFacility*)`,
  CP at `+0x938`, pointer at `+0x94C`, list node `+0x8E8`). **Values 0,1,4,9,10,11,13,16 get no home.**
- **Costume** (`FUN_00598540(staff, career)`, cdecl): it chooses the "vip" table when
  `(staff->vfunc[+0xF0]())->+0x10 & 1`, otherwise the "costume" table, then
  `FUN_004335F0(..., listName, gender=+0x4A0, ...)`. It has special cases for 5 (`FUN_004319B0(1)`) and
  16, and refreshes the outfit through `vfunc +0x128`.
- **Career label**: `FUN_00907EA0` (the details window) and `FUN_009086B0` (short form) build
  `<translate exclude=GENDER_x>SITT_DETAILS_CAREER_<UPPER(name[career])></translate>` (the gender picks
  `_10`/`_20`). Career 0 with a rival employer becomes `RIVALSTAR`.
- **Held cursor** `FUN_0091EE70`: careers have cursors such as "wannabe", "unhired", "staff", "crew" and
  "writer".
- **HUD sliders**: the jobs card has 9 hard-coded pages (`FUN_007E5290` switch 0..8, `FUN_007E6180`/
  `FUN_007E6630`, icons `ui/job_*.dds`). Each page is an icon-manager singleton (WBuilderIconManager
  tests `==5` at `0x007DBE30`/`0x007DEAC0`, WJanitorIconManager `==6` at `0x007EA340`, WScientistIconManager
  `==0xF` at `0x007F8150`, writers, crew, extras, stars). A new career won't appear on any page unless
  the mod adds one.
- Counts: CTTINumJanitors/Builders/Crew/Writers/Extras (`num_*` keys, `FUN_008D14E0`) are
  tooltip variables.

## 4. Save/load

CStaff's save function is **`FUN_005823F0`** (`__fastcall`, `this` = CStaff+0x78, the serialisable
subobject; object offset = save offset + 0x78):

| Field | Object offset | Type |
|---|---|---|
| `(int&)(CurrentTask)` | +0x814 | int, raw 4 bytes |
| `(int&)(LastTask)` | +0x818 | int |
| `Hired` | +0x86C | 4 bytes |
| `Birth` / `Death` | +0x860 / +0x864 | 4 bytes |
| `PEmployer` | CP at +0x920 (pointer +0x934) | |
| `PSubFacility` | CP at +0x938 (pointer +0x94C) | |
| `PStaffCosts` | CP at +0x848 | |

The career is stored as a plain int with no validation. A new value ≥ 17 would load, and then index the
three unchecked tables. A save would also stay poisoned if the mod were removed.

## 5. Recommendation: Accountant

**Don't add a new enum value.** A new value would need all of the following, for a modest gain:
- relocating or patching the three 17-entry tables (`0x00E54078`, `0x0104D0C8/0x0104D2E8`, `0x00F87C80`)
  and the `0x21F` loop bound in `FUN_00597F70`
- patching the `>0x10` bound in `FUN_005984B0`
- auditing about 120 compare sites
- risking save files that crash once the mod is removed

**Reuse career 1 ("staff") internally, and tag accountants mod-side.** Why it is the safest:
- no task desires (`FUN_00598250`)
- no home facility (`FUN_005988C0` default)
- not crew, not castable
- no HUD page, no IsCrew/IsGroundStaff
- costume and label come from data (`tasks.ini [staff]`, `SITT_DETAILS_CAREER_STAFF_10/_20`)
- in range for every table

Caveats:
- The held cursor for career 1 is "unhired".
- I found no constant `SetCareer(1)` caller. About 15 callers pass a register, so it's not fully
  proven that the game never uses 1 (guess: unused or legacy). Confirm at runtime by logging
  `+0x814 == 1` across the `0x0104CFC0` staff list in a normal game.
- Writer (14) and scientist (15) are **not** suitable: `FUN_005988C0` binds them to the nearest script
  office or lab, and their rooms put them to work. Builder and janitor (5, 6) bring task desires, HUD
  pages and the manager's handyman dispatch (`src/manager.cpp` keys on 5).

Hooks needed (all game-thread):
1. **Hire point.**
   - Simplest: a new zone (for example `room_tv_acc_hire`) on the mod's own `fac_tv_accounting.msh`,
     which becomes a plain TMRoom that `src/rooms.cpp` already intercepts.
   - Doing it "at the Employment Office" literally means adding a fourth zone to `fac_gatehouse.msh`
     (`msh.py` only renames zones, so this is mesh editing).
   - In the drop handler, mirror `FUN_009246E0`: check `person` is a CStaff with `+0x4C4 < 8` and a
     player or no employer (`+0x934`), then:
     - `CStudio::Employ` `0x0050A7D0` (`__thiscall(studio, staff)`, studio = `*(0x0104BCE4)`)
     - `staff->vtbl[0x224/4](1, 0)` (SetCareer)
     - `FUN_00598540(staff, 1)` (cdecl)
     - optionally `SetSubFacility` `0x0057B4B0` to a `[subfacilities/accountant]` subfacility of the
       accounting building, so the game counts them as staff of that building. Whether they then go
       there and stay is a guess.
   - Alternative: hook the factory `FUN_009405F0` to build a real `CHireRoom(1)` (`FUN_00924230` on a
     0x25C `operator new` block) for the new zone. You get the game's own accept/tooltip logic, but
     you still need a post-hook to tag the person.
2. **Tagging and persistence.** Keep a set of accountant CStaff* in a `persist` section, keyed by
   name + `Birth` (`+0x860`). Re-resolve after load via `0x0104CFC0`. Drop the tag if the career
   changes (hook `SetCareer` `0x00577230`, or poll). Firing goes through `CFireRoom` and SetCareer(0).
3. **Label.** If career 1 is otherwise unused, text-override `SITT_DETAILS_CAREER_STAFF_10/_20` to
   "Accountant" in `src/text.cpp`. Otherwise hook `FUN_00907EA0` and `FUN_009086B0`: for a tagged person,
   swap name-table entry 1 (`0x00E54098` String) to "accountant" around the original call, and supply
   `SITT_DETAILS_CAREER_ACCOUNTANT_10/_20`.
4. **Costume.** Ship a `data/tasks.ini` override with `[staff] costume = costume_writer`, or a new
   `costume_accountant.ini` list. Or hook `FUN_00598540` and swap table entry `0x0104D0E8`/`0x0104D308`
   for tagged people only.
5. **Behaviour.** Accountants have only generic needs. Their "work" should be mod-driven (for example
   periodically walking them to the accounting building, or modelling their effect on finances
   directly), like the handyman dispatch in `src/manager.cpp`.
6. **Optional.** A HUD page, a cursor (`FUN_0091EE70` case 1 → "unhired"), and accept-tooltip text for
   the new zone.

---

# Adding career 17 ("accountant")

This replaces the recommendation in section 5. The user wants a real new career value, 17, and doesn't
care about old saves.

Dumps:
- `re/c17_tables.txt`: every instruction touching the tables, with bytes
- `re/c17_win.txt`: a 14-instruction window after every `+0x814/+0x818/+0x258` access
- `re/c17_dis.txt`, `re/c17_dis2.txt`, `re/c17_cred.txt`
- `re/hud1.txt` .. `re/hud10.txt`

The new Ghidra scripts `RangeRefs.java` (instructions referencing an address range, with bytes) and
`DispWindow.java` (an instruction window after each use of a displacement) are in the session scratchpad
`gs/` folder. Copy them into `ghidra_scripts` if they're needed again.

## A. Per-career tables

These are the only unchecked career-indexed data found. They were found by scanning every
`+0x814/+0x818` read for `SHL/IMUL/scaled index` and by range-scanning each table. "disp@+n" is the
offset of the 32-bit field to rewrite with `newBase + (old - oldBase)`.

### A1. Name table `0x00E54078`
- Layout: 17 × 0x20 `String` {ptr, len, cap, char buf[0x14]}.
- Built by the CRT initialiser `FUN_00D09220`; destroyed by the atexit stub `0x00D12180`
  (`push 0x401490; push 0x11; push 0x20; push 0xE54078; call 0xACDC22`). Leave both on the old table.
- No free space after it: `0x00E5429C` is a float used by `FUN_00599CB0`. **It has to be relocated.**
- Instructions to patch (all other references are inside the initialiser):

  | Address | Bytes | Instruction | Field | Function |
  |---|---|---|---|---|
  | `0x00598022` | `8D 96 78 40 E5 00` | LEA EDX,[ESI+0xE54078] | disp@+2 | `FUN_00597F70` loop, ESI = i*0x20 |
  | `0x005984B9` | `BF 78 40 E5 00` | MOV EDI,0xE54078 | imm@+1 | `FUN_005984B0` |
  | `0x00907F05` | `05 78 40 E5 00` | ADD EAX,imm | imm@+1 | `FUN_00907EA0`, after `SHL EAX,5` at `0x00907F01` |
  | `0x00908725` | `05 78 40 E5 00` | ADD EAX,imm | imm@+1 | `FUN_009086B0`, after `SHL EAX,5` at `0x00908722` |

- Build the new 18-entry table from the 17 literal names (unemployed, staff, actor, director, extra,
  builder, janitor, cameraman, soundman, directorsassistant, dollyop, setop, clapperman, assistant, writer,
  scientist, stuntman) plus "accountant". Each entry uses its inline buffer (ptr = &buf, cap = 0x14), so the
  table doesn't depend on the CRT initialiser having run.

### A2. Costume tables `0x0104D0C8` ("costume") and `0x0104D2E8` ("vip")
- Layout: 17 × 0x20 Strings each, back to back.
- eh-vector ctor stubs `0x00D091C0` / `0x00D091F0` (`push 0x11; push 0x20; push base; call 0xACDC82`) and
  atexit stubs `0x00D12140` / `0x00D12160`. Leave them.
- `0x0104D508` and `0x0104D50C` are other globals, so there's no free space. **Relocate both.**
- `FUN_00597F70` reassigns entries: it frees the old ptr when cap > 0x14, otherwise it mallocs. So each new
  entry must start as an empty String {ptr=&buf, len 0, cap 0x14, buf[0]=0}.
- costume table (fields: +0 ptr, +4 len, +8 cap):

  | Address | Bytes | Field |
  |---|---|---|
  | `0x00598097` | `8B 86 D0 D0 04 01` | disp@+2, base+8 |
  | `0x005980A6` | `8B 96 C8 D0 04 01` | disp@+2 |
  | `0x005980BF` | `89 86 D0 D0 04 01` | disp@+2, base+8 |
  | `0x005980CD` | `89 86 C8 D0 04 01` | disp@+2 |
  | `0x005980D3` | `8B 86 C8 D0 04 01` | disp@+2 |
  | `0x005980E1` | `8B 86 C8 D0 04 01` | disp@+2 |
  | `0x005980E7` | `89 BE CC D0 04 01` | disp@+2, base+4 |
  | `0x0059859F` | `05 C8 D0 04 01` | imm@+1 (`FUN_00598540`) |

- vip table:

  | Address | Bytes | Field |
  |---|---|---|
  | `0x0059817A` | `8B 86 F0 D2 04 01` | disp@+2, base+8 |
  | `0x00598189` | `8B 96 E8 D2 04 01` | disp@+2 |
  | `0x005981A2` | `89 86 F0 D2 04 01` | disp@+2, base+8 |
  | `0x005981B0` | `89 86 E8 D2 04 01` | disp@+2 |
  | `0x005981B6` | `8B 86 E8 D2 04 01` | disp@+2 |
  | `0x005981C4` | `8B 86 E8 D2 04 01` | disp@+2 |
  | `0x005981CA` | `89 BE EC D2 04 01` | disp@+2, base+4 |
  | `0x00598598` | `05 E8 D2 04 01` | imm@+1 (`FUN_00598540`) |

- `FUN_00598540` indexes with `SHL EAX,5` at `0x00598590` and no bounds check.

### A3. Credits table `0x00F87C80`
- Layout: 17 (`0x11`) × 0x10 `vector<String>`.
- eh-vector ctor `0x00D06F20`, dtor `0x00D10C70`. `0x00F87D94` is another String, so there's no free
  space.
- Filled by `FUN_0043A340` (called from `FUN_005C4970`) through absolute entry addresses, for entries 4, 7,
  8, 10, 12 and 16 (about 36 instructions).
- Read only by `FUN_0043B220` (`cdecl(credits, CStaff*)`, RET 0, called from `FUN_004EACE0`):
  - `SHL ECX,4` at `0x0043B247`
  - loads at `0x0043B24A`, `0x0043B25F`, `0x0043B2C7`, `0x0043B2D9`, `0x0043B2EC` (`[ECX+0xF87C84/88]`)
  - it then calls `FUN_0043AE50`, which stores the career at credit entry `+0x60`
- **Don't relocate it.** Hook `FUN_0043B220` and return early when `*(int*)(staff+0x814) >= 17`.
  Accountants aren't film crew, so they just stay out of the credits.

### A4. Loop and bound immediates

| Address | Old bytes | New bytes | Effect |
|---|---|---|---|
| `0x00598210` | `81 FE 20 02 00 00` (CMP ESI,0x220) | `81 FE 40 02 00 00` | `FUN_00597F70` reads tasks.ini for 18 careers (the "0x21F" bound in the decompile) |
| `0x0059851E` | `83 FD 11` (CMP EBP,0x11) | `83 FD 12` | `FUN_005984B0` resolves "accountant" to 17 |

### Timing
- `FUN_00597F70` runs once from game init (`FUN_004244A0`).
- The mod's `OnAttach` (`DllMain` of the dinput8 proxy) runs before the exe's CRT initialisers *if
  dinput8.dll is a static import of MoviesSE.exe* (guess, probably). Then A1, A2 and A4 can be patched
  there and `FUN_00597F70` fills the new costume tables, including entry 17.
- Check this: at `OnAttach`, `*(void**)0x00E54078` should still be 0.
- If the patches ever land after game init, call `FUN_00597F70()` (cdecl, no arguments) once more.
  It re-reads tasks.ini and only frees heap strings.

### Data
- `data/tasks.ini` with an `[accountant]` section: `costume = costume_writer`, `vip = costume_writer_vip`,
  or a new costume list.
- Text keys `SITT_DETAILS_CAREER_ACCOUNTANT_10/_20` through `src/text.cpp`. The label key is built from
  the name table, so "accountant" becomes `ACCOUNTANT`.

## B. Switches and compares with career 17

| Site | Behaviour for 17 | Verdict |
|---|---|---|
| `0x004D7943` | `career-2`, then `CMP EAX,EDI / JA 0x004D79DC`. EDI = 0xA is loaded at `0x004D77AB` (assumed unchanged up to here). 15 > 10 takes the default | OK |
| `0x0091EEDB` (held cursor) | `CMP EAX,0xF / JA 0x0091F0DC`, the default, which leaves the cursor as it was (same as extras 4 and stuntmen 16). An optional hook on `FUN_0091EE70` could set the "staff" cursor | OK |
| `FUN_005988C0` (home facility) | `0x005988F8 career-2; CMP EAX,0xD; JA 0x00598A57`: default, no home | OK |
| `FUN_00598250` (desires) | only `==5` / `==6` | OK |
| `FUN_00598540` (costume) | needs A2; its special cases are only 5 and 0x10 | OK after A2 |
| `SetCareer 0x00577230` | only `==0xD` | OK |
| other ~110 `+0x814` sites, predicates `0x00576040/70/90`, `0x00905740..` | `CMP const` or ranges (2..3, 7..12) | OK |

- No `BT` or `1<<career` mask tests turned up in any `+0x814/+0x818` window. The only shift/scale uses are
  the A1/A2/A3 sites and the two jump tables above.
- Residual risk: career values passed as function arguments were only followed down the SetCareer and hire
  chains. A table indexed by an argument somewhere else wouldn't be caught by this scan.

## C. SetCareer(17), filters, counts, salary

- `FUN_00598A90` → costume (A2), desires (none), home (none). `vfunc +0x1D4` is generic.
- People filter `FUN_008F45F0`: maps names to fixed careers, so 17 is never selected. No crash.
- `CTTINum*` tooltip counts and SITT counts are compare-based: accountants are simply not counted.
- Salary is career-independent (`global.ini [staff]`).
- `FUN_005984B0` (SITT condition career names): after A1 + A4 it resolves "accountant" to 17.
- `FUN_0091B300` (re-employ/drop path, switch on LastTask `+0x818`): for 17 it falls into the
  `(<7 || >0xC)` branch and adds the person to no HUD manager. Harmless; the HUD page re-enumerates every
  tick (section E).

## D. CHireRoom(17)

- `+0x258` is read only by `FUN_009246E0`: it goes to `SetCareer` and `FUN_00598540`, plus a `==8` test.
- The other `+0x258` jump table (`0x009284B3`, `[EDI*4+0x928794]`) belongs to a different room class (the
  postprod room built by `FUN_00928BE0`).
- None of CHireRoom's own vtable functions (`vt_hire.txt`) read it except the ctor.
- So `CHireRoom(17)` is safe once A2 is done.

## E. HUD jobs card

### Structure
- **HUD root.**
  - Ctor `FUN_006A39C0` (`__fastcall(this)`, 0x364 bytes, vtable `0x00D3A744`).
  - Stored in the CP at `0x0104DB48` (pointer `0x0104DB5C`).
  - (Re)built by `FUN_006A44F0`. `FUN_006A44F0` and `FUN_0071C440` first call each manager's clear
    function (janitor: `FUN_007EBB40`).
- **Card = WHudCardButtons.**
  - Ctor `FUN_007E6E30` (`__thiscall(this, float slideDist)`, 0x3A0 bytes, vtable `0x00D593F4`).
  - Fields:

    | Offset | Field |
    |---|---|
    | `+0x358` | prev button |
    | `+0x370` | next button |
    | `+0x374` (CP), `+0x388` (pointer) | current-page job icon: a 0x360-byte image widget, starts as `ui/job_star.dds` |
    | **`+0x38C`** | **page index** |
    | `+0x390` | timestamp |
    | `+0x394` | "hudcards_tickstillchange" |
    | `+0x398` | slide distance |
    | `+0x39C` | enabled |

  - Built in `FUN_006A39C0` and kept in ESI there; the managers anchor to it. A second card is built by
    `FUN_00619150`.
- **Pages: 9, hard-coded.** `FUN_007E4F70(int page)` (cdecl) returns the manager for a page:

  | Page | Manager | Getter (pointer) | Hidden when `DAT_0104D8E8 != 0`? |
  |---|---|---|---|
  | 0 | star | `FUN_007FD5D0` (`0x0104EAE0`, or `[0x0104D8E8]+0x5EC`) | no |
  | 1 | stunt | `FUN_007329D0` (`0x0104E0AC`, or `+0x61C`) | no |
  | 2 | extras | `FUN_007E34B0` (`0x0104EA44`, or `+0x604`) | no |
  | 3 | writers | `FUN_00803080` (`0x0104EAF8`) | yes |
  | 4 | builders | `FUN_007DC3A0` (`0x0104EA14`) | yes |
  | 5 | crew | `FUN_007E00E0` (`0x0104EA2C`) | yes |
  | 6 | janitors | `FUN_007EA7B0` (`0x0104EA7C`) | yes |
  | 7 | scientists | `FUN_007F8640` (`0x0104EAC4`) | yes |
  | 8 | special mode | `FUN_007F53C0` (`0x0104EAAC`) | shown only in that mode |

  - Bound check: `0x007E4F76 CMP ECX,8 / JA` returns 0.
  - What `DAT_0104D8E8` is: a guess, a special mode such as star-maker/sandbox.
- **Skip test.** `FUN_007E5290(int page)` (cdecl, returns bool "skip"): `0x007E5294 CMP EAX,8 / JA`
  returns 1. Otherwise it skips when the manager is null or `mgr+0x344` (count) is 0, plus the mode gating.
- **Next page.** `FUN_007E5FB0(int cur)` (cdecl), with wrap tests `0x007E5FB9 CMP ESI,9` and
  `0x007E5FD2 CMP ESI,9`.
- **Next button handler.** `FUN_007E6180` (thiscall card):
  - clamp `0x007E61F0 CMP EDI,8 / JLE`
  - slide `FUN_007E5120(old, new, card+0x398)`, which moves both managers through `FUN_007E4F70`
  - stores `+0x38C`
  - jump table `0x007E6210 CMP EDI,8 / JA 0x007E65C1` → `[EDI*4+0x7E660C]`
  - each case sets the job icon:
    ```
    String s = <ui/job_*.dds>;   // FUN_00401DE0 = String::Assign(this, const char*, -1)
    icon->vtbl[0x100/4](&s, Rect{0,0,1,1});   // the rect is passed by value, 16 bytes, after the String*
    ```
    Case 6 is at `0x007E6425` (texture literal `0x00D59318`).
  - the default `0x007E65C1` only calls `card->vtbl[0x88/4](2.0f)`.
- **Prev button handler.** `FUN_007E6630`:
  - wraps with `0x007E66B0 MOV EDI,8` (`BF 08 00 00 00`)
  - jump table `0x007E66DB CMP EDI,8 / JA 0x007E6A8C` → `[EDI*4+0x7E6AD8]`
- **Other per-page code.**
  - `FUN_007E5190` (cdecl): are all pages empty?
  - `FUN_007E53C0` (cdecl): first non-empty page.
  - `FUN_007E5530` and `FUN_007E5710` (cdecl, called by the resolution handler `FUN_00645F60`):
    re-layout. For each manager:
    - `mgr->vtbl[0x74/4](x, y)`
    - `x = starMgr->vtbl[0x10/4]()`
    - `y = FUN_0071B2B0()->vtbl[0x14/4]() * [0x00D1BA7C]` (5530) or `* [0x00D17098]` (5710)
    - then the manager's relayout (janitor: `FUN_007EB040`, fastcall)
  - `FUN_0091B300`: re-add by LastTask.
  - `FUN_006A44F0` / `FUN_0071C440`: clear.
- There are no arrays sized 9. The page count lives only in code.

### WJanitorIconManager (the template)
- **Size and ctor.** 0x37C bytes. Ctor `FUN_007EBC90` (`__thiscall(this, char registerSingleton)`,
  RET 4). The flag replaces the singleton CP at `0x0104EA68` (pointer `0x0104EA7C`); pass 0 for a private
  instance.
- **Vtables.**
  - Main vtable `0x00D59944`, 64 slots. It overrides:
    - slot 0: dtor `0x007EBDD0`
    - slot 10: Update `0x007EC2A0`
    - slot 11: `0x007EAC70`
    - slot 63: `0x007EAF20` "has icon for person"
  - Second vtable `0x00D59928` at `+0x50`.
  - The dtor body is `FUN_007EB7C0`; it doesn't touch the singleton.
- **Fields.**

  | Offset | Field |
  |---|---|
  | `+0x344` | count (read by the page code) |
  | `+0x348/+0x34C/+0x350` | vector of `CP<CStaff>` (0x18 each, pointer at +0x14) |
  | `+0x35C/+0x360` | vector of `CP<WJanitorIcon>` (0x18 each, icon at +0x14) |
  | `+0x368` | update timer (`FUN_0043B490`) |
  | `+0x378` | icon spacing |

- **Update** (slot 10, `0x007EC2A0`): when the timer fires it runs
  `FUN_007EC130; FUN_007EAE40; FUN_007EB040; FUN_007EC130`, then the base `FUN_00740BA0`.
- **Enumerate** `FUN_007EC130` (`__fastcall(mgr)`):
  1. zeroes `+0x344`
  2. walks the global staff list (first node `0x0104CFC8`, sentinel `0x0104CFD4`)
  3. keeps a person when all of these hold:
     - `person->vtbl[0x204/4]()` is true (guess: on the lot/active)
     - the employer (`FUN_005773C0`) is the player (`FUN_0050A830`)
     - the career equals **the constant 6, at `0x007EC1AA` `83 7C 24 10 06` CMP [ESP+0x10],6**
  4. for each match: `++count`, and `FUN_004DB640` pushes a CP
  5. sorts with `FUN_007DDAF0(..., LAB_007EA480)`
  6. `FUN_007EAF60` (spacing)
  7. `FUN_007EBE80` creates the missing icons:
     - `new 0x500`, then `FUN_007EA020(person, index)` (the WJanitorIcon ctor)
     - `FUN_007EBDF0` pushes the icon
     - `FUN_007EACE0(i)` positions it and adds it as a child
- **Other functions.** AddPerson `FUN_007EB930` (thiscall mgr, person); remove-icon `FUN_007EBA80`
  (thiscall mgr, icon); clear `FUN_007EBB40` (fastcall).

### WJanitorIcon
- **Size and ctor.** 0x500 bytes. Ctor `FUN_007EA020` (`__thiscall(this, CStaff*, int index)`).
- **Vtables.** Main vtable `0x00D597CC` (78 slots); second vtable `0x00D597B4` at `+0x50`.
- **Person.** CP at `+0x4B4`, pointer **`+0x4C8`**.
- **Slot 10, `0x007EA340`: per-frame update.**
  - If the person is gone, or **`0x007EA351` `83 B8 14 08 00 00 06` CMP [EAX+0x814],6** fails, it calls
    `FUN_007EA7B0()` (the *singleton*) and then `->FUN_007EBA80(this)` to remove itself.
  - Otherwise it refreshes the portrait (`FUN_007E7EC0`, `FUN_007E7B00`) and the "star_card" tooltip,
    then calls `FUN_007E8200`.
- **Slot 11, `0x007E9650`.** Hover/hit test through the singleton `FUN_007EA7B0()->vtbl[0x34/4]`, and
  `+0x118 == singleton`.
- **Other overrides: generic person actions.**
  - slot 65 `0x007E9700`: IsFor(obj)
  - slot 68 `0x007E95E0`: `person->vtbl[0xB8/4]()` (click: select/focus)
  - slot 74 `0x007E95B0`: pick-up (`FUN_0053C900(person)`)
  - slot 75 `0x007E95F0`: message 0x4B2 to the person (details)
  - slot 76 `0x007E9620`

  **Clicking or picking up works without changes** (from reading the code, not tested).

### Plan for a page 9 (career 17)

**1. Create the instance.** Hook `FUN_006A39C0` (fastcall). After the original runs:
- `m = FUN_00ACBC50(0x37C)` (operator new, cdecl), then `FUN_007EBC90(m, 0)` (thiscall).
- Anchor it the way the janitor manager is anchored (`0x006A3DEB..0x006A3E1D`):
  - `m->vtbl[0x5C/4](1, FUN_0071B2B0(), -100.0f)`
  - `m->vtbl[0x64/4](2, card, -16.0f)`
  - `FUN_0071B2B0()->vtbl[0xC/4](m, 2)` (AddChild)
- `card` comes from a hook on `FUN_007E6E30` that records the instance created inside `FUN_006A39C0`.
- Keep a reference (`++*(int*)(m+0x48)`; guess: the refcount at `+0x48`, as the singletons use it).
- Hook `FUN_006A44F0` and `FUN_0071C440` (pre). If `m` exists:
  - `FUN_007EBB40(m)`
  - release the reference: `if (--*(int*)(m+0x48) == 0) m->vtbl[0](1)`
  - set `m = 0`

**2. Career filter.** Reuse the janitor code with a switchable constant. `g_career` defaults to 6.
- **P1** at `0x007EC1AA`: replace the 5 bytes with `JMP stub1`.
  stub1 = `push eax; mov eax,[g_career]; cmp [esp+0x14],eax; pop eax; jmp 0x007EC1AF`.
- **P2** at `0x007EA351`: replace the 7 bytes with `JMP stub2; nop; nop`.
  stub2 = `push ecx; mov ecx,[g_career]; cmp [eax+0x814],ecx; pop ecx; jmp 0x007EA358`.

**3. Wrappers (MinHook).**
- `FUN_007EC130(mgr)`: if `mgr == m`, set `g_career = 17; g_mgr = m`, call the original, then restore.
- `FUN_007EA340(icon)` and `FUN_007E9650(icon)`: if the icon is in `m`'s icon vector (`+0x35C..+0x360`,
  stride 0x18, icon at +0x14), set `g_career = 17; g_mgr = m` around the original.
- `FUN_007EA7B0` (`A1 7C EA 04 01 C3`): return `g_mgr ? g_mgr : *(void**)0x0104EA7C`.

**4. Pages.**
- Hooks:

  | Function | Change |
  |---|---|
  | `FUN_007E4F70` | page 9 → `m` |
  | `FUN_007E5290` | page 9 → `!(m && *(int*)0x0104D8E8 == 0 && *(int*)(m+0x344))` |
  | `FUN_007E5190` | return 0 when page 9 isn't skipped |
  | `FUN_007E53C0` | when the original returns 0 and `skip(0)` and `!skip(9)`, return 9 |

- Immediate patches:

  | Address | Old | New |
  |---|---|---|
  | `0x007E5FB9` | `83 FE 09` | `83 FE 0A` |
  | `0x007E5FD2` | `83 FE 09` | `83 FE 0A` |
  | `0x007E61F0` | `83 FF 08` | `83 FF 09` |
  | `0x007E66B0` | `BF 08 00 00 00` | `BF 09 00 00 00` |

- Leave the two jump-table bounds (`0x007E6210`, `0x007E66DB`) at 8, so page 9 takes the default path.
- Post-hook `FUN_007E6180` and `FUN_007E6630` (thiscall card): if `*(int*)(card+0x38C) == 9`, call
  `(*(void**)(card+0x388))->vtbl[0x100/4](&String("ui/job_acct.dds"), Rect{0,0,1,1})`.
  - Keep the texture name at 19 characters or fewer so it fits the inline buffer, or point `ptr` at static
    memory.
  - Ship `data/textures/ui/job_acct.dds`. The others are 5616 bytes; that size suggests 32×32 (guess).

**5. Layout.** Post-hook `FUN_007E5530` and `FUN_007E5710`. When `*(int*)0x0104D8E8 == 0`:
- call `m->vtbl[0x74/4](x, y)`, with x and y computed as in "Other per-page code" above
- then call `FUN_007EB040(m)`

**6. Optional.** In `FUN_0091B300`, on a drop with LastTask 17, call `FUN_007EB930(m, person)`. The next
tick re-enumerates anyway.
