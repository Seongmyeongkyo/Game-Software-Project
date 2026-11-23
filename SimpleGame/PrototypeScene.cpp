#include "stdafx.h"
#include "PrototypeScene.h"
#include "CombatVisuals.h"
#include "Quests.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <sstream>

namespace prototype
{
    namespace
    {
        Renderer* draw = nullptr;
        const echo::GameState* game = nullptr;
        const View* view = nullptr;
        constexpr double Pi = 3.141592653589793;

        Color C(unsigned rgb, float alpha = 1)
        {
            return Color(((rgb >> 16) & 255) / 255.f,
                         ((rgb >> 8) & 255) / 255.f,
                         (rgb & 255) / 255.f,
                         alpha);
        }

        Color Tint(Color c, float value)
        {
            return Color(c.r * value, c.g * value, c.b * value, c.a).Emissive(c.emission);
        }

        uint32_t Hash(int x, int y, int salt = 0)
        {
            uint32_t v =
                uint32_t(x) * 374761393u + uint32_t(y) * 668265263u + uint32_t(salt) * 2246822519u;
            v = (v ^ (v >> 13)) * 1274126177u;
            return v ^ (v >> 16);
        }

        int Mod(int n, int m)
        {
            return (n % m + m) % m;
        }

        constexpr int RoadSpacing = 6;
        constexpr int BuildingSpacing = 4;
        constexpr int LotTileRadius = 1;
        constexpr double BuildingWidth = 1.85;
        constexpr double BuildingDepth = 1.65;
        constexpr double CafeX = 2.0;
        constexpr double CafeY = -3.0;
        constexpr double CafeWidth = 2.6;
        constexpr double CafeDepth = 1.5;
        constexpr double CafeRoofWidth = 2.8;
        constexpr double CafeRoofDepth = 1.7;
        constexpr double LotSize = 2 * LotTileRadius + 1;
        constexpr double PlayerClearance = .12;
        static_assert(BuildingWidth < LotSize && BuildingDepth < LotSize,
                      "Building must fit inside its lot");
        static_assert(CafeRoofWidth < LotSize && CafeRoofDepth < LotSize,
                      "Cafe roof must fit inside its lot");

        bool PlazaTile(int x, int y)
        {
            return std::abs(x) < 3 && std::abs(y) < 3;
        }

        bool RoadGridTile(int x, int y)
        {
            return Mod(x, RoadSpacing) == 0 || Mod(y, RoadSpacing) == 0;
        }

        bool CityRoadTile(int x, int y)
        {
            return !PlazaTile(x, y) && RoadGridTile(x, y);
        }

        // Preserve the original decoration anchors for Lumia, independently of city lots.
        bool DecorationAnchor(int x, int y)
        {
            return !(std::abs(x) < 5 && std::abs(y) < 5) && Mod(x, BuildingSpacing) == 0
                   && Mod(y, BuildingSpacing) == 0;
        }

        bool LotAvoidsRoad(double x, double y)
        {
            // Tiles are centered on integer coordinates, with boundaries at +/-0.5.
            // Exact boundary contact is allowed; any overlap with a road tile is rejected.
            const int firstX = int(std::floor(x - LotSize * .5 + .5));
            const int lastX = int(std::ceil(x + LotSize * .5 + .5)) - 1;
            const int firstY = int(std::floor(y - LotSize * .5 + .5));
            const int lastY = int(std::ceil(y + LotSize * .5 + .5)) - 1;
            for (int tx = firstX; tx <= lastX; ++tx)
            {
                for (int ty = firstY; ty <= lastY; ++ty)
                {
                    if (CityRoadTile(tx, ty))
                    {
                        return false;
                    }
                }
            }
            return true;
        }

        bool CityBuildingAt(int x, int y)
        {
            return DecorationAnchor(x, y) && LotAvoidsRoad(x, y);
        }

        bool CityLotTile(int x, int y)
        {
            if (std::abs(x - int(CafeX)) <= LotTileRadius
                && std::abs(y - int(CafeY)) <= LotTileRadius)
            {
                return true;
            }
            const int bx = int(std::round(double(x) / BuildingSpacing)) * BuildingSpacing;
            const int by = int(std::round(double(y) / BuildingSpacing)) * BuildingSpacing;
            return std::abs(x - bx) <= LotTileRadius && std::abs(y - by) <= LotTileRadius
                   && CityBuildingAt(bx, by);
        }

        Vec2 Iso(double x, double y, float z = 0)
        {
            return Vec2(float(710 + (x - view->cameraX - y + view->cameraY) * 48),
                        float(485 + (x - view->cameraX + y - view->cameraY) * 24 - z));
        }

        float Distance(double ax, double ay, double bx, double by)
        {
            return float(std::hypot(ax - bx, ay - by));
        }

        void Poly(std::initializer_list<Vec2> points, Color color)
        {
            draw->Polygon(std::vector<Vec2>(points), color);
        }

        void Label(float x,
                   float y,
                   const std::wstring& text,
                   float size = 16,
                   Color color = C(0xDDE8EE),
                   int weight = 400)
        {
            draw->Text(x, y, text, size, color, weight);
        }

        void Diamond(double x, double y, float rx, float ry, Color color, float z = 0)
        {
            Vec2 p = Iso(x, y, z);
            Poly({Vec2(p.x, p.y - ry),
                  Vec2(p.x + rx, p.y),
                  Vec2(p.x, p.y + ry),
                  Vec2(p.x - rx, p.y)},
                 color);
        }

        void Ellipse(float x, float y, float rx, float ry, Color color)
        {
            std::vector<Vec2> vertices;
            for (int i = 0; i < 40; ++i)
            {
                float a = float(i * 2 * Pi / 40);
                vertices.emplace_back(x + std::cos(a) * rx, y + std::sin(a) * ry);
            }
            draw->Polygon(vertices, color);
        }

        void Ring(float x, float y, float rx, float ry, float thickness, Color color)
        {
            Vec2 previous(x + rx, y);
            for (int i = 1; i <= 64; ++i)
            {
                float a = float(i * 2 * Pi / 64);
                Vec2 p(x + std::cos(a) * rx, y + std::sin(a) * ry);
                draw->Line(previous, p, thickness, color);
                previous = p;
            }
        }

        void AttackRange()
        {
            // Combat selects enemies by Euclidean distance between world-space feet.
            // Project that same circle instead of guessing a screen-space radius.
            constexpr int segments = 128;
            const double range = game->combat.Stats().range;
            std::vector<Vec2> boundary;
            boundary.reserve(segments);

            for (int index = 0; index < segments; ++index)
            {
                const double angle = index * 2.0 * Pi / segments;
                boundary.push_back(
                    Iso(game->x + std::cos(angle) * range, game->y + std::sin(angle) * range));
            }

            draw->Polygon(boundary, C(0x51A9F5, .018f));

            const Color edge = game->combat.targetId != 0 ? C(0x8CCBFF, .98f).Emissive(.25f)
                                                          : C(0x6FB5EE, .9f).Emissive(.15f);

            for (size_t index = 0; index < boundary.size(); ++index)
            {
                const Vec2 start = boundary[index];
                const Vec2 end = boundary[(index + 1) % boundary.size()];
                draw->Line(start, end, 5.0f, C(0x6CB6FF, .06f).Emissive(.3f));
                draw->Line(start, end, 3.5f, C(0x081C33, .65f));
                draw->Line(start, end, 1.5f, edge);
            }
        }

        void Box(double x,
                 double y,
                 double width,
                 double depth,
                 float height,
                 Color color,
                 float base = 0)
        {
            Vec2 a = Iso(x - width / 2, y - depth / 2, base);
            Vec2 b = Iso(x + width / 2, y - depth / 2, base);
            Vec2 c = Iso(x + width / 2, y + depth / 2, base);
            Vec2 d = Iso(x - width / 2, y + depth / 2, base);
            Vec2 at(a.x, a.y - height);
            Vec2 bt(b.x, b.y - height);
            Vec2 ct(c.x, c.y - height);
            Vec2 dt(d.x, d.y - height);
            Poly({d, c, ct, dt}, Tint(color, .62f));
            Poly({c, b, bt, ct}, Tint(color, .84f));
            Poly({at, bt, ct, dt}, color);
            draw->Line(dt, ct, 1, Tint(color, 1.3f));
            draw->Line(ct, bt, 1, Tint(color, 1.3f));
        }

        void Panel(float x, float y, float w, float h, float opacity = .93f)
        {
            draw->RoundedRect(x, y + 4, w, h, 12, C(0x000710, .22f));
            draw->RoundedRect(x, y, w, h, 12, C(0x101D2D, opacity));
            draw->Rect(x + 14, y, w - 28, 1, C(0x7699AF, .22f));
        }

        void KeyCap(float x, float y, const std::wstring& key, const std::wstring& name)
        {
            draw->RoundedRect(x, y, 30, 27, 5, C(0x23374A));
            Label(x + 8, y + 3, key, 13, C(0xD3E2E9), 600);
            Label(x + 39, y + 3, name, 13, C(0x9FB3C0));
        }

        void PlayerAvatar(Vec2 p, Color coat)
        {
            const Color skin = C(0xF0C4AA);
            const Color hair = C(0x8A4B3E);
            const float bottom = p.y;

            draw->Line(Vec2(p.x - 4, bottom - 17), Vec2(p.x - 6, bottom - 2), 5, C(0x39434B));
            draw->Line(Vec2(p.x + 4, bottom - 17), Vec2(p.x + 6, bottom - 2), 5, C(0x303A43));
            draw->RoundedRect(p.x - 10, bottom - 4, 9, 5, 2, C(0xE1D7C6));
            draw->RoundedRect(p.x + 3, bottom - 4, 9, 5, 2, C(0xD4CBBE));
            Poly({Vec2(p.x - 10, bottom - 32),
                  Vec2(p.x + 9, bottom - 32),
                  Vec2(p.x + 8, bottom - 22),
                  Vec2(p.x + 11, bottom - 10),
                  Vec2(p.x - 10, bottom - 10),
                  Vec2(p.x - 8, bottom - 22)},
                 coat);
            Poly({Vec2(p.x - 3, bottom - 32),
                  Vec2(p.x + 4, bottom - 32),
                  Vec2(p.x + 5, bottom - 11),
                  Vec2(p.x - 3, bottom - 11)},
                 C(0x29384D));
            draw->Line(Vec2(p.x - 10, bottom - 29),
                       Vec2(p.x - 14, bottom - 16),
                       5,
                       Tint(coat, .84f));
            draw->Line(Vec2(p.x + 10, bottom - 29), Vec2(p.x + 14, bottom - 16), 5, coat);
            draw->Line(Vec2(p.x - 10, bottom - 29), Vec2(p.x - 12, bottom - 23), 5, C(0xF1E4CE));
            draw->Circle(p.x - 14, bottom - 13.5f, 2.5f, skin);
            draw->Circle(p.x + 14, bottom - 13.5f, 2.5f, skin);
            draw->RoundedRect(p.x + 12, bottom - 18, 4, 2, 1, C(0xE4C29A));
            draw->Line(Vec2(p.x - 7, bottom - 32), Vec2(p.x + 6, bottom - 15), 2, C(0x354356));
            draw->RoundedRect(p.x + 3, bottom - 20, 6, 7, 1.5f, C(0x28374C));
            draw->Rect(p.x + 5, bottom - 18, 2, 1, C(0xE4BA66));
            draw->Rect(p.x - 2, bottom - 35, 5, 5, skin);

            Poly({Vec2(p.x - 10, bottom - 52),
                  Vec2(p.x - 15, bottom - 49),
                  Vec2(p.x - 16, bottom - 42),
                  Vec2(p.x - 13, bottom - 36),
                  Vec2(p.x - 8, bottom - 32),
                  Vec2(p.x - 10, bottom - 41),
                  Vec2(p.x - 7, bottom - 48)},
                 Tint(hair, .85f));
            Ellipse(p.x, bottom - 43, 11.5f, 10.5f, hair);
            Poly({Vec2(p.x - 11, bottom - 45),
                  Vec2(p.x - 11, bottom - 37),
                  Vec2(p.x - 7, bottom - 35),
                  Vec2(p.x - 5, bottom - 43)},
                 hair);
            Poly({Vec2(p.x + 7, bottom - 46),
                  Vec2(p.x + 12, bottom - 42),
                  Vec2(p.x + 10, bottom - 36),
                  Vec2(p.x + 6, bottom - 38)},
                 Tint(hair, .88f));
            draw->Circle(p.x, bottom - 42, 9, skin);
            Poly({Vec2(p.x - 11, bottom - 44),
                  Vec2(p.x - 8, bottom - 52),
                  Vec2(p.x + 3, bottom - 54),
                  Vec2(p.x + 11, bottom - 47),
                  Vec2(p.x + 7, bottom - 44),
                  Vec2(p.x + 3, bottom - 48),
                  Vec2(p.x - 3, bottom - 41),
                  Vec2(p.x - 2, bottom - 47),
                  Vec2(p.x - 8, bottom - 38)},
                 hair);
            draw->Line(Vec2(p.x - 7, bottom - 48), Vec2(p.x - 4, bottom - 50), 1, C(0xBC8058));
            draw->Line(Vec2(p.x - 8, bottom - 51), Vec2(p.x - 11, bottom - 50), 1.5f, C(0xEAC36E));
            draw->Line(Vec2(p.x - 11, bottom - 50), Vec2(p.x - 11, bottom - 47), 1.5f, C(0xEAC36E));
            draw->Line(Vec2(p.x - 11, bottom - 47), Vec2(p.x - 8, bottom - 46), 1.5f, C(0xEAC36E));
            draw->Rect(p.x + 4, bottom - 44, 2, 2, C(0x493B33));
            draw->Line(Vec2(p.x + 3, bottom - 37), Vec2(p.x + 6, bottom - 38), 1, C(0xAF7665));
            draw->Line(Vec2(p.x - 5, bottom - 33), Vec2(p.x, bottom - 29), 2, Tint(coat, .85f));
            draw->Line(Vec2(p.x + 5, bottom - 33), Vec2(p.x, bottom - 29), 2, coat);
            draw->Line(Vec2(p.x + 3, bottom - 26), Vec2(p.x + 3, bottom - 22), 1, C(0xD5937A));
        }

        void SeaAvatar(Vec2 p)
        {
            const Color skin = C(0xF0C4AA);
            const Color hair = C(0x26344E);
            const Color jacket = C(0x303B53);
            const Color lime = C(0xC5D86F);
            const float bottom = p.y;

            Ellipse(p.x, p.y + 3, 15, 6, C(0x020A14, .5f));
            draw->Line(Vec2(p.x - 4, bottom - 17), Vec2(p.x - 6, bottom - 1), 5, C(0x283346));
            draw->Line(Vec2(p.x + 4, bottom - 17), Vec2(p.x + 6, bottom - 1), 5, C(0x232F42));
            draw->RoundedRect(p.x - 10, bottom - 3, 9, 4, 1.5f, C(0xD9DED1));
            draw->RoundedRect(p.x + 3, bottom - 3, 9, 4, 1.5f, C(0xCAD2CA));
            Poly({Vec2(p.x - 11, bottom - 32),
                  Vec2(p.x + 10, bottom - 32),
                  Vec2(p.x + 12, bottom - 11),
                  Vec2(p.x - 12, bottom - 11)},
                 jacket);
            Poly({Vec2(p.x - 4, bottom - 32),
                  Vec2(p.x + 4, bottom - 32),
                  Vec2(p.x + 5, bottom - 12),
                  Vec2(p.x - 4, bottom - 12)},
                 C(0xADA0C0));
            draw->Line(Vec2(p.x - 10, bottom - 29), Vec2(p.x - 14, bottom - 15), 6, jacket);
            draw->Line(Vec2(p.x + 10, bottom - 29), Vec2(p.x + 14, bottom - 15), 6, jacket);
            draw->Line(Vec2(p.x + 10, bottom - 29), Vec2(p.x + 12, bottom - 23), 6, lime);
            draw->Line(Vec2(p.x - 10, bottom - 24), Vec2(p.x - 12, bottom - 20), 2, lime);
            draw->Circle(p.x - 14, bottom - 13, 2.5f, skin);
            draw->Circle(p.x + 14, bottom - 13, 2.5f, skin);
            draw->Circle(p.x - 7, bottom - 26, 2, lime);
            draw->Rect(p.x - 2, bottom - 35, 5, 5, skin);

            Ellipse(p.x, bottom - 43, 11.5f, 10.5f, hair);
            Poly({Vec2(p.x + 6, bottom - 46),
                  Vec2(p.x + 12, bottom - 42),
                  Vec2(p.x + 10, bottom - 37),
                  Vec2(p.x + 13, bottom - 35),
                  Vec2(p.x + 7, bottom - 32),
                  Vec2(p.x + 1, bottom - 35)},
                 hair);
            draw->Circle(p.x, bottom - 42, 9, skin);
            Poly({Vec2(p.x - 11, bottom - 44),
                  Vec2(p.x - 10, bottom - 50),
                  Vec2(p.x - 3, bottom - 54),
                  Vec2(p.x + 8, bottom - 52),
                  Vec2(p.x + 12, bottom - 44),
                  Vec2(p.x + 7, bottom - 45),
                  Vec2(p.x + 4, bottom - 48),
                  Vec2(p.x + 2, bottom - 41),
                  Vec2(p.x - 3, bottom - 48),
                  Vec2(p.x - 7, bottom - 43)},
                 hair);
            draw->Line(Vec2(p.x + 2, bottom - 51), Vec2(p.x, bottom - 45), 2.5f, lime);
            draw->Rect(p.x - 5, bottom - 44, 2, 2, C(0x574936));
            draw->Line(Vec2(p.x - 6, bottom - 37), Vec2(p.x - 3, bottom - 37), 1, C(0xAD7666));
            draw->Circle(p.x + 7, bottom - 42, 3, C(0xE9EADF));
            draw->Circle(p.x + 7, bottom - 42, 1.3f, C(0x778693));
            draw->Line(Vec2(p.x + 8, bottom - 40), Vec2(p.x + 5, bottom - 36), 1, C(0xDCE0D6));
            draw->Line(Vec2(p.x + 5, bottom - 36), Vec2(p.x, bottom - 36), 1, C(0xDCE0D6));
        }

        void Character(double x, double y, Color coat, bool player, bool spirit = false, int id = 0)
        {
            Vec2 p = Iso(x, y);
            float bob = spirit ? float(std::sin(view->time * 2 + id) * 4 - 7) : 0;
            Ellipse(p.x, p.y + 3, 15, 6, C(0x020A14, .5f));
            if (player)
            {
                Ring(p.x, p.y + 3, 19, 8, 2, C(0x83F3DE, .7f).Emissive(.65f));
                PlayerAvatar(p, coat);
                return;
            }
            if (spirit)
            {
                coat = coat.Emissive(.65f);
                Ellipse(p.x,
                        p.y - 23 + bob,
                        28,
                        36,
                        Color(coat.r, coat.g, coat.b, .07f).Emissive(.5f));
                Ring(p.x, p.y, 22, 9, 1.4f, Color(coat.r, coat.g, coat.b, .45f).Emissive(.8f));
            }
            float bottom = p.y + bob;
            draw->Line(Vec2(p.x - 5, bottom - 18), Vec2(p.x - 7, bottom), 5, C(0x202D48));
            draw->Line(Vec2(p.x + 5, bottom - 18), Vec2(p.x + 7, bottom), 5, C(0x202D48));
            Poly({Vec2(p.x - 10, bottom - 33),
                  Vec2(p.x + 9, bottom - 33),
                  Vec2(p.x + 13, bottom - 9),
                  Vec2(p.x - 12, bottom - 9)},
                 coat);
            draw->Line(Vec2(p.x - 10, bottom - 29),
                       Vec2(p.x - 15, bottom - 14),
                       5,
                       Tint(coat, .8f));
            draw->Line(Vec2(p.x + 10, bottom - 29), Vec2(p.x + 15, bottom - 14), 5, coat);
            draw->Circle(p.x,
                         bottom - 42,
                         10,
                         C(spirit ? 0xCBFAF2 : 0xF1C9AF).Emissive(spirit ? .45f : 0.f));
            Poly({Vec2(p.x - 11, bottom - 44),
                  Vec2(p.x - 8, bottom - 53),
                  Vec2(p.x + 5, bottom - 54),
                  Vec2(p.x + 12, bottom - 44),
                  Vec2(p.x + 5, bottom - 47),
                  Vec2(p.x - 2, bottom - 41)},
                 C(player ? 0xE2D9EF : 0x29354A));
            draw->Rect(p.x + 4, bottom - 44, 2, 2, C(0x18263C));
            draw->Line(Vec2(p.x - 7, bottom - 31),
                       Vec2(p.x + 7, bottom - 31),
                       3,
                       C(player ? 0xF4B27A : 0xC9F9EF));
        }

        Color QuestColor(echo::QuestTarget target)
        {
            switch (target)
            {
            case echo::QuestTarget::Sea:
                return C(0xC5D86F);
            case echo::QuestTarget::Mion:
                return C(0xD7AF7F);
            case echo::QuestTarget::Doyun:
                return C(0x8BA8BC);
            case echo::QuestTarget::Yunseo:
                return C(0xCC9EC3);
            case echo::QuestTarget::Ihyeon:
                return C(0xABD0D7);
            case echo::QuestTarget::Harin:
                return C(0xB5CEB1);
            default:
                return C(0xE5CA91);
            }
        }

        void QuestNpc(const echo::QuestAnchor& anchor)
        {
            const Vec2 p = Iso(anchor.x, anchor.y);
            Character(anchor.x, anchor.y, QuestColor(anchor.target), false);

            if (anchor.target == echo::QuestTarget::Mion)
            {
                draw->Circle(p.x - 10, p.y - 48, 4, C(0x665044));
                Poly({Vec2(p.x - 7, p.y - 28),
                      Vec2(p.x + 6, p.y - 28),
                      Vec2(p.x + 8, p.y - 10),
                      Vec2(p.x - 8, p.y - 10)},
                     C(0xE8DCC4));
                draw->Rect(p.x - 4, p.y - 21, 7, 5, C(0xC8B9A0));
            }
            else if (anchor.target == echo::QuestTarget::Doyun)
            {
                Ellipse(p.x - 1, p.y - 50, 10, 4, C(0x5D5E65));
                draw->Rect(p.x + 5, p.y - 26, 9, 12, C(0xE5DDD1));
                draw->Rect(p.x + 6, p.y - 25, 7, 7, C(0x8C9AA3));
            }
            else if (anchor.target == echo::QuestTarget::Yunseo)
            {
                draw->Line(Vec2(p.x - 9, p.y - 46), Vec2(p.x - 10, p.y - 28), 4, C(0x785B7D));
                draw->Line(Vec2(p.x + 9, p.y - 46), Vec2(p.x + 11, p.y - 31), 4, C(0x785B7D));
                draw->Line(Vec2(p.x - 8, p.y - 50), Vec2(p.x + 3, p.y - 52), 3, C(0x967697));
                draw->Circle(p.x + 9, p.y - 42, 3, C(0xE2C195));
            }
            else if (anchor.target == echo::QuestTarget::Ihyeon)
            {
                draw->Line(Vec2(p.x - 7, p.y - 49), Vec2(p.x + 5, p.y - 52), 4, C(0x9AA9AF));
                draw->Rect(p.x - 6, p.y - 44, 5, 3, C(0x314D60));
                draw->Rect(p.x + 1, p.y - 44, 5, 3, C(0x314D60));
                draw->Line(Vec2(p.x - 1, p.y - 43), Vec2(p.x + 1, p.y - 43), 1, C(0x314D60));
                draw->Rect(p.x + 5, p.y - 26, 11, 14, C(0x314D60));
                draw->Rect(p.x + 6, p.y - 25, 9, 10, C(0x8CB9BA));
            }
        }

        void QuestObject(const echo::QuestAnchor& anchor)
        {
            const Vec2 p = Iso(anchor.x, anchor.y);
            const Color color = QuestColor(anchor.target);

            if (anchor.npc && anchor.target != echo::QuestTarget::Harin)
            {
                QuestNpc(anchor);
            }
            else if (anchor.target == echo::QuestTarget::Grove)
            {
                Ellipse(p.x, p.y + 1, 22, 9, C(0x3D6D5E, .5f));
                draw->Line(Vec2(p.x, p.y), Vec2(p.x + 2, p.y - 32), 3, C(0x6F9C7E));
                Ellipse(p.x - 7, p.y - 24, 10, 5, C(0x76AE8E));
                Ellipse(p.x + 10, p.y - 32, 11, 6, C(0x9EC3A2));

                if (game->quests.GroveDecorated())
                {
                    for (int index = 0; index < 5; ++index)
                    {
                        const float x = p.x - 17 + index * 8.0f;
                        const float y = p.y - 7 - (index % 2) * 5.0f;
                        draw->Circle(x, y, 4, C(0xDFC39F));
                        draw->Circle(x, y, 1.5f, C(0xFFF0C3));
                    }
                }
            }
            else if (anchor.target == echo::QuestTarget::Trace)
            {
                Ring(p.x, p.y, 23, 9, 1, C(0x96D7CB, .55f));
                draw->Circle(p.x, p.y - 19, 7, C(0xB8E9D4, .24f).Emissive(.5f));
                draw->Circle(p.x, p.y - 19, 3, C(0xB8E9D4).Emissive(.8f));
            }
            else if (anchor.target == echo::QuestTarget::Photo)
            {
                Poly({Vec2(p.x - 10, p.y - 30),
                      Vec2(p.x + 11, p.y - 27),
                      Vec2(p.x + 9, p.y - 5),
                      Vec2(p.x - 12, p.y - 8)},
                     C(0xDDD5C7));
                draw->Rect(p.x - 7, p.y - 26, 14, 13, C(0x718795));
                draw->Circle(p.x - 3, p.y - 21, 2, C(0xB6C3BC));
                draw->Circle(p.x + 3, p.y - 21, 2, C(0xB6C3BC));
            }
            else
            {
                Ellipse(p.x, p.y + 1, 21, 8, C(0x06121D, .5f));
                draw->Line(p, Vec2(p.x, p.y - 22), 5, C(0x486275));
                draw->RoundedRect(p.x - 19, p.y - 50, 38, 28, 3, C(0x304D60));
                draw->Rect(p.x - 15, p.y - 46, 30, 19, C(0x142F40));
                draw->Line(Vec2(p.x - 11, p.y - 39), Vec2(p.x + 10, p.y - 39), 1, color);
                draw->Line(Vec2(p.x - 11, p.y - 34), Vec2(p.x + 5, p.y - 34), 1, color);

                if (anchor.target == echo::QuestTarget::Harin)
                {
                    draw->Circle(p.x, p.y - 40, 5, C(0xC9C9B2));
                    draw->RoundedRect(p.x - 7, p.y - 34, 14, 8, 3, C(0x8EAAA2));
                }
            }

            const bool active = game->quests.Tracked().target == anchor.target;
            const float labelY = p.y - (anchor.npc ? 78.0f : 65.0f);
            std::wstring name = anchor.name;
            if (anchor.target == echo::QuestTarget::Harin && !game->quests.LinksDisabled()
                && !active)
            {
                const auto chapters = game->quests.Entries(0);
                if (chapters.size() <= 4 || chapters[4].progress == 0)
                {
                    name = L"응답 없는 송신기";
                }
            }
            const float nameWidth = static_cast<float>(name.size()) * 6.4f;
            Label(p.x - nameWidth, labelY, name, 13, color, 600);

            if (active)
            {
                Ring(p.x, p.y + 3, 20, 8, 1.5f, C(0xF3D493, .8f).Emissive(.3f));
                draw->Circle(p.x, labelY - 15, 7, C(0xF0D18B));
                Label(p.x - 2, labelY - 23, L"!", 12, C(0x273241), 700);
            }

            if (Distance(game->x, game->y, anchor.x, anchor.y) < 2.2)
            {
                Label(p.x - 26, p.y + 16, anchor.npc ? L"[E] 대화" : L"[E] 조사", 12, C(0xD7D6BD));
            }
        }

        void Building(int x, int y)
        {
            uint32_t seed = Hash(x, y, 5);
            float height = 75 + (seed % 4) * 30.f;
            Color color = C(seed % 3 == 0 ? 0x3D4D6D : (seed % 3 == 1 ? 0x34485B : 0x4B4667));
            Box(x, y, BuildingWidth, BuildingDepth, height, color);
            Box(x, y, 1.35, 1.2, 6, Tint(color, 1.17f), height);
            Box(x - .25, y - .15, .5, .5, 18, C(0x51647B), height + 6);
            Vec2 b = Iso(x + BuildingWidth * .5, y - BuildingDepth * .5);
            Vec2 c = Iso(x + BuildingWidth * .5, y + BuildingDepth * .5);
            Vec2 d = Iso(x - BuildingWidth * .5, y + BuildingDepth * .5);
            for (int row = 0; row < int(height / 25) - 1; ++row)
            {
                for (int col = 0; col < 4; ++col)
                {
                    float t = (col + .55f) / 4;
                    bool lit = ((Hash(x + row, y + col) >> 2) % 4) != 0;
                    Color light =
                        lit ? C(seed % 2 ? 0xBDA5D2 : 0xE7BC86, .74f).Emissive(1.1f) : C(0x26374C);
                    Vec2 q(b.x + (c.x - b.x) * t, b.y + (c.y - b.y) * t - 22 - row * 25);
                    Poly({q, Vec2(q.x - 8, q.y + 4), Vec2(q.x - 8, q.y - 5), Vec2(q.x, q.y - 9)},
                         light);
                    Vec2 u(d.x + (c.x - d.x) * t, d.y + (c.y - d.y) * t - 22 - row * 25);
                    Poly({u, Vec2(u.x + 8, u.y + 4), Vec2(u.x + 8, u.y - 5), Vec2(u.x, u.y - 9)},
                         Tint(light, .7f));
                }
            }
        }

        void Lamp(double x, double y)
        {
            Vec2 p = Iso(x, y);
            Ellipse(p.x + 5, p.y, 39, 16, C(0xF4C780, .045f));
            draw->Line(p, Vec2(p.x, p.y - 75), 3, C(0x435B70));
            draw->Line(Vec2(p.x, p.y - 75), Vec2(p.x + 15, p.y - 75), 3, C(0x60748C));
            draw->Circle(p.x + 15, p.y - 73, 11, C(0xFFDDA4, .06f).Emissive(.6f));
            draw->Rect(p.x + 8, p.y - 75, 14, 3, C(0xF1D9B0).Emissive(2.4f));
        }

        void Tree(double x, double y, uint32_t seed)
        {
            Vec2 p = Iso(x, y);
            Ellipse(p.x, p.y + 2, 29, 11, C(0x020D16, .34f));
            draw->Line(p, Vec2(p.x, p.y - 51), 6, C(0x3B4859));
            float h = 48 + float(seed % 18);
            Poly({Vec2(p.x, p.y - h - 36),
                  Vec2(p.x + 31, p.y - 31),
                  Vec2(p.x, p.y - 19),
                  Vec2(p.x - 31, p.y - 31)},
                 C(0x285D64));
            Poly({Vec2(p.x, p.y - h - 36), Vec2(p.x + 31, p.y - 31), Vec2(p.x, p.y - 19)},
                 C(0x368578));
            draw->Line(Vec2(p.x, p.y - h - 30), Vec2(p.x, p.y - 27), 1, C(0x80C6A2, .3f));
        }

        void Crystal(double x, double y, float height = 65)
        {
            Vec2 p = Iso(x, y);
            Ellipse(p.x, p.y, 38, 17, C(0x76E6D2, .035f));
            Poly({Vec2(p.x, p.y - height),
                  Vec2(p.x + 14, p.y - 20),
                  Vec2(p.x, p.y + 2),
                  Vec2(p.x - 14, p.y - 20)},
                 C(0x3EBAAC));
            Poly({Vec2(p.x, p.y - height), Vec2(p.x + 14, p.y - 20), Vec2(p.x, p.y + 2)},
                 C(0x81F1D1).Emissive(2.0f));
            draw->Line(Vec2(p.x, p.y - height), Vec2(p.x, p.y + 2), 1, C(0xE0FFEA).Emissive(2.8f));
            Ring(p.x, p.y + 5, 25, 11, 1, C(0x66E0CB, .45f).Emissive(.7f));
        }

        void Portal()
        {
            Vec2 p = Iso(-2, .8);
            Box(-2, .8, 1.1, .7, 8, C(game->realm ? 0x437B80 : 0x536784));
            Ellipse(p.x, p.y - 45, 24, 42, C(0x70DBC9, .12f));
            Ring(p.x, p.y - 45, 25, 42, 3, C(0x70DBC9, .8f).Emissive(2.2f));
            Ring(p.x, p.y - 45, 31, 49, 1, C(0xC3A5EE, .4f).Emissive(1.0f));
            for (int i = 0; i < 5; ++i)
            {
                float a = float(view->time * .6 + i * 1.3);
                draw->Circle(p.x + std::cos(a) * 26,
                             p.y - 45 + std::sin(a) * 43,
                             2,
                             C(0xD2FFF0).Emissive(2.5f));
            }
            Label(p.x - 37,
                  p.y + 13,
                  game->realm ? L"도시로 연결" : L"LUMIA 접속",
                  12,
                  C(0xA7E5DD));
        }

        void Cafe()
        {
            Box(CafeX, CafeY, CafeWidth, CafeDepth, 70, C(0x57627A));
            Box(CafeX, CafeY, CafeRoofWidth, CafeRoofDepth, 7, C(0x8E797B), 70);
            Vec2 p = Iso(CafeX + CafeWidth * .5, CafeY + CafeDepth * .5, 27);
            Poly({Vec2(p.x - 62, p.y - 30),
                  Vec2(p.x, p.y + 1),
                  Vec2(p.x, p.y - 23),
                  Vec2(p.x - 62, p.y - 54)},
                 C(0xC9B6A1));
            Vec2 sign = Iso(CafeX, CafeY, 91);
            draw->RoundedRect(sign.x - 63, sign.y - 12, 126, 29, 4, C(0x1D2F42));
            Label(sign.x - 49, sign.y - 9, L"달빛 우체국", 17, C(0xF0D2AD), 600);
            Vec2 table = Iso(1.0, -1.8);
            Ellipse(table.x, table.y - 15, 21, 10, C(0xB29580));
            draw->Line(Vec2(table.x, table.y - 15), Vec2(table.x, table.y + 2), 3, C(0x596172));
        }

        void Planter(double x, double y)
        {
            Box(x, y, 1.25, .6, 14, C(0x69798C));
            Box(x, y, 1.08, .48, 3, C(0x304B4E), 14);
            for (int i = 0; i < 5; ++i)
            {
                Vec2 p = Iso(x - .4 + i * .2, y, 25);
                Ellipse(p.x, p.y, 10, 9, C(i % 2 ? 0x5C8E7B : 0x477866));
            }
            Vec2 light = Iso(x, y + .32, 7);
            draw->Line(Vec2(light.x - 23, light.y - 11),
                       Vec2(light.x + 23, light.y + 11),
                       2,
                       C(0xE0C094, .6f));
        }

        void Bench(double x, double y)
        {
            Box(x, y, 1.1, .35, 8, C(0x8E7B75), 9);
            Box(x, y - .16, 1.1, .08, 19, C(0xA19389), 17);
            Box(x - .37, y, .08, .3, 9, C(0x43556B));
            Box(x + .37, y, .08, .3, 9, C(0x43556B));
        }

        void DiscoveryMarker(int cx, int cy)
        {
            double x = cx * 8 + 2.5;
            double y = cy * 8 + 2.5;
            if (Distance(game->x, game->y, x, y) > 11)
            {
                return;
            }
            Vec2 p = Iso(x, y);
            bool found = game->HasDiscovery(game->realm, cx, cy);
            Box(x, y, .45, .45, 16, C(found ? 0x47697B : (game->realm ? 0x547F89 : 0x87776F)));
            Ring(p.x, p.y - 27, 12, 5, 1, C(found ? 0x9CAEB9 : 0xE7C68C, .8f));
            draw->Circle(p.x,
                         p.y - 31 + float(std::sin(view->time * 2) * 3),
                         3,
                         C(found ? 0x9CAEB9 : 0xFFE5AA).Emissive(found ? .35f : 2.0f));
            if (Distance(game->x, game->y, x, y) < 2.4)
            {
                Label(p.x - 68,
                      p.y + 17,
                      found ? L"발견한 기억 보관함" : L"[E] 이름 없는 기록",
                      13,
                      C(0xECD8B0));
            }
        }

        void World()
        {
            bool lumia = game->realm == 1;
            for (int row = 0; row < 18; ++row)
            {
                draw->Rect(
                    0,
                    row * 50.f,
                    1440,
                    50,
                    C(lumia ? (row < 6 ? 0x101D30 : 0x101F2C) : (row < 6 ? 0x192239 : 0x17283A)));
            }
            for (int i = 0; i < 27; ++i)
            {
                float bx = i * 62.f - 100;
                float by = 260 + float(Hash(i, 0) % 70);
                draw->Rect(bx,
                           by - 90 - float(Hash(i, 2) % 130),
                           40 + float(Hash(i, 1) % 30),
                           230,
                           C(lumia ? 0x213047 : 0x26314A, .33f));
            }
            int centerX = int(std::floor(view->cameraX));
            int centerY = int(std::floor(view->cameraY));
            int radius = 15;
            for (int sum = -radius * 2; sum <= radius * 2; ++sum)
            {
                for (int dx = -radius; dx <= radius; ++dx)
                {
                    int dy = sum - dx;
                    if (dy < -radius || dy > radius)
                    {
                        continue;
                    }
                    int x = centerX + dx;
                    int y = centerY + dy;
                    uint32_t seed = Hash(x, y);
                    const bool lot = !lumia && CityLotTile(x, y);
                    const bool plaza = PlazaTile(x, y) && !lot;
                    const bool road = lumia ? RoadGridTile(x, y) : CityRoadTile(x, y);
                    Color tile = lumia ? C(seed % 3 == 0 ? 0x24464B : 0x203E44)
                                       : C(road ? 0x26364A : (lot ? 0x465164 : 0x3A495C));
                    if (lumia && road)
                    {
                        tile = C(0x304C59);
                    }
                    Diamond(x, y, 47.3f, 23.5f, tile);
                    if (plaza)
                    {
                        Diamond(x, y, 45, 22, C(lumia ? 0x315D61 : 0x46566A));
                    }
                    if (road && Mod(x + y, 3) == 0 && !plaza)
                    {
                        draw->Line(Iso(x - .23, y),
                                   Iso(x + .23, y),
                                   2,
                                   C(lumia ? 0x7FD5BB : 0xB7A68B, .45f));
                    }
                    if (lumia && seed % 19 == 0)
                    {
                        Vec2 a = Iso(x, y);
                        draw->Circle(a.x, a.y, 2, C(0x8DE2B8, .5f));
                    }
                }
            }
            Vec2 center = Iso(0, 0);
            const float hubRadiusX = lumia ? 190.07f : 104.0f;
            const float hubRadiusY = lumia ? 95.04f : 52.0f;
            Ring(center.x,
                 center.y,
                 hubRadiusX,
                 hubRadiusY,
                 1,
                 C(lumia ? 0x87D5C6 : 0xD0BFA4, .5f));
            Ring(center.x,
                 center.y,
                 hubRadiusX + 6,
                 hubRadiusY + 3,
                 1,
                 C(lumia ? 0x87D5C6 : 0xD0BFA4, .15f));
            if (lumia)
            {
                AttackRange();
                Label(center.x - 57, center.y + 105, L"공명 거점 · 안전지대", 12, C(0x91C6BD));
                Vec2 arena = Iso(6.0, 4.0);
                Ring(arena.x, arena.y, 180, 90, 1.5f, C(0xB493CF, .38f).Emissive(.25f));
                Ring(arena.x, arena.y, 170, 85, 1, C(0xB493CF, .16f));
                Label(arena.x - 62, arena.y + 93, L"송신탑의 잔향 · 보스", 12, C(0xB3A4CA));
                for (const auto& monster : game->combat.monsters)
                {
                    if (monster.health > 0 && monster.telegraph > 0
                        && Distance(game->x, game->y, monster.x, monster.y) < 18)
                    {
                        combat_visuals::DrawTelegraph(*draw, monster, Iso(monster.x, monster.y));
                    }
                }
            }

            struct Object
            {
                double depth;
                int kind;
                double x;
                double y;
                int id;
            };

            std::vector<Object> objects;
            for (int x = centerX - radius; x <= centerX + radius; ++x)
            {
                for (int y = centerY - radius; y <= centerY + radius; ++y)
                {
                    if (Distance(game->x, game->y, x, y) > 15)
                    {
                        continue;
                    }
                    if (lumia ? DecorationAnchor(x, y) : CityBuildingAt(x, y))
                    {
                        objects.push_back({double(x + y), lumia ? 1 : 0, double(x), double(y), 0});
                    }
                    if (Mod(x, 6) == 1 && Mod(y, 6) == 1 && (std::abs(x) > 3 || std::abs(y) > 3))
                    {
                        objects.push_back({double(x + y), lumia ? 2 : 3, double(x), double(y), 0});
                    }
                }
            }
            if (Distance(game->x, game->y, -2, .8) < 18)
            {
                objects.push_back({-1.2, 4, -2, .8, 0});
            }
            if (!lumia && Distance(game->x, game->y, CafeX, CafeY) < 18)
            {
                objects.push_back({CafeX + CafeY, 5, CafeX, CafeY, 0});
            }
            if (!lumia && Distance(game->x, game->y, 1, -1.3) < 18)
            {
                objects.push_back({-.3, 6, 1, -1.3, 0});
            }
            if (!lumia)
            {
                objects.push_back({-4, 10, -3, -1, 0});
                objects.push_back({5, 10, 3, 2, 0});
                objects.push_back({1.75, 11, -1.25, 3, 0});
                objects.push_back({-1, 11, 3, -4, 0});
            }
            const auto& anchors = echo::QuestAnchors();

            for (size_t index = 0; index < anchors.size(); ++index)
            {
                const auto& anchor = anchors[index];

                if (anchor.target != echo::QuestTarget::Sea && anchor.realm == game->realm
                    && Distance(game->x, game->y, anchor.x, anchor.y) < 18)
                {
                    objects.push_back(
                        {anchor.x + anchor.y, 14, anchor.x, anchor.y, static_cast<int>(index)});
                }
            }
            int cellX = int(std::floor(game->x / 8));
            int cellY = int(std::floor(game->y / 8));
            for (int x = cellX - 1; x <= cellX + 1; ++x)
            {
                for (int y = cellY - 1; y <= cellY + 1; ++y)
                {
                    objects.push_back({double(x * 8 + y * 8 + 5), 9, double(x), double(y), 0});
                }
            }
            if (lumia)
            {
                for (size_t i = 0; i < game->combat.monsters.size(); ++i)
                {
                    const auto& monster = game->combat.monsters[i];
                    if (monster.health > 0 && Distance(game->x, game->y, monster.x, monster.y) < 18)
                    {
                        objects.push_back(
                            {monster.x + monster.y, 12, monster.x, monster.y, int(i)});
                    }
                }
                for (size_t i = 0; i < game->combat.drops.size(); ++i)
                {
                    const auto& drop = game->combat.drops[i];
                    if (Distance(game->x, game->y, drop.x, drop.y) < 18)
                    {
                        objects.push_back({drop.x + drop.y, 13, drop.x, drop.y, int(i)});
                    }
                }
                for (size_t i = 0; i < game->echoes.size(); ++i)
                {
                    const auto& e = game->echoes[i];
                    if (Distance(game->x, game->y, e.x, e.y) < 16)
                    {
                        objects.push_back({e.x + e.y, 8, e.x, e.y, int(i)});
                    }
                }
            }
            objects.push_back({game->x + game->y, 7, game->x, game->y, 0});
            std::stable_sort(objects.begin(),
                             objects.end(),
                             [](const Object& a, const Object& b)
                             {
                                 return a.depth < b.depth;
                             });
            for (const auto& object : objects)
            {
                Vec2 p = Iso(object.x, object.y);
                switch (object.kind)
                {
                case 0:
                    Building(int(object.x), int(object.y));
                    break;
                case 1:
                    Tree(object.x, object.y, Hash(int(object.x), int(object.y)));
                    break;
                case 2:
                    Crystal(object.x,
                            object.y,
                            60 + float(Hash(int(object.x), int(object.y)) % 35));
                    break;
                case 3:
                    Lamp(object.x, object.y);
                    break;
                case 4:
                    Portal();
                    break;
                case 5:
                    Cafe();
                    break;
                case 6:
                    SeaAvatar(p);
                    Label(p.x - 13, p.y - 78, L"세아", 14, C(0xE6D5EE));
                    if (game->quests.Tracked().target == echo::QuestTarget::Sea)
                    {
                        Ring(p.x, p.y + 3, 20, 8, 1.5f, C(0xF0D18B, .8f));
                        draw->Circle(p.x, p.y - 94, 7, C(0xF0D18B));
                        Label(p.x - 2, p.y - 102, L"!", 12, C(0x273241), 700);
                    }
                    if (Distance(game->x, game->y, 1, -1.3) < 2)
                    {
                        Label(p.x - 30, p.y + 15, L"[E] 대화", 12, C(0xD2C2DF));
                    }
                    break;
                case 7:
                    Character(object.x, object.y, C(0xD78376), true);
                    if (view->guarding)
                    {
                        Ring(p.x, p.y - 21, 25, 31, 2, C(0x96EACB, .7f).Emissive(.6f));
                    }
                    break;
                case 8:
                {
                    const auto& e = game->echoes[object.id];
                    Color color = e.protection >= e.aggression ? C(0x86DDD0) : C(0xDAA1B0);
                    Character(object.x, object.y, color, false, true, e.id);
                    Label(p.x - 27, p.y - 91, L"유실체 " + std::to_wstring(e.id), 13, color);
                    if (game->quests.Tracked().target == echo::QuestTarget::Echo)
                    {
                        draw->Circle(p.x, p.y - 105, 7, C(0xF0D18B));
                        Label(p.x - 2, p.y - 113, L"!", 12, C(0x273241), 700);
                    }
                    if (Distance(game->x, game->y, e.x, e.y) < 2)
                    {
                        Label(p.x - 25, p.y + 18, L"[E] 교감", 12, color);
                    }
                    break;
                }
                case 9:
                    DiscoveryMarker(int(object.x), int(object.y));
                    break;
                case 10:
                    Planter(object.x, object.y);
                    break;
                case 11:
                    Bench(object.x, object.y);
                    break;
                case 12:
                {
                    const auto& monster = game->combat.monsters[object.id];
                    combat_visuals::DrawMonster(*draw,
                                                monster,
                                                p,
                                                view->time,
                                                monster.id == game->combat.targetId);
                    break;
                }
                case 13:
                    combat_visuals::DrawDrop(*draw, game->combat.drops[object.id], p, view->time);
                    break;
                case 14:
                    QuestObject(anchors[object.id]);
                    break;
                }
            }
            if (lumia)
            {
                for (const auto& projectile : game->combat.projectiles)
                {
                    std::vector<Vec2> projectedTrail;
                    projectedTrail.reserve(projectile.trail.size() + 1);

                    for (const auto& point : projectile.trail)
                    {
                        projectedTrail.push_back(Iso(point.x, point.y));
                    }

                    projectedTrail.push_back(Iso(projectile.x, projectile.y));
                    combat_visuals::DrawProjectile(*draw, projectile, projectedTrail, view->time);
                }
                for (int i = 0; i < 65; ++i)
                {
                    float x = float(Hash(i, 4) % 1440);
                    float y = float(std::fmod(Hash(i, 9) % 900 + view->time * (3 + i % 5), 900));
                    draw->Circle(x, y, i % 5 == 0 ? 2.f : 1.f, C(0xA8E6D1, .14f + (i % 4) * .06f));
                }
            }
        }

        void Minimap()
        {
            float x = 1164;
            float y = 653;
            Panel(x, y, 244, 153);
            Label(x + 17, y + 13, L"주변의 흔적", 13, C(0xAFC2D0), 600);
            for (int i = -3; i <= 3; ++i)
            {
                for (int j = -3; j <= 3; ++j)
                {
                    float px = x + 122 + (i - j) * 13;
                    float py = y + 88 + (i + j) * 6;
                    int wx = int(std::floor(game->x)) + i;
                    int wy = int(std::floor(game->y)) + j;
                    const bool road =
                        game->realm == 0 ? CityRoadTile(wx, wy) : RoadGridTile(wx, wy);
                    const bool lot = game->realm == 0 && CityLotTile(wx, wy);
                    Poly({Vec2(px, py - 5), Vec2(px + 11, py), Vec2(px, py + 5), Vec2(px - 11, py)},
                         C(road ? 0x355267 : (lot ? 0x3B4B60 : 0x223B4C)));
                }
            }
            auto marker = [&](double wx, double wy, Color c)
            {
                double dx = wx - game->x;
                double dy = wy - game->y;
                if (std::abs(dx) > 3.2 || std::abs(dy) > 3.2)
                {
                    return;
                }
                draw->Circle(float(x + 122 + (dx - dy) * 13), float(y + 88 + (dx + dy) * 6), 3, c);
            };
            marker(-2, .8, C(0xC9B4E2));
            for (const auto& anchor : echo::QuestAnchors())
            {
                if (anchor.realm == game->realm)
                {
                    marker(anchor.x,
                           anchor.y,
                           game->quests.Tracked().target == anchor.target
                               ? C(0xF0D18B)
                               : QuestColor(anchor.target));
                }
            }
            if (game->realm == 1)
            {
                for (const auto& monster : game->combat.monsters)
                {
                    if (monster.health > 0)
                    {
                        marker(monster.x, monster.y, C(monster.boss ? 0xE99EB7 : 0xCF997A));
                    }
                }
                for (const auto& drop : game->combat.drops)
                {
                    marker(drop.x, drop.y, C(0xE9D38E));
                }
                for (const auto& life : game->echoes)
                {
                    marker(life.x, life.y, C(0x9AEAD8));
                }
            }
            for (const auto& found : game->discoveries)
            {
                if (found.realm == game->realm)
                {
                    marker(found.cellX * 8 + 2.5, found.cellY * 8 + 2.5, C(0xF1D093));
                }
            }
            draw->Circle(x + 122, y + 88, 4, C(0x9AEAD8));
            Label(x + 17, y + 126, L"미지의 구역은 계속 이어집니다", 11, C(0x718C9D));
        }

        void QuestText(float x,
                       float y,
                       const std::wstring& text,
                       float size,
                       Color color,
                       double columns,
                       size_t maximumLines,
                       int weight = 400)
        {
            std::vector<std::wstring> lines;
            std::wstring line;
            double width = 0.0;

            for (wchar_t character : text)
            {
                const double nextWidth = character < 0x100 ? .55 : 1.0;

                if (character == L'\n' || (!line.empty() && width + nextWidth > columns))
                {
                    lines.push_back(line);
                    line.clear();
                    width = 0.0;
                }

                if (character != L'\n')
                {
                    line.push_back(character);
                    width += nextWidth;
                }
            }

            if (!line.empty())
            {
                lines.push_back(line);
            }

            for (size_t index = 0; index < std::min(lines.size(), maximumLines); ++index)
            {
                std::wstring visible = lines[index];

                if (index + 1 == maximumLines && lines.size() > maximumLines)
                {
                    if (!visible.empty())
                    {
                        visible.pop_back();
                    }
                    visible += L"…";
                }

                Label(x, y + static_cast<float>(index) * (size + 7), visible, size, color, weight);
            }
        }

        std::wstring QuestDestination(const echo::QuestEntry& entry)
        {
            if (entry.realm != game->realm && entry.target != echo::QuestTarget::None)
            {
                return entry.realm == 1 ? L"[TAB] 루미아에서 계속하기"
                                        : L"[TAB] 현실 도시로 돌아가기";
            }

            for (const auto& anchor : echo::QuestAnchors())
            {
                if (anchor.target == entry.target)
                {
                    return anchor.name + L"  ·  거리 "
                           + std::to_wstring(static_cast<int>(
                               std::ceil(Distance(game->x, game->y, anchor.x, anchor.y))))
                           + L"  ·  (" + std::to_wstring(static_cast<int>(anchor.x)) + L", "
                           + std::to_wstring(static_cast<int>(anchor.y)) + L")";
                }
            }

            if (entry.target == echo::QuestTarget::Echo && !game->echoes.empty())
            {
                float distance = 1.0e8f;

                for (const auto& life : game->echoes)
                {
                    distance = std::min(distance, Distance(game->x, game->y, life.x, life.y));
                }

                return L"가까운 유실체  ·  거리 "
                       + std::to_wstring(static_cast<int>(std::ceil(distance)));
            }

            return L"[K] 퀘스트에서 다음 여정 확인";
        }

        void QuestTracker()
        {
            const auto quest = game->quests.Tracked();
            const Color accent = C(0xE8CB8D);
            Panel(32, 156, 250, 194);
            Label(50, 172, L"현재 여정", 12, accent, 600);
            Label(223, 172, L"[K]", 12, C(0xB6C5C5));
            QuestText(50, 197, quest.title, 17, C(0xE6ECE8), 12.0, 2, 600);
            QuestText(50, 247, quest.objective, 12, C(0xB8C8CE), 17.5, 2);
            QuestText(50, 294, QuestDestination(quest), 10, C(0xC8BFA6), 21.5, 2);
            draw->Rect(50, 332, 212, 2, C(0x3A4C55));
            const float fraction =
                quest.required > 0
                    ? std::clamp(static_cast<float>(quest.progress) / quest.required, 0.0f, 1.0f)
                    : 0.0f;
            draw->Rect(50, 332, 212 * fraction, 2, accent);
        }

        std::wstring QuestRewardText(const echo::QuestEntry& entry)
        {
            std::wstring result;

            if (entry.rewardXP > 0)
            {
                result = L"경험 " + std::to_wstring(entry.rewardXP);
            }

            if (entry.rewardFragments > 0)
            {
                result += (result.empty() ? L"" : L" · ") + std::wstring(L"파편 ")
                          + std::to_wstring(entry.rewardFragments);
            }

            if (entry.rewardCores > 0)
            {
                result += (result.empty() ? L"" : L" · ") + std::wstring(L"공명핵 ")
                          + std::to_wstring(entry.rewardCores);
            }

            return result.empty() ? L"이야기와 관계의 기록" : result;
        }

        void QuestJournal()
        {
            const int tab = std::clamp(view->questTab, 0, 2);
            const auto entries = game->quests.Entries(tab);
            const auto tracked = game->quests.Tracked();
            const wchar_t* tabs[] = {L"[1] 메인 퀘스트", L"[2] 서브 퀘스트", L"[3] 일일 퀘스트"};
            const int today = echo::QuestState::CurrentDay();
            const std::wstring date = std::to_wstring(today / 10000) + L"."
                                      + std::to_wstring((today / 100) % 100) + L"."
                                      + std::to_wstring(today % 100);
            draw->Rect(0, 0, 1440, 900, C(0x020813, .74f));
            Panel(230, 115, 980, 690, .99f);
            Label(265, 140, L"우리의 여정을 따라", 28, C(0xE6EEE8), 600);
            Label(265,
                  182,
                  tab == 2 ? date + L" · 한국 시간 매일 00:00에 일일 의뢰가 새로 시작됩니다."
                           : L"현재 목표를 확인하고, 인물과 기록을 직접 찾아 이야기를 이어가세요.",
                  13,
                  C(0x93AEBB));

            for (int index = 0; index < 3; ++index)
            {
                const float left = 265.0f + index * 305.0f;
                draw->RoundedRect(left, 215, 285, 35, 6, C(index == tab ? 0x354E59 : 0x152D3D));
                Label(left + 17, 223, tabs[index], 14, C(index == tab ? 0xEAD39E : 0x819EAC), 600);
            }

            for (size_t index = 0; index < std::min<size_t>(entries.size(), 5); ++index)
            {
                const auto& entry = entries[index];
                const float top = 267.0f + static_cast<float>(index) * 88.0f;
                const bool current = entry.title == tracked.title;
                const Color accent =
                    entry.claimable ? C(0xF1D496) : (entry.complete ? C(0x91CDB5) : C(0xCBD9DF));
                draw->RoundedRect(265, top, 910, 80, 7, C(current ? 0x203C48 : 0x112838));
                draw->Rect(265, top + 9, 3, 62, accent);
                QuestText(280, top + 9, entry.title, 17, accent, 37, 1, 600);
                const std::wstring status =
                    entry.claimable
                        ? L"보상 받기 가능"
                        : (entry.complete ? L"완료"
                                          : ((current || entry.progress > 0 || tab == 2)
                                                 ? L"진행 중"
                                                 : (tab == 0 ? L"다음 이야기" : L"미완료")));
                Label(1031, top + 11, status, 12, accent, 600);
                QuestText(280, top + 36, entry.objective, 13, C(0xADC2CC), 57.0, 1);
                QuestText(280, top + 60, entry.detail, 10, C(0x6F91A3), 54.0, 1);
                std::wstring progress =
                    std::to_wstring(entry.progress) + L" / " + std::to_wstring(entry.required);
                Label(1088, top + 38, progress, 11, C(0xC9BEA6));
                QuestText(861, top + 60, QuestRewardText(entry), 10, C(0xBDAC8E), 29.0, 1);
            }

            Label(265,
                  732,
                  tab == 2 ? L"[F] 받을 수 있는 일일 보상 받기"
                           : L"목표의 인물과 대화하거나 장소를 조사하면 진행과 보상이 기록됩니다.",
                  13,
                  C(0xC9B993));
            Label(265,
                  767,
                  L"[1 / 2 / 3] 분류 선택     [K / ESC] 여정으로 돌아가기",
                  13,
                  C(0x93B5AE));
        }

        void Hud()
        {
            if (view->dialogueOpen && view->dialogue.CinematicActive())
            {
                narrative::Draw(*draw, view->dialogue, view->time);
                return;
            }
            bool lumia = game->realm == 1;
            Color accent = lumia ? C(0x86E4CD) : C(0xD9BBA0);
            draw->Rect(0, 0, 1440, 5, accent);
            Label(33, 24, L"E C H O", 19, C(0x9AB3C8), 600);
            Label(31, 53, L"잔향의 법칙", 37, C(0xEDF0F0), 700);
            Label(34, 103, L"존재의 대가  /  PROTOTYPE 01", 11, C(0x849BB1));
            Panel(520, 25, 397, 62);
            draw->Circle(543, 53, 4, accent);
            Label(557,
                  36,
                  lumia ? L"LUMIA  /  가상세계" : L"REALITY  /  현실 도시",
                  17,
                  C(0xDCE6E9),
                  600);
            Label(557,
                  63,
                  lumia ? L"잔향 정원 · 위험도 "
                              + std::to_wstring(
                                  echo::CombatWorld::ZoneLevel(int(std::floor(game->x / 12)),
                                                               int(std::floor(game->y / 12))))
                        : L"2050  ·  해온시 달빛 지구",
                  10,
                  C(0x8CA7B8));
            Panel(1164, 26, 244, 62);
            Label(1182,
                  37,
                  lumia ? L"[TAB]  현실로 돌아가기" : L"[TAB]  루미아로 접속",
                  14,
                  accent,
                  600);
            Label(1182, 64, L"[K] 퀘스트  [J] 기록  [F1] 도움말", 10, C(0x8CA1B5));
            if (!view->dialogueOpen && !view->journalOpen && !view->helpOpen && !view->questsOpen)
            {
                QuestTracker();
            }
            Panel(32, 673, 250, 133);
            Label(50, 688, L"내가 남기는 것", 13, C(0xB7C7D1), 600);
            Label(50, 716, game->ProfileName(), 18, accent, 600);
            float total = std::max(1.f, game->protection + game->aggression + game->curiosity);
            float start = 50;
            Color colors[] = {C(0x8DE1C8), C(0xC794AB), C(0xB8B5E9)};
            float values[] = {game->protection, game->aggression, game->curiosity};
            for (int i = 0; i < 3; ++i)
            {
                float width = 212 * values[i] / total;
                draw->Rect(start, 753, width, 4, colors[i]);
                start += width;
            }
            Label(50,
                  774,
                  L"탄생 " + std::to_wstring(game->echoes.size()) + L"   /   발견 "
                      + std::to_wstring(game->discoveries.size()),
                  11,
                  C(0x8DA4B6));
            Panel(1164, 156, 244, 257);
            draw->Circle(1184, 179, 3, C(game->broadcasting ? 0xED938C : 0x6D8195));
            Label(1197,
                  168,
                  game->broadcasting ? L"LIVE  ·  달빛 탐사 방송" : L"방송 대기 중",
                  13,
                  C(0xDDE5E9),
                  600);
            Label(1182,
                  203,
                  game->broadcasting ? L"오늘도 같이 걷는 중" : L"[B] 방송 시작하기",
                  11,
                  C(0x8EA5B8));
            draw->Rect(1182, 232, 208, 1, C(0x768C9E, .19f));
            Label(1182, 249, L"달콩  여기 분위기 너무 좋다", 12, C(0xA8C8CB));
            Label(1182,
                  280,
                  lumia ? L"새벽별  붉은 원 밖으로 피해요!" : L"새벽별  세아님 또 길 잃으심?",
                  11,
                  C(0xB9B1D5));
            Label(1182, 310, L"세아  ...지름길 찾는 중이거든?", 11, C(0xD5BFAF));
            Label(1182,
                  342,
                  game->echoes.empty() ? L"익명  저 문, 어제도 있었나요?"
                                       : L"익명  저 아이, 움직임이 닮았네",
                  11,
                  C(0xA8B9C9));
            Label(1182, 383, L"일상도 모험도, 함께라서.", 10, C(0x667E90));
            Minimap();
            Panel(313, 829, 815, 48);
            KeyCap(328, 840, L"W", L"ASD 이동");
            KeyCap(466, 840, L"E", L"대화·조사");
            KeyCap(608, 840, L"B", L"방송");
            KeyCap(722, 840, L"Q", L"보호");
            KeyCap(837, 840, L"R", L"사망 체험");
            KeyCap(1000, 840, L"J", L"기록");
            Label(34, 849, L"한 번의 죽음, 하나의 생명.", 12, C(0x839DAC));
            std::wostringstream coords;
            coords << int(std::floor(game->x)) << L" : " << int(std::floor(game->y));
            Label(1178, 839, coords.str(), 16, C(0xA8B9C5));
            Label(1178, 864, L"[F5] 저장   [ESC] 닫기", 11, C(0x718B9C));
            if (!game->message.empty() && !view->dialogueOpen && !view->journalOpen
                && !view->helpOpen && !view->questsOpen)
            {
                Panel(350, 741, 775, 51, .94f);
                Label(370, 755, game->message, 13, C(0xCCDFDD));
            }
            if (lumia)
            {
                combat_visuals::DrawHud(*draw, game->combat, game->x, game->y);
            }
            if (view->deathFlash > 0)
            {
                draw->Rect(0, 0, 1440, 900, C(0x9BE8DD, view->deathFlash * .18f));
            }
            if (view->questsOpen)
            {
                QuestJournal();
                return;
            }
            if (view->dialogueOpen)
            {
                narrative::Draw(*draw, view->dialogue, view->time);
            }
            if (view->journalOpen || view->helpOpen)
            {
                draw->Rect(0, 0, 1440, 900, C(0x020813, .58f));
                Panel(385, 175, 670, 550, .99f);
                Label(420,
                      207,
                      view->helpOpen ? L"첫 번째 여정" : L"세계가 기억하는 이야기",
                      28,
                      C(0xE4EEE9),
                      600);
                Label(420,
                      260,
                      view->helpOpen
                          ? L"WASD / 방향키   이동     SHIFT   빠르게 걷기"
                          : L"사망 기록 " + std::to_wstring(game->deaths) + L"회  ·  태어난 생명 "
                                + std::to_wstring(game->echoes.size()),
                      16,
                      C(0xAEC7CE));
                Label(420,
                      306,
                      view->helpOpen ? L"TAB   현실과 루미아 전환"
                                     : L"현실과 루미아의 발견 기록 "
                                           + std::to_wstring(game->discoveries.size()) + L"개",
                      16,
                      C(0xAEC7CE));
                Label(420,
                      352,
                      view->helpOpen ? L"E   가까운 인물·유실체·기록과 상호작용"
                                     : L"현재 성향  ·  " + game->ProfileName(),
                      16,
                      C(0xAEC7CE));
                Label(420,
                      398,
                      view->helpOpen ? L"사거리 안의 적 자동 공격   Q 유지 보호"
                                     : L"한 번 태어난 생명과 발견한 기록은 저장됩니다.",
                      16,
                      C(0xAEC7CE));
                Label(420,
                      444,
                      view->helpOpen
                          ? L"R   루미아에서 사망·탄생을 체험"
                          : L"공명 Lv." + std::to_wstring(game->combat.growth.level) + L"  ·  처치 "
                                + std::to_wstring(game->combat.growth.kills) + L"  ·  보스 "
                                + std::to_wstring(game->combat.growth.bosses),
                      16,
                      C(0xAEC7CE));
                Label(420,
                      490,
                      view->helpOpen ? L"B 방송   K 퀘스트   J 기록   4 / 5 / 6 공명 강화"
                                     : L"“네가 가고 싶은 곳부터 가자.”",
                      16,
                      C(0xAEC7CE));
                Label(420,
                      548,
                      view->helpOpen ? L"F5   저장     F1   도움말     ESC   닫기·종료"
                                     : L"지금의 행동이 다음 생명의 성향이 됩니다.",
                      15,
                      C(0xCDBDAA));
                if (view->helpOpen)
                {
                    Label(420,
                          588,
                          L"F2 후처리 전체   F3 블룸   F4 비넷   F6 가장자리 블러",
                          14,
                          C(0xA4C4CB));
                }
                Label(420, 640, L"[ESC] 여정으로 돌아가기", 14, C(0x7FA99F));
            }
        }
    } // namespace

    bool IsBlocked(double x, double y, int realm)
    {
        if (realm != 0)
        {
            return false;
        }
        const int bx = int(std::round(x / BuildingSpacing)) * BuildingSpacing;
        const int by = int(std::round(y / BuildingSpacing)) * BuildingSpacing;
        if (CityBuildingAt(bx, by) && std::abs(x - bx) < BuildingWidth * .5 + PlayerClearance
            && std::abs(y - by) < BuildingDepth * .5 + PlayerClearance)
        {
            return true;
        }
        return std::abs(x - CafeX) < CafeWidth * .5 + PlayerClearance
               && std::abs(y - CafeY) < CafeDepth * .5 + PlayerClearance;
    }

    void Render(Renderer& renderer, const echo::GameState& state, const View& currentView)
    {
        draw = &renderer;
        game = &state;
        view = &currentView;
        World();
        renderer.BeginOverlay();
        Hud();
    }
} // namespace prototype
