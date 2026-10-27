# Awards: the movie awards ceremony, and a TV "Golden Antenna" ceremony

Research notes from MoviesSE.exe (Ghidra headless). All addresses are VAs in MoviesSE.exe.
Supporting dumps: `re/aw1.txt` .. `re/aw19.txt`. Data: `data/awards.ini`, `data/awardsbonuses.ini`,
`data/awardsannouncers.ini`, `data/costume/award/*.ccs` (MISC0000.pak, Addon_2704_0000.pak).
"Confirmed" means the code was read. "Guess" means it wasn't.

## 0. Short version

- Ceremonies are held every **5 game years**. The awards are judged and stored as `CAward` objects in one
  global list, each stamped with its ceremony date (`When`). The ceremony screen is opened with a
  date. It **collects the CAwards whose date equals that date** and plays them one by one.
- The mod can open a ceremony at any time by calling `FUN_007C8760(&info)` (or `FUN_007C8990()`
  to replay the most recent one). Holding a TV ceremony therefore means: create CAwards dated
  "now" with new type ids, then open the screen for "now". Five small hooks make the new type
  ids show correctly (section 5).
- Nominees are **not shown as individual names or pictures**. The results board has one row per
  studio, and the contending studios light up. Only the winner's name is shown, in the presenter's
  speech bubble ("%WINNER% from %STUDIO%!") and in the trophy tooltip.
- Effects come only from the award's winner object (`+0xA0`) and its category (`+0x68`). A TV award with
  winner object NULL and category 2 has no game effect (section 4).

## 1. Scheduling

### Globals (CAwardsManager is a singleton; most of its state is in globals)
| Address | Type | Meaning | Saved as (`FUN_0085C360`) |
|---|---|---|---|
| `0x0104F0E8` | float (days) | PreviousAwardsDate: date of the last ceremony | `PreviousAwardsDate.YearInDays` |
| `0x00E5D2D8` | int (years) | interval, tuning `[awards] interval` in the global tuning file, **= 5** | not saved |
| `0x00E5D2D4` | int (year) | FirstAwardsYear = start year + interval | `FirstAwardsYear` |
| `0x0104F0D3` | bool | Enabled (set at new game from game mode, `FUN_0085F160`) | `Enabled` |
| `0x0104F0D4` | float | dev var `awd_enableinsandbox` (non-zero enables awards in sandbox) | no |
| `0x0104F0E0` | float | dev var `awd_noforceceremony` (0 = ceremony opens automatically) | no |
| `0x0104F0D0/D1` | bool | "coming soon" / "about to start" tannoys already played | no |
| `0x0104F0C8/CC` | int | LastRankCeremony / LastStuntRankCeremony (lifetime ranks) | yes |
| `0x0104E858` | float (days) | MostRecentCeremonyViewed (`FUN_007BD3B0`) | `MostRecentCeremonyViewed.YearInDays` |
| `0x00E4FA4C` | float | current game date in days (see GAME_INTERNALS) | - |

Date helpers (all confirmed): `FUN_0043B520(float* out, float years)` (`out = years*365`),
`FUN_0043B540(out, years, days)`, `FUN_0043B560` (date to year), `FUN_0043B710` (days to years),
`FUN_0043B6A0(a,b)` (`a >= b`), `FUN_0043B6E0(a,b)` (`a <= b`), `FUN_0043B680(a,b)` (`a > b`), `FUN_0043B660` (`!=`).
`FUN_0085C530(float* out)` returns the next ceremony date (`prev + interval*365`). `FUN_0085BAE0(out)` returns
PreviousAwardsDate. `FUN_0085BB00()` returns FirstAwardsYear. `FUN_0085BB10()` returns whether awards are enabled.

### The tick: `FUN_008605C0` = CAwardsManager vslot +0xC (vtable `0x00D61D50`) (confirmed)
Runs only when Enabled is set or the sandbox override is non-zero. Each tick:
1. Once, when `now >= next - 1 year`: tannoy `TANNOY_AWARDS_COMINGSOON`.
2. Once, when `now >= next - 15 days`: tannoy `TANNOY_CEREMONY_ABOUTTOSTART`, `FUN_007955A0()`,
   `0x0104E79E = 1` (guess: timeline/HUD flag).
3. On a timer, `FUN_0085FBC0`: lifetime-achievement judging (one factory per tick).
4. If no ceremony is open (`FUN_007C88F0()`, i.e. `0x0104E8E4 == 0`) and the UI is free (`FUN_0053D1D0`:
   no modal dialog, at least 2 s since the last one), and `now >= next`:
   - `FUN_00860580()` judges the ceremony: `FUN_0085FB00` (each factory whose type is due calls its
     vslot +0x1C Judge), then `FUN_008600E0` (lifetime status), then `FUN_00860190` (winner bonuses).
     Last, `PreviousAwardsDate += interval*365`.
   - It builds the 0x18-byte **CeremonyInfo** (below) with `ceremony=1` and the date. It also sets
     rank-up flags when the lifetime/stunt rank rose.
   - If `awd_noforceceremony == 0` and the info is not empty (`FUN_0085B990`), it calls `FUN_007C8760(&info)`.

Timeline: `WTimelineAwardsIcon` (vtable `0x00D50F44`, ctors `FUN_007A2030`/`FUN_007A2190`). Clicking it
(`FUN_007A1D60`) opens `FUN_007C8760` for the icon's date (a replay). The countdown toasts
`HUD_TIMELINE_AWARDS_NOTIFICATION_4MINS/2MINS/30SECS` come from `FUN_00797C50`, at 240 s, 120 s and 30 s of
real time before the next date.

### Opening a ceremony (confirmed)
```
struct CeremonyInfo {      // 0x18 bytes, copied to globals 0x0104E8B8..0x0104E8CC
  float when;              // +0  ceremony date (days). Awards with CAward.When == when are shown
  u8    ceremony;          // +4  1 = a ceremony (as the tick and awd_screen pass)
  u8    rankUp;            // +5  show the lifetime rank-up screen first
  u8    stuntRankUp;       // +6  show the stunt rank-up screen first
  u8    pad;
  int   oldRank, newRank, oldStuntRank, newStuntRank;   // -1 when unused
};
void __cdecl FUN_007C8760(CeremonyInfo*);   // "open awards screen"
```
- `FUN_007C8760` registers `MT_AWARDS_OPENAWARDSCREEN` (0x408) and `MT_AWARDS_CLOSEAWARDSCREEN` (0x430).
  It creates the `CAwardsLauncherMessenger` (vtable `0x00D577B0`), stores it in **`0x0104E8E4`** (non-zero
  while the ceremony is pending or open), and posts message 0x408. It does nothing if one is already open.
- The messenger's handler `FUN_007C9450` creates **WAwards** (`FUN_007C8FF0`, 0x3AC bytes, vtable
  `0x00D57844`). This happens on a later frame, because the message is queued.
- WAwards ctor: `FUN_007BD260` stores {when, rankUp, stuntRankUp, ceremony} in its info. It pauses and
  hides the HUD (guess: `FUN_0053CA50`, `FUN_0071C290`, `FUN_00424130(1,0,0)`). Then, unless a rank-up
  screen is due (`WAchievementAwards`, `FUN_007C7190`), it creates **WStandardAwards** (`FUN_007DADE0`,
  0x454 bytes, vtable `0x00D587B4`) = the 3D ceremony.
- The WAwards dtor `FUN_007C9260` clears `0x0104E8E4`.
- **On demand:** `FUN_007C8990` (`void __cdecl()`, the `awd_screen` dev command) opens the ceremony for
  PreviousAwardsDate, which replays the last one. To open any date, call `FUN_007C8760` directly with
  `{when, ceremony=1, 0, 0, -1...}`. Call it from the game thread, when `FUN_007C88F0()` is false and
  `FUN_0053D1D0()` is true.
- Replay vs live: `FUN_007BD2C0` is true when `when <= MostRecentCeremonyViewed`. If so, WStandardAwards
  skips the show and goes straight to the results board with all trophies (`FUN_007D8840`).
  `FUN_007BD3D0` raises MostRecentCeremonyViewed to `when` (if `when <= now`). A TV ceremony must therefore
  use a date later than the last viewed one; use "now".
- Other dev commands, registered in `FUN_0085ED10` (`awd_*`) and `FUN_00867D20`/`FUN_0084FFF0`:
  `awd_achievement`, `awd_stuntachievement`, `awd_allachievement`, `awd_filltallies` (`FUN_0085D460`,
  makes fake awards won by "Kouki!"), `awd_lifetime`, `awd_playeronly` (`0x0104EF50`), `awd_rankup`,
  `awd_stuntrankup`, `awd_enableinsandbox`, `awd_noforceceremony`. The retail game has no console for them.
  The mod would call the handlers directly.

## 2. Categories

### Award type ids (confirmed: 70 names, a String table at `0x00E5D2E0`, stride 0x20, `FUN_00860970(type)`)
| Ids | Set (std::set<int>, built once) | Names (`<NAME>`) |
|---|---|---|
| 0-5 | stars `FUN_00861AF0` | STARS_FIRSTPLACED, HIGHESTCLIMBER, BESTNEWCOMER, BESTDIRECTOR, BESTACTOR, MOSTPROLIFIC |
| 6-0xB | studios `FUN_00860EC0` | STUDIOS_FIRSTPLACED, HIGHESTCLIMBER, QUALITYOUTPUT, MOSTPROLIFIC, BESTLOT, BESTEMPLOYER |
| 0xC-0xD | movies `FUN_00861040` | MOVIES_FIRSTPLACED, MOVIES_BESTSCRIPT (available_from 3000 = never) |
| 0xE-0x30 | lifetime `FUN_00861290` | ACHIEVEMENT_CASHRICH1..AWARDS4 (35) |
| 0x31-0x33 | stunt `FUN_00861150` (addon only) | STUNT_BESTPERFORMER, BESTMOVIE, DAREDEVIL |
| 0x34-0x45 | stunt achievements `FUN_00861740` | STUNTACHIEVEMENT_* (18) |
| **0x46** | - | **wildcard/"none"**: the award iterator treats type filter 0x46 as "any". `FUN_00860970` returns "" for ids >= 0x46 |

"Standard" = the union of stars, studios, movies and stunt (`FUN_00861C60`), as tested by `FUN_00861DB0`.
Other predicates: `FUN_00861A30` (studio set), `FUN_00861A60` (stunt), `FUN_00861A90` (lifetime), `FUN_00861AC0` (stunt achievements).
Text keys: the ceremony title `FUN_00861DE0(WString* out, int type)` returns `AWARDSNEW_<NAME>` for standard
types and `AWARDS_TYPES_<NAME>` for lifetime/stunt achievements. For anything else it returns **empty**.
Icon: `FUN_00861FB0(type)` returns a `String*`: `ui\award_actor.dds` (stars), `ui\awards_film.dds` (movies),
`ui\award_studio.dds` (studios), `ui\awards_stunt.dds` (stunt). For anything else it returns **NULL**.
Per-type data comes from `data/awards.ini` (`available_from`, `tally_threshold`, `available_post_stunt`),
loaded by `FUN_00860A80` into arrays of 0x46 ints (`0x0104F110`, `0x0104F228`). Bonus per type comes from
`awardsbonuses.ini [awards]`.

### Factories (confirmed)
Each CAwardFactory (base vtable `0x00D61664`) is a static object. Its ctor `FUN_0084FA40` registers it with
`FUN_0085F2E0` in the vector `FUN_0085EB40()` (`0x0104F0C4`: {?, begin, end, cap}). Vtable slots:
`+0x0` dtor, `+0x8` IsEligible?, `+0xC` **GetType**, `+0x10` description, `+0x14` progress (lifetime),
`+0x18` float, `+0x1C` **Judge** (`FUN_00851AA0` for standard factories), `+0x20` contender name (movie
`FUN_00865EB0` = project title; star `FUN_008681D0` = star vfunc +0x5C; studio `FUN_00869090` = vfunc +0x20),
`+0x24` **GatherContenders**(vector<WinnerInfo>).
Classes: CMovieAwardFactory, CBestScriptMovieAwardFactory (type 0xD), CFirstPlacedMovieAwardFactory (0xC),
CStarAwardFactory and its 6 subclasses, CStudioAwardFactory and its 6 subclasses, 3 stunt factories,
CLifetimeAwardFactory and its subclasses, CAwardCountFactory.
`Judge` (`FUN_00851AA0`): gathers contenders, sorts them by score (`FUN_00851450`), and calls
`award = FUN_00858930(type, &nextDate, 0)`. It then fills the award (`FUN_008586B0`): the top 3 entries
with **different studios**, and WinnerName = GetName(winner). If logging is on (`FUN_0054B120`, guess) it
writes an HTML "Now judging entries for" table and AwardLog.csv.

### CAward (0x13C bytes, ctor `FUN_00858820`, scalar-deleting dtor `FUN_00857D60`) (confirmed)
| Offset | Field |
|---|---|
| +0x00 | vtable `0x00D61908` (CAward) |
| +0x38 | serializable sub-object, vtable `0x00D618E8` (+4 save `FUN_00857040`, +8 fields `FUN_00857400`) |
| +0x60 | int **Type** (`(int&)(Type)`) |
| +0x64 | float **When**, the ceremony date in days (`When`) |
| +0x68 | int **Category** (`(int&)(Category)`): 0 = ceremony award, 1 = lifetime (`FUN_008622E0`) |
| +0x6C | WString **WinnerName** (0x20 bytes) |
| +0x8C | **WinnerInfo[3]**, 0x34 each (`Winners[x]`). [0] = winner, [1..2] = runners-up |
| +0x128 | int **NumRunnersUp** (= entries - 1) |
| +0x12C/+0x130 | InList<CAward> links (global list, sentinel `0x0104F00C`) |

WinnerInfo (0x34): `+0x00 CP<TMObject>` winner object (pointer at +0x14: CProject*, CStar* or CStudio*),
`+0x18 CP<CStudio>` the winner's studio (pointer at +0x2C), `+0x30 float` score.
For [0] the absolute offsets are **+0xA0 winner object, +0xB8 studio, +0xBC score**. A CP is set as
`cp->vt[1](cp)` (unlink), write the pointer, then `cp->vt[0](cp)` (link). See `FUN_00857110`.

Creation API (confirmed by disassembly):
```
CAward* __cdecl FUN_00858930(int type, const float* when, int category); // new + link into global list
void __thiscall FUN_00857110(CAward*, const WString* name, TMObject* winner, float score, CStudio* studio);
        // SetWinner: WinnerName, [0].winner, [0].score, [0].studio, NumRunnersUp = 0,
        // then FUN_00856CC0 (effects, section 4). ret 0x10
award->vtable[0](1)   // delete: unlinks from the global list and frees
```
Iterator over all awards: `FUN_00857D80` starts it, `FUN_00857BD0` gives the end, `FUN_00856DD0` compares
(!=), `FUN_00856DA0` returns the CAward*, `FUN_00857260` advances. Filters, checked in `FUN_008571B0`:
`+0x18` winner object (`FUN_00857AE0`, 0 = any), `+0x1C` type (`FUN_00857B30`, **0x46 = any**),
`+0x20/+0x24` category (`FUN_00857B90`), `+0x00` date (`FUN_00857B60`, 0 = any).
Awards **are saved**: CAward is a registered TMObject with a serializer (`GlobalAwardsLink`).

## 3. Presentation (WStandardAwards, `FUN_007DADE0`)

### Building the award list (confirmed)
`FUN_007D9270` (fastcall this) builds **`std::vector<CAward*>` at this+0x408** (+0x40C begin,
+0x410 end, +0x414 cap). For each of the 4 sets (stars, studios, movies, stunt), in order, and each type in
the set, it finds the award with that type and `When == info.when`. Push-back is
`FUN_007D7A00(this+0x408, end, 1, &award)` (thiscall). Lifetime awards are never in a ceremony.
Then `FUN_007D9520` builds the UI:
| Field | Widget |
|---|---|
| +0x358 | WAwardsResultsBoard (`FUN_007CEF30`, 0x3FC bytes): **one row per studio** (studio list `0x0104BCF0`, the player's first) |
| +0x370 | award info panel (`FUN_007C7700`, vtable `0x00D57708`). Slot 0xFC = `FUN_007C7760(type, isNew)` shows icon, title, `AWARDSNEW_<NAME>_HINT`, `AWARDS_SCREEN_WINNERBONUS` + bonus name |
| +0x388 | skip/next button (`AWARDS_SKIP`) |
| +0x3A0 / +0x3B8 | speech bubbles of presenter A (male) / B (female). +0x3BC is the hide time for B |
| +0x3D4 | programme list (`FUN_007D89E0`): one row per award in the vector (icon + title), row widgets at +0x348 |
| +0x3EC | WAwardsAnnouncers (`FUN_007CBC20`): the two 3D presenters (`ai_award_present.flm`, costumes `data\costume\award\[mf]_announce_<year>.ccs`) |
| +0x418 | current award (`CAward**` into the vector) |
| +0x41C | current CCeremonyState* |
| +0x440..+0x44C | vector<int> of new award types to explain at the end (`FUN_007DAAF0`) |

Global `0x0104E9C8` = the open WStandardAwards (the states use it).

### State machine (confirmed)
A CCeremonyState is 8 bytes: {vtable, start time ms}. Vtable `+0` dtor, `+4` **CreateNext()**,
`+8` Update (no-op), `+0xC` **IsDone()**. Each state's ctor performs its action. The driver is
`FUN_007D6880` (WStandardAwards vslot +0x28): if a state is done it replaces it with CreateNext(). With no
state it advances +0x418 and starts `CStateInitialPause` for the next award, or the goodbye states after
the last one. Most IsDone() check the talk animation (`FUN_007CA010(anim) > 0.5`) and that the voice has
finished (`FUN_007C9690`).

| State (vtable) | Entry action (ctor) |
|---|---|
| WelcomeInitialPause `D58278` -> WelcomeManWaitToTalk `D582A0` | animations |
| WelcomeManTalks `D585B4` | `FUN_007D4F10`: `AWARDS_DIALOGUE_WELCOME_FIRST` / `_<year>` (keys exist for 1930..2010 in steps of 5) / `_AFTER_2010` |
| WelcomeWomanWaitToTalk `D585C8` -> WelcomeWomanTalks `D585DC` | `FUN_007D5240`: `AWARDS_DIALOGUE_WELCOME_REPLY_FIRST/_GENERIC` |
| **per award:** InitialPause `D58578` | highlights the programme row (`FUN_007D1A20(index)`) |
| WaitToStartTalking `D5858C` | |
| AnnounceNomineesMessage `D585A0` | `FUN_007D4B60`: `AWARDS_DIALOGUE_NOMINEES_<NAME>` (random variant, male), info panel `->0xFC(type,0)` |
| HighlightContenders `D58A14` | `FUN_007D4E40`: `AWARDS_DIALOGUE_EXPLAIN_<NAME>` (female). `FUN_007D9FA0`: nominee studios = [0].studio plus [1..NumRunnersUp].studio (deduplicated, NULL skipped), passed to `FUN_007CF600`, which lights the board rows one every 2 s (`HUD_AWARDS_NOMINATIONS_ARE`, `HUD_AWARDS_NOMINEELIGHTUP`, cheer/heckle) |
| FinishingNominees `D58A28` -> WaitingToOpenEnvelope `D58A3C` | envelope animation, 2 s |
| StartAnnounceWinner `D58A50` | `FUN_007D4CD0`: `AWARDS_DIALOGUE_WINNERIS_<NAME>` (female) |
| WaitingToSpeakWinnerName `D58A64` | |
| AnnounceWinnerName `D58A78` | `FUN_007D84D0`: **only if [0].studio (+0xB8) != NULL**: phrasebook `AWARDS_SCREEN_WINNERANDSTUDIO` ("%WINNER% from %STUDIO%!") or `AWARDS_SCREEN_WINNINGSTUDIO` (studio types). WINNER = WinnerName +0x6C, STUDIO = studio vfunc +0x20. Voiced studio name `FUN_007D3CB0(out, studio)` (`AWARDS_DIALOGUE_WINNERIS_<studio+0x1F0>` for AI studios, `PROFILESCREEN_DIALOGUE_DEFAULTSTUDIONAME` for the player). `ai_reaction` (full if player). **Trophy** `FUN_007D8260`: on the studio's board row (`FUN_007CE3F0`, icon from `FUN_00861FB0`), tooltip WinnerName<br>Studio<br>Title, and the programme row text `FUN_007D17C0(vectorIndex, text)` |
| Reaction `D58A8C` -> FinishedAward `D58AA0` | turns the nominee lights off (`FUN_007CD0B0`). CreateNext = NULL, so the next award starts |
| ManWaitToSayGoodbye `D5828C` -> ManSaysGoodbye `D58514` | `FUN_007D5380`: `AWARDS_DIALOGUE_CONCLUDE_1` |
| WomanWait/SaysGoodbye `D58528`/`D5853C` | `FUN_007D5490`: `AWARDS_DIALOGUE_CONCLUDE_2` or `_2_/_2A_<new award>` |
| ManWaitsToExplain/ExplainsExtraAward `D58564`/`D58648` | `FUN_007D6BA0`: `AWARDS_DIALOGUE_CONCLUDE_3_<new award>` |
| AllDone `D58550` | |

Dialogue: the text goes through `FUN_009B5F90` (TranslateKey: random `_10/_20/_30` variant; record +0x40
text). The mod already hooks this in `src/text.cpp`. Some lines use `FUN_009B5030` (LookupText,
also hooked). The text is shown in a bubble (`FUN_007D3F90` for A, `FUN_007D40A0` for B). **Every line is
voiced.** `FUN_007C9F40(String* key)` (male, `0x0104E8F0`) and `FUN_007C9F80(String* key)` (female,
`0x0104E8EC`) play the sound with the same key plus `Data\Audio\LipsSync\<key>.lps`, via `FUN_00982230`.
A key with no sample returns early (`-1`, guess) and plays nothing. Generic voiced lines that could be
reused: `AWARDS_DIALOGUE_WINNERIS_MOVIES_BESTMOVIE_10` ("And the winner is..."), `CONCLUDE_1_*`,
`CONCLUDE_2_10/20/30`, `WELCOME_REPLY_GENERIC`. Every `NOMINEES_*` and `EXPLAIN_*` line names its category.

### What to override for custom awards
| What | Where it comes from | Mod lever |
|---|---|---|
| Category text keys (nominees/explain/winner-is/hint) | `FUN_00860970(type)` -> `<NAME>` | hook it: TV ids return a static game String like `TV_BESTSITCOM`; supply the keys with `text::Set` |
| Title (info panel, programme, tooltip) | `FUN_00861DE0(out,type)` | hook it: TV ids give the title |
| Icon | `FUN_00861FB0(type)` returns NULL for unknown ids, **which crashes** (`FUN_0069D820`/vfunc +0x100) | hook it: TV ids return a valid String* (e.g. the film icon, or a new `ui\awards_tv.dds`) |
| Programme row exists | `FUN_0085CEA0(type, when)` reads `0x0104F228[type]`, **out of bounds for ids >= 0x46**. If a row is skipped, `FUN_007D17C0(vectorIndex)` indexes past the row vector | hook it: TV ids return true |
| Which awards are shown | `FUN_007D9270` (only the 4 sets) | hook it: after the original, push the TV CAwards with `FUN_007D7A00` (or, for a TV-only show, push only those) |
| Nominee lights | WinnerInfo[0..2].studio | set the studios (max 3 distinct). NULL = no light |
| Winner text | WinnerName +0x6C (any text), studio +0xB8 (**must be non-NULL** or nothing is announced or awarded) | WinnerName = show title. Shows from rival networks need a stand-in studio or a hook on `FUN_007D84D0`/`FUN_007D3CB0` |
| Voice | `FUN_007C9F40`/`FUN_007C9F80` | optional hook: swap TV keys for generic voiced keys, or skip them |
| Welcome/conclude ("Lionhead Motion Picture Awards") | keys in the table above | `text::Set` while the TV show runs (the voice still says "Motion Picture" unless the voice hook skips it) |

## 4. Effects of winning (what a TV award must avoid)
- `FUN_00856CC0` (called by Judge and by SetWinner). For lifetime/stunt-achievement types it calls
  `FUN_00866AB0`. Otherwise, by **winner object +0xA0**:
  - CStar: message `0x8000069B` (guess: MT_STAR_WINAWARD, star likes the award) and star rating recompute `FUN_00591BF0`.
  - CProject: film rating `FUN_005B8C90` (`starratingawards` weight).
  - CStudio: studio rating `FUN_0050AB70`.
  - NULL: nothing happens.
- Studio rating's awards factor (`FUN_0050D8E0`, CStudio vslot +0x98) = 0.169 × the number of **category-0**
  awards at PreviousAwardsDate whose +0xB8 is the studio.
- Winner bonuses (`FUN_00860190`, called only by the judging path): standard types won by the player
  switch bonuses on or off via `awardsbonuses.ini`. Unknown types have no bonus (`FUN_0085A3D0` returns 0x11).
- Lifetime "Awards 1-4" (`FUN_0085FE50`) and the end tally (`FUN_007D0BE0`/`FUN_007D0690`) count player wins
  per type. Only the standard set is checked against `tally_threshold`, so TV ids are counted and ignored (guess for the tally display).
- Timeline award graph (`FUN_0080BAD0`) and charts list category-0 player wins at a date.
- Object-specific counts (star/project/studio pages, league tables) filter on the winner object, so they never match a NULL winner.
- AwardLog.csv: debug log written by Judge only (guess). Manual awards never write it.

**So, for TV awards:** category **2**, winner object **NULL**, type ids **0x47+** (never 0x46), create
them yourself (never through a factory or `FUN_00860580`), and don't call `FUN_00860190`.

## 5. Plans

### (b) Separate TV-only ceremony (recommended; the mod's Golden Antenna is yearly, the movie one every 5 years)
1. On the TV awards day (`tv_events.cpp` HoldAwards, mid-May), on the game thread, when
   `!FUN_007C88F0() && FUN_0053D1D0()`. If the UI is busy, retry on later ticks.
2. `now = *(float*)0x00E4FA4C`. For each TV category i: `a = FUN_00858930(0x47+i, &now, 2)`.
   Then `FUN_00857110(a, &wname, NULL, score, studio)`, then write runners-up into
   `a+0x8C+k*0x34` (studio CP at +0x18, pointer at +0x2C) and set `+0x128`.
3. `text::Set` the per-category keys (`AWARDS_DIALOGUE_NOMINEES_TV_X`, which can list the nominee show names,
   `..._EXPLAIN_..`, `..._WINNERIS_..`, `AWARDSNEW_TV_X_HINT`). While the TV show runs, also override the
   welcome/reply/conclude keys and `AWARDS_SCREEN_WINNERANDSTUDIO`.
4. Set `g_tvCeremony = true` and call `FUN_007C8760(&{now, 1, 0, 0, -1,-1,-1,-1})`.
5. Hooks, active only for TV ids or while the flag is set: `FUN_00860970`, `FUN_00861DE0`, `FUN_00861FB0`,
   `FUN_0085CEA0`, `FUN_007D9270` (push the TV awards). Optional: `FUN_007C9F40`/`F80` (voice),
   `FUN_007D3CB0` (return "" so no studio name is spoken for TV).
6. When `FUN_007C88F0()` turns false again: delete the TV CAwards (`vt[0](1)`), unset the text and clear
   the flag. The mod keeps its own trophy history (it already has `g_trophies`).

### (a) Append TV categories to the movie ceremony
Hook `FUN_00860580` (judging). After the original, PreviousAwardsDate is the ceremony date: create the
TV CAwards with that date (same as step 2). The `FUN_007D9270` hook appends them after the stunt awards.
Delete them after the screen closes, or keep them so timeline replays include them (they are then saved).
This happens only every 5 years and mixes voiced and silent lines, so (b) fits better.

### Risks
- **Saves:** CAwards are saved. TV awards left in a save (a crash mid-ceremony, or plan (a) keeping them)
  load without the mod as unknown types. Those are ignored by the ceremony (not in the sets). Without the
  icon/availability hooks, only a replay of that date's movie ceremony with them pushed would crash, and
  nothing pushes them without the mod. Guess: low risk. Clean up anyway: on load, delete awards with
  `Type >= 0x47`.
- **Crashes without hooks:** NULL icon (`FUN_00861FB0`) and the programme-row mismatch (`FUN_0085CEA0`
  out of bounds). Hook both before showing any TV award.
- Winner studio NULL: no announcement, no trophy (no crash). The trophy hover callback `LAB_007D6CE0`
  was not read: it may open the winner object's page (+0xA0 = NULL). Test hovering and clicking the TV trophies.
- Missing voice samples: guess that they are silent and fast (IsDone only waits for the animation). Test
  readability. A bubble may need `+0x3BC`-style timing.
- Board rows are studios only: shows from rival TV networks (not studios) have no row of their own.
- The mod's TranslateKey hook falls back to `<key>_10`/`_20`, so define the base key.
- Don't open a TV ceremony on the same date as a movie one (both lists would merge), and never during the
  movie ceremony's judging tick.
