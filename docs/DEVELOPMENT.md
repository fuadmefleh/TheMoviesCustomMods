# Development guide

How MoviesMod is built, how it hooks into the game, and how to add to it.

## Contents

- [Requirements](#requirements)
- [Building](#building)
- [How the mod gets into the game](#how-the-mod-gets-into-the-game)
- [Start-up sequence](#start-up-sequence)
- [Modules](#modules)
- [Threading and safety](#threading-and-safety)
- [Adding things](#adding-things)
- [Testing](#testing)
- [Code conventions](#code-conventions)

## Requirements

| Tool | Version | Used for |
|---|---|---|
| Visual Studio 2022 (Community) | C++ x86 toolset | Building the DLL. `build.bat` calls `vcvars32.bat`. |
| Python | 3.13 | `tools\*.py` (pak extraction, RTTI, field maps) |
| Ghidra | 12.1.4, at `%USERPROFILE%\tools\ghidra_12.1.4_PUBLIC` | Reverse engineering |
| Java | Amazon Corretto 25 | Running Ghidra |

Third-party code is vendored in `third_party\`:
- **MinHook 1.3.4:** function hooks.
- **Dear ImGui 1.91.6:** in-game UI, with the `dx9` and `win32` backends.

## Building

```
build.bat            builds build\dinput8.dll
build.bat install    builds, then copies it into the game folder
```

The build is one `cl` call:
- Flags: `/O2 /MT /EHa /std:c++17 /LD /utf-8`.
- It compiles all of `src\`, MinHook and ImGui together.
- It links with `src\exports.def` against `user32`, `d3d9` and `dxguid`.

Notes on the flags:
- `/MT` links the CRT statically, so the DLL has no runtime dependency.
- `/EHa` lets `__try/__except` coexist with C++ objects. The game-state readers rely on SEH.

**Adding a source file:** list it in `build.bat`. The only expected warning is C4644 (`offsetof` in the `DIMOFS_*` macros), which is harmless.

**Before installing, close the game.** Windows locks a DLL while it is loaded.

## How the mod gets into the game

`MoviesSE.exe` **delay-loads** `DINPUT8.dll` and imports only `DirectInput8Create`. Windows searches the exe's folder first, so a `dinput8.dll` there is loaded instead of the system one.

Our DLL:
1. Exports `DirectInput8Create` as `Proxy_DirectInput8Create` (`src\exports.def`).
2. On the first call, loads the real `dinput8.dll` from the system directory (`SysWOW64` for this 32-bit process) and forwards every call to it.
3. Uses that first call to set up the parts that can't run inside `DllMain`.

The DLL is loaded the first time the game calls DirectInput. That happens on the game thread, early in start-up, before any game world exists.

## Start-up sequence

`DllMain(PROCESS_ATTACH)` calls `OnAttach`, which runs under the loader lock and keeps the work minimal:

1. Opens `MoviesMod.log` with `_wfsopen(..., _SH_DENYWR)`, so the log can be read while the game runs.
2. Checks the exe's image base (`0x400000`) and PE timestamp (`0x44500000`). On a mismatch, every hook stays off and the DLL is a plain passthrough.
3. `MH_Initialize`.
4. `mod::LoadConfig()` reads `MoviesMod.ini`; `mod::InstallHooks()` installs the tuning hook.
5. `text::Install()` installs the string database hook.

The first `Proxy_DirectInput8Create` call (game thread, no loader lock):

1. `overlay::Install()` hooks Direct3D 9 `Present` and `Reset`.
2. `persist::Install()` hooks kernel32 file APIs.
3. `overlay::AddFrameCallback(&persist::Tick)`.
4. `tv::Install()` registers its window, HUD, frame callback and save section.
5. `mod::RegisterInspector()`.
6. Calls the real `DirectInput8Create`, then `overlay::OnDirectInputCreated` hooks `CreateDevice` on the returned object.

## Modules

### `dllmain.cpp`: proxy and logging
- `mod::Log(fmt, ...)` writes timestamped lines and flushes each one, so a crash loses nothing.
- `mod::GameDir()` returns the game folder.

### `features.cpp`: tuning overrides

The game reads balance values through `TuningFile::GetFloat` at `0x00558610`:
- Signature: `float __thiscall (String* key, float def, int flag)`.
- The tuning file's name is a `String` at `this+0xA8`.

The hook (`__fastcall` with a dummy `edx` to match `__thiscall`):
1. Calls the original.
2. Writes every distinct `file:key = value` to `MoviesMod_tuning.txt`.
3. Applies any override from `MoviesMod.ini`.

Override sections are named after the tuning file. Values are either absolute or `*factor`. Ini sections named `MoviesMod`, `text` and `tv` are skipped.

### `overlay.cpp`: in-game UI

**Finding Present without knowing when the device is created.** The overlay creates a throwaway `NULLREF` device (falling back to `HAL`) on a dummy window. All devices share one vtable inside `d3d9.dll`, so it reads the vtable from the dummy device and hooks slot 17 (`Present`) and slot 16 (`Reset`).

**Every `Present`:**
1. Run the frame callbacks (the game simulation hooks).
2. On the first call, initialise ImGui on the game's window and subclass its `WndProc`.
3. If the overlay is open or any HUD exists, start an ImGui frame:
   - HUDs draw always.
   - Panels draw only while open (F8).
4. Render inside `BeginScene/EndScene`.

`Reset` invalidates and recreates the ImGui DX9 objects.

**Input.** The game uses DirectInput with an *exclusive* mouse, so Windows sends no mouse messages and the OS cursor never moves. The overlay therefore hooks `IDirectInput8::CreateDevice` (slot 3) to tell mouse and keyboard devices apart by GUID. It also hooks `GetDeviceState` (slot 9) and `GetDeviceData` (slot 10).

While the overlay is open:
- **Mouse:** deltas, buttons and wheel are accumulated (under a critical section) into the overlay's own cursor, which ImGui draws itself. The game receives zeroed mouse state.
- **Keyboard:** input is blanked for the game when ImGui wants the keyboard. Keys reach ImGui through `WndProc`.

**Theme.** `ApplyMoviesTheme()` matches the game's look:
- Font: Arial Rounded MT Bold 17 px (`C:\Windows\Fonts\ARLRDBD.TTF`).
- Colours: from the game's Flash UI (`re\ui_palette.json`), i.e. slate panels, teal frames, slate-blue borders, gold and yellow highlights.

**API (`overlay.h`):**

| Function | Purpose |
|---|---|
| `AddPanel(name, draw)` | An interactive window, drawn while the overlay is open |
| `AddHud(draw)` | Always drawn; must use `ImGuiWindowFlags_NoInputs` |
| `AddFrameCallback(fn)` | Runs every frame, before any ImGui work |
| `IsOpen()` | Whether the overlay is open |

### `game.cpp`: game state

Everything goes through `SafeCopy` (`memcpy` inside `__try`). A wrong offset therefore gives "unavailable", never a crash. Calls into game functions are wrapped the same way.

| API | Notes |
|---|---|
| `Ready()` | The player studio and finance objects exist (a game is loaded) |
| `DateDays()` | Game date as days since year 0 (float); `/365` gives the year |
| `ReadMoney()` / `AddMoney(dollars, category = 8)` | Money goes through the game's own `CFinance` transaction, so it shows in the game's books |
| `ReadPlayerStudio()` | Name, prestige, star rating, number of films |
| `PlayerBackCatalogue()`, `PlayerProductions()`, `AllProjects()` | Films (`CProject`): title, stage, quality, stars, dates, box office |
| `Facilities()` / `CountFacilities(type)` | Every building on the lot, with its type name |
| `ClassName(obj)` | MSVC RTTI class name of any game object |

**Building types are found at runtime.**
1. For the first facility, it scans the object for a pointer whose RTTI class is `CFacilityBlueprint`.
2. In the blueprint, it takes the first identifier-like `String` that isn't a mesh name (`fac_*`).
3. Both offsets are cached and logged as `facilities: ...`.

If this fails, `type` is empty, and the TV system assumes the player owns an office.

Addresses and offsets are listed in [GAME_INTERNALS.md](GAME_INTERNALS.md).

### `persist.cpp`: save data

Mod data lives in a sidecar file: `Saved Games\X.jad` gets `Saved Games\X.tvs`.
- **Save:** the hooks on `CreateFileW/A` see the game open a `\Saved Games\*.jad` for writing (or a `MoveFileW/A` onto one). The mod then asks every registered section for its data and writes the `.tvs` file: first to `.tvs.tmp`, then replacing the old file.
- **Load:** the load menu opens *every* save to read its header, so a read is not proof of a load. The mod remembers the last `.jad` opened for reading. `Tick()` (each frame) detects that the game world changed:
  - a different player studio object, or
  - the date going backwards, or jumping forward more than 30 days.

  Then it either loads that save's `.tvs` file, or resets every section if the source was not a player save (a new game starts from `Data\Scenarios\*.jad`).
- **Delete:** `DeleteFileW/A` on a save also deletes its `.tvs` file.

**File format:** `[section]` headers followed by `key=value` lines. Values escape `\`, newline and carriage return.

**API:**
```cpp
persist::Register("name",
    [](persist::Section& out)      { out["key"] = "value"; },   // save
    [](const persist::Section& in) { /* restore */ },           // load
    [] { /* new game: clear */ });                              // reset
```
Loading can fire twice for one load, so make `load` idempotent: reset first, then fill.

### `text.cpp`: game text

The game resolves display text through `LookupText` at `0x009B5030`:
- Signature: `WString* __cdecl LookupText(WString* out, const String* key)`.

For keys listed under `[text]` in `MoviesMod.ini`, the hook builds the result the way the game does: `out` is set up with its empty inline buffer, then filled with the game's own `WString` assign (`0x004036D0`). Every other key passes through.

The first time any key is looked up, the hook checks it against the database (`FindIndex`, `0x00A35EB0`). Missing keys are logged as `text: missing key 'X'`, which shows the keys new content needs.

### `tv*.cpp`: Television & Streaming

Split into `tv.cpp` (rules tables, series, daily loop, save section `tv`), `tv_network.cpp` (networks, schedules, rival shows, audience model, your network, save section `tv_networks`) and `tv_ui.cpp` (window and ticker), sharing `tv_internal.h`.

**Structure:**
- **Data tables:** `kFormats` (era availability, 1950 cost and fee, appeal in 1950 and 2000, multi-camera flag), `kNetworks` (pitch bar, pay, reach, taste per format), budgets, deals, management levels.
- **State:** `std::vector<Series>`, plus the management level and the next id. Saved under section `tv`, with keys `s<i>.<field>`.
- **`Simulate()`** (frame callback):
  1. Recounts buildings every 3 seconds.
  2. Applies the production capacity.
  3. For each series, resolves due verdicts, airs due episodes, and ends seasons.

  It runs on the game clock, so pausing works. After a load or a big date jump, it re-baselines instead of replaying the gap.
- **Determinism:** dice rolls are `Roll(seriesId, season*1000 + episode, salt)`, a hash, so reloading never rerolls an outcome.
- **UI:**
  - `DrawWindow` has three tabs: My Shows, Pitch a Pilot and Networks.
  - `DrawToasts` is the HUD news ticker.

**Formulas:**

| Quantity | Formula |
|---|---|
| Pitch score | `quality×80 + stars×10 + prestige×8 + network taste + (appeal−1)×20 + budget ±3 + roll ±8`; the roll is keyed to series, network and pilot film, so the same film at the same network scores the same |
| Verdict | score ≥ bar+15: full season · ≥ bar: limited (8) · ≥ bar−12: rework · otherwise: pass |
| Episode quality | `0.55×pilot quality + 0.45×(0.45 + budget ± 0.08 + soundstage 0.08 or 0.03) ± 0.07` |
| Pull (per network, per slot) | `q^1.3 × format appeal × freshness × event × brand × reach × lead-in × roll 0.9–1.1`; lead-in = `1 + 0.35×(previous hour's share × networks − 1)`, 0.75–1.35 |
| Share, rating | share = pull^1.5 ÷ Σ pull^1.5; rating = 62 × share |
| Own network income | `rating% × homes × $0.0025 × era` per airing; station costs weekly by reach |
| Cost | `format cost × era factor × budget × CostScale` |
| License income | `fee × era × pay + max(0, rating − 12) × $500 × era × pay` |
| Ad-share income | `fee/2 + rating × $1,100 × era × reach` |
| Renewal (rivals) | season share ≥ fair share × (0.75 + bar/200) · full order if a further quarter of fair share above |

The **era factor** is the price level relative to 1950: 0.55 in 1920, 3.6 in 1980, 12 in 2030. TV homes per year follow historical US figures.

### `inspector.cpp`: developer inspector
It is hidden until it is ticked on in the Television window. While shown, it displays live state and test buttons. Each time it opens, it logs a snapshot: the studio, films and buildings by type.

## Threading and safety

- **Threads:** all game access happens on the game thread (`Present`, `WndProc` and the first DirectInput call). The exception is the DirectInput hooks, which share mouse state through a critical section. persist also locks around the file hooks.
- **Avoid the loader lock:** never call into the game or create windows or devices from `DllMain`.
- **Reading game memory:** read through `game::Read`/`SafeCopy`, or wrap in `__try`.
- **Calling game functions:** use the exact calling convention. Most member functions are `__thiscall`; declare the pointer as `__thiscall`, or use `__fastcall` with a dummy `edx`.
- **Money:** prefer the game's own ledger (`AddMoney`) over writing `CFinance` fields.

## Adding things

### A new feature with settings
1. Create `src\myfeature.cpp/.h` with an `Install()`, and call it from `Proxy_DirectInput8Create`. Use `OnAttach` only for hooks that must exist before the game starts.
2. Read settings from your own ini section with `GetPrivateProfile*W`. Add the section name to the skip list in `LoadTuningOverrides` (`features.cpp`).
3. If it has state, register a `persist` section.
4. Add the file to `build.bat`.

### A UI window
```cpp
overlay::AddPanel("mine", [] {
    if (ImGui::Begin("My Window (F8)")) { /* ... */ }
    ImGui::End();
});
```
Use the shared theme colours. Keep window titles as names, not explanations.

### A new building
1. Put an `.ini` file in `content\data\facility\` and copy it to `<game>\Data\facility\`. Start from an extracted original in `extracted\<pak>\data\facility\`.
   - Loose files load alongside the pak archives.
2. Use an existing mesh (`fac_*.msh`; list them with `tools\pak.py`).
3. **Start the file name with `facility_`** (or `trailer_`). The build menu ignores anything else: the building is listed but no placement ghost appears.
4. Gate eras in mod code rather than `[blueprint/requires]`.
5. Name it under `[text]` with its file name in capitals.
6. Buildings with unknown names get the game's default behaviour. They can be placed and counted, but nobody uses them. Give them gameplay meaning from mod code: `game::CountFacilities("my_building")`.

### A new hook on a game function
1. Find it in Ghidra (see [GAME_INTERNALS.md](GAME_INTERNALS.md#reverse-engineering-workflow)). Note the address and calling convention.
2. Hook it:
   ```cpp
   MH_CreateHook(reinterpret_cast<void*>(addr), &Detour, reinterpret_cast<void**>(&g_orig));
   MH_EnableHook(...);
   ```
3. Record the address and what it does in GAME_INTERNALS.md.

## Testing

There is no automated test suite; testing happens in the game:

1. `build.bat install` with the game closed.
2. Play the scenario being tested, then quit from the game's menu, so the log ends with `MoviesMod unloading`.
3. Read `MoviesMod.log`. Useful prefixes:

| Prefix | Meaning |
|---|---|
| `override` | Tuning values changed |
| `hooked` / `FAILED` | Hook installation |
| `persist:` | Save and load |
| `snapshot:` | Inspector state dump |
| `facilities:` | Building layout discovery |
| `text: missing key` | Text the game wanted |
| `tv:` | Pitches, verdicts, every airing (quality, rating, income, cost), renewals, building counts |
| `debugshots:` | Each game window as it opens (its RTTI class), with `[MoviesMod] DebugShots = 1` |

Check the `.tvs` files in `Documents\The Movies\Saved Games\`.

With `[MoviesMod] DebugShots = 1`, `src/debugshots.cpp` saves a screenshot of every game window as it opens (it hooks the root window's `AddChild` and `AddPopup`) to `MoviesMod\shots\win_<time>_<class>.bmp`, and Ctrl+F12 saves one at any time.

**Timing:** one in-game year is about 4 real minutes at normal speed (about 1.6 game days per second), and a film takes about a game year to make.

## Code conventions

- C++17, 4-space indentation, `namespace module { namespace { ...internals... } }` with a small public API in the header.
- Names: `g_` for globals, `k` for constants, PascalCase functions.
- Every game address is a named `constexpr` with a comment saying what it is.
- Comments explain *why* and record reverse-engineering facts. Don't narrate the code.
- Log anything that would help diagnose a tester's session, without logging every frame.
