#pragma once

#include <string>
#include <vector>

namespace echo
{
    struct EchoLife
    {
        int id = 0;
        double x = 0.0;
        double y = 0.0;
        float protection = 0.0f;
        float aggression = 0.0f;
        float curiosity = 0.0f;
    };

    struct Discovery
    {
        int realm = 0;
        int cellX = 0;
        int cellY = 0;
    };

    class GameState
    {
    public:
        explicit GameState(bool loadSaved = true);

        int realm = 0;
        double x = 0.0;
        double y = 0.0;
        double travelled = 0.0;
        int deaths = 0;
        float protection = 0.4f;
        float aggression = 0.2f;
        float curiosity = 0.4f;
        bool broadcasting = false;
        std::vector<EchoLife> echoes;
        std::vector<Discovery> discoveries;
        std::wstring message;

        void Update(double dt, float moveX, float moveY, bool sprint);
        void SwitchRealm();
        void Die();
        void Act(bool protect);
        bool Discover(int realm, int cx, int cy);
        bool HasDiscovery(int realm, int cx, int cy) const;
        bool Save();
        bool Load();
        std::wstring ProfileName() const;

    private:
        int nextEchoId = 1;
        double saveTimer = 0.0;
        bool saveBlocked = false;
        void NormalizeProfile();
        void PreserveUnreadableSave(const std::wstring& path);
    };
} // namespace echo
