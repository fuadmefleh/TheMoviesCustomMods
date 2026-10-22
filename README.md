# MoviesMod

A code mod for **The Movies: Stunts & Effects v1.2** (Lionhead, 2006). It runs inside the game as a proxy `dinput8.dll` and adds:

- **Quality-of-life fixes** for the game's best-known complaints: the 4 GB memory patch, and less star micromanagement (slower stress and boredom, faster recovery, slower addiction).
- **The Movies: Television & Streaming**, a fan expansion in progress. Turn your films into TV pilots, pitch them to networks, and run shows through seasons, ratings and renewals. It is planned to grow into syndication and a streaming era up to 2030.
- **Modding plumbing** for future work: game tuning overrides, new buildings, new game text, in-game windows, and mod data stored alongside save games.

> Works only with `MoviesSE.exe` v1.2 (timestamp `0x44500000`). On any other build the DLL turns its hooks off and passes DirectInput straight through, so the game still runs unmodded.

## Contents

- [Status](#status)
- [Installing](#installing)
- [Playing](#playing)
- [Configuration](#configuration)
- [Files the mod creates](#files-the-mod-creates)
- [Works with other mods](#works-with-other-mods)
- [Troubleshooting](#troubleshooting)
- [Uninstalling](#uninstalling)
- [Project layout](#project-layout)
- [More documentation](#more-documentation)

## Status

| Feature | State |
|---|---|
| Large Address Aware patch (4 GB) for `MoviesSE.exe` and `StarMaker.exe` | Done |
| Tuning overrides from `MoviesMod.ini`, plus a catalogue of every tuning value | Done, verified in game |
| Less micromanagement preset (stress, boredom, addiction) | Done, verified in game |
| In-game windows (F8), styled like the game, with mouse support | Done, verified in game |
| Reading game state: money, date, studio, films | Done, verified in game |
| Adding and removing money through the game's own ledger | Done, verified in game |
| Mod data saved with each save game (`.tvs` files) | Done, verified in game |
| Loose building files in `Data\facility\` | Done, verified in game |
| **TV Phase 1: Broadcast Era** (TV buildings, pilots, networks, ratings, renewals) | Done, verified in game |
| **TV Phase 2: Network Owner** (prime-time schedules, rival shows, your own network) | Built, **not yet tested in game** |
| Custom game text (building names and other strings) | Done, verified in game |
| TV Phases 3 to 6 | Planned, see [docs/ROADMAP.md](docs/ROADMAP.md) |

## Installing

The game folder is `C:\Program Files (x86)\Lionhead Studios Ltd\The Movies`.

### From a release (no build tools needed)

1. Download `MoviesMod-<version>.zip` from the [Releases page](https://github.com/fuadmefleh/TheMoviesCustomMods/releases).
2. Close the game and extract the zip into the game folder, merging folders.
3. Optional: for the Television screen's artwork, run `python MoviesMod\tools\extract_ui.py .` in the game folder (Python 3 with Pillow). It copies banners and rosettes out of your own copy of the game.

Releases are built by GitHub Actions (`.github/workflows/release.yml`) whenever a `v*` tag is pushed: `git tag v0.2.0 && git push origin v0.2.0`.

### From source

1. **Optional: apply the 4 GB patch.** This sets the Large Address Aware flag on `MoviesSE.exe` and `StarMaker.exe`, fixing many Windows 10/11 crashes and black textures. It is already applied on this machine, and the originals are kept as `*.pre-laa.bak`.
2. **Build and install the DLL** (needs Visual Studio 2019 or later, any edition, with the C++ tools; clone with `--recursive` for the submodules):
   ```
   cd %USERPROFILE%\source\MoviesMod
   build.bat install
   ```
   This copies `dinput8.dll` into the game folder.
3. **Copy the settings file:** `MoviesMod.ini` goes next to `MoviesSE.exe`.
4. **Copy the content:** everything under `content\` goes into the game folder, keeping the folder structure. For example, `content\data\facility\facility_tv_production_office.ini` goes to `<game>\Data\facility\facility_tv_production_office.ini`.

Start the game as usual. `MoviesMod.log` appears in the game folder.

## Playing

### In-game windows

Press **F8** to open or close the mod's windows.
- While a window is open, the mouse and keyboard control the window, and the game sees no mouse input.
- Press F8 again to return control to the game.

### Television & Streaming (Phase 1: the Broadcast Era)

**Unlocking television.** Television starts in **1938**. You also need a **TV Production Office**, found in the build menu.

**New buildings:**

| Building | Available | What it does |
|---|---|---|
| TV Production Office | Always (TV itself starts in 1938) | Required for TV. Each one keeps **2 shows** in production at once. |
| Multi-Camera Soundstage | Always for now | Adds room for **1 more show**. Better episodes, especially sitcoms, soaps, talk shows and game shows. |
| Network HQ | From 1946 | Lets you launch **your own network** (Networks tab). One per lot. |
| Broadcast Control Room | From 1946 | Needed to take your network **Regional**, then **National** (from 1950). One per lot. |

**The TV loop:**
1. **Pitch a pilot.** On the *Pitch a Pilot* tab, choose one of your released films as the pilot. Then give the series a title and pick a format. Each film can be used as a pilot only once.
2. **Wait for the verdict.** After about three weeks of game time, the network does one of four things:
   - **Full season:** 26 episodes, or 22 from 1970 on. Talk shows, game shows and soaps get 39.
   - **Limited run:** 8 episodes.
   - **Rework:** pitch again with a stronger pilot film, or to another network.
   - **Pass:** that network is closed to this series. Try the other networks.
3. **On air.** The first episode airs two weeks after the order, then one episode every week.
   - Each episode gets a **rating**, and your money changes by its income minus its production cost.
   - Premieres and finales draw extra viewers.
   - Shows slowly lose freshness over time.
4. **Renewal.** At the end of a season, the network looks at the average rating. It either offers another season, full or limited, or cancels the show. Miniseries end after one run.

Production capacity is limited. Shows beyond what your buildings can handle go **on hold** until you build more.

**Formats:**

| Format | From | Notes |
|---|---|---|
| Game Show | 1941 | Cheap, with huge ratings in the 1950s |
| Sitcom | 1947 | Popular in every era |
| Soap Opera | 1949 | Very cheap, with loyal fans |
| Procedural | 1949 | Reliable |
| Science Fiction | 1949 | Costly, gaining popularity over the decades |
| Serialized Drama | 1950 | Takes off after 1980 |
| Talk Show | 1951 | Cheap |
| Documentary | 1951 | Prestige over ratings |
| Reality | 1973 | Cheap; booms after 2000 |
| Miniseries | 1974 | A 6-episode event with no renewals |

**Networks:**

| Network | Character | Standards | Pay | Reach |
|---|---|---|---|---|
| Liberty Broadcasting Network (LBN) | Prestige: drama, procedurals, documentaries | Very high | 125% | 115% |
| Starlight Television (STV) | Family entertainment: sitcoms, game shows | High | 100% | 105% |
| Meridian Network (MRN) | Risk-taking upstart: sci-fi, soaps, reality | Relaxed | 80% | 80% |

**Management levels** (chosen at the top of the Television window):

| Level | You decide | The studio decides |
|---|---|---|
| Casual | Pilot film, title and format | Network, budget and renewals; it automatically re-pitches after a pass |
| Studio Boss | Network, episode budget and renewals | Deal terms |
| Media Mogul | Everything, including the deal type and budget changes mid-season | Nothing |

**Episode budgets:**

| Budget | Cost | Effect |
|---|---|---|
| Shoestring | 70% | Weaker episodes, cooler pitch reception |
| Standard | 100% | Baseline |
| Premium | 150% | Better episodes, warmer pitch reception |

**Deals (Media Mogul only):**
- **License fee:** a fixed fee per episode, plus a bonus for ratings above 12. This is the safe option.
- **Advertising share:** half the fee, plus a cut of advertising that grows with the rating. A hit pays far more and a flop far less.

**Prime time (Phase 2).** Every network airs a schedule of 7 nights × 3 slots (8, 9 and 10 PM), shown on the *Schedule* tab.
- Rival networks fill their slots with shows of their own. Your shows compete with theirs hour by hour: the **share** of viewers depends on quality, format popularity, freshness, the network's reach and the **lead-in** from the show before. Rating = share of the ~62% of homes watching.
- A network that orders your show gives it a slot, dropping its weakest show. Rivals renew your shows when they win a fair share of their slot.
- Every **mid-September** the fall season starts: rivals cancel their flops and premiere new shows.
- The *Ratings* tab shows last week's top 15 and each network's share.

**Your own network (Phase 2).** Build a **Network HQ**, then launch a local station on the *Networks* tab.
- Greenlight shows straight onto it (no pitching) and place them on the *Schedule* tab, or use **Fill empty slots**.
- It keeps all the advertising money, but pays weekly station costs; empty slots air reruns and earn little.
- Expand Local (12% of homes) → Regional (40%) → National (90%, from 1950); both need a **Broadcast Control Room**.
- Hire a **Programming Director** to schedule new shows every Monday. On Casual the studio does it for free.

**News pop-ups** at the top of the screen announce pitches, verdicts, premieres, renewals and cancellations, even when the window is closed.

TV data is saved with your game; see [Files the mod creates](#files-the-mod-creates).

### Developer inspector

Tick **Show developer inspector** at the bottom of the Television window. The inspector shows:
- Live studio data: name, prestige, money and date.
- Your films, with quality, box office and release dates.
- Test buttons to add and remove $100,000.
- A test counter that is stored in the save.

Each time the inspector opens, it writes a snapshot to the log, including every building on the lot.

## Configuration

All settings live in `MoviesMod.ini`, next to `MoviesSE.exe`. The file is read at game start.

```ini
[MoviesMod]
TuningOverrides = 1      ; apply the tuning sections below
DumpTuning = 1           ; write MoviesMod_tuning.txt (every tuning value the game reads)

; One section per game tuning file. KEY = value replaces, KEY = *factor multiplies.
[mood]
WORKINGONAMOVIESTRESS = *0.5

[tv]
IncomeScale = 1          ; multiply all TV income
CostScale   = 1          ; multiply all TV production costs

[text]                   ; add or replace game text (UTF-8)
FACILITY_TV_PRODUCTION_OFFICE = TV Production Office
```

### Tuning overrides

The game reads about 5,000 balance values from its data files: moods, addictions, finance, research and more.
- After a run, `MoviesMod_tuning.txt` lists every value the game read: the section (file) name, the key and the game's value.
- Copy any line into `MoviesMod.ini` under its section to change it.
- Mood values are changes per tick: **positive values build up and negative values relieve.** So to make a relief work faster, multiply it by more than 1.

The shipped preset (*less micromanagement*):

| Section | Keys | Change |
|---|---|---|
| `[mood]` | `WORKINGONAMOVIESTRESS`, `REHEARSINGSTRESS`, `WAITINGONAMOVIESTRESS` | ×0.5: stress builds up half as fast on the job |
| `[mood]` | `IDLINGBOREDOM` | ×0.5: idle stars get bored half as fast |
| `[mood]` | Restaurant, snack van, trailer and eat/drink-on-set stress and boredom relief | ×1.5: healthy breaks work faster |
| `[addictions]` | `feed` | ×0.5: addiction builds half as fast |

Sections named `MoviesMod`, `tv` and `text` configure the mod itself and are never treated as tuning files.

### Game text

A building's display name uses its file name in capitals, so `facility_tv_soundstage.ini` is named by `FACILITY_TV_SOUNDSTAGE`. Any key the game asks for but can't find is logged as `text: missing key 'X'`. Add it under `[text]` to give it a name.

## Files the mod creates

| File | Where | What |
|---|---|---|
| `MoviesMod.log` | Game folder | Everything the mod did this session. It is rewritten each launch and can be read while the game runs. |
| `MoviesMod_tuning.txt` | Game folder | Catalogue of every tuning value the game read |
| `<save name>.tvs` | `Documents\The Movies\Saved Games\` | The mod's data for that save. It is plain text, written whenever the game writes the `.jad`, and deleted when the save is deleted. |

**A `.tvs` file belongs with its `.jad` save.** If you copy a save to another PC, copy both files. Without the `.tvs` file, the save loads fine but the mod starts fresh: no TV shows.

## Works with other mods

The Movies still has a small modding community. MoviesMod is meant to sit alongside it.

**Fixes we recommend instead of duplicating them** (back up the game folder first; these are third-party downloads):

| Mod | What it does | Notes with MoviesMod |
|---|---|---|
| [Widescreen & FOV Fix](https://community.pcgamingwiki.com/files/file/3506-the-movies-fov-fix/) (AlphaYellow) | Stops the picture being cropped on widescreen monitors | Loads through ThirteenAG's Ultimate ASI Loader as `winmm.dll`, so it doesn't clash with our `dinput8.dll`. Not yet tested together |
| [The Movies Fixer](https://community.pcgamingwiki.com/files/file/4020-the-movies-fixer/) | FOV fix, intro skip, resolution picker, dgVoodoo | dgVoodoo replaces `d3d9.dll`; our overlay hooks Direct3D 9, so test the mod's windows after installing it |
| [Windows 10/11 Patch with 4GB fix](https://www.nexusmods.com/themovies/mods/253) | Compatibility and the Large Address Aware flag | The 4 GB patch is the same one this README recommends; apply it once |

**Community content** - costumes, sets, props and makeup from [TheMovies3D.com](https://www.themovies3d.com/en/), [Nexus Mods](https://www.nexusmods.com/games/themovies/mods) and [ModDB](https://www.moddb.com/games/the-movies/downloads) - installs as loose files under `Data`, like ours.

- **Shared lists are merged, not replaced.** Files that many mods add to, such as a wardrobe tab's costume list (`Data\costume\category_*.ini`), are not in our zip's `Data` folder. Our entries live in `MoviesMod\lists\` and the DLL merges them into the game's copies at every start, keeping everyone else's entries. Install order doesn't matter: if a later costume pack replaces a list, our costumes come back on the next start. `MoviesMod.log` reports it as `lists: ...`.
- **Names:** our costumes are called `m_mm_*` / `f_mm_*` and our buildings `facility_tv_*`, `facility_script_5star` and `trailer_6`..`trailer_9`. If another mod uses the same names, the one installed last wins.
- **Tools:** [The Movies Editor](https://www.themovies3d.com/en/downloads/category/22-tools-a-utilities) (sets, costumes, props), FlmReaderZero (scenes), PakPoker (paks) and CastingCouch (StarMaker) all work on the game's files and are independent of this mod.

## Troubleshooting

| Symptom | What to do |
|---|---|
| F8 does nothing | Check `MoviesMod.log` for `overlay ready`. If it says `unexpected game build`, your exe isn't v1.2. |
| The game won't start or crashes at start | Delete `dinput8.dll` from the game folder; the game runs unmodded without it. Then send the log. |
| Mouse stuck while the window is open | The window takes the mouse while it's open. Press F8 to give it back. |
| TV buildings missing from the build menu | Check that the `.ini` files are in `<game>\Data\facility\`, and look for `facility/facility_tv_` lines in `MoviesMod_tuning.txt`. |
| A building shows a raw name like `FACILITY_TV_SOUNDSTAGE` | Add the key under `[text]` in `MoviesMod.ini`. |
| The Television window says "Building check unavailable" | The mod couldn't identify buildings and assumes you own a TV Production Office. Send the `facilities:` lines from the log. |
| TV money feels too high or too low | Change `IncomeScale` and `CostScale` under `[tv]`. |
| TV shows gone after loading | The save's `.tvs` file is missing or was made before the mod. |

## Uninstalling

1. Delete these from the game folder:
   - `dinput8.dll`, `MoviesMod.ini`, `MoviesMod.log` and `MoviesMod_tuning.txt`.
   - The added building files in `Data\facility\` (`trailer_6.ini`, `facility_tv_production_office.ini`, `facility_tv_soundstage.ini`).
2. **Before removing the building files**, demolish any mod buildings in saves you'll keep. A save that contains a building whose file is gone may not load.
3. To undo the 4 GB patch, restore `MoviesSE.exe.pre-laa.bak` and `StarMaker.exe.pre-laa.bak`. The patch is harmless to keep.

`.tvs` files are ignored by the unmodded game.

## Project layout

```
MoviesMod\
  build.bat               build (and "build.bat install")
  MoviesMod.ini           settings shipped with the mod
  src\                    the DLL
    dllmain.cpp           proxy entry point, log, start-up
    features.cpp          tuning hook and overrides
    overlay.cpp/.h        Direct3D 9 + DirectInput hooks, ImGui, theme, HUD
    game.cpp/.h           safe access to game state (money, date, films, buildings)
    persist.cpp/.h        .tvs save data
    text.cpp/.h           string database hook
    tv.cpp/.h             Television & Streaming
    inspector.cpp         developer inspector
    exports.def           exports DirectInput8Create
  content\data\facility\  new building files
  third_party\            MinHook 1.3.4, Dear ImGui 1.91.6
  tools\                  Python: pak extractor, RTTI lookup, field mapper, UI palette
  ghidra_scripts\         Ghidra headless scripts used for reverse engineering
  re\                     analysis copy of MoviesSE.exe, Ghidra outputs, notes
  ghidra\                 Ghidra project
  extracted\              files unpacked from the game's .pak archives
  docs\                   developer documentation
```

## More documentation

- [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md): building, architecture, how each module works, adding features, testing.
- [docs/GAME_INTERNALS.md](docs/GAME_INTERNALS.md): reverse-engineered addresses, structures and file formats, plus the reverse-engineering toolchain.
- [docs/BUILDINGS.md](docs/BUILDINGS.md): making new buildings and floor plans in Blender.
- [docs/ROADMAP.md](docs/ROADMAP.md): the Television & Streaming plan, phase by phase, and known issues.

## Credits

- *The Movies* © Lionhead Studios / Activision. This is an unofficial fan mod; no game files are redistributed.
- [MinHook](https://github.com/TsudaKageyu/minhook) (BSD 2-Clause), [Dear ImGui](https://github.com/ocornut/imgui) (MIT).
