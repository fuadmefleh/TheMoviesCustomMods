#pragma once
// Forced into every file of the simulator build (cl /FI). The TV code throttles some work
// with GetTickCount; in the simulator a game day passes in microseconds, so time comes
// from the simulated clock instead.

#define NOMINMAX
#include <windows.h>

DWORD SimTickCount();
#define GetTickCount() SimTickCount()

// ImGui misuse (a missing End(), a bad ID stack...) stops the run with a message.
void SimAssert(const char* expr, const char* file, int line);
#define IM_ASSERT(e) ((e) ? (void)0 : SimAssert(#e, __FILE__, __LINE__))
