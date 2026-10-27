#pragma once
// New employee types: careers past the game's 17 (careers.cpp), and the accountants' page on
// the HUD's people card.

#include <cstdint>

namespace careers {

constexpr int kGameCareers = 17;   // the game's own: 0 unemployed ... 16 stuntman
constexpr int kAccountant  = 17;
constexpr int kCareers     = 18;

// Moves the career tables and adds the HUD page. Call once from the game thread after
// MH_Initialize, before anything hires an accountant.
void Install();

const char* Name(int career);   // "janitor", "accountant"...

// The HUD being built (with its people card) and torn down, for other modules that hang
// things on it (projectlist.cpp). careers.cpp owns those hooks; MinHook allows one per function.
using HudBuilt = void (*)(uintptr_t card);
using HudCleared = void (*)();
void OnHud(HudBuilt built, HudCleared cleared);

} // namespace careers
