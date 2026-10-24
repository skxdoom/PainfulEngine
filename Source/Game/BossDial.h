#pragma once
#include <cstdint>

namespace painful {

// HUD::DrawBossHealth's wipe (Engine.dll 0x1008e450): the share of the boss
// dial that is lost turns transparent, clockwise from twelve o'clock, as lines
// from the centre to the border. rgba is w*h texels, top row first. A wiped
// texel gets alpha 1, the value the original writes.
// Docs/Reference/Hud.md, "The boss dial".
void BossDialWipe(uint8_t* rgba, int w, int h, int percent);

} // namespace painful
