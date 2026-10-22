#pragma once
// Logs data files the game asks for but can't find ("file: MISSING ..." in MoviesMod.log).
// [MoviesMod] LogMissingFiles = 0 turns it off; WatchFiles = text also logs files found
// whose path contains that text.

namespace diag {

void Install();

} // namespace diag
