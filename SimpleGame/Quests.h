#pragma once

#include <array>
#include <cstddef>
#include <iosfwd>
#include <string>
#include <vector>

namespace echo
{
    enum class QuestTarget
    {
        Sea,
        Mion,
        Signal,
        Doyun,
        Photo,
        Trace,
        Yunseo,
        Ihyeon,
        PrivateLogs,
        Control,
        Harin,
        Echo,
        Grove,
        None
    };

    struct QuestAnchor
    {
        QuestTarget target;
        int realm;
        double x;
        double y;
        std::wstring name;
        bool npc;
    };

    const std::vector<QuestAnchor>& QuestAnchors();

    struct QuestLine
    {
        std::wstring speaker;
        std::wstring text;
    };

    struct QuestScene
    {
        std::vector<QuestLine> lines;
        bool hasChoice = false;
        std::wstring choices[2];
        std::vector<QuestLine> responses[2];
    };

    struct QuestReward
    {
        int xp = 0;
        int fragments = 0;
        int cores = 0;
    };

    struct QuestMetrics
    {
        int realm = 0;
        int kills = 0;
        int bosses = 0;
        int discoveries = 0;
        int deaths = 0;
        int today = 0;
        bool broadcasting = false;
        double dt = 0.0;
    };

    struct QuestEntry
    {
        std::wstring title;
        std::wstring objective;
        std::wstring detail;
        QuestTarget target = QuestTarget::None;
        int realm = 0;
        int progress = 0;
        int required = 1;
        bool complete = false;
        bool claimable = false;
        int rewardXP = 0;
        int rewardFragments = 0;
        int rewardCores = 0;
    };

    class QuestState
    {
    public:
        std::wstring notification;

        QuestScene Scene(QuestTarget target) const;
        bool Complete(QuestTarget target, int choice, QuestReward& reward);
        bool Update(const QuestMetrics& metrics, QuestReward& reward);
        std::vector<QuestEntry> Entries(int category) const;
        QuestEntry Tracked() const;
        bool ClaimDaily(size_t index, QuestReward& reward);
        void Write(std::ostream& output) const;
        bool Read(std::istream& input);
        static int CurrentDay();
        bool EpisodeFinished() const;
        bool LinksDisabled() const;
        bool GroveDecorated() const;

    private:
        int m_MainStep = 0;
        int m_IntroChoice = -1;
        int m_PolicyChoice = -1;
        int m_EndingChoice = -1;
        bool m_LinksDisabled = false;
        std::array<int, 3> m_SideSteps = {};
        double m_CollabSeconds = 0.0;
        int m_VoiceDiscoveries = 0;
        int m_Day = 0;
        std::array<int, 3> m_DailyProgress = {};
        std::array<bool, 3> m_DailyClaimed = {};
        bool m_MetricsInitialized = false;
        int m_Realm = 0;
        int m_KnownKills = 0;
        int m_KnownBosses = 0;
        int m_KnownDiscoveries = 0;
        int m_KnownDeaths = 0;

        QuestScene MainScene() const;
        QuestScene SideScene(QuestTarget target) const;
        bool IsMainTarget(QuestTarget target) const;
        bool Valid() const;
    };
} // namespace echo
