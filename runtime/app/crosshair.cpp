// The crosshair (Settings::crosshair_*): the game's own is a HUD sprite it hides while aiming; with a custom style on
// (not Game's) the game's sprite is hidden (src/game/aim.cpp) and this one drawn at the middle of the screen, in the
// ImGui frame, so every renderer shows it. Style 0 is the game's own.
#include <algorithm>
#include <cmath>

#include <imgui.h>

#include "app/app.h"

namespace app {
void DrawCrosshair() {
    const Settings& s = Get();
    if (s.crosshair_style == 0 || s.crosshair_style > 4) return;
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    // sizes are for a 1080-line screen, scaled to this one
    const float k = vp->Size.y / 1080.0f;
    const ImVec2 c(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.5f);
    const float len = s.crosshair_size * k, gap = s.crosshair_gap * k, thick = std::max(1.0f, s.crosshair_thickness * k);
    const ImU32 col = s.crosshair_color;
    const ImU32 edge = IM_COL32(0, 0, 0, 160);  // a thin dark edge keeps it readable on snow and light walls
    auto line = [&](ImVec2 a, ImVec2 b) {
        dl->AddLine(ImVec2(a.x, a.y), ImVec2(b.x, b.y), edge, thick + 2 * k);
        dl->AddLine(a, b, col, thick);
    };
    const int style = int(s.crosshair_style);
    if (style == 1 || style == 4) {  // a dot
        const float r = std::max(1.5f, thick);
        dl->AddCircleFilled(c, r + 1, edge, 16);
        dl->AddCircleFilled(c, r, col, 16);
    }
    if (style == 2 || style == 4) {  // four strokes with a gap
        line(ImVec2(c.x - gap - len, c.y), ImVec2(c.x - gap, c.y));
        line(ImVec2(c.x + gap, c.y), ImVec2(c.x + gap + len, c.y));
        line(ImVec2(c.x, c.y - gap - len), ImVec2(c.x, c.y - gap));
        line(ImVec2(c.x, c.y + gap), ImVec2(c.x, c.y + gap + len));
    }
    if (style == 3) {  // a ring
        dl->AddCircle(c, gap + len, edge, 32, thick + 2 * k);
        dl->AddCircle(c, gap + len, col, 32, thick);
    }
}
}  // namespace app
