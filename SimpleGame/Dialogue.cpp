#include "stdafx.h"
#include "Dialogue.h"
#include "Renderer.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <sstream>

namespace
{
    constexpr double CinematicDuration = 5.0;
    constexpr double CharactersPerSecond = 32.0;
    constexpr double MaximumLineWidth = 36.5;
    constexpr size_t LinesPerPage = 4;

    uint32_t CodePointAt(const std::wstring& value, size_t position, size_t& length)
    {
        const uint32_t first = static_cast<uint32_t>(value[position]);
        length = 1;

        if (first >= 0xD800 && first <= 0xDBFF && position + 1 < value.size())
        {
            const uint32_t second = static_cast<uint32_t>(value[position + 1]);

            if (second >= 0xDC00 && second <= 0xDFFF)
            {
                length = 2;
                return 0x10000 + ((first - 0xD800) << 10) + second - 0xDC00;
            }
        }

        return first;
    }

    bool CombiningMark(uint32_t point)
    {
        return (point >= 0x0300 && point <= 0x036F) || (point >= 0x1AB0 && point <= 0x1AFF)
               || (point >= 0x1DC0 && point <= 0x1DFF) || (point >= 0x20D0 && point <= 0x20FF)
               || (point >= 0xFE00 && point <= 0xFE0F) || (point >= 0xFE20 && point <= 0xFE2F)
               || (point >= 0xE0100 && point <= 0xE01EF) || (point >= 0x1F3FB && point <= 0x1F3FF);
    }

    bool HangulLeading(uint32_t point)
    {
        return (point >= 0x1100 && point <= 0x115F) || (point >= 0xA960 && point <= 0xA97C);
    }

    bool HangulVowel(uint32_t point)
    {
        return (point >= 0x1160 && point <= 0x11A7) || (point >= 0xD7B0 && point <= 0xD7C6);
    }

    bool HangulTrailing(uint32_t point)
    {
        return (point >= 0x11A8 && point <= 0x11FF) || (point >= 0xD7CB && point <= 0xD7FB);
    }

    size_t ClusterEnd(const std::wstring& value, size_t begin)
    {
        size_t count = 0;
        uint32_t previous = CodePointAt(value, begin, count);
        size_t end = begin + count;
        bool joinNext = false;

        while (end < value.size())
        {
            size_t nextCount = 0;
            const uint32_t next = CodePointAt(value, end, nextCount);
            const bool composedVowel =
                previous >= 0xAC00 && previous <= 0xD7A3 && ((previous - 0xAC00) % 28) == 0;
            const bool hangulJoin =
                (HangulLeading(previous) && (HangulLeading(next) || HangulVowel(next)))
                || ((HangulVowel(previous) || composedVowel)
                    && (HangulVowel(next) || HangulTrailing(next)))
                || (HangulTrailing(previous) && HangulTrailing(next));

            if (!CombiningMark(next) && next != 0x200D && !joinNext && !hangulJoin)
            {
                break;
            }

            joinNext = next == 0x200D;
            previous = next;
            end += nextCount;
        }

        return end;
    }

    double CharacterWidth(uint32_t point)
    {
        if (point == L' ' || point == L'\t')
        {
            return .34;
        }

        return point < 0x1100 ? .57 : 1.0;
    }

    std::vector<std::wstring> MakePages(const std::wstring& text)
    {
        std::vector<std::wstring> lines;
        std::wstring line;
        double width = 0.0;

        for (size_t offset = 0; offset < text.size();)
        {
            size_t count = 0;
            const uint32_t point = CodePointAt(text, offset, count);
            const size_t end = ClusterEnd(text, offset);

            if (point == L'\r')
            {
                offset = end;
                continue;
            }

            if (point == L'\n')
            {
                lines.push_back(line);
                line.clear();
                width = 0.0;
                offset = end;
                continue;
            }

            const double nextWidth = CharacterWidth(point);

            if (!line.empty() && width + nextWidth > MaximumLineWidth)
            {
                lines.push_back(line);
                line.clear();
                width = 0.0;
            }

            line.append(text, offset, end - offset);
            width += nextWidth;
            offset = end;
        }

        lines.push_back(line);
        std::vector<std::wstring> pages;

        for (size_t offset = 0; offset < lines.size(); offset += LinesPerPage)
        {
            std::wstring page;
            const size_t limit = std::min(lines.size(), offset + LinesPerPage);

            for (size_t index = offset; index < limit; ++index)
            {
                if (index != offset)
                {
                    page.push_back(L'\n');
                }

                page += lines[index];
            }

            pages.push_back(page);
        }

        return pages;
    }

    double DelayAfter(uint32_t point)
    {
        if (point == L'\n')
        {
            return 0.0;
        }

        if (point == L' ')
        {
            return .012;
        }

        const double base = 1.0 / CharactersPerSecond;

        if (point == L',' || point == L';' || point == 0x3001)
        {
            return base + .12;
        }

        if (point == L'.' || point == L'?' || point == L'!' || point == 0x2026 || point == 0x3002)
        {
            return base + .22;
        }

        return base;
    }

    Color C(unsigned rgb, float opacity = 1.0f)
    {
        return Color(((rgb >> 16) & 255) / 255.0f,
                     ((rgb >> 8) & 255) / 255.0f,
                     (rgb & 255) / 255.0f,
                     opacity);
    }

    float Ease(float value)
    {
        value = std::clamp(value, 0.0f, 1.0f);
        return value * value * (3.0f - 2.0f * value);
    }

    std::wstring SpeakerName(narrative::Speaker speaker)
    {
        switch (speaker)
        {
        case narrative::Speaker::Sea:
            return L"세아  ·  동료 스트리머";
        case narrative::Speaker::Echo:
            return L"이름을 기다리는 생명";
        case narrative::Speaker::Player:
            return L"당신";
        default:
            return L"잔향의 기록";
        }
    }

    std::wstring MomentName(narrative::StoryMoment moment)
    {
        switch (moment)
        {
        case narrative::StoryMoment::FirstEcho:
            return L"하나의 끝에서, 처음의 숨결이";
        case narrative::StoryMoment::MemoryDiscovery:
            return L"기억은 아직 이곳에 남아 있다";
        case narrative::StoryMoment::BossResolved:
            return L"마지막 송신 너머의 이야기";
        default:
            return L"우리의 이야기";
        }
    }

    void Silhouette(Renderer& renderer, float x, float y, Color color)
    {
        renderer.Circle(x + 115.0f, y + 95.0f, 44.0f, color);
        renderer.Polygon({Vec2(x + 76.0f, y + 142.0f),
                          Vec2(x + 153.0f, y + 142.0f),
                          Vec2(x + 185.0f, y + 300.0f),
                          Vec2(x + 44.0f, y + 300.0f)},
                         color);
    }

    void Portrait(Renderer& renderer,
                  const std::wstring& asset,
                  const std::wstring& title,
                  float x,
                  bool speaking,
                  double time,
                  float arrival,
                  Color accent,
                  bool mirror = false)
    {
        const float slide = (1.0f - arrival) * (x < 720.0f ? -24.0f : 24.0f);
        const float left = x + slide;
        const float breath = static_cast<float>(std::sin(time * 1.65 + x * .003) * 1.6);
        renderer.RoundedRect(left, 335, 230, 410, 14, C(0x0A1829, .88f * arrival));
        renderer.RoundedRect(left + 8, 343, 214, 394, 10, C(0x243B4C, .42f * arrival));

        for (int index = 0; index < 8; ++index)
        {
            renderer.Line(Vec2(left + 13, 382.0f + index * 43.0f),
                          Vec2(left + 217, 348.0f + index * 43.0f),
                          1.0f,
                          C(0x91C8CD, .035f * arrival));
        }

        const float alpha = arrival * (speaking ? 1.0f : .70f);

        if (!renderer.Image(asset,
                            left + 4,
                            350 + breath,
                            222,
                            379,
                            Color(1, 1, 1, alpha),
                            true,
                            mirror))
        {
            Silhouette(renderer,
                       left,
                       370 + breath,
                       Color(accent.r, accent.g, accent.b, .30f * alpha));
        }

        renderer.Rect(left + 19,
                      334,
                      192,
                      speaking ? 3.0f : 1.0f,
                      Color(accent.r, accent.g, accent.b, arrival * (speaking ? .95f : .35f)));
        renderer.RoundedRect(left + 9, 745, 212, 39, 7, C(0x0D1D2C, .95f * arrival));
        renderer.Text(left + 24, 756, title, 14, Color(accent.r, accent.g, accent.b, arrival), 600);
    }

    void Cinematic(Renderer& renderer, const narrative::DialogueState& state)
    {
        const float progress = static_cast<float>(state.cinematicElapsed / CinematicDuration);
        const float opacity =
            Ease(static_cast<float>(state.cinematicElapsed / .55))
            * Ease(static_cast<float>((CinematicDuration - state.cinematicElapsed) / .8));
        const float zoom = 1.03f + progress * .055f;
        const float x = -(zoom - 1.0f) * 720.0f + std::sin(progress * 2.0f) * 7.0f;
        const float y = -(zoom - 1.0f) * 450.0f - progress * 6.0f;

        renderer.Image(L"Assets/Dialogue/story-reunion.png",
                       x,
                       y,
                       1440.0f * zoom,
                       900.0f * zoom,
                       Color(1, 1, 1, opacity * .92f),
                       true);
        renderer.Rect(0, 0, 1440, 900, C(0x051525, .18f * opacity));
        renderer.Rect(0, 0, 1440, 112, C(0x071421, .86f * opacity));
        renderer.Text(52, 25, L"잔향의 순간", 12, C(0x91BCBA, opacity), 600);
        renderer.Text(50, 51, MomentName(state.moment), 28, C(0xE1F3EE, opacity), 600);
        renderer.Rect(51, 97, 310 * std::min(1.0f, progress * 2.0f), 2, C(0x96E1D1, opacity * .7f));

        for (int index = 0; index < 32; ++index)
        {
            const double seed = index * 2.399963;
            const float px =
                static_cast<float>(std::fmod(index * 113.17 + state.cinematicElapsed * 11, 1440.0));
            const float py = static_cast<float>(
                120 + std::fmod(index * 71.31 + state.cinematicElapsed * 23, 480.0));
            const float radius = index % 4 == 0 ? 2.5f : 1.2f;
            const float alpha =
                opacity
                * static_cast<float>(.13 + .12 * (std::sin(seed + state.cinematicElapsed) + 1));
            renderer.Circle(px, py, radius, C(index % 3 == 0 ? 0xD8B6F2 : 0xA2F0D9, alpha));
        }
    }
} // namespace

namespace narrative
{
    void DialogueState::Begin(const std::wstring& content,
                              Speaker currentSpeaker,
                              StoryMoment storyMoment)
    {
        text = content;
        speaker = currentSpeaker;
        if (currentSpeaker != Speaker::Player)
        {
            counterpart = currentSpeaker == Speaker::Sea ? Speaker::Sea : Speaker::Echo;
        }
        moment = storyMoment;
        elapsed = 0.0;
        cinematicElapsed = 0.0;
        choicesAvailable = true;
        m_Page = 0;
        m_Pages = MakePages(text);
        StartPage();
    }

    void DialogueState::StartPage()
    {
        m_Revealed = 0;
        m_RevealBudget = 0.0;
        m_NextDelay = 1.0 / CharactersPerSecond;
        m_ClusterEnds.clear();

        if (m_Page >= m_Pages.size())
        {
            return;
        }

        const auto& page = m_Pages[m_Page];

        for (size_t offset = 0; offset < page.size();)
        {
            offset = ClusterEnd(page, offset);
            m_ClusterEnds.push_back(offset);
        }
    }

    void DialogueState::Tick(double dt)
    {
        if (!std::isfinite(dt) || dt <= 0.0)
        {
            return;
        }

        dt = std::min(.25, dt);
        elapsed += dt;
        cinematicElapsed += dt;
        m_RevealBudget += dt;

        while (!Complete() && m_RevealBudget >= m_NextDelay)
        {
            m_RevealBudget -= m_NextDelay;
            const size_t begin = m_Revealed == 0 ? 0 : m_ClusterEnds[m_Revealed - 1];
            size_t count = 0;
            const uint32_t point = CodePointAt(m_Pages[m_Page], begin, count);
            ++m_Revealed;
            m_NextDelay = DelayAfter(point);
        }
    }

    void DialogueState::RevealAll()
    {
        m_Revealed = m_ClusterEnds.size();
        m_RevealBudget = 0.0;
        cinematicElapsed = CinematicDuration;
    }

    bool DialogueState::Complete() const
    {
        return m_Revealed >= m_ClusterEnds.size();
    }

    bool DialogueState::HasNextPage() const
    {
        return m_Page + 1 < m_Pages.size();
    }

    bool DialogueState::Advance()
    {
        if (!Complete() || !HasNextPage())
        {
            return false;
        }

        ++m_Page;
        StartPage();
        return true;
    }

    std::wstring DialogueState::VisibleText() const
    {
        if (m_Page >= m_Pages.size() || m_Revealed == 0)
        {
            return std::wstring();
        }

        return m_Pages[m_Page].substr(0, m_ClusterEnds[m_Revealed - 1]);
    }

    bool DialogueState::CinematicActive() const
    {
        return moment != StoryMoment::None && cinematicElapsed < CinematicDuration;
    }

    size_t DialogueState::PageIndex() const
    {
        return m_Page;
    }

    size_t DialogueState::PageCount() const
    {
        return m_Pages.size();
    }

    void Draw(Renderer& renderer, const DialogueState& state, double time)
    {
        renderer.Rect(0, 0, 1440, 900, C(0x020A15, .64f));

        if (state.CinematicActive())
        {
            Cinematic(renderer, state);
        }

        const float arrival = Ease(static_cast<float>(state.elapsed / .28));
        const bool playerSpeaking = state.speaker == Speaker::Player;
        const bool seaCounterpart = state.counterpart == Speaker::Sea;
        const Color accent =
            playerSpeaking ? C(0xA9D9E3) : (seaCounterpart ? C(0xD4B4E5) : C(0x9BE4D3));
        Portrait(renderer,
                 L"Assets/Dialogue/player.png",
                 L"당신",
                 40,
                 playerSpeaking,
                 time,
                 arrival,
                 C(0xA9D9E3));
        Portrait(renderer,
                 seaCounterpart ? L"Assets/Dialogue/sea.png" : L"Assets/Dialogue/echo.png",
                 seaCounterpart ? L"세아"
                                : (state.speaker == Speaker::Narrator ? L"잔향의 기록" : L"유실체"),
                 1170,
                 !playerSpeaking,
                 time + .6,
                 arrival,
                 accent,
                 !seaCounterpart);

        const float panelY = 610 + (1.0f - arrival) * 14.0f;
        renderer.RoundedRect(300, panelY + 5, 840, 250, 14, C(0x000711, .35f * arrival));
        renderer.RoundedRect(300, panelY, 840, 250, 14, C(0x0B1B2B, .98f * arrival));
        renderer.Rect(321, panelY, 798, 2, Color(accent.r, accent.g, accent.b, .55f * arrival));
        renderer.Text(328, panelY + 18, SpeakerName(state.speaker), 16, accent, 600);
        renderer.Rect(329, panelY + 48, 780, 1, C(0x7091A2, .2f * arrival));
        std::wistringstream lines(state.VisibleText());
        std::wstring line;
        float lineY = panelY + 65;

        while (std::getline(lines, line))
        {
            renderer.Text(329, lineY, line, 20, C(0xE4EEEE, arrival));
            lineY += 29;
        }

        const bool choicesReady = state.Complete() && !state.CinematicActive()
                                  && !state.HasNextPage() && state.choicesAvailable;
        const std::wstring hint =
            !state.Complete() || state.CinematicActive()
                ? L"[E / SPACE] 대사 전체 보기    [ESC] 닫기"
                : (state.HasNextPage() ? L"[E / SPACE] 다음 이야기    [ESC] 닫기"
                                       : L"[E / SPACE] 대화 마치기    [ESC] 닫기");

        if (choicesReady)
        {
            renderer.RoundedRect(327, panelY + 199, 217, 32, 6, C(0x203C47));
            renderer.RoundedRect(556, panelY + 199, 247, 32, 6, C(0x263345));
            renderer.Text(341, panelY + 206, L"[1] 함께할게", 13, C(0xB7E4D8));
            renderer.Text(570, panelY + 206, L"[2] 이야기를 듣는다", 13, C(0xB8C9D6));
            renderer.Text(825, panelY + 207, L"[E / SPACE] 마치기", 11, C(0x8FA9B8));
        }
        else
        {
            renderer.Text(329, panelY + 207, hint, 12, C(0x8FA9B8));
        }

        if (state.PageCount() > 1)
        {
            renderer.Text(1024,
                          panelY + 22,
                          std::to_wstring(state.PageIndex() + 1) + L" / "
                              + std::to_wstring(state.PageCount()),
                          11,
                          C(0x7C9AA9));
        }
    }
} // namespace narrative
