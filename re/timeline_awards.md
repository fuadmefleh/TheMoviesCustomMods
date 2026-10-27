# Timeline awards icons and the ceremony countdown

Research notes from MoviesSE.exe (Ghidra headless + capstone). All addresses are VAs in MoviesSE.exe.
Supporting dumps: `re/tl1.txt` .. `re/tl12.txt` (decompiles), `re/tl_d1.txt` .. `re/tl_d6.txt` (disassembly).
Builds on `re/awards.md`. "Confirmed" means the code was read. "Guess" means it wasn't.

## 0. Short version

- The bottom bar is **`TM::WTimeline`** (0x54C bytes, vtable `0x00D50A64`). The HUD's instance is at **`*(WTimeline**)0x0104E7B8`**
  (`FUN_007955A0()` returns it; CP at `0x0104E7A4`). Created by `FUN_007A1630(0)`.
- There is **no list** of awards icons. The timeline has **4 fixed CP slots**: the last ceremony, the next one,
  the one after, and the stunt-rank icon. Only the **most recent past** ceremony has an icon.
- Icons are not saved. A new WTimeline (new game / load) rebuilds them from `PreviousAwardsDate`.
- Every frame, the timeline lays out its own 4 slots (date -> x). Extra icons added by the mod are **not touched** by the game:
  the mod must position them every frame, and they die with the timeline (it releases its children).
- Click: the ctor registers a per-instance callback (`FUN_00741630` slot 0 -> `0x007A1EB0` -> `FUN_007A1D60`). The mod can
  **re-register slot 0 with its own callback** on its own icons. No global hook is needed.
- Tooltip: the text is `SITT_AWARDS_UPCOMING` / `_PAST` (+ `%date%`), built from the slot's **kind**, not the icon's date.
- The "countdown pop-ups" are **sounds, not text**: `HUD_TIMELINE_AWARDS_NOTIFICATION_4MINS/2MINS/30SECS` are sample names
  in `Data\Audio\soundbank_01.lug` (sample ids 0x4E/0x4F/0x50). There is no text key for them and no window. The mod's
  text hook cannot change them.

## 1. Classes and globals

| Address | What | Status |
|---|---|---|
| `0x0104E7B8` | `WTimeline*` HUD timeline (raw ptr of CP at `0x0104E7A4`); `FUN_007955A0()` returns it | confirmed |
| `0x00D50A64` | WTimeline vtable (RTTI `.?AVWTimeline@TM@@` `0x00E59DB4`). Slot +0x28 = `FUN_007A0DA0` (tick: countdown, shift), +0xE0 = `FUN_007A1700` (calls the layout `FUN_00798C10`), +0xC = AddChild `0x0073F6E0` | confirmed |
| `0x00D50F44` | WTimelineAwardsIcon vtable (RTTI `0x00E59E7C`), 0x3A0 bytes, base WImage (`FUN_0069D820`) / WWindow | confirmed |
| `0x0104E838` | raw ptr of CP `0x0104E824`: **the last-constructed awards icon**. The tutorial target `"stunt_icon"` (`FUN_009127D0`) points at it | confirmed |
| `0x0104E7A0/A1/A2` | bools: 4MINS / 2MINS / 30SECS sound already played | confirmed |
| `0x0104E79C` | set by `FUN_007942E0(0)` (lifetime achievements `FUN_0085F5C0`/`FUN_0085FBC0`): pop the next icon's tooltip with the prizes panel | confirmed |
| `0x0104E79E` | set by the awards tick 15 days before (and `0x0085B950`): show the 3D award cup on the next icon (`FUN_007A2DA0` -> `FUN_007A22A0`, `SITT_AWARDS_TAKENPLACE`) | confirmed |
| `0x0104E79F` | set by the awards tick after judging: shift the icons (`FUN_007978E0`) | confirmed |
| `0x00E59C20` | float: icons' y (`FUN_00790EA0()` returns it) | confirmed |
| `0x00E59C85` | byte: icons visible (passed to vt+0x20) | confirmed (meaning guess) |

### WTimeline fields (icon slots)
Each slot is a `CP<WTimelineAwardsIcon>` (24 bytes, raw ptr at +0x14).

| CP | Raw ptr | Kind (+0x364) | Date written to +0x370 |
|---|---|---|---|
| +0x4C4 | **+0x4D8** | 0 | PreviousAwardsDate (`FUN_0085BAE0`) |
| +0x4DC | **+0x4F0** | 1 | next date (`FUN_0085C530`) |
| +0x4F4 | **+0x508** | 2 | next + interval*365 (`FUN_007A1990`) |
| +0x50C | **+0x520** | 3 | none (stunt rank icon `ui/awards_stunt.dds`; clicking it opens the stunt rank-up screen) |
| +0x528 | int | | handle of the countdown sound playing (-1 = none) |

### WTimelineAwardsIcon fields (ctor `FUN_007A2030`, dtor `FUN_007A2190`, deleting dtor vt[0] = `FUN_007A2AE0`)
| Offset | Field |
|---|---|
| +0x48 | refcount (TMBase). The game removes an icon with `if (--icon[+0x48] == 0) icon->vt[0](1)` (`FUN_007978E0`) |
| +0x118 | parent window (set by AddChild) |
| +0x360 | byte: enlarged (hovered, 32x32) |
| +0x361 | byte: hovered this frame (cleared at the end of every update) |
| +0x362 | byte: texture already swapped to `ui/timeline_award2.dds` (the 2005 centennial). **Set it to 1 to block the swap** |
| +0x364 | int **kind** (0..3, setter `FUN_007A19E0(this, kind)`, thiscall) |
| +0x368 | tooltip popup (0x14-byte holder from `FUN_008DF0A0`; +0x10 = lifetime ms) |
| +0x36C | 3D cup window (`FUN_007A22A0`) |
| +0x370 | float **date** (days). Ctor sets `(year(now) - 100) * 365` as a placeholder |
| +0x374 | timer (`FUN_0043B460/4D0`, 10) |
| +0x384 | byte: "award event generated" sound played |
| +0x388 | CP<WTimeline> (raw +0x39C), found from the parent with a dynamic_cast in the update |

## 2. Who creates the icons, when, and for which dates (confirmed)

- **`FUN_00797E90`** (fastcall WTimeline*), called first thing by the layout `FUN_00798C10` every frame. If awards are
  enabled (`FUN_0085BB10`) and **slots +0x4D8, +0x4F0 and +0x508 are all null** (new timeline), it creates:
  the previous icon (kind 0, only if `year(now) >= FirstAwardsYear`), the next icon (kind 1) and the one after
  (kind 2, `FUN_0043B600`), all `ui/timeline_award.dds`. Separately, the stunt icon (kind 3, `ui/awards_stunt.dds`) when
  `*(0x0104A974) == 0 || *(float*)(*(0x0104A974)+0x80) == 0` and slot +0x520 is null.
- **`FUN_007978E0`** (fastcall WTimeline*), from the timeline tick when `0x0104E79F` is set (after a ceremony is judged):
  deletes the previous icon, moves next -> previous (kind 0) and after -> next (kind 1), creates a new "after" icon
  (kind 2), plays `HUD_TIMELINE_EVENT_AWARD`, stops the countdown sound and **clears the three countdown flags**.

Creation sequence (from `FUN_007978E0`, confirmed by disassembly):
```
String tex = "ui/timeline_award.dds";                     // game String {ptr,len,cap,buf[0x14]}; the ctor copies it
void* mem = ((void*(__cdecl*)(size_t))0x00ACBC50)(0x3A0); // game operator new (the deleting dtor frees with the game heap)
icon = FUN_007A2030(mem, &tex, 0.f, 0.f, 1.f, 1.f);        // __thiscall(this, const String* texture, float u0,v0,u1,v1), ret 0x14
cp->vt[1](cp); cp->ptr = icon; cp->vt[0](cp);               // store in the slot CP
icon->vt[0x74](16.f, 16.f);                                // SetSize
FUN_007A19E0(icon, kind);                                  // +0x364
icon->+0x370 = date;
x = FUN_00793F50(timeline, date, NULL);                    // float __thiscall(WTimeline*, float date, float* frac), ret 8; ST0
icon->vt[0x5C](1, timeline, x);                            // SetX (mode 1 = relative to window), ret 0xC
icon->vt[0x64](1, timeline, FUN_00790EA0());               // SetY
timeline->vt[0xC](icon, 1);                                // AddChild (0x0073F6E0: child+0x118 = parent, link at child+0x150)
```
The ctor also: registers callbacks with `FUN_00741630(this, slot, cb, listener, soundPrefix)` (thiscall, ret 0x10):
slot 5 = `0x007A1A50` (no-op), slot 0 = `0x007A1EB0` (click), prefix `"TIMELINEAWARDS"`; and **sets `0x0104E838` = this**.

### Layout every frame: `FUN_00798C10` (fastcall WTimeline*) (confirmed)
For each of the slots +0x4D8 / +0x4F0 / +0x508: `x = FUN_00793F50(date)`, (prev only: `x < 0` -> 0), `vt[0x5C](1,tl,x)`,
**rewrites +0x370** from PreviousAwardsDate / next / next+interval, `vt[0x64](1,tl,y)`, `vt[0x20](*(u8*)0x00E59C85)` (show).
It also plays `HUD_TIMELINE_AWARD_EVENT_GENERATED` once when the next icon scrolls into view, and pops the next icon's
tooltip when `0x0104E79C` is set. `FUN_00793F50` returns -1 when the date is beyond the bar's range.

### Icon update (vt+0x28 = `0x007A2B00`) (confirmed)
Finds its WTimeline (+0x39C) from the parent. When hovered (+0x361): if there is no tooltip and
(kind == 3 or `year(now) >= FirstAwardsYear` or `*(u8*)0x010503D0`), and the x is on screen (`FUN_00795FA0`), it calls
**`FUN_007A2790(this, 0)`** (tooltip). It grows to 32x32 and plays `HUD_TIMELINE_AWARD_EVENT_HIGHLIGHTED` (a sound key).
When the hover ends it shrinks to 16x16. If `year(+0x370)` is in [2005, 2005+interval) (`FUN_0085C570` = 2005, or 9999
when awards are off) and +0x362 is 0, it swaps the texture to `ui/timeline_award2.dds` via vt+0x100
(`SetTexture(String*, float rect[4])`, guess from the call).

## 3. Tooltip (confirmed)
`FUN_007A2790(icon, char autoShow)` (thiscall, ret 4). It does nothing if +0x368 is set. The date comes from the **kind**:
0 = PreviousAwardsDate, 1 = next, 2 = next+interval, **anything else = an uninitialised stack value**. Then
`FUN_007C1780(date, kind == 3)` (cdecl): if `*(0x0104A974)` and its float +0x78 is 1.0 or 3.0, it uses the rich panel
`FUN_007C0810` (ignores the date; game mode unknown). Otherwise `FUN_007BF6D0(date)` (cdecl) ->
`FUN_007BF360(WString* out, float date)` (cdecl) builds
`<e2><phrasebook>` + LookupText(key) + `<phrase key=date>` + date + `</phrase></phrasebook></e2>`, where key is
`SITT_AWARDS_UPCOMING` ("An awards ceremony will take place on %date%!") if date > now, otherwise `SITT_AWARDS_PAST`
("This Awards Ceremony took place on %date%!"), or the `_CENTENNIAL` variants for 2005. The text goes in a 0x3FC
window (`FUN_00833290`) inside a 0x4E0 popup (vtable `0x00D50E8C`), shown for 8000 ms (autoShow: 1000 ms, plus the
prizes panel `FUN_0063C630`/`FUN_0085B860`).
`HUD_TIMELINE_EVENT_AWARD` and `HUD_TIMELINE_AWARD_EVENT_HIGHLIGHTED` are **sound** keys (soundbank_01.lug), not text.

## 4. Click (confirmed)
`0x007A1EB0` is `char __stdcall(WWindow* sender, TMBase* listener)` (ret 8): it dynamic_casts the listener to
WTimelineAwardsIcon and calls **`FUN_007A1D60(icon, sender)`** (thiscall, ret 4, returns 1). That handler requires:
sender is a WTimelineAwardsIcon, `*(int*)0x0104D8E8 == 0`, `icon->vt[0x34](&0x0104CCE0, &tmp)` (mouse over it, guess),
and `year(now) >= FirstAwardsYear` (`FUN_0085BB00`). It builds a CeremonyInfo (`FUN_007A1900`: ceremony=0, ranks -1).
Kind 3: stunt rank-up info (`FUN_008666A0(1)` twice -> +0x10/+0x14, stuntRankUp=1). Otherwise **`when = icon+0x370`**.
Then `FUN_007C8760(&info)`. It does not check that CAwards exist for that date, or that no ceremony is open.

## 5. Countdown sounds: `FUN_00797C50` (fastcall WTimeline*), from the tick `FUN_007A0DA0` every frame (confirmed)
```
if (!FUN_0085BB10()) return;                                   // awards enabled
days = (int)next - (int)*(float*)0x00E4FA4C;                   // FUN_0043B570 = truncate
ms   = days * *(int*)0x00E4FA60 * 100.0f / *(float*)(*(int*)0x0104CDF4 + 0x38);
```
- `0x00E4FA60` = the current date object's +0x14 (`0x00E4FA4C` is a 0x18-byte date: float days at +0, int at +0x14).
  It is `(int)(curve(year) + 0.5)`, from the global tuning `[time] start`/`end` (pairs "year,value"; statics 1900,4 and
  ?,20; exponential between them, k = 0.05776 at `0x00E4FA48`; `FUN_0043B800`). So `value*100` = real ms per game day.
  Read it live (it changes with the year).
- `*(0x0104CDF4)` is the real-time clock object (+0x3C = ms tick). **+0x38 is the game-speed multiplier** (1.0 normal;
  forced to 1.0 and restored by `FUN_004129E0`/`FUN_00412A30`). Meaning confirmed by use, values other than 1.0 guess.
- Each threshold fires when `ms <= T` (240000, 120000, 30000; also when negative) and its flag is clear, only if
  `FUN_00423320(*(0x00F87B04)) == 0` (top of the game-mode stack is 0 = the lot, guess) and not paused
  (`*(u8*)(*(0x00F87B04)+0xC7) == 0`). At most one per frame (4MINS returns early).
- Posting: `FUN_0041C9C0(&rec, "HUD_..._4MINS")` (thiscall, ret 4: a 0x28-byte sound record, rec[1] = sound id from
  `FUN_009B01A0(key)`, rec[2] = -1), `rec[0] &= ~1`, stop the previous `FUN_009B11D0(tl+0x528)` (cdecl), then
  `handle = FUN_004F3270(*(void**)0x0104B0E4, 2, &rec, -1, (char*)0x00D17518 /* "none" */)` (thiscall, ret 0x10; plays only
  if the sound manager's current context is 2), stored at tl+0x528.
- Repeats: the flags are cleared by `FUN_007978E0` (after a ceremony), by `0x00795D10`/`0x00795D70` (pause / unpause
  handlers, guess) and by `0x00424160`/`0x00429080` (game-mode changes, guess). After a reset, the game plays the cues
  again in order (4, then 2, then 30 s), even when less time is left.
- **Text:** there is none. A mod key in `FUN_0041C9C0` would give sound id -1 and play nothing. To show something visual,
  pop the TV icon's own tooltip with `FUN_007A2790(icon, 0)` (with the mod's text, section 7) and/or use the mod overlay.

## 6. Comparison with src/news.cpp
News globes are **CNoduleEvent** data objects (`AddTimelineEvent` `0x00795AD0`, global list `0x0104E7BC`). The timeline
makes a window for any nodule without one, so news.cpp never touches windows. Awards icons are not nodules: they are
windows owned by WTimeline's 4 slots, so a TV awards icon has to be a window the mod creates and lays out itself.
(Fallback without windows: a CNoduleEvent per TV ceremony gives a news globe with headline text, not an awards icon.)

## 7. Recommended approach

### Goal 1: TV awards icons
1. **Create** on the game thread, best in a hook of the layout **`FUN_00798C10`** (`void __fastcall(WTimeline*)`): after the
   original, if `this == *(WTimeline**)0x0104E7B8` and the mod has no icons for this timeline, create one per TV date
   (upcoming + past ones still in range) with the creation sequence above. Use kind **0** (or 1). Never use 2 (the game
   only relayouts its own slots, but 2 is "after next") or 3 (stunt click path), and never an out-of-range kind (the
   tooltip would read an uninitialised date). Set **+0x362 = 1** (no centennial texture). Use the mod's own texture
   (e.g. `ui\awards_tv.dds`) as the ctor's String, or `ui/timeline_award.dds`.
2. **Restore `0x0104E838`** after each ctor call (save the old raw ptr, then `cp=0x0104E824: cp->vt[1](cp); *(0x0104E838)=old;
   cp->vt[0](cp)`), or the "stunt_icon" tutorial arrow points at the TV icon.
3. **Lay out every frame** in the same hook: `x = FUN_00793F50(tl, date, NULL)`; if `x < 0` hide it (`vt[0x20](0)`);
   else `vt[0x5C](1, tl, x)`, `vt[0x64](1, tl, FUN_00790EA0())`, `vt[0x20](*(u8*)0x00E59C85)`. Keep +0x370 = the TV date.
4. **Click:** after the ctor, `FUN_00741630(icon, 0, &ModClick, icon, "TIMELINEAWARDS")` replaces slot 0, with
   `char __stdcall ModClick(WWindow* sender, void* listener)` returning 1. Inside, mirror the game's checks
   (`*(int*)0x0104D8E8 == 0`, `listener->vt[0x34](&0x0104CCE0, &tmp)`), then only flag the replay request and run it from
   the mod's game-thread frame (when `!FUN_007C88F0() && FUN_0053D1D0()`), as tv_ceremony.cpp does. (Alternative: hook
   `FUN_007A1D60` and return 1 for mod icons without calling the original.)
5. **Recognise** icons by keeping their pointers in a set. **Hook the dtor `FUN_007A2190`** (`__fastcall(icon)`) and erase
   `this` from the set before calling the original: the timeline deletes its children when it is destroyed (load, quit to
   menu), so this is the only safe way to never hold a dangling pointer. When the set is empty for the current timeline,
   step 1 creates the icons again (this also covers loading a save: icons are never saved).
6. **Remove** a mod icon (e.g. a date out of range) the game's way: `if (--*(int*)(icon+0x48) == 0) icon->vt[0](1)`
   (the WWindow dtor `FUN_00742900` unlinks it from the parent). Refcount semantics are inferred from `FUN_007978E0`.
7. **Tooltip:** hook **`FUN_007A2790`** (`void __fastcall(icon, edx, char autoShow)`). For a mod icon, `text::Set` the keys
   `SITT_AWARDS_UPCOMING`, `SITT_AWARDS_PAST`, `SITT_AWARDS_CENTENNIALUPCOMING`, `SITT_AWARDS_CENTENNIALPAST` to the mod's
   text (with the date written in, and **no `%date%`**: the appended date is the kind's movie date), call the original
   with autoShow 0, then unset them. The text is looked up synchronously inside. Caveat: when `*(0x0104A974)+0x78` is
   1.0 or 3.0 the game shows the rich panel `FUN_007C0810` instead (game mode unknown); to cover it, also hook
   `FUN_007C1780` (cdecl `(float, char)`) and call `FUN_007BF6D0(date)` directly while a mod icon is being shown.

### Goal 2: countdown for the TV date
Run it from the mod's own frame (do not touch the game's flags `0x0104E7A0..A2`; they belong to the movie date):
```
float days  = tvDate - *(float*)0x00E4FA4C;                       // fractional is fine
float msDay = *(int*)0x00E4FA60 * 100.0f / *(float*)(*(uintptr_t*)0x0104CDF4 + 0x38);
float ms    = days * msDay;
```
Gate like the game (mode stack top 0, not paused, `FUN_0053D1D0()` optional). Keep the mod's own 3 flags per TV date and,
unlike the game, play only the tightest threshold crossed (so a pause doesn't replay "4 minutes" at 1 minute). Play the
same cue with `FUN_0041C9C0` + `FUN_004F3270(*(0x0104B0E4), 2, &rec, -1, 0x00D17518)`, stopping the mod's previous handle
with `FUN_009B11D0`. Never post within ~2 days of the movie ceremony's date (both would talk). For text, pop the TV icon's
tooltip (`FUN_007A2790(icon, 0)` with the overrides above) or use the overlay.

## 8. What could break
- The mod's icons are not in the slots, so `FUN_00797E90`, `FUN_007978E0` and `FUN_00798C10` never touch them; the
  movie icons are unaffected. The game never iterates children looking for awards icons (RTTI uses: only the icon's own
  functions and the tutorial-arrow position `FUN_008F8D70`).
- Clicking a game icon opens `FUN_007C8760` for its date without checking for CAwards (empty ceremony risk is the game's,
  not the mod's). If the mod hooks `FUN_007A1D60` instead of replacing the callback, it must not call the original for
  TV icons (no CAwards exist for TV dates after the show; tv_ceremony.cpp deletes them).
- Tooltip with an unknown kind: uninitialised date (avoid). Centennial years swap the texture unless +0x362 = 1.
- Hover before FirstAwardsYear shows no tooltip unless `0x010503D0`; the game's click requires it too (own callback avoids it).
- Save/load: icons are never saved. Holding icon pointers across a load without the dtor hook would be use-after-free.
- The ctor overwrites the tutorial target `0x0104E838` (restore it).
- Off-range dates: `FUN_00793F50` returns -1 (hide the icon; the game clamps only the previous icon to 0).
