#pragma once
#include "Renderer.h"
#include "GameState.h"
#include <string>

namespace prototype {
struct View {
    double cameraX = 0, cameraY = 0, time = 0;
    float attack = 0, deathFlash = 0;
    bool dialogueOpen = false, journalOpen = false, helpOpen = false;
    std::wstring dialogue;
};
void Render(Renderer& renderer, const echo::GameState& state, const View& view);
bool IsBlocked(double x, double y, int realm);
}
