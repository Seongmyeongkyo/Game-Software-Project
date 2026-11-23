#pragma once
#include "Renderer.h"
#include "GameState.h"
#include "Dialogue.h"
#include <string>

namespace prototype
{
    struct View
    {
        double cameraX = 0;
        double cameraY = 0;
        double time = 0;
        float attack = 0;
        float deathFlash = 0;
        bool dialogueOpen = false;
        bool journalOpen = false;
        bool helpOpen = false;
        bool guarding = false;
        narrative::DialogueState dialogue;
    };

    void Render(Renderer& renderer, const echo::GameState& state, const View& view);
    bool IsBlocked(double x, double y, int realm);
} // namespace prototype
