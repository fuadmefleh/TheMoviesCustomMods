# Roadmap

The plan for **The Movies: Television & Streaming**, a fan expansion built as a code mod.

The full design covers:
- TV eras from 1920 to 2030.
- Pilots, seasons, networks and scheduling.
- Syndication, cable and streaming.
- Twelve new buildings and twelve new professions.
- The *Season Finale premiere* newspaper as its defining moment.

## Approach

- **New screens are mod windows (Dear ImGui) drawn over the game**, styled like the game's own interface. Adding tabs to the game's Flash UI would take months of reverse engineering before anything was playable.
- **TV content builds on the films you make.** Your released films become pilots. Episodes are produced by your TV buildings at a cost, with quality led by the pilot. Making every episode by hand would be impossible at the game's pace (a film takes a game year).
- **Phases ship playable features**, each tested in game before the next starts.

## Phases

| Phase | Contents | Status |
|---|---|---|
| **0. Foundations** | Read and change game state; in-game windows; mod data in saves | **Done** |
| **1. Broadcast Era** | TV Production Office and Multi-Camera Soundstage; formats by era; pilots and network verdicts; weekly ratings and income; simple renewals; management levels; news pop-ups | **Done, verified in game** |
| **2. Network Owner** | Prime-time scheduling grid, time slots and lead-ins, rival networks with their own shows, a Programming Director, growing from local to national | **Built, awaiting first test** |
| **3. Long-running shows** | Deeper renewals, cast pay demands, departures and recasting, story arcs, cliffhangers, spinoffs, **the Season Finale premiere and newspaper** | **Built, awaiting first test** |
| **4. Library and syndication** | Rerun rights, contract terms (fees, exclusivity, ownership), residuals, Media Archive, cable channels | **Built, awaiting first test** |
| **5. Streaming Wars** | Extend the game to 2030, a streaming service with subscribers and churn, originals and licensing, release models, recommendations, outages, going international | **Built, awaiting first test** |
| **6. Polish** | Dynamic events, TV awards, Media Empire Value, six scenarios, research trees, showrunners and writers' rooms | **Partly built**: awards, events, Media Empire Value, showrunners and writers' rooms |

### Phase 0: Foundations (done)

- [x] Large Address Aware patch
- [x] Proxy DLL, logging and game build check
- [x] Tuning hook, overrides and catalogue, plus the *less micromanagement* preset
- [x] Direct3D 9 overlay with DirectInput mouse support and the game-style theme
- [x] Game state: money (read and ledger transactions), date, studio, films (quality, box office, dates)
- [x] Loose building files load
- [x] Save sidecars (`.tvs`): write on save, read on load, reset on new game, delete with the save

### Phase 1: Broadcast Era (done)

- [x] Building detection through the global facility list and RTTI
- [x] Text hook for new names
- [x] `facility_tv_production_office`, `facility_tv_soundstage` building files (names must start with `facility`)
- [x] 10 formats with era availability, costs, fees and appeal over time
- [x] 3 networks (LBN, STV, MRN) with standards, pay, reach and tastes
- [x] Pitch, verdict (full, limited, rework or pass), re-pitch
- [x] Weekly airings: episode quality, rating, homes reached, income minus cost through the game's ledger
- [x] Season end and renewal or cancellation
- [x] Casual, Studio Boss and Media Mogul levels; budgets; license vs advertising-share deals
- [x] Production capacity and shows on hold
- [x] HUD news pop-ups
- [x] In-game test: buildings placed and counted, verdicts, limited order, weekly airings, premiere, save
- [x] Re-pitching the same film to the same network gives the same score
- [ ] Tune money against film income using test results

### Phase 2: Network Owner (built)

- [x] Prime time: 7 nights × 3 slots (8, 9, 10 PM) on every network
- [x] Rival networks fill their schedules with generated shows (format by taste, era and hour; quality by network)
- [x] Audience model: shares split by each offering's pull (quality, format appeal, freshness, premieres and finales, brand, reach), lead-in from the previous hour; rating = 62% of homes × share
- [x] Shows sold to rivals get a slot (the network drops its weakest show); renewals judged by share
- [x] Fall season every mid-September: rivals cancel flops, premiere new shows
- [x] Your own network: Network HQ to found it; Local → Regional → National (Broadcast Control Room, coast-to-coast from 1950); ad revenue, weekly station costs
- [x] Greenlight shows straight onto your network; manual schedule grid; "Fill empty slots"; Programming Director (automatic on Casual)
- [x] Ratings tab: weekly top 15 and network shares
- [ ] **In-game test** and balance

### Phase 3: Long-running shows (built)

- [x] Season plans: standalone episodes, a season-long story arc (builds to the finale, slows wear; best for dramas, soaps, science fiction) or an event season (better, bigger premieres and finales, 35% dearer)
- [x] Finales: closure or cliffhanger (bigger finale, networks keener to renew, the next premiere becomes an event; fans furious if it's cancelled)
- [x] "Finale next week" news before every finale
- [x] Star contracts: stars of hits ask for raises after each season (pay or refuse; refused stars may walk); long-serving stars may leave for films; recast by dropping a new star on Cast; viewers notice an empty role
- [x] Renewal talks with rival networks: ask for 20% more per episode (they may cut the order instead)
- [x] Spinoffs from hits: no pilot film needed, starts from the parent's quality, the parent's network is keenest, up to two per show
- [x] **The Season Finale newspaper**: a front page for every finale of your shows (headline by how it did, the cliffhanger, the week's ranking, renewal news, the critics, last week's top shows), era-appropriate mastheads, and a Press tab with past editions
- [ ] **In-game test** and balance

### Phase 4: Library and syndication (built)

- [x] Library tab: every show with 22+ episodes (or a finished miniseries); episodes, average rating, how well the format reruns, freshness, rights, earnings
- [x] Sell reruns to local stations (weekly fee, 1-5 years) or exclusively to a network (lump sum up front, by its taste and pay)
- [x] Wear: reruns wear a show out (local 10%/year, exclusive 7%, cable 12%); unsold shows recover in the vault
- [x] Residuals to cast and writers from 1960 (10%, 14% from 1980)
- [x] A third deal at pitch time, *Network-financed*: cost plus 12% whatever the ratings, but the network owns the reruns (and likes that); deal choice opened to Studio Boss
- [x] **Media Archive** building (`facility_tv_archive`): rerun prices +25%, shows rest twice as fast
- [x] Your own cable channel from 1976 (needs a Broadcast Control Room): carried in more cable homes the more popular and fresh its reruns
- [x] Casual: the studio syndicates rested titles by itself
- [x] Network HQ and Broadcast Control Room got their own models: they used to borrow Publicity's and Research's working zones (including Research's Fire zone)
- [ ] **In-game test** and balance

### Phase 5: Streaming Wars (built)

- [x] The game already runs past 2005 (`EndYear` only belongs to the music system); the TV tables run to 2030
- [x] Your streaming service from 2007: monthly price, subscribers who sign up and leave (content, price, reliability, recommendations), rival streamers Flixnet (2007), Primereel (2011) and Kingdom+ (2019) splitting the market
- [x] Library shows on your service, or licensed to rival streamers for big lump sums (a boom in the 2010s)
- [x] Originals: pitch to your service; weekly releases or every episode at once (a burst of sign-ups, then a wave of cancellations)
- [x] **Data Center** building (`facility_tv_datacenter`): server capacity; over it, outages drive subscribers away
- [x] Recommendation engine upgrades; going international in four regions
- [x] Streaming tab with the subscriber curve
- [ ] **In-game test** and balance (needs a game in 2007 or later)

### Phase 6: Polish (partly built)

- [x] **Golden Antenna Awards** every May from 1949: seven categories (comedy, drama, variety, documentary, miniseries, lead performance, writing), your shows against the rivals' by quality and popularity; winners stay fresher, get better fees and bigger premieres; an awards edition of the paper
- [x] **Golden Antenna ceremony** on the game's own awards stage (`src/tv_ceremony.cpp`, `re/awards.md`): the TV categories as the game's awards for one night, the presenters' lines in TV words, only the fitting recordings voiced; replay from the Empire tab
- [x] **Television history**: coast-to-coast (1951), colour (1954), the quiz show scandal (1958: game shows shunned until 1962), "a vast wasteland" (1961: networks stricter for a year), the moon landing (1969), satellites (1976), broadband (2007); **writers' strikes** in 1960, 1988 and 2007 halt scripted production while unscripted shows boom
- [x] **Star scandals**: stand by the star or let them go
- [x] **Media Empire Value** (cash, library, shows, network, cable, streaming, awards) with a yearly history, trophy cabinet and timeline in the new Empire tab
- [x] Showrunners and writers' rooms (the TV Production Office's zones, Phase 2)
- [x] **Research**: twelve technologies from 1950 to 2012 (laugh track, audience research, multi-camera technique, remote vans, syndication sales team, videotape, colour, satellite uplink, stereo, HD, content delivery network, recommendation AI), one project at a time
- [x] **Challenges** (in place of scenarios): six goals across the eras with rewards and a front page
- [x] **Live episodes**: once a season, a big audience at 50% more cost; flawless, disaster or a legendary blunder
- [x] **The TV screen** restyled after the game's Studio Ranking screen (full screen, the game's own banners, rosettes and sunburst from `tools/extract_ui.py`)
- [x] **New building models made in Blender** for all six TV buildings (`tools/make_tv_models.py`): ON AIR signs, a lattice transmitter tower, satellite dishes, rooftop cooling units, a MEDIA ARCHIVE sign
- [ ] **In-game test** and balance

### TV Writers' Office (built)

- [x] `facility_tv_script_office` (donor: the 3-star Script Office, dressed in Blender with a neon TV WRITERS sign): six format desks (sitcom, crime, drama, soap, science fiction, variety); the donor's sit-and-type scenes copied to the new zones
- [x] Drop writers on a desk: they write TV scripts of that format (faster and better with more writers; writers gain experience); weekly pay
- [x] Writers tab with desks, progress and the script shelf; shelf scripts are pilots in Pitch a Pilot (no film needed)
- [x] Scripts and film canisters (`CProjectObject`) dropped on any TV zone become pilots too
- [x] Desks verified in game (first test wrote ~60 scripts: too fast, duplicate titles, no physical script)
- [x] Finished scripts appear on the lot as real script objects (the game's research-pack spawn, `0x005C43C0`), titled with the TV title; drag one onto a TV building to pitch it
- [x] 75 days a script, unique titles, shelf limit of 4 waiting scripts, desks stop during writers' strikes; Pitch and Bin buttons on the shelf
- [x] Second test: writers were only "put down" - the desks were plain rooms with no job, and the TV office was a plain `CFacility`
- [x] **Native writing**: TV desks are now built as the game's own `CScriptRoom`s and `facility/facility_tv_script_office` is registered in the facility-class table as a `CFacilityScriptOffice`, so writers sit down and type, and the office puts the finished script outside. The script is renamed for TV when it starts and goes on the shelf when finished (`NativeTvWriters = 0` turns this off). Offices built before this keep plain rooms: demolish and rebuild them
- [ ] **In-game test** of native writing: desk animation, the TV title on the script, shelving when finished, the hall link

### Balance fixes from the 1955-1971 test
- [x] Rival networks no longer cancel the maximum (7) every fall: slower show fatigue, rookies on a shorter leash than veterans, varying patience, at most a quarter of a lineup
- [x] Shows stuck in "Rework" (Casual has no way to act on it) go to the next network; other levels after a year
- [x] Casual: with nothing in development, the best shelf script is pitched automatically; other levels get a yearly reminder
- [x] Awards no longer say "none of your shows were nominated" when you have no shows on air
- [x] TV screen: tabs wrap to two rows on narrow screens, the page banner no longer covers the tabs, the memory inspector is hidden unless `DevTools = 1`

### Offline simulator (built)
- [x] `tools/sim`: the TV code compiled against stand-ins for the game (`build_sim.bat`, then `tvsim.exe --seed N --level 0..2 [--ui] [--dump]`). A bot player runs 1950-2030 with buildings on a timetable and a film every few months; every tab of the TV screen is drawn monthly (ImGui asserts stop the run), saves are round-tripped every five years, numbers are checked for NaNs, and `--dump` prints veteran shows and a money breakdown by calling function. See [SIMULATOR.md](SIMULATOR.md)
- [x] Fixes found by it: cable channels and streaming services could be launched again (and paid for) every week; shows never ended (freshness floor 0.55 and no ageing: one show ran 38 seasons at a 56% share); rival shows never improved and rarely retired; license fees paid up to 7x the episode cost; reruns earned ten times first-run income (no market saturation); streaming cost $0.25 a subscriber; network filler out-earned real shows
- [x] Balance now (seeds 1-7): ~220 series over 80 years, long-runners take a final bow after 6-15 seasons, money ~$10M by 1960, ~$200M by 2000, ~$1.3B by 2030 (bot player, no film income)

### Buildings from Blender (built)

- [x] `.msh` writer (every game building model rebuilds byte-identical), convex hull generator, `.inf` writer
- [x] `tools/building.py`: export a game building to OBJ, edit in Blender, build `fac_`/`fld_`/`blp_` models, zones, labels, points and the facility file (see [BUILDINGS.md](BUILDINGS.md))
- [x] Tested through Blender 4.2's OBJ import/export; test building `facility_tv_test` (the office with a transmitter mast) installed
- [ ] **In-game test** of a built building

### Later, from Phase 1 feedback
- Building descriptions and menu icons, depending on which text keys the game asks for (`text: missing key`).
- Front-page news through the game's own newspaper, if its event system can be driven from the mod.

### Fairer scoring, news, rivals and the studio manager (built, awaiting first test)
- [x] **Film scoring** (`src/ratings.cpp`, `[Ratings]`): critics weigh quality over box office and reward awards; production can lift a weak script further and hurts less; box office grows with success^1.5 instead of success²; outdated technology and costumes audiences have seen before lower a film's success; studio rankings lean on reputation and awards
- [x] **Script Office: Ultimate** revived (`facility_script_5star`, research pack `tech_10`, 1985-1995); the 4-star script cap is lifted once it's unlocked
- [x] **Luxury trailers 7-9** (and trailer 6 made better than 5), each unlocked by a research pack
- [x] **News on the game's own timeline** (`src/news.cpp`): TV finales, awards and challenge front pages appear as world-news globes; a second text hook (`0x009B5F90`) supplies their text
- [x] **Rival studios' films** (`CProjectAI`, `game::RivalFilms()`): a Box Office tab ranks every studio's films, and rival networks sometimes adapt a rival's hit into a series
- [x] **Own models** for the Ultimate office (gold ULTIMATE SCRIPTS sign, searchlights) and trailers 6-9 (rooftop dish, then a star sign, a hot tub, a helipad), with tier badges on their menu pictures (`LUXURY` in `tools/make_tv_models.py`)
- [x] **Whole-number tuning overrides**: the second getter (`0x00558750`) is hooked too, so values such as `maxlitterobjects` or `revenue_base` can be set from MoviesMod.ini
- [x] **Studio manager** (`src/manager.cpp`, `[Manager]`): rest breaks for idle stars, handymen sent to the most run-down building, plus chore-reducing tuning
- [x] **Native interface** (`src/nativeui.cpp`, `src/tv_native.cpp`): a HUD button opens the Television screen as a game dialog with icon tabs and rich-text pages; the game's yes/no dialog is available to the mod
- [x] **Movie Maker** (`src/moviemaker.cpp`, `[MovieMaker]`): 1280x720 Best export (exe width patch + generated WMV profiles), 30 fps exports, every preset renders at screen size, 12 more title colours, optional instant shooting; `CharacterTextureSize` ready for a high-resolution costume/face texture pack (`tools/upscale_textures.py`)
- [x] **Pilots**: a TV script is shot as a pilot episode on a Multi-Camera Soundstage (drop the script on a TV building, cast a star by dropping them on the soundstage, a director in its control room); only finished pilots are pitched and the pilot's star becomes the lead. Films are no longer pilots unless `[tv] FilmPilots = 1`
- [x] **TV decisions as native yes/no dialogs**: scandals, pay demands and renewals
- [x] **High-resolution people**: 4,956 costume, face, hair, makeup and accessory textures upscaled 2x (`tools/make_hires_pack.py`, installed as links by `tools/hires_pack_install.py`) with `CharacterTextureSize = 1024`
- [ ] **In-game test** of all of the above

## Toon Studio: an animation expansion (planned)

A second production line next to live action, sized like an expansion pack. Cartoons are made on the lot, the way everything else is: scripts, voice stars and animators are dragged onto buildings.

**The pipeline**
1. **Story Department**: writers at native script desks (the `CScriptRoom` route of the TV Writers' Office) write animation scripts, which come out on the lot as script objects.
2. **Voice Booths**: drop the script on a booth, then drop stars on it to voice the roles. Voice work takes days, so stars can voice cartoons between films.
3. **Animators' Desks**: the script goes onto the desks and the animators work through its scenes. More animators work faster, better ones work better. This replaces shooting on sets: the film is an ordinary `CProject` whose scenes are finished by the desks through the game's own path (`0x004EB900`, as the second unit does).
4. **Ink & Paint**: the slow finishing step, until the **CGI Render Farm** (1995) replaces it.
5. The finished canister goes through the game's own release.

**It looks like a cartoon**: animated films are drawn with a cel-shading pass (flat colours, ink outlines, paper grain in early eras) wherever they play: premiere, Movie Maker, cinema. After 1995 the look changes to a clean CGI style.

**The eras**
- 1928-37, shorts: 7-minute cartoons shown before your features, a box-office bump, and the birth of your studio mascot.
- 1937, the gamble: the first animated feature. It takes years and huge sums.
- 1950s-80s, the slump: TV cartoons (feeding the TV system), cheap "limited animation", quality against cost.
- 1989-99, the renaissance: musicals and Broadway-style songs.
- 1995 onwards, CGI: the Render Farm, a new generation of animators, hand-drawn animation dying unless you keep it alive.

**Different from live action**
- Animated films don't age: re-release them from the vault every 7 years, and they earn again each time.
- A studio mascot who grows from shorts to features to merchandise and TV, and can become an icon or go stale.
- Voice fame as its own track: a faded actor can come back through voice work.

**Spikes first**
- [ ] **Spike 2, a film with no shoot**: can a `CProject` have all its scenes finished through `0x004EB900` without being shot on sets, then release and play back cleanly? Everything else depends on this.
- [ ] **Spike 1, the cel-shading pass**: find where film playback renders and put a shader there. This decides whether the pack looks the part.
  - [x] The look (`src/toon.cpp`): a two-pass post-process in Present. A Kuwahara filter flattens texture into paint, then ink is traced on the flattened image and brightness pulled into soft cel bands; the hand-drawn style adds paper grain and boiling lines. Tuned on screenshots with a Python copy of the shader. Ctrl+F9 previews it in game, Ctrl+Shift+F9 switches hand-drawn/CGI
  - [ ] **In-game test** of the preview (look, speed, alt-tab/resolution change)
  - [ ] When a film is playing: game-mode changes are logged (`toon: game mode`) to find the playback mode, then which film it is
  - [ ] Keep the game's interface out of the pass (draw before the HUD, or only inside the film's viewport)
- [ ] Animators as a role for existing staff (writers or crew), since new walking character types aren't feasible.

## Not feasible as a mod (or deferred)

- **New animations** (little background scenes). These need the game's `.flm` animation format. New building models are now possible (see BUILDINGS.md); new film sets would use the same `.msh` format.
- **New walking character types** for the new professions. They will be roles in mod windows instead.
- **Native Movie Maker features** (multi-camera editing, laugh tracks).
- **Adjacent lots.** These need more reverse engineering; to be assessed later.
- **Episodes generated semi- or fully automatically from the writers' room.** These come after Phase 3, once the episode pipeline exists.

## Other ideas found along the way

- A building alias hook, so new buildings can borrow an existing building's behaviour.

## Known issues

- The player studio's **BackCatalogue** list was empty while it had one released film. Released films are read from the projects list instead (stage 7).
- **All projects in the world** showed only the player's film: rival films are a separate class, `CProjectAI`, in their own list (`0x0104D628`); now read by `game::RivalFilms()`. The player's BackCatalogue only holds *archived* films.
- `persist` applies a load twice in a row. This is harmless, because every section's load is idempotent.
- One early test session's log ended without `MoviesMod unloading`, and no crash was recorded. Quit from the game's menu when testing.
