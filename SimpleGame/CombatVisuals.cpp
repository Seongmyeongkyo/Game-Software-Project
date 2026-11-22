#include "stdafx.h"
#include "CombatVisuals.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <vector>

namespace combat_visuals
{
    namespace
    {
        constexpr double Pi = 3.141592653589793;

        Color C(unsigned rgb, float alpha = 1.0f)
        {
            return Color(((rgb >> 16) & 255) / 255.0f,
                         ((rgb >> 8) & 255) / 255.0f,
                         (rgb & 255) / 255.0f,
                         alpha);
        }

        void Poly(Renderer& renderer, std::initializer_list<Vec2> points, Color color)
        {
            renderer.Polygon(std::vector<Vec2>(points), color);
        }

        void Ellipse(Renderer& renderer, Vec2 center, float rx, float ry, Color color)
        {
            std::vector<Vec2> vertices;

            for (int index = 0; index < 40; ++index)
            {
                const float angle = static_cast<float>(index * 2.0 * Pi / 40.0);
                vertices.emplace_back(center.x + std::cos(angle) * rx,
                                      center.y + std::sin(angle) * ry);
            }

            renderer.Polygon(vertices, color);
        }

        void Ring(Renderer& renderer,
                  Vec2 center,
                  float rx,
                  float ry,
                  Color color,
                  float thickness = 1.5f)
        {
            Vec2 previous(center.x + rx, center.y);

            for (int index = 1; index <= 64; ++index)
            {
                const float angle = static_cast<float>(index * 2.0 * Pi / 64.0);
                const Vec2 point(center.x + std::cos(angle) * rx, center.y + std::sin(angle) * ry);
                renderer.Line(previous, point, thickness, color);
                previous = point;
            }
        }

        void Panel(Renderer& renderer, float x, float y, float width, float height)
        {
            renderer.RoundedRect(x, y + 4.0f, width, height, 12.0f, C(0x000710, .25f));
            renderer.RoundedRect(x, y, width, height, 12.0f, C(0x101D2D, .96f));
            renderer.Rect(x + 14.0f, y, width - 28.0f, 1.0f, C(0x7699AF, .25f));
        }

        void Text(Renderer& renderer,
                  float x,
                  float y,
                  const std::wstring& text,
                  float size = 13.0f,
                  Color color = C(0xDDE8EE),
                  int weight = 400)
        {
            renderer.Text(x, y, text, size, color, weight);
        }

        std::wstring Fixed(float value, int precision = 1)
        {
            std::wostringstream text;
            text << std::fixed << std::setprecision(precision) << value;
            return text.str();
        }

        void NoiseFragment(Renderer& renderer, Vec2 foot, float bob)
        {
            const float x = foot.x;
            const float y = foot.y - 31.0f + bob;

            Poly(renderer,
                 {Vec2(x - 20.0f, y - 9.0f),
                  Vec2(x - 3.0f, y - 25.0f),
                  Vec2(x - 6.0f, y + 6.0f),
                  Vec2(x - 23.0f, y + 1.0f)},
                 C(0x426878));
            Poly(renderer,
                 {Vec2(x + 1.0f, y - 23.0f),
                  Vec2(x + 20.0f, y - 10.0f),
                  Vec2(x + 23.0f, y + 3.0f),
                  Vec2(x + 5.0f, y + 9.0f)},
                 C(0x708896));
            Poly(renderer,
                 {Vec2(x - 5.0f, y + 11.0f),
                  Vec2(x + 7.0f, y + 12.0f),
                  Vec2(x + 1.0f, y + 23.0f),
                  Vec2(x - 11.0f, y + 20.0f)},
                 C(0x305764));
            renderer.Line(Vec2(x - 15.0f, y - 1.0f),
                          Vec2(x + 14.0f, y - 1.0f),
                          4.0f,
                          C(0x91FADE).Emissive(1.8f));
            renderer.Rect(x - 3.0f, y - 4.0f, 6.0f, 8.0f, C(0xE6FFF6).Emissive(2.0f));
            renderer.Rect(x - 29.0f, y - 15.0f, 5.0f, 2.0f, C(0x91FADE, .65f));
            renderer.Rect(x + 25.0f, y + 8.0f, 7.0f, 2.0f, C(0xA7B5DC, .65f));
        }

        void Sentinel(Renderer& renderer, Vec2 foot, float bob)
        {
            const float x = foot.x;
            const float y = foot.y + bob;
            renderer.Line(Vec2(x - 7.0f, y - 18.0f), Vec2(x - 10.0f, y - 1.0f), 7.0f, C(0x394E69));
            renderer.Line(Vec2(x + 7.0f, y - 18.0f), Vec2(x + 10.0f, y - 1.0f), 7.0f, C(0x394E69));
            Poly(renderer,
                 {Vec2(x - 15.0f, y - 47.0f),
                  Vec2(x + 15.0f, y - 47.0f),
                  Vec2(x + 21.0f, y - 22.0f),
                  Vec2(x, y - 12.0f),
                  Vec2(x - 21.0f, y - 22.0f)},
                 C(0x526A7C));
            Poly(renderer,
                 {Vec2(x, y - 46.0f),
                  Vec2(x + 15.0f, y - 47.0f),
                  Vec2(x + 21.0f, y - 22.0f),
                  Vec2(x, y - 12.0f)},
                 C(0x708897));
            renderer.RoundedRect(x - 13.0f, y - 64.0f, 26.0f, 20.0f, 4.0f, C(0x314B61));
            renderer.Line(Vec2(x - 8.0f, y - 54.0f),
                          Vec2(x + 8.0f, y - 54.0f),
                          3.0f,
                          C(0xE7BF8A).Emissive(1.6f));
            Poly(renderer,
                 {Vec2(x - 28.0f, y - 44.0f),
                  Vec2(x - 17.0f, y - 40.0f),
                  Vec2(x - 19.0f, y - 16.0f),
                  Vec2(x - 32.0f, y - 24.0f)},
                 C(0x9D879D));
            renderer.Line(Vec2(x + 25.0f, y - 43.0f),
                          Vec2(x + 27.0f, y - 13.0f),
                          3.0f,
                          C(0x9DE6D3).Emissive(.8f));
            renderer.Line(Vec2(x - 18.0f, y - 71.0f),
                          Vec2(x - 2.0f, y - 78.0f),
                          2.0f,
                          C(0xD8BF8A, .7f));
            renderer.Line(Vec2(x + 4.0f, y - 78.0f),
                          Vec2(x + 17.0f, y - 72.0f),
                          2.0f,
                          C(0xD8BF8A, .7f));
        }

        void BrokenMask(Renderer& renderer, Vec2 foot, float bob)
        {
            const float x = foot.x;
            const float y = foot.y - 36.0f + bob;

            Poly(renderer,
                 {Vec2(x - 20.0f, y - 17.0f),
                  Vec2(x - 2.0f, y - 23.0f),
                  Vec2(x - 5.0f, y + 12.0f),
                  Vec2(x - 15.0f, y + 5.0f)},
                 C(0xC3AEC9));
            Poly(renderer,
                 {Vec2(x + 3.0f, y - 22.0f),
                  Vec2(x + 21.0f, y - 14.0f),
                  Vec2(x + 16.0f, y + 11.0f),
                  Vec2(x + 4.0f, y + 18.0f)},
                 C(0x847BA4));
            renderer.Line(Vec2(x - 15.0f, y - 6.0f), Vec2(x - 6.0f, y - 4.0f), 3.0f, C(0x71506F));
            renderer.Line(Vec2(x + 6.0f, y - 5.0f),
                          Vec2(x + 15.0f, y - 7.0f),
                          3.0f,
                          C(0xE9A5C9).Emissive(1.7f));
            Poly(renderer,
                 {Vec2(x + 11.0f, y + 2.0f), Vec2(x + 15.0f, y + 14.0f), Vec2(x + 8.0f, y + 14.0f)},
                 C(0xE0A3D8).Emissive(1.0f));
            Poly(renderer,
                 {Vec2(x - 8.0f, y + 21.0f), Vec2(x + 2.0f, y + 23.0f), Vec2(x - 3.0f, y + 32.0f)},
                 C(0x756987));
        }

        void Broadcaster(Renderer& renderer, Vec2 foot, float bob, double time)
        {
            const float x = foot.x;
            const float y = foot.y - 52.0f + bob;

            Ellipse(renderer, Vec2(x, y - 3.0f), 72.0f, 70.0f, C(0xC58DAE, .035f).Emissive(.5f));
            renderer.Line(Vec2(x - 19.0f, y - 32.0f),
                          Vec2(x - 32.0f, y - 68.0f),
                          4.0f,
                          C(0x7C819B));
            renderer.Line(Vec2(x + 20.0f, y - 32.0f),
                          Vec2(x + 31.0f, y - 61.0f),
                          4.0f,
                          C(0x7C819B));
            renderer.Circle(x - 32.0f, y - 68.0f, 4.0f, C(0xF0B1CE).Emissive(2.0f));
            Poly(renderer,
                 {Vec2(x - 46.0f, y - 36.0f),
                  Vec2(x + 34.0f, y - 39.0f),
                  Vec2(x + 48.0f, y - 21.0f),
                  Vec2(x + 43.0f, y + 21.0f),
                  Vec2(x - 35.0f, y + 27.0f),
                  Vec2(x - 47.0f, y + 11.0f)},
                 C(0x626780));
            renderer.RoundedRect(x - 34.0f, y - 27.0f, 68.0f, 41.0f, 6.0f, C(0x172B42));
            renderer.Line(Vec2(x - 28.0f, y - 15.0f),
                          Vec2(x + 28.0f, y - 15.0f),
                          1.0f,
                          C(0xA7C6D3, .35f));
            renderer.Line(Vec2(x - 28.0f, y - 2.0f),
                          Vec2(x + 28.0f, y - 2.0f),
                          1.0f,
                          C(0xA7C6D3, .25f));
            renderer.Circle(x, y - 6.0f, 13.0f, C(0xDB91BC).Emissive(1.6f));
            renderer.Circle(x, y - 6.0f, 6.0f, C(0xFCE4E6).Emissive(2.2f));
            renderer.Line(Vec2(x - 5.0f, y + 24.0f), Vec2(x + 2.0f, y + 42.0f), 6.0f, C(0x65657C));
            Poly(renderer,
                 {Vec2(x - 20.0f, y + 45.0f),
                  Vec2(x + 24.0f, y + 41.0f),
                  Vec2(x + 14.0f, y + 52.0f),
                  Vec2(x - 13.0f, y + 55.0f)},
                 C(0x3C465E));

            for (int index = 0; index < 5; ++index)
            {
                const float angle = static_cast<float>(time * .65 + index * 2.0 * Pi / 5.0);
                const float sx = x + std::cos(angle) * 63.0f;
                const float sy = y + std::sin(angle) * 27.0f;
                Poly(renderer,
                     {Vec2(sx, sy - 9.0f), Vec2(sx + 8.0f, sy + 2.0f), Vec2(sx - 4.0f, sy + 7.0f)},
                     C(0xA895B5).Emissive(.6f));
            }
        }

        void UpgradeRow(Renderer& renderer,
                        float y,
                        const std::wstring& title,
                        const std::wstring& cost,
                        bool affordable)
        {
            renderer.RoundedRect(47.0f, y, 220.0f, 47.0f, 6.0f, C(0x1A2B3C));
            Text(renderer,
                 58.0f,
                 y + 6.0f,
                 title,
                 13.0f,
                 affordable ? C(0xBCEBD8) : C(0x93A7B8),
                 600);
            Text(renderer, 58.0f, y + 27.0f, cost, 11.0f, affordable ? C(0xD9C596) : C(0x70899B));
        }
    } // namespace

    void DrawMonster(Renderer& renderer,
                     const echo::Monster& monster,
                     Vec2 foot,
                     double time,
                     bool targeted)
    {
        if (monster.health <= 0.0f)
        {
            return;
        }

        const float bob =
            static_cast<float>(std::sin(time * 2.2 + static_cast<double>(monster.id % 97)) * 3.0);
        Ellipse(renderer,
                Vec2(foot.x, foot.y + 3.0f),
                monster.boss ? 43.0f : 22.0f,
                monster.boss ? 17.0f : 9.0f,
                C(0x020A14, .45f));

        if (targeted)
        {
            Ring(renderer,
                 Vec2(foot.x, foot.y + 2.0f),
                 monster.boss ? 52.0f : 30.0f,
                 monster.boss ? 21.0f : 12.0f,
                 C(0xEAC38E).Emissive(.7f),
                 2.0f);
        }

        switch (monster.kind)
        {
        case 0:
            NoiseFragment(renderer, foot, bob);
            break;
        case 1:
            Sentinel(renderer, foot, bob);
            break;
        case 2:
            BrokenMask(renderer, foot, bob);
            break;
        default:
            Broadcaster(renderer, foot, bob, time);
            break;
        }

        const float width = monster.boss ? 118.0f : 76.0f;
        const float top = foot.y - (monster.boss ? 142.0f : 102.0f) + bob;
        const float fraction =
            std::clamp(monster.health / std::max(1.0f, monster.maxHealth), 0.0f, 1.0f);
        Text(renderer,
             foot.x - width * .5f,
             top,
             echo::CombatWorld::MonsterName(monster.kind) + L"  Lv."
                 + std::to_wstring(monster.level),
             monster.boss ? 14.0f : 11.0f,
             monster.boss ? C(0xE6B4CB) : C(0xB9CED7),
             600);
        renderer.RoundedRect(foot.x - width * .5f, top + 22.0f, width, 4.0f, 2.0f, C(0x17283C));
        renderer.RoundedRect(foot.x - width * .5f,
                             top + 22.0f,
                             width * fraction,
                             4.0f,
                             2.0f,
                             monster.boss ? C(0xDA93B4) : C(0xA2D9CF));
    }

    void DrawProjectile(Renderer& renderer,
                        const echo::Projectile& projectile,
                        const std::vector<Vec2>& projectedTrail,
                        double time)
    {
        if (projectedTrail.empty())
        {
            return;
        }

        // The supplied history is tail-to-head, projected with this frame's camera.
        // Width, opacity and particle density all increase towards the moving head.
        constexpr size_t MaximumTrailPoints = 24;
        const size_t first = projectedTrail.size() > MaximumTrailPoints
                                 ? projectedTrail.size() - MaximumTrailPoints
                                 : 0;
        std::vector<Vec2> points(projectedTrail.begin() + first, projectedTrail.end());
        std::vector<float> distances(points.size(), 0.0f);

        for (size_t index = 0; index < points.size(); ++index)
        {
            points[index].y -= 26.0f;

            if (index > 0)
            {
                distances[index] = distances[index - 1]
                                   + std::hypot(points[index].x - points[index - 1].x,
                                                points[index].y - points[index - 1].y);
            }
        }

        const float length = distances.back();
        const float phase =
            static_cast<float>(projectile.age * 9.0 + double(projectile.targetId % 97) * .071);

        auto normalized = [](Vec2 direction)
        {
            const float magnitude = std::hypot(direction.x, direction.y);

            if (magnitude < .001f)
            {
                return Vec2(1.0f, 0.0f);
            }

            return Vec2(direction.x / magnitude, direction.y / magnitude);
        };

        auto tangentAt = [&](size_t index)
        {
            const size_t before = index == 0 ? 0 : index - 1;
            const size_t after = std::min(index + 1, points.size() - 1);
            return normalized(
                Vec2(points[after].x - points[before].x, points[after].y - points[before].y));
        };

        // A small standing wave gives the hologram a flexible ribbon silhouette;
        // the moving head remains attached to its actual gameplay position.
        std::vector<Vec2> ribbon = points;

        for (size_t index = 0; index < points.size(); ++index)
        {
            const float progress = length > .001f ? distances[index] / length : 1.0f;
            const Vec2 tangent = tangentAt(index);
            const float wave =
                std::sin(progress * float(Pi)) * std::sin(progress * 10.0f - phase) * 2.2f;
            ribbon[index].x -= tangent.y * wave;
            ribbon[index].y += tangent.x * wave;
        }

        auto ribbonWidth = [](float progress)
        {
            return .4f + 6.6f * std::pow(progress, 1.4f);
        };

        // Draw the dark silhouette first so the effect is readable without bloom.
        // Azure carries the body, violet the holographic fringe, and white only its core.
        for (int layer = 0; layer < 4; ++layer)
        {
            for (size_t index = 1; index < ribbon.size(); ++index)
            {
                if (distances[index] - distances[index - 1] < .001f)
                {
                    continue;
                }

                const float start = distances[index - 1] / std::max(.001f, length);
                const float end = distances[index] / std::max(.001f, length);
                const float middle = (start + end) * .5f;
                const Vec2 startTangent = tangentAt(index - 1);
                const Vec2 endTangent = tangentAt(index);
                const Vec2 startNormal(-startTangent.y, startTangent.x);
                const Vec2 endNormal(-endTangent.y, endTangent.x);
                float startWidth = ribbonWidth(start);
                float endWidth = ribbonWidth(end);
                Color color = C(0x07172F, .35f + middle * .25f);

                if (layer == 0)
                {
                    startWidth += 1.5f;
                    endWidth += 1.5f;
                }
                else if (layer == 1)
                {
                    startWidth += 3.2f;
                    endWidth += 3.2f;
                    color = C(0x697AFF, .035f + middle * .07f).Emissive(.7f);
                }
                else if (layer == 2)
                {
                    color = C(0x39A9FF, .18f + middle * .58f).Emissive(1.05f);
                }
                else
                {
                    startWidth *= .22f;
                    endWidth *= .22f;
                    color = C(0xD4EEFF, .15f + middle * .65f).Emissive(1.25f);
                }

                const Vec2& a = ribbon[index - 1];
                const Vec2& b = ribbon[index];
                Poly(renderer,
                     {Vec2(a.x + startNormal.x * startWidth, a.y + startNormal.y * startWidth),
                      Vec2(b.x + endNormal.x * endWidth, b.y + endNormal.y * endWidth),
                      Vec2(b.x - endNormal.x * endWidth, b.y - endNormal.y * endWidth),
                      Vec2(a.x - startNormal.x * startWidth, a.y - startNormal.y * startWidth)},
                     color);

                if (layer == 2)
                {
                    renderer.Line(Vec2(a.x + startNormal.x * startWidth * .73f,
                                       a.y + startNormal.y * startWidth * .73f),
                                  Vec2(b.x + endNormal.x * endWidth * .73f,
                                       b.y + endNormal.y * endWidth * .73f),
                                  .7f + middle * .8f,
                                  C(0xDB91FF, .15f + middle * .6f).Emissive(.8f));
                }
            }
        }

        auto spark = [&](Vec2 position, float radius, float opacity, bool violet)
        {
            renderer.Circle(position.x, position.y, radius + .85f, C(0x0A1A3D, opacity * .7f));
            renderer.Circle(position.x,
                            position.y,
                            radius,
                            C(violet ? 0xE4A7FF : 0x83D8FF, opacity).Emissive(1.15f));
        };

        if (length > .001f)
        {
            constexpr int TrailParticles = 36;

            for (int index = 0; index < TrailParticles; ++index)
            {
                // A fixed nonuniform distribution avoids per-frame randomness:
                // the last quarter of the trail has about five times the particles
                // of its first quarter, independent of frame rate and trail samples.
                const float progress = std::pow((index + .5f) / TrailParticles, .55f);
                const float desired = progress * length;
                auto found = std::upper_bound(distances.begin(), distances.end(), desired);
                const size_t after =
                    std::clamp(size_t(found - distances.begin()), size_t(1), ribbon.size() - 1);
                const size_t before = after - 1;
                const float span = distances[after] - distances[before];
                const float mix = span > .001f ? (desired - distances[before]) / span : 0.0f;
                const Vec2 tangent = normalized(
                    Vec2(ribbon[after].x - ribbon[before].x, ribbon[after].y - ribbon[before].y));
                const float side = std::sin(index * 2.399963f + phase * .46f);
                const float spread = 1.5f + progress * 12.0f;
                Vec2 position(ribbon[before].x + (ribbon[after].x - ribbon[before].x) * mix,
                              ribbon[before].y + (ribbon[after].y - ribbon[before].y) * mix);
                position.x -= tangent.y * side * spread;
                position.y += tangent.x * side * spread;
                spark(position,
                      .45f + progress * 1.15f,
                      .2f + progress * progress * .66f,
                      index % 3 == 0);
            }
        }

        const Vec2 head = ribbon.back();
        const Vec2 heading = tangentAt(points.size() - 1);
        const Vec2 normal(-heading.y, heading.x);
        renderer.Circle(head.x, head.y, 12.0f, C(0x678EFF, .09f).Emissive(.7f));
        Poly(renderer,
             {Vec2(head.x + heading.x * 8.0f, head.y + heading.y * 8.0f),
              Vec2(head.x + normal.x * 6.5f, head.y + normal.y * 6.5f),
              Vec2(head.x - heading.x * 6.0f, head.y - heading.y * 6.0f),
              Vec2(head.x - normal.x * 6.5f, head.y - normal.y * 6.5f)},
             C(0x55BFFF, .92f).Emissive(1.3f));
        renderer.Circle(head.x, head.y, 2.6f, C(0xF0FAFF).Emissive(1.5f));

        constexpr int HeadParticles = 12;
        const float twinkle = static_cast<float>(std::sin(time * 4.0 + projectile.age));

        for (int index = 0; index < HeadParticles; ++index)
        {
            const float angle = index * 2.399963f + phase * .32f;
            const float radius = 4.0f + (index % 5) * 2.1f;
            const float along = std::cos(angle) * radius * .75f;
            const float across = std::sin(angle) * radius;
            spark(Vec2(head.x + heading.x * along + normal.x * across,
                       head.y + heading.y * along + normal.y * across),
                  1.0f + (index % 3) * .38f,
                  .64f + twinkle * .13f,
                  index % 2 == 0);
        }
    }

    void DrawDrop(Renderer& renderer, const echo::LootDrop& drop, Vec2 foot, double time)
    {
        const float bob =
            static_cast<float>(std::sin(time * 2.8 + static_cast<double>(drop.id % 71)) * 2.5);
        const float radius = drop.kind == 1 ? 8.0f : 5.0f;
        const Color color =
            drop.kind == 0 ? C(0xF0CE8A) : (drop.kind == 1 ? C(0xE5A8CF) : C(0xA0E0B3));
        Ellipse(renderer, Vec2(foot.x, foot.y + 2.0f), radius + 4.0f, 4.0f, C(0x000A13, .3f));
        Ring(renderer, foot, radius + 7.0f, 5.0f, Color(color.r, color.g, color.b, .4f));
        const float y = foot.y - 13.0f + bob;
        Poly(renderer,
             {Vec2(foot.x, y - radius),
              Vec2(foot.x + radius, y),
              Vec2(foot.x, y + radius),
              Vec2(foot.x - radius, y)},
             color.Emissive(1.1f));

        if (drop.kind == 1)
        {
            Text(renderer, foot.x - 21.0f, y - 30.0f, L"공명핵", 11.0f, C(0xE8C2DB), 600);
        }
        else if (drop.kind == 2)
        {
            renderer.Line(Vec2(foot.x - 3.0f, y), Vec2(foot.x + 3.0f, y), 2.0f, C(0xF0FFF1));
            renderer.Line(Vec2(foot.x, y - 3.0f), Vec2(foot.x, y + 3.0f), 2.0f, C(0xF0FFF1));
        }
    }

    void DrawTelegraph(Renderer& renderer, const echo::Monster& monster, Vec2 foot)
    {
        if (!monster.boss || monster.health <= 0.0f || monster.telegraph <= 0.0f)
        {
            return;
        }

        // A world-space radius of 3 projects to these ellipse axes under Iso().
        constexpr float radiusX = 203.65f;
        constexpr float radiusY = 101.82f;
        const float progress = std::clamp((1.1f - monster.telegraph) / 1.1f, 0.0f, 1.0f);
        Ellipse(renderer, foot, radiusX, radiusY, C(0xDD7D9B, .12f));
        Ring(renderer, foot, radiusX, radiusY, C(0xED91AA, .85f).Emissive(.7f), 2.5f);
        Ring(renderer, foot, radiusX * progress, radiusY * progress, C(0xED91AA, .55f), 2.0f);
    }

    void DrawHud(Renderer& renderer,
                 const echo::CombatWorld& combat,
                 double playerX,
                 double playerY)
    {
        const echo::CombatStats stats = combat.Stats();
        const echo::Growth& growth = combat.growth;
        const int required = combat.RequiredXP();
        const float healthFraction =
            std::clamp(combat.health / std::max(1.0f, stats.maxHealth), 0.0f, 1.0f);
        const float experienceFraction =
            std::clamp(static_cast<float>(growth.xp) / std::max(1, required), 0.0f, 1.0f);

        Panel(renderer, 315.0f, 100.0f, 810.0f, 100.0f);
        Text(renderer,
             334.0f,
             113.0f,
             L"공명 Lv." + std::to_wstring(growth.level),
             21.0f,
             C(0xA7E6D7),
             600);
        const float ready =
            std::clamp(1.0f - combat.shotCooldown / std::max(.01f, stats.cooldown), 0.0f, 1.0f);
        renderer.Rect(335.0f, 143.0f, 135.0f, 3.0f, C(0x273B4C));
        renderer.Rect(335.0f, 143.0f, 135.0f * ready, 3.0f, C(0xA7E6D7));
        Text(renderer,
             503.0f,
             114.0f,
             L"생명  " + Fixed(combat.health, 0) + L" / " + Fixed(stats.maxHealth, 0),
             12.0f);
        renderer.RoundedRect(503.0f, 139.0f, 235.0f, 5.0f, 2.0f, C(0x273B4C));
        renderer.RoundedRect(503.0f,
                             139.0f,
                             235.0f * healthFraction,
                             5.0f,
                             2.0f,
                             C(healthFraction < .25f ? 0xE494A2 : 0x94DAC3));
        Text(renderer,
             762.0f,
             114.0f,
             L"경험  " + std::to_wstring(growth.xp) + L" / " + std::to_wstring(required),
             12.0f);
        renderer.RoundedRect(762.0f, 139.0f, 340.0f, 5.0f, 2.0f, C(0x273B4C));
        renderer.RoundedRect(762.0f, 139.0f, 340.0f * experienceFraction, 5.0f, 2.0f, C(0xB8B3E6));
        Text(renderer,
             334.0f,
             153.0f,
             L"잔향탄 " + Fixed(stats.damage, 0) + L"  ·  재사용 " + Fixed(stats.cooldown, 2)
                 + L"초  ·  사거리 " + Fixed(stats.range),
             12.0f,
             C(0xAFC6D1));
        Text(renderer,
             334.0f,
             178.0f,
             L"푸른 지면 경계 = 자동 공격 사거리    Q 유지  보호",
             11.0f,
             C(0x819FB0));

        const int zoneX = static_cast<int>(std::floor(playerX / 12.0));
        const int zoneY = static_cast<int>(std::floor(playerY / 12.0));
        Text(renderer,
             847.0f,
             162.0f,
             L"위험도 " + std::to_wstring(echo::CombatWorld::ZoneLevel(zoneX, zoneY))
                 + L"  ·  처치 " + std::to_wstring(growth.kills),
             12.0f,
             C(0xCBB8B7));

        Panel(renderer, 32.0f, 377.0f, 250.0f, 267.0f);
        Text(renderer, 49.0f, 391.0f, L"잔향을 나의 힘으로", 16.0f, C(0xC8E8DF), 600);
        Text(renderer,
             49.0f,
             421.0f,
             L"기억 파편 " + std::to_wstring(growth.fragments) + L"  /  공명핵 "
                 + std::to_wstring(growth.cores),
             12.0f,
             C(0xCFBD9D));

        const int focusCost = 12 + growth.focusRank * 8;
        const int magnetCost = 10 + growth.magnetRank * 8;
        const int vitalityCost = 1 + growth.vitalityRank / 5;
        UpgradeRow(renderer,
                   452.0f,
                   L"[4] 잔향탄 조율  " + std::to_wstring(growth.focusRank),
                   growth.focusRank >= 30
                       ? L"최대 조율 단계"
                       : L"파편 " + std::to_wstring(focusCost) + L"  ·  위력과 발사 속도",
                   growth.focusRank < 30 && growth.fragments >= focusCost);
        UpgradeRow(renderer,
                   506.0f,
                   L"[5] 공명 인력  " + std::to_wstring(growth.magnetRank),
                   growth.magnetRank >= 12
                       ? L"최대 인력 단계"
                       : L"파편 " + std::to_wstring(magnetCost) + L"  ·  자동 줍기 거리",
                   growth.magnetRank < 12 && growth.fragments >= magnetCost);
        UpgradeRow(renderer,
                   560.0f,
                   L"[6] 기억 그릇  " + std::to_wstring(growth.vitalityRank),
                   growth.vitalityRank >= 20
                       ? L"최대 그릇 단계"
                       : L"공명핵 " + std::to_wstring(vitalityCost) + L"  ·  최대 생명",
                   growth.vitalityRank < 20 && growth.cores >= vitalityCost);
        Text(renderer,
             49.0f,
             619.0f,
             L"자동 줍기 반경  " + Fixed(stats.pickupRadius),
             11.0f,
             C(0x7E9BAC));
    }
} // namespace combat_visuals
