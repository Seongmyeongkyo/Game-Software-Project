#include "stdafx.h"
#include "Combat.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
    constexpr int MaximumLevel = 1000;
    constexpr int MaximumCounter = 1000000000;
    constexpr size_t MaximumMonsters = 100000;
    constexpr size_t MaximumDrops = 250000;
    constexpr double ZoneSize = 12.0;
    constexpr double HubRadius = 2.8;

    int AddBounded(int value, int amount)
    {
        return int(std::clamp(static_cast<long long>(value) + amount,
                              0LL,
                              static_cast<long long>(MaximumCounter)));
    }

    double Distance(double ax, double ay, double bx, double by)
    {
        return std::hypot(ax - bx, ay - by);
    }

    uint64_t Zigzag(int value)
    {
        const int64_t wide = value;
        return uint64_t(wide >= 0 ? wide * 2 : -wide * 2 - 1);
    }

    uint64_t MonsterId(int zoneX, int zoneY, int slot)
    {
        // The prototype's +/-1e7 coordinate limit fits each signed zone in 22 bits.
        return (Zigzag(zoneX) << 25) | (Zigzag(zoneY) << 3) | uint64_t(slot + 1);
    }

    void RecordProjectileTrail(echo::Projectile& projectile)
    {
        constexpr double spacing = 0.08;
        constexpr size_t maximumSamples = 23;
        const double dx = projectile.x - projectile.previousX;
        const double dy = projectile.y - projectile.previousY;
        const double distance = std::hypot(dx, dy);

        if (distance <= 0.000001)
        {
            return;
        }

        double consumed = 0.0;
        double needed = spacing - projectile.trailSampleDistance;

        while (consumed + needed <= distance)
        {
            consumed += needed;
            const double fraction = consumed / distance;
            projectile.trail.push_back(
                {projectile.previousX + dx * fraction, projectile.previousY + dy * fraction});
            projectile.trailSampleDistance = 0.0;
            needed = spacing;
        }

        projectile.trailSampleDistance =
            std::clamp(projectile.trailSampleDistance + distance - consumed, 0.0, spacing);

        if (projectile.trail.size() > maximumSamples)
        {
            projectile.trail.erase(projectile.trail.begin(),
                                   projectile.trail.end() - maximumSamples);
        }
    }
} // namespace

namespace echo
{
    CombatStats CombatWorld::Stats() const
    {
        const int level = std::clamp(growth.level, 1, MaximumLevel);
        const int focus = std::clamp(growth.focusRank, 0, 30);
        const int magnet = std::clamp(growth.magnetRank, 0, 12);
        const int vitality = std::clamp(growth.vitalityRank, 0, 20);
        CombatStats result;
        result.maxHealth = 100.0f + 6.0f * (level - 1) + 15.0f * vitality;
        result.damage = 10.0f + 2.5f * (level - 1) + 2.0f * focus;
        result.cooldown = std::max(0.18f, 0.75f / (1.0f + 0.045f * (level - 1) + 0.07f * focus));
        result.range = 6.0f + std::min(3.0f, 0.035f * (level - 1));
        result.pickupRadius = 1.5f + 0.35f * magnet;
        result.moveSpeed = 3.0f + 0.03f * std::min(15, level - 1);
        return result;
    }

    int CombatWorld::RequiredXP() const
    {
        const int level = std::clamp(growth.level, 1, MaximumLevel);
        return 40 + 20 * level + 3 * (level - 1) * (level - 1);
    }

    int CombatWorld::ZoneLevel(int zx, int zy)
    {
        const double distance = std::hypot(double(zx), double(zy));
        return int(std::clamp(1.0 + std::floor(distance * 0.75), 1.0, 50.0));
    }

    std::wstring CombatWorld::MonsterName(int kind)
    {
        switch (kind)
        {
        case 0:
            return L"잡음 편린";
        case 1:
            return L"미련의 파수체";
        case 2:
            return L"끊어진 가면";
        case 3:
            return L"마지막 송신자";
        default:
            return L"손상된 기록";
        }
    }

    void CombatWorld::EnsureNearbyZones(double px, double py)
    {
        if (!std::isfinite(px) || !std::isfinite(py) || std::abs(px) >= 1.0e7
            || std::abs(py) >= 1.0e7)
        {
            return;
        }

        const int centerX = int(std::floor(px / ZoneSize));
        const int centerY = int(std::floor(py / ZoneSize));
        const double offsetsX[] = {3.0, 4.0, 5.0, 6.0};
        const double offsetsY[] = {1.5, -1.0, 2.0, 4.0};

        for (int zx = centerX - 1; zx <= centerX + 1; ++zx)
        {
            for (int zy = centerY - 1; zy <= centerY + 1; ++zy)
            {
                const bool exists =
                    std::any_of(monsters.begin(),
                                monsters.end(),
                                [zx, zy](const Monster& monster)
                                {
                                    return monster.zoneX == zx && monster.zoneY == zy;
                                });
                if (exists)
                {
                    continue;
                }

                if (monsters.size() + 4 > MaximumMonsters)
                {
                    announcement =
                        L"프로토타입 지역 보존 한계에 도달했습니다. 기존 구역은 유지됩니다.";
                    return;
                }

                // A neighbour at the numeric world limit must not create invalid save coordinates.
                bool validHomes = true;
                for (int slot = 0; slot < 4; ++slot)
                {
                    if (std::abs(zx * ZoneSize + offsetsX[slot]) >= 1.0e7
                        || std::abs(zy * ZoneSize + offsetsY[slot]) >= 1.0e7)
                    {
                        validHomes = false;
                    }
                }
                if (!validHomes)
                {
                    continue;
                }

                for (int slot = 0; slot < 4; ++slot)
                {
                    Monster monster;
                    monster.id = MonsterId(zx, zy, slot);
                    monster.zoneX = zx;
                    monster.zoneY = zy;
                    monster.slot = slot;
                    monster.kind = slot;
                    monster.level = ZoneLevel(zx, zy);
                    monster.boss = slot == 3;
                    monster.homeX = zx * ZoneSize + offsetsX[slot];
                    monster.homeY = zy * ZoneSize + offsetsY[slot];
                    monster.x = monster.homeX;
                    monster.y = monster.homeY;
                    monster.maxHealth = monster.boss
                                            ? 140.0f + 24.0f * (monster.level - 1)
                                            : 32.0f + 8.0f * slot + 11.0f * (monster.level - 1);
                    monster.health = monster.maxHealth;
                    monster.attackTimer = 1.0f;
                    monsters.push_back(monster);
                }
                dirty = true;
            }
        }
    }

    void CombatWorld::AddExperience(int amount)
    {
        if (amount <= 0)
        {
            return;
        }

        const float previousMaximum = Stats().maxHealth;
        const int previousLevel = growth.level;
        growth.xp = AddBounded(growth.xp, amount);

        while (growth.level < MaximumLevel && growth.xp >= RequiredXP())
        {
            growth.xp -= RequiredXP();
            ++growth.level;
        }

        if (growth.level >= MaximumLevel)
        {
            growth.level = MaximumLevel;
            growth.xp = 0;
        }

        if (growth.level > previousLevel)
        {
            health =
                std::min(Stats().maxHealth, health + Stats().maxHealth - previousMaximum + 20.0f);
            announcement = L"공명 레벨 " + std::to_wstring(growth.level)
                           + L" · 잔향탄 위력·공격 속도·생명력이 성장했습니다.";
        }
        dirty = true;
    }

    void CombatWorld::DropLoot(double x, double y, int kind, int amount)
    {
        if (drops.size() >= MaximumDrops)
        {
            announcement =
                L"프로토타입 아이템 보존 한계에 도달했습니다. 남겨진 아이템을 먼저 수집해주세요.";
            return;
        }
        if (nextDropId == (std::numeric_limits<uint64_t>::max)())
        {
            announcement = L"아이템 기록 한계에 도달했습니다. 기존 아이템은 보존됩니다.";
            return;
        }

        LootDrop drop;
        drop.id = nextDropId++;
        drop.x = x;
        drop.y = y;
        drop.kind = kind;
        drop.amount = amount;
        drops.push_back(drop);
        dirty = true;
    }

    void CombatWorld::Defeat(Monster& monster)
    {
        monster.health = 0.0f;
        monster.telegraph = 0.0f;
        monster.attackTimer = 0.0f;
        monster.respawnTimer = monster.boss ? 0.0f : 25.0f;
        growth.kills = AddBounded(growth.kills, 1);
        const int experience =
            monster.boss ? 110 + monster.level * 10 : 16 + monster.kind * 4 + monster.level * 2;
        const int fragments =
            monster.boss ? 25 + monster.level * 4 : 3 + monster.kind + monster.level;
        announcement =
            MonsterName(monster.kind) + L" 정화 · 공명 경험 +" + std::to_wstring(experience);
        AddExperience(experience);
        DropLoot(monster.x, monster.y, 0, fragments);

        if (monster.boss)
        {
            growth.bosses = AddBounded(growth.bosses, 1);
            DropLoot(monster.x + 0.3, monster.y - 0.2, 1, 1);
            DropLoot(monster.x - 0.3, monster.y + 0.2, 2, 35);
            if (announcement.find(L"공명 레벨") == std::wstring::npos)
            {
                announcement =
                    L"마지막 송신자 정화 · 공명핵이 남았습니다. 송신자의 기록을 회수할 수 있습니다.";
            }
        }
        else if (monster.id % 3 == 0)
        {
            DropLoot(monster.x + 0.2, monster.y, 2, 18);
        }
        dirty = true;
    }

    bool CombatWorld::Upgrade(int index)
    {
        if (index == 1)
        {
            const int cost = 12 + growth.focusRank * 8;
            if (growth.focusRank >= 30 || growth.fragments < cost)
            {
                announcement = growth.focusRank >= 30
                                   ? L"잔향탄 조율이 최대 단계입니다."
                                   : L"잔향탄 조율 강화에 기억 파편 " + std::to_wstring(cost)
                                         + L"개가 필요합니다.";
                return false;
            }
            growth.fragments -= cost;
            ++growth.focusRank;
            announcement = L"잔향탄 조율 강화 · 잔향탄 위력이 오르고 공격 간격이 짧아집니다.";
        }
        else if (index == 2)
        {
            const int cost = 10 + growth.magnetRank * 8;
            if (growth.magnetRank >= 12 || growth.fragments < cost)
            {
                announcement = growth.magnetRank >= 12
                                   ? L"공명 인력이 최대 단계입니다."
                                   : L"공명 인력 강화에 기억 파편 " + std::to_wstring(cost)
                                         + L"개가 필요합니다.";
                return false;
            }
            growth.fragments -= cost;
            ++growth.magnetRank;
            announcement = L"공명 인력 강화 · 기억 파편과 공명핵을 더 먼 곳에서 끌어옵니다.";
        }
        else if (index == 3)
        {
            const int cost = 1 + growth.vitalityRank / 5;
            if (growth.vitalityRank >= 20 || growth.cores < cost)
            {
                announcement =
                    growth.vitalityRank >= 20
                        ? L"기억 그릇이 최대 단계입니다."
                        : L"기억 그릇 강화에 공명핵 " + std::to_wstring(cost) + L"개가 필요합니다.";
                return false;
            }
            growth.cores -= cost;
            ++growth.vitalityRank;
            health = std::min(Stats().maxHealth, health + 15.0f);
            announcement = L"기억 그릇 강화 · 최대 생명력이 15 증가했습니다.";
        }
        else
        {
            return false;
        }

        dirty = true;
        return true;
    }

    void CombatWorld::OnRealmChanged()
    {
        projectiles.clear();
        targetId = 0;
        shotCooldown = 0.0f;
        invulnerability = 0.0f;
        pickupFlash = 0.0f;
        announcement.clear();
        deathReported = false;
    }

    void CombatWorld::ResetAfterDeath()
    {
        OnRealmChanged();
        health = Stats().maxHealth;
        invulnerability = 2.0f;
        dirty = true;
    }

    CombatOutcome CombatWorld::Update(double dt, double playerX, double playerY, bool guarding)
    {
        CombatOutcome outcome;
        announcement.clear();
        if (!std::isfinite(dt) || !std::isfinite(playerX) || !std::isfinite(playerY))
        {
            return outcome;
        }

        dt = std::clamp(dt, 0.0, 0.1);
        if (health <= 0.0f)
        {
            if (!deathReported)
            {
                deathReported = true;
                outcome.playerDied = true;
                outcome.changed = true;
            }
            return outcome;
        }

        const size_t originalMonsters = monsters.size();
        EnsureNearbyZones(playerX, playerY);
        outcome.changed = monsters.size() != originalMonsters;
        const CombatStats stats = Stats();
        shotCooldown = std::max(0.0f, shotCooldown - float(dt));
        invulnerability = std::max(0.0f, invulnerability - float(dt));
        pickupFlash = std::max(0.0f, pickupFlash - float(dt));

        Monster* target = nullptr;
        double nearest = stats.range;
        for (Monster& monster : monsters)
        {
            if (monster.health <= 0.0f)
            {
                continue;
            }
            const double distance = Distance(playerX, playerY, monster.x, monster.y);
            if (distance <= nearest)
            {
                target = &monster;
                nearest = distance;
            }
        }
        targetId = target ? target->id : 0;

        if (target && shotCooldown <= 0.0f)
        {
            Projectile projectile;
            projectile.x = projectile.previousX = playerX;
            projectile.y = projectile.previousY = playerY;
            projectile.targetId = target->id;
            projectile.damage = stats.damage;
            projectile.life = 2.0f;
            projectile.trail.push_back({playerX, playerY});
            projectiles.push_back(projectile);
            shotCooldown = stats.cooldown;
            ++outcome.shotsFired;
        }

        for (auto projectile = projectiles.begin(); projectile != projectiles.end();)
        {
            auto destination = std::find_if(monsters.begin(),
                                            monsters.end(),
                                            [&](const Monster& monster)
                                            {
                                                return monster.id == projectile->targetId;
                                            });
            projectile->life -= float(dt);
            projectile->age += float(dt);
            if (projectile->life <= 0.0f || destination == monsters.end()
                || destination->health <= 0.0f)
            {
                projectile = projectiles.erase(projectile);
                continue;
            }

            projectile->previousX = projectile->x;
            projectile->previousY = projectile->y;
            const double distance =
                Distance(projectile->x, projectile->y, destination->x, destination->y);
            const double travel = dt * 12.0;
            if (distance <= travel + 0.2)
            {
                const bool lethal = destination->health <= projectile->damage;
                const size_t dropCount = destination->boss ? 3 : (destination->id % 3 == 0 ? 2 : 1);
                if (lethal
                    && (drops.size() + dropCount > MaximumDrops
                        || nextDropId > (std::numeric_limits<uint64_t>::max)() - dropCount))
                {
                    // Preserve earned rewards: wait for storage space rather than killing an
                    // enemy and silently discarding its guaranteed drop.
                    announcement =
                        L"남겨진 아이템을 먼저 수집해주세요. 드랍을 보존하기 위해 정화를 잠시 멈춥니다.";
                    projectile = projectiles.erase(projectile);
                    continue;
                }
                destination->health = std::max(0.0f, destination->health - projectile->damage);
                dirty = true;
                if (destination->health <= 0.0f)
                {
                    Defeat(*destination);
                    outcome.changed = true;
                }
                projectile = projectiles.erase(projectile);
            }
            else
            {
                projectile->x += (destination->x - projectile->x) / distance * travel;
                projectile->y += (destination->y - projectile->y) / distance * travel;
                RecordProjectileTrail(*projectile);
                ++projectile;
            }
        }

        auto hurtPlayer = [&](float damage)
        {
            if (invulnerability > 0.0f || Distance(playerX, playerY, 0.0, 0.0) < HubRadius)
            {
                return;
            }
            health = std::max(0.0f, health - damage * (guarding ? 0.35f : 1.0f));
            invulnerability = 0.65f;
            dirty = true;
        };

        for (Monster& monster : monsters)
        {
            const double distance = Distance(playerX, playerY, monster.x, monster.y);
            if (distance > 20.0)
            {
                continue;
            }

            if (monster.health <= 0.0f)
            {
                if (!monster.boss && monster.respawnTimer > 0.0f)
                {
                    monster.respawnTimer = std::max(0.0f, monster.respawnTimer - float(dt));
                    dirty = true;
                    if (monster.respawnTimer <= 0.0f)
                    {
                        monster.health = monster.maxHealth;
                        monster.x = monster.homeX;
                        monster.y = monster.homeY;
                        monster.attackTimer = 1.0f;
                        outcome.changed = true;
                    }
                }
                continue;
            }

            if (monster.attackTimer > 0.0f)
            {
                monster.attackTimer = std::max(0.0f, monster.attackTimer - float(dt));
                dirty = true;
            }

            if (monster.boss && monster.telegraph > 0.0f)
            {
                monster.telegraph = std::max(0.0f, monster.telegraph - float(dt));
                dirty = true;
                if (monster.telegraph <= 0.0f)
                {
                    if (distance < 3.0)
                    {
                        hurtPlayer(18.0f + 3.0f * (monster.level - 1));
                    }
                    monster.attackTimer = 3.8f;
                }
                continue;
            }

            if (monster.boss && monster.attackTimer <= 0.0f && distance < 4.2)
            {
                monster.telegraph = 1.1f;
                dirty = true;
                continue;
            }

            if (!monster.boss && distance < 0.8 && monster.attackTimer <= 0.0f)
            {
                hurtPlayer(8.0f + 2.0f * monster.kind + 1.5f * (monster.level - 1));
                monster.attackTimer = 1.1f;
                dirty = true;
            }

            double destinationX = monster.homeX;
            double destinationY = monster.homeY;
            const double homeDistance =
                Distance(monster.x, monster.y, monster.homeX, monster.homeY);
            if (distance < 6.0 && homeDistance < 6.0)
            {
                destinationX = playerX;
                destinationY = playerY;
            }

            const double remaining = Distance(monster.x, monster.y, destinationX, destinationY);
            if (remaining > 0.55)
            {
                const double speed = monster.boss ? 0.85 : (monster.kind == 2 ? 1.6 : 1.05);
                const double step = std::min(remaining, dt * speed);
                const double nextX = monster.x + (destinationX - monster.x) / remaining * step;
                const double nextY = monster.y + (destinationY - monster.y) / remaining * step;
                if (Distance(nextX, nextY, 0.0, 0.0) >= HubRadius)
                {
                    monster.x = nextX;
                    monster.y = nextY;
                    dirty = true;
                }
            }
        }

        if (health <= 0.0f)
        {
            deathReported = true;
            outcome.playerDied = true;
            outcome.changed = true;
            projectiles.clear();
            return outcome;
        }

        int collectedFragments = 0;
        int collectedCores = 0;
        int collectedHealing = 0;
        for (auto drop = drops.begin(); drop != drops.end();)
        {
            const double distance = Distance(playerX, playerY, drop->x, drop->y);
            if (distance > stats.pickupRadius)
            {
                ++drop;
                continue;
            }

            const double speed = 5.0 + 12.0 * (1.0 - distance / stats.pickupRadius);
            const double travel = speed * dt;
            if (distance <= std::max(0.3, travel))
            {
                if (drop->kind == 0)
                {
                    growth.fragments = AddBounded(growth.fragments, drop->amount);
                    collectedFragments = AddBounded(collectedFragments, drop->amount);
                }
                else if (drop->kind == 1)
                {
                    growth.cores = AddBounded(growth.cores, drop->amount);
                    collectedCores = AddBounded(collectedCores, drop->amount);
                }
                else
                {
                    health = std::min(Stats().maxHealth, health + drop->amount);
                    collectedHealing = AddBounded(collectedHealing, drop->amount);
                }
                drop = drops.erase(drop);
                outcome.changed = true;
                pickupFlash = 1.0f;
                dirty = true;
            }
            else
            {
                drop->x += (playerX - drop->x) / distance * travel;
                drop->y += (playerY - drop->y) / distance * travel;
                dirty = true;
                ++drop;
            }
        }

        if (collectedFragments > 0 || collectedCores > 0 || collectedHealing > 0)
        {
            const bool levelUp = announcement.find(L"공명 레벨") != std::wstring::npos;
            if (!levelUp)
            {
                announcement = L"자동 수집 · 기억 파편 +" + std::to_wstring(collectedFragments)
                               + L"  공명핵 +" + std::to_wstring(collectedCores);
                if (collectedHealing > 0)
                {
                    announcement += L"  회복 잔광 +" + std::to_wstring(collectedHealing);
                }
            }
        }
        return outcome;
    }
} // namespace echo
