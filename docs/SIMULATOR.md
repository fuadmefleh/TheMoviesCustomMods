# TV simulator

`tools/sim` builds the Television & Streaming code (`src/tv*.cpp`) into a console program,
`tvsim.exe`, with stand-ins for everything that normally comes from the game. It plays
80 years in a few seconds, so balance changes and new features can be checked without a
test session.

## Build and run

```
tools\sim\build_sim.bat
tools\sim\tvsim.exe --seed 1 --level 0
```

| Option | Meaning |
| --- | --- |
| `--seed N` | random seed for films and the bot's choices |
| `--level 0/1/2` | Casual, Studio Boss, Media Mogul |
| `--from Y --to Y` | years to simulate (default 1950-2030) |
| `--ui` | draw every tab of the TV screen once a month (ImGui asserts end the run) |
| `--dump` | every ten years: long-running shows and their competition, and where the money came from, by calling function |
| `--echo` | print the mod's log to the console |
| `--log file` | where the mod's log goes (default `tvsim.log`) |

Each year prints one row: money, Media Empire Value, series by status, the best rating so
far, the rivals' lineup sizes and streaming subscribers. The exit code is non-zero if any
anomaly was found (non-finite numbers, impossible states, a save section that changes when
saved, loaded and saved again).

## How it works

- `sim_stubs.cpp` implements `game::`, `overlay::`, `persist::`, `rooms::` and `mod::` with a
  fake studio: money, a building list, released films, script objects.
- `sim_shim.h` is forced into every file: `GetTickCount` follows the simulated clock, and
  `IM_ASSERT` reports and stops.
- `sim_main.cpp` advances the date a day at a time, adds buildings on a timetable (Production
  Office, Soundstage and Writers' Office in 1950, Network HQ 1956, Control Room 1960, Archive
  1966, ...), releases a film every 90-220 days, runs the TV code's frame callback, and once a
  week lets a bot act: pitch the best available pilot when there's room, renew (not on
  Casual, which renews by itself), settle scandals, sell reruns, found and grow a network,
  launch cable and streaming.

The bot is a reasonable player, not a perfect one; it never schedules its own shows on its
own network or researches technology.
