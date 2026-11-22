#pragma once

#include "Renderer.h"
#include "Combat.h"

namespace combat_visuals
{
    void DrawMonster(Renderer& renderer,
                     const echo::Monster& monster,
                     Vec2 foot,
                     double time,
                     bool targeted);

    void DrawProjectile(Renderer& renderer,
                        const echo::Projectile& projectile,
                        const std::vector<Vec2>& projectedTrail,
                        double time);

    void DrawDrop(Renderer& renderer, const echo::LootDrop& drop, Vec2 foot, double time);

    void DrawTelegraph(Renderer& renderer, const echo::Monster& monster, Vec2 foot);

    void DrawHud(Renderer& renderer,
                 const echo::CombatWorld& combat,
                 double playerX,
                 double playerY);
} // namespace combat_visuals
