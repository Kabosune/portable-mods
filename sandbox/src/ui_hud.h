#pragma once

#include "sandbox_app.h"

namespace sb {

// Draw always-visible HUD (Quake speedometer, free-aim sub-pixel reticle, debug ellipse, hit marker)
// and the collapsible multi-tab Dear ImGui Tuning Panel when unlocked.
void draw_sandbox_ui(SandboxApp& app, int win_w, int win_h);

} // namespace sb
