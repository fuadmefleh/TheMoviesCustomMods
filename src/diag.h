#pragma once
// Logs data files the game asks for but can't find ("file: MISSING ..." in MoviesMod.log).
// [MoviesMod] LogMissingFiles = 0 turns it off; WatchFiles = text also logs files found
// whose path contains that text.

namespace diag {

void Install();
// Logs faults in the game's code with a stack, and writes MoviesMod_crash.dmp on a fatal one.
// [MoviesMod] LogCrashes = 0 turns it off.
void InstallCrashLog();
// Logs where the game is stuck when no frame has come for [MoviesMod] HangWatchSeconds (30;
// 0 = off), and writes MoviesMod_hang.dmp. Call after InstallCrashLog.
void InstallHangWatch();

} // namespace diag
