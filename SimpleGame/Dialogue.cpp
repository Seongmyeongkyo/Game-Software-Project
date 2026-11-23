#include "stdafx.h"
#include "Dialogue.h"
#include "Renderer.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <sstream>

namespace
{
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

    struct CinematicShot
    {
        double begin;
        double end;
        float zoomFrom;
        float zoomTo;
        Vec2 focusFrom;
        Vec2 focusTo;
    };

    float FadeWindow(double time, double begin, double end, double fade = .45)
    {
        return Ease(static_cast<float>((time - begin) / fade))
               * Ease(static_cast<float>((end - time) / fade));
    }

    void CenteredText(Renderer& renderer,
                      float y,
                      const std::wstring& text,
                      float size,
                      Color color,
                      int weight = 400)
    {
        double columns = 0.0;

        for (size_t offset = 0; offset < text.size();)
        {
            size_t count = 0;
            columns += CharacterWidth(CodePointAt(text, offset, count));
            offset += count;
        }

        const float left = 720.0f - static_cast<float>(columns) * size * .5f;
        renderer.Text(left + 1, y + 2, text, size, Color(0, 0, 0, color.a * .65f), weight);
        renderer.Text(left, y, text, size, color, weight);
    }

    void CinematicFrame(Renderer& renderer, const CinematicShot& shot, double time, float opacity)
    {
        const float progress =
            Ease(static_cast<float>((time - shot.begin) / (shot.end - shot.begin)));
        const float zoom = shot.zoomFrom + (shot.zoomTo - shot.zoomFrom) * progress;
        const float width = 1440.0f * zoom;
        const float height = 900.0f * zoom;
        const float focusX = shot.focusFrom.x + (shot.focusTo.x - shot.focusFrom.x) * progress;
        const float focusY = shot.focusFrom.y + (shot.focusTo.y - shot.focusFrom.y) * progress;
        const float driftX = static_cast<float>(std::sin(time * .42) * 3.0);
        const float driftY = static_cast<float>(std::sin(time * .31) * 2.0);
        const float x = std::clamp(720.0f - focusX * width + driftX, 1440.0f - width, 0.0f);
        const float y = std::clamp(450.0f - focusY * height + driftY, 900.0f - height, 0.0f);

        renderer.Image(L"Assets/Dialogue/story-reunion.png",
                       x,
                       y,
                       width,
                       height,
                       Color(1, 1, 1, opacity),
                       true);
    }

    void CinematicCaptions(Renderer& renderer, const narrative::DialogueState& state)
    {
        const wchar_t* first = L"하나의 죽음은 하나의 생명을 만듭니다.";
        const wchar_t* second = L"끝난 줄 알았던 이야기가, 다른 목소리로 이어집니다.";

        if (state.moment == narrative::StoryMoment::MemoryDiscovery)
        {
            first = L"처음 온 장소인데,";
            second = L"누군가 나를 기다린 것 같습니다.";
        }
        else if (state.moment == narrative::StoryMoment::BossResolved)
        {
            first = L"송신이 멈춘 자리에도, 누군가의 마지막 인사는 남아 있습니다.";
            second = L"우리가 기억하는 동안, 그 목소리는 어디로 가는 걸까요?";
        }

        const double time = state.cinematicElapsed;
        const float titleAlpha = FadeWindow(time, .7, 3.1, .6);
        const bool memoryScene = state.moment == narrative::StoryMoment::MemoryDiscovery;
        const float firstAlpha =
            FadeWindow(time, memoryScene ? 2.0 : 1.2, memoryScene ? 5.1 : 5.85);
        const float secondAlpha = FadeWindow(time, 6.0, memoryScene ? 10.5 : 10.8);
        CenteredText(renderer, 62, MomentName(state.moment), 22, C(0xDBE6E7, titleAlpha), 600);
        CenteredText(renderer, 787, first, 22, C(0xF1ECE3, firstAlpha));
        CenteredText(renderer, 787, second, 22, C(0xF1ECE3, secondAlpha));
    }

    void Cinematic(Renderer& renderer, const narrative::DialogueState& state)
    {
        const double time = state.cinematicElapsed;
        const double duration = state.CinematicDuration();
        const CinematicShot shots[] = {
            {0.0, 3.5, 1.04f, 1.12f, Vec2(.50f, .50f), Vec2(.50f, .48f)},
            {3.1, 6.0, 1.88f, 2.03f, Vec2(.22f, .38f), Vec2(.23f, .36f)},
            {5.6, 8.6, 1.87f, 2.02f, Vec2(.79f, .38f), Vec2(.78f, .36f)},
            {8.1, 11.5, 1.18f, 1.07f, Vec2(.50f, .46f), Vec2(.50f, .49f)}};
        size_t current = 0;

        for (size_t index = 1; index < sizeof(shots) / sizeof(shots[0]); ++index)
        {
            if (time >= shots[index].begin)
            {
                current = index;
            }
        }

        renderer.Rect(0, 0, 1440, 900, C(0x000000));
        const float dissolve =
            current == 0 ? 1.0f : Ease(static_cast<float>((time - shots[current].begin) / .5));

        if (current > 0 && dissolve < 1.0f)
        {
            CinematicFrame(renderer, shots[current - 1], time, 1.0f);
        }

        CinematicFrame(renderer, shots[current], time, dissolve);
        renderer.Rect(0, 0, 1440, 900, C(0x102039, .08f));

        for (int index = 0; index < 20; ++index)
        {
            const float edge = 1.0f - index / 20.0f;
            const Color shade(0, 0, 0, edge * edge * .28f);
            renderer.Rect(index * 8.0f, 0, 8, 900, shade);
            renderer.Rect(1432.0f - index * 8.0f, 0, 8, 900, shade);
        }

        for (int index = 0; index < 22; ++index)
        {
            const double seed = index * 2.399963;
            const float px = static_cast<float>(std::fmod(index * 137.17 + time * 5.0, 1440.0));
            const float py = static_cast<float>(140 + std::fmod(index * 71.31 + time * 8.0, 610.0));
            const float radius = index % 5 == 0 ? 2.1f : 1.0f;
            const float alpha = static_cast<float>(.08 + .06 * (std::sin(seed + time * .6) + 1));
            const unsigned color = index % 3 == 0 ? 0xF3C28E : 0xA2DDCF;
            renderer.Circle(px, py, radius * 3.0f, C(color, alpha * .12f));
            renderer.Circle(px, py, radius, C(color, alpha));
        }

        const float barHeight = 132.0f + 17.0f * Ease(static_cast<float>(time / .8));
        renderer.Rect(0, 0, 1440, barHeight, C(0x000000));
        renderer.Rect(0, 900 - barHeight, 1440, barHeight, C(0x000000));
        CinematicCaptions(renderer, state);
        renderer.Text(1110,
                      866,
                      L"[E / SPACE / ENTER / ESC] 건너뛰기",
                      11,
                      C(0x839397, Ease(static_cast<float>((time - 1.0) / .8)) * .65f));

        const float opening = Ease(static_cast<float>(time / .8));
        const float closing = Ease(static_cast<float>((duration - time) / .95));
        renderer.Rect(0, 0, 1440, 900, Color(0, 0, 0, 1.0f - opening * closing));
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
        m_DialogueElapsed = 0.0;
        m_CinematicOnly = false;
        choicesAvailable = true;
        m_Page = 0;
        m_Pages = MakePages(text);
        StartPage();
    }

    void DialogueState::BeginCinematic(StoryMoment storyMoment)
    {
        Begin(std::wstring(), Speaker::Narrator, storyMoment);
        m_CinematicOnly = true;
        choicesAvailable = false;
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

        if (CinematicActive())
        {
            const double cinematicStep = std::min(dt, CinematicDuration() - cinematicElapsed);
            cinematicElapsed = std::min(CinematicDuration(), cinematicElapsed + cinematicStep);
            dt -= cinematicStep;

            if (dt <= 0.0)
            {
                return;
            }
        }

        if (m_CinematicOnly)
        {
            return;
        }

        m_DialogueElapsed += dt;
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
        if (m_CinematicOnly || CinematicActive() || !Complete() || !HasNextPage())
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
        return moment != StoryMoment::None && cinematicElapsed < CinematicDuration();
    }

    double DialogueState::CinematicDuration() const
    {
        return moment == StoryMoment::None ? 0.0 : 11.5;
    }

    double DialogueState::DialogueElapsed() const
    {
        return m_DialogueElapsed;
    }

    bool DialogueState::SkipCinematic()
    {
        if (!CinematicActive())
        {
            return false;
        }

        cinematicElapsed = CinematicDuration();
        m_DialogueElapsed = 0.0;
        m_Page = 0;
        StartPage();
        return true;
    }

    bool DialogueState::ShouldClose() const
    {
        return m_CinematicOnly && !CinematicActive();
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
        if (state.ShouldClose())
        {
            return;
        }

        if (state.CinematicActive())
        {
            Cinematic(renderer, state);
            return;
        }

        renderer.Rect(0, 0, 1440, 900, C(0x020A15, .64f));
        const float arrival = Ease(static_cast<float>(state.DialogueElapsed() / .28));
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

        if (state.moment != StoryMoment::None && state.DialogueElapsed() < .45)
        {
            renderer.Rect(
                0,
                0,
                1440,
                900,
                Color(0, 0, 0, 1.0f - Ease(static_cast<float>(state.DialogueElapsed() / .45))));
        }
    }
} // namespace narrative
