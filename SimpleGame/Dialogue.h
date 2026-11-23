#pragma once

#include <cstddef>
#include <string>
#include <vector>

class Renderer;

namespace narrative
{
    enum class Speaker
    {
        Sea,
        Echo,
        Player,
        Narrator
    };

    enum class StoryMoment
    {
        None,
        FirstEcho,
        MemoryDiscovery,
        BossResolved
    };

    class DialogueState
    {
    public:
        std::wstring text;
        Speaker speaker = Speaker::Narrator;
        Speaker counterpart = Speaker::Sea;
        StoryMoment moment = StoryMoment::None;
        double elapsed = 0.0;
        double cinematicElapsed = 0.0;
        bool choicesAvailable = true;

        void Begin(const std::wstring& content,
                   Speaker currentSpeaker,
                   StoryMoment storyMoment = StoryMoment::None);
        void Tick(double dt);
        void RevealAll();
        bool Complete() const;
        bool HasNextPage() const;
        bool Advance();
        std::wstring VisibleText() const;
        bool CinematicActive() const;
        size_t PageIndex() const;
        size_t PageCount() const;

    private:
        std::vector<std::wstring> m_Pages;
        std::vector<size_t> m_ClusterEnds;
        size_t m_Page = 0;
        size_t m_Revealed = 0;
        double m_RevealBudget = 0.0;
        double m_NextDelay = 1.0 / 32.0;

        void StartPage();
    };

    void Draw(Renderer& renderer, const DialogueState& state, double time);
} // namespace narrative
