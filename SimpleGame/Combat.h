#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace echo
{
    struct Growth
    {
        int level = 1;
        int xp = 0;
        int fragments = 0;
        int cores = 0;
        int focusRank = 0;
        int magnetRank = 0;
        int vitalityRank = 0;
        int kills = 0;
        int bosses = 0;
    };

    struct CombatStats
    {
        float maxHealth = 100.0f;
        float damage = 10.0f;
        float cooldown = 0.75f;
        float range = 6.0f;
        float pickupRadius = 1.5f;
        float moveSpeed = 3.0f;
    };

    struct Monster
    {
        uint64_t id = 0;
        int zoneX = 0;
        int zoneY = 0;
        int slot = 0;
        int kind = 0;
        int level = 1;
        bool boss = false;
        double x = 0.0;
        double y = 0.0;
        double homeX = 0.0;
        double homeY = 0.0;
        float health = 0.0f;
        float maxHealth = 0.0f;
        float respawnTimer = 0.0f;
        float attackTimer = 0.0f;
        // Remaining charge time before the boss's three-unit-radius pulse.
        float telegraph = 0.0f;
    };

    struct ProjectileTrailPoint
    {
        double x = 0.0;
        double y = 0.0;
    };

    struct Projectile
    {
        double x = 0.0;
        double y = 0.0;
        double previousX = 0.0;
        double previousY = 0.0;
        uint64_t targetId = 0;
        float damage = 0.0f;
        float life = 0.0f;
        float age = 0.0f;
        double trailSampleDistance = 0.0;
        // Fixed-distance history, oldest first. The renderer appends the live head.
        std::vector<ProjectileTrailPoint> trail;
    };

    struct LootDrop
    {
        uint64_t id = 0;
        double x = 0.0;
        double y = 0.0;
        // 0: memory fragments, 1: resonance core, 2: restorative afterglow.
        int kind = 0;
        int amount = 0;
    };

    struct CombatOutcome
    {
        bool playerDied = false;
        int shotsFired = 0;
        // Important events requiring a prompt save; ordinary movement uses dirty.
        bool changed = false;
    };

    class CombatWorld
    {
    public:
        Growth growth;
        float health = 100.0f;
        float shotCooldown = 0.0f;
        float invulnerability = 0.0f;
        float pickupFlash = 0.0f;
        uint64_t targetId = 0;
        std::vector<Monster> monsters;
        std::vector<Projectile> projectiles;
        std::vector<LootDrop> drops;
        std::wstring announcement;
        bool dirty = false;
        uint64_t nextDropId = 1;

        CombatStats Stats() const;
        int RequiredXP() const;
        static int ZoneLevel(int zx, int zy);
        CombatOutcome Update(double dt, double playerX, double playerY, bool guarding);
        bool Upgrade(int index);
        void ResetAfterDeath();
        void OnRealmChanged();
        void EnsureNearbyZones(double px, double py);
        void AddExperience(int amount);
        static std::wstring MonsterName(int kind);

    private:
        bool deathReported = false;
        void DropLoot(double x, double y, int kind, int amount);
        void Defeat(Monster& monster);
    };
} // namespace echo
