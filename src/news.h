#pragma once
// Stories on the game's own timeline (the newspaper bar at the bottom of the screen).

#include <string>

namespace news {

// Queues a world-news story: a globe icon on the timeline that opens to headline,
// subtitle and body. Safe from any game-thread code; posted on the next frame.
void Post(const std::string& headline, const std::string& subtitle, const std::string& body);

void Install();   // after overlay::Install and persist::Install

} // namespace news
