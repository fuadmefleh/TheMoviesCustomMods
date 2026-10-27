# HUD: the right-hand project list

Research notes (MoviesSE.exe, Ghidra) for showing the mod's share objects in their own category.

Dumps:
- `re/prj1.txt` .. `re/prj11.txt`
- for the HUD root and the left-hand jobs card: `re/hud*.txt` and `re/employees.md` §E

"Guess" marks anything not confirmed by code.

## 1. Structure

### WMovieIconManager: the whole right-hand column
- **Size and construction.** 0x3A4 bytes. Ctor **`FUN_007F2A60`** (`__fastcall(this)`).
  - The ctor *always* takes the singleton: it drops the previous one's refcount at `+0x48` (deleting it at
    0) and sets the CP at `0x0104EA80`, pointer **`0x0104EA94`**. The getter is `FUN_007EF840`
    (`A1 94 EA 04 01 C3`).
  - It sets its size: `FUN_0073E4E0(DAT_00E5BD38, screenH * [0x00D17098])`. This is vtable slot 29
    (`+0x74`) SetSize(w,h).
  - It sets the update timer `+0x380` to 10.
  - It registers the event "movie_update" → `LAB_007F2A10`, which uses the *singleton*.
- **Vtables.** Main `0x00D59E2C` (64 slots). It overrides:
  - slot 0: dtor `0x007F2C20` → `FUN_007F2340`
  - slot 10: Update `0x007F3240`
  - slot 11: `0x007F02B0`
  - slot 63: `0x007F0610` (object → icon)

  Second vtable `0x00D59E10` at `+0x50`.
- **Created in the HUD root ctor `FUN_006A39C0`, `0x006A3E7D..0x006A3EE1`:**
  ```
  m = FUN_00ACBC50(0x3A4); FUN_007F2A60(m);
  m->vtbl[0x60/4](2, FUN_0071B2B0(), FUN_0071AFE0() + [0x00D16B78]);   // right edge of the screen
  m->vtbl[0x64/4](2, card, -16.0f);                                   // card = left WHudCardButtons (ESI): top row
  FUN_0071B2B0()->vtbl[0xC/4](m, 2);                                  // AddChild
  ```
  Teardown: `FUN_006A44F0` and `FUN_0071C440` call **`FUN_007F2830`** (fastcall: clears the icons and
  `+0x390/+0x394`).
- **Fields.**

  | Offset | Field |
  |---|---|
  | `+0x348/+0x34C/+0x350` | vector of `CP<CProject>` (0x18 each, pointer at +0x14), refilled on every enumeration |
  | `+0x358/+0x35C/+0x360` | vector of `CP<WMovieIcon>` (0x18 each, icon at +0x14) |
  | `+0x364` | project count |
  | `+0x368` | CP (vtable `0x00D59E00`) |
  | `+0x380` | update timer (`FUN_0043B490`) |
  | **`+0x390`** | count of full-size icons |
  | **`+0x394`** | count of shrunk icons |
  | `+0x398` | spacing |
  | `+0x39C` | flag |
  | **byte `+0x3A0`** | freeze: when set, enumeration is skipped |

- **No categories, pages, headers or switcher.** It is one vertical list. When the icons don't fit the
  manager's fixed height (`vtbl+0x14` height compared with content height `FUN_007EF850`), the extra
  icons are drawn shrunk:
  - icon height `DAT_00E5BD34`; shrunk icons are `× [0x00D17200]`
  - `FUN_007F0C80` decides which icons are shrunk and `FUN_007F0470` changes them
  - `FUN_007E7910(icon)` = "is full size"

### Update and enumeration
- **Update** (slot 10, `FUN_007F3240`, fastcall), unless `FUN_00423320() == 4`:
  - on a timer tick:
    - `FUN_007F2FD0` (enumerate)
    - `FUN_007F3100` (grows a shrunk icon on hover)
    - `FUN_007F1080` (layout)
    - `FUN_007F06F0` (each icon's league check `FUN_007EC8C0`) when `+0x39C == 0`
  - between ticks, when the full-size count has changed: `FUN_007F2FD0`
  - always ends with `FUN_007F1080`, then the base `FUN_00740BA0`
- **Enumerate `FUN_007F2FD0`** (`__fastcall(mgr)`, manager in EDI inside):
  1. Returns at once when byte `+0x3A0` is set.
  2. `+0x364 = 0`; `FUN_007F1780` (clears the CP vector).
  3. Walks the **global `InList<CProject>` `0x0104D680`** (first node `[0x0104D688]`, sentinel
     `0x0104D694`; node {prev, next, item}).
  4. Keeps a project `p` when:
     ```
     ( FUN_005B3C80(p) || (phase(p)->vtbl[0x24/4]() == 7 && *(p+0x210) != 0) )  &&  !FUN_005B3C00(p)
     ```
     - `phase(p)` = `FUN_005B22A0(p)` = `*(p+0x228)`; phase vtbl+0x24 = stage: 2 writing, 3 script done,
       4 casting/pre-production, 5 shooting, 6 shot, 7 released. Stage names inferred from the
       texture choices.
     - `FUN_005B3C80(p)` (fastcall, call site `0x007F3039`) = `stage != 7 && !FUN_005B3C00(p)`.
     - **`FUN_005B3C00(p)`** (fastcall bool, **call site `0x007F305B`** `E8 rel32`) = true when
       `!phase->vtbl[0x30/4]()` and `p` is in the player studio's list at `studio+0xC8` (sentinel
       `+0xD4`; the BackCatalogue InList at `+0xC0`). That is: "filed away". It has 9 callers in total.
     - `p+0x210` = the CProjectObject on the lot.
  5. Each project kept: `+++0x364`, then push `CP<CProject>` (vtable `0x00D18C3C`) with `FUN_007F2C40`.
  6. `FUN_007F0C80` (shrink logic), then **`FUN_007F2D60`** if `DAT_0104D8E8 == 0`.
- **`FUN_007F2D60`** (fastcall): for each listed project with no icon yet (`FUN_007F0320`), creates one:
  - `new 0x574` → `FUN_007EE310(project, index)`
  - `+++0x390`
  - pushes `CP<WMovieIcon>` (vtable `0x00D59E00`) with `FUN_007F2CD0`
  - positions it with `FUN_007F0AD0(i, 0)`
- **Layout `FUN_007F0AD0`** (`__thiscall(mgr, int i, char)`):
  - icon 0: `vtbl+0x64(1, mgr, -10.0f)`, `vtbl+0x5C(1, mgr, 0)`
  - icon i: `vtbl+0x64(2, prevIcon, spacing(+0x398) + [0x00D16B78])`, i.e. each icon hangs below the
    one before
  - size: `vtbl+0x74(DAT_00E5BD38, DAT_00E5BD34 [× 0x00D17200 when shrunk])`
- **Other functions.**

  | Function | Convention | What it does |
  |---|---|---|
  | `FUN_007F2570` | `__thiscall(mgr, CProject*)` | remove project; callers `FUN_005B2510`, `FUN_0060A580`, `FUN_0091B300`, all through the singleton getter |
  | `FUN_007F2740` | `__thiscall(mgr, icon)` | remove icon |
  | `FUN_007F05B0` | `(mgr, CProject*)` | project → icon |

### WMovieIcon: one project card
- **Size and construction.** 0x574 bytes. Ctor **`FUN_007EE310`** (`__thiscall(this, CProject*, int index)`).
  - It runs the base `FUN_007E8E70(index)` and sets vtables **`0x00D59BFC`** (77 slots) and `0x00D59BE4`
    at `+0x50`.
  - CP<CProject> at `+0x4B4`, **pointer `+0x4C8`** (getter `FUN_007EC410`).
  - It builds its children with `FUN_007ED6D0`. The dtor body is `FUN_007ED050`.
- **Children** (built by `FUN_007ED6D0`):
  - the 3D card model "filmpanel2" (scene nodes found with `FUN_008819D0`/`FUN_0088A2B0` and kept at
    `+0x428..+0x440`)
  - a 0x480 widget `FUN_007AC3F0` at CP `+0x4FC` / pointer **`+0x510`**: the card view (guess)
  - a 900-byte `FUN_00737730` at `+0x3A8`: guess, the star bar ("showstars")
  - a 0x344 WWindow at `+0x3C0`
  - a 0xF8 `FUN_005EB780` at `+0x48C` with "number_dummy": guess, the chart-position number
  - a 0x7C `FUN_005EFB20` at `+0x4A4`
  - the per-stage card picture widget at **`+0x4E0`**
  - The red skull is probably the Stunts-addon "stunt_indicator" (the string is used in the icon
    update at `0x007EEDEF..0x007EEE61`). Guess.
- **Card picture: `FUN_007EE480`** (`__fastcall(icon)`). It switches on the stage and calls
  `(*(icon+0x4E0))->vtbl[0x100/4](&String tex, Rect)` (the same SetTexture shape as the jobs-card icon):

  | Stage | Texture |
  |---|---|
  | 2 | writing pictures (two variants by `FUN_004BDC40`) |
  | 3 | `ui/filmpro_3writen.dds` and others, by `phase->vtbl+0x1C()->+0x38` |
  | 4 | `ui/filmpro_caststart`, `ui/filmpro_casting.dds`, `ui/filmpro_castdone.dds` |
  | 5 | shooting |
  | 6 | `ui/filmpro_5filmed.dds` |
  | 7 | released |
  | default | `ui/filmTC.dds` |

  Afterwards it stores the stage at `+0x550` and the sub-state at `+0x54C`, and calls
  **`FUN_007EC9B0(icon, stage)`**, which posts the `HUD_PROJECT_*` notifications on a change.
- **Update** (slot 10, **`0x007EEC30`**, thiscall icon; Ghidra hasn't made it a function):
  - refreshes the portrait (`FUN_007E7EC0`/`FUN_007E7B00` on `p+0x210`)
  - **checks the filter again** (`0x007EEC8F` call 5B3C80, stage == 7, **`0x007EECAB` call 5B3C00**,
    `+0x210`); when it fails: **`0x007EECC5` `CALL FUN_007EF840`** (singleton) → `FUN_007F2740(icon)`
  - when `+0x4C8 == 0`: `0x007EF2A0` → **`0x007EF2A1` `CALL FUN_007EF840`** → `FUN_007F2740`
- **Clicks** (slot numbers in the WHudIcon vtable, offset = slot × 4):
  - 65 `0x007EC5C0`: IsFor(obj): is the object `p+0x210`?
  - 68 `0x007EC600`: `(p+0x210)->vtbl[0xB8/4]()`, select/focus (guess)
  - 74 `0x007EC4F0`: **pick up** (`FUN_0053C900(p+0x210)`), the drag
  - **75 `0x007EC510`: sends message 0x4B2 for `p+0x210` through the messenger `[0x0104917C]`
    (`FUN_00470A70`). Probably the "i" button / open details (guess, by analogy with the person icons'
    slot 75).**
  - 76 `0x007EC540`: `FUN_00539330(3)` / `FUN_009021D0`. Guess: camera to the object.
  - 70/71/72: no-ops or base.

## 2. Excluding the share projects from the movie list

The filter is evaluated in two places, enumeration and icon update, and both go through
`FUN_005B3C00` by direct `CALL rel32`. Redirect only those two call sites:

- **C1** `0x007F305B` (in `FUN_007F2FD0`) and **C2** `0x007EECAB` (in the icon update): change the
  `E8 rel32` target to `bool __fastcall ModFiled(CProject* p)`:
  ```
  bool r = FUN_005B3C00(p);
  if (!r && g_ctx) r = (g_ctx == g_shareMgr) ? !IsShare(p) : IsShare(p);
  return r;
  ```
- Set `g_ctx` with two MinHooks:
  - `FUN_007F2FD0` (fastcall mgr): `g_ctx = mgr` around the original.
  - `0x007EEC30` (thiscall icon): `g_ctx = OwnerOf(icon)` around the original. `OwnerOf` scans
    `g_shareMgr+0x358..+0x35C` (stride 0x18, icon at +0x14); otherwise it returns the real manager.
- The other 7 callers of `FUN_005B3C00` are unaffected (`g_ctx == 0`).

Applied to the movie manager alone, this removes the shares from the movie list cleanly. The count,
spacing and shrinking all follow, because they are computed from the vector.

## 3. A separate "shares" category

### Second manager instance
Post-hook `FUN_006A39C0`:
1. `real = *(void**)0x0104EA94; if (real) ++*(int*)(real+0x48);`. The ctor will decrement it again.
2. `m = FUN_00ACBC50(0x3A4); FUN_007F2A60(m);`
3. Put the singleton back: `*(void**)0x0104EA94 = real;`. Ideally do it the way the ctor does it:
   call `(*(void***)0x0104EA80)[1]` with ECX = `0x0104EA80`, write, then `[0]` (guess).
4. Keep our own reference: `++*(int*)(m+0x48)`.
5. Anchor `m` (see the layout options below) and `FUN_0071B2B0()->vtbl[0xC/4](m, 2)`.
6. Teardown: pre-hook `FUN_006A44F0` and `FUN_0071C440`: `FUN_007F2830(m)`, release the reference.

Side effect: the "movie_update" event is registered a second time, and its handler acts on the
singleton only. Probably harmless (an extra re-sort); guess.

### Making the instance-specific code use our instance
| Site | Old | New | Why |
|---|---|---|---|
| **P1** `0x007F04B6` | `A1 94 EA 04 01` (MOV EAX,[singleton]) | `8B C3 90 90 90` (MOV EAX,EBX; EBX = this manager) | Identical for the real manager; correct for ours |
| **P2** `0x007EECC5` (CALL getter) | `CALL FUN_007EF840` | `CALL ModMovieMgr` | Remove the icon from its own manager |
| **P3** `0x007EF2A1` (CALL getter) | `CALL FUN_007EF840` | `CALL ModMovieMgr` | Same |

`ModMovieMgr` is cdecl with no arguments (the pushed icon stays on the stack for `FUN_007F2740`) and
returns `g_ctx ? g_ctx : *(void**)0x0104EA94`.

Everything else in the update, enumerate, layout and icon-creation chain uses `this` (checked:
`FUN_007F3240`, `FUN_007F2FD0`, `FUN_007F2D60`, `FUN_007F0C80`, `FUN_007F1080`, `FUN_007F0AD0`,
`FUN_007F3100`, `FUN_007F2740`, `FUN_007F0470` after P1). The only other direct reads of `0x0104EA94` are
in the ctor and `LAB_007F2A10`.

### Layout options
Each manager is a fixed-size window: the full column height, set at construction and through
`vtbl+0x74` SetSize(w,h).
- **(a)** Shrink the movie manager and stack ours below it:
  ```
  movie->vtbl[0x74/4](DAT_00E5BD38, H - hs);
  m->vtbl[0x60/4](2, FUN_0071B2B0(), sameX);
  m->vtbl[0x64/4](2, movie, gap);
  m->vtbl[0x74/4](DAT_00E5BD38, hs);
  ```
  - `hs` ≈ n × `DAT_00E5BD34` + margins
  - when ours is empty, give it a height of 0 and the movie list its full height
- **(b)** Put ours on top (anchored to the card the way movies are), and re-anchor movies below ours with
  `movie->vtbl[0x64/4](2, m, -16.0f)`.
- The anchor meanings (edge code 1/2, sign of the offset) are inferred from the existing calls (a guess).
  Test them, and redo the sizes in the resolution post-hooks `FUN_007E5530/FUN_007E5710`.

### Header
- There is no header widget to reuse. The jobs card's image widget is class `FUN_0069D820`
  (0x360 bytes, vtable `0x00D39AE4`, built with a by-value String texture).
- To use it: allocate 0x360, construct with `"ui/shares_hdr.dds"`, AddChild, and anchor it above `m`.
  Its ctor signature isn't verified, so disassemble `FUN_007E6E30` around its `operator_new(0x360)` call
  before relying on it. Optional.

### Our own picture
- Post-hook **`FUN_007EE480`** (fastcall icon). When `IsShare(*(icon+0x4C8))`, call
  `(*(void**)(icon+0x4E0))->vtbl[0x100/4](&String("ui/shares_bag.dds"), Rect{0,0,1,1})`, one texture per
  project.
- Short names (19 characters or fewer) fit the String's inline buffer. Ship the textures under
  `data/textures/ui/`.
- If `+0x4E0` turns out to be only a decal on the 3D "filmpanel2" card, the card frame itself still
  shows (guess). Hiding it (`+0x510`) and the star bar (`+0x3A8`) needs a widget-visibility call that
  hasn't been identified. One candidate is the flags at `+0x114` (bit 1, cleared in `FUN_007ED6D0`); test
  it.

### Clicks and the "i" button
- Pick-up (slot 74) and select (slot 68) work for share objects as they are.
- Suppress or redirect **slot 75 `0x007EC510`** with a MinHook (thiscall icon): when the project is a
  share, open the mod's own panel and don't call the original.
  - Do the same for slot 76 `0x007EC540` if it turns out to open a project screen.
  - Hooking the function, not a vtable copy, is fine: movie icons are its only users.

## 4. Other places the shares would still appear or trigger

- **Notifications.** `FUN_007EC9B0(icon, stage)` (`HUD_PROJECT_WRITING_COMPLETE`, `CASTING_*`, `SHOOTING_*`)
  runs from `FUN_007EE480` for every icon, ours included. Hook it and skip shares.
  - Also skip `FUN_007EC650` (`HUD_MOVIERATINGUP/DOWN`, star-bar changes) and `FUN_007EC8C0` (league
    position up/down; it goes through `p->vtbl+0x20` and returns early when that is null) for share icons.
- **Removing a project from the HUD.** `FUN_007F2570` is called through the singleton by
  `FUN_005B2510`, `FUN_0060A580` and `FUN_0091B300` (dropping a canister in a room). With the
  shares excluded, the lookup finds nothing and does nothing. Our instance removes stale icons itself:
  when `+0x4C8` goes null or the filter fails, the icon update removes it through P2/P3.
- **Object → icon lookups.** `FUN_007F05B0` and `FUN_007F0610` (slot 63), used by `0x005AD346`,
  `0x005CD06F/95` and `FUN_007B4900`, return 0 for shares in the real manager. Callers already cope
  with projects that have no icon (rival films).
- **Outside the HUD.** The global project list `0x0104D688` is read by 23 functions (`re/prj8.txt`). Among
  them:
  - studio counts (`FUN_005B3CB0/5B3D10/5B4870/5B5B20/5B83F0`)
  - the Movies screens (`FUN_008661C0`, `FUN_00868310..`, `FUN_0086C5B0..`)
  - `FUN_0057A7A0`, `FUN_00585180`, `FUN_00590060/D0`, `FUN_0080E5B0`, `FUN_009362E0`

  None of these were inspected. They may count or list the share CProjects too (they already do today).
