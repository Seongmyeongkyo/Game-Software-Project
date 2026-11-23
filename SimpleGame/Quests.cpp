#include "stdafx.h"
#include "Quests.h"
#include <algorithm>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <istream>
#include <ostream>
#include <utility>

namespace
{
    using echo::QuestLine;
    using echo::QuestScene;
    using echo::QuestTarget;

    struct MainStep
    {
        QuestTarget target;
        int realm;
        const wchar_t* objective;
    };

    const MainStep MainSteps[] = {
        {QuestTarget::Mion, 0, L"미온의 식당 앞에서 미온과 이야기하기"},
        {QuestTarget::Sea, 0, L"세아와 이상한 생방송 기록 확인하기"},
        {QuestTarget::Signal, 1, L"루미아의 이상 송출 지점 조사하기"},
        {QuestTarget::Doyun, 1, L"기록 거리에서 도윤의 이야기 듣기"},
        {QuestTarget::Photo, 1, L"도윤의 사진과 남은 기록 비교하기"},
        {QuestTarget::Trace, 1, L"기록 거리 끝의 잔향 흔적 조사하기"},
        {QuestTarget::Yunseo, 0, L"현실의 윤서에게 방송 상황 묻기"},
        {QuestTarget::Ihyeon, 0, L"이현과 방송 대응 방침 결정하기"},
        {QuestTarget::Yunseo, 0, L"윤서에게 방침을 전하고 조사 권한 받기"},
        {QuestTarget::PrivateLogs, 1, L"루미아의 비공개 송출 기록 열람하기"},
        {QuestTarget::Control, 1, L"보스 처치 증거를 확보하고 관리 단말 조사하기"},
        {QuestTarget::Ihyeon, 0, L"이현과 차단 방법 검증하기"},
        {QuestTarget::Harin, 1, L"하린과 상황을 공유하고 의사 확인하기"},
        {QuestTarget::Control, 1, L"관리 단말에서 무단 기억 연결 차단하기"},
        {QuestTarget::Harin, 1, L"하린과 후속 방송 운영 방침 결정하기"},
        {QuestTarget::Mion, 0, L"미온의 식당으로 돌아가 일상 나누기"}};

    constexpr int MainStepCount = sizeof(MainSteps) / sizeof(MainSteps[0]);
    const int ChapterEnds[] = {0, 3, 6, 9, 12, MainStepCount};
    const wchar_t* ChapterTitles[] = {L"CH01 · 화면 너머의 첫인사",
                                      L"CH02 · 기억되지 않는 사람",
                                      L"CH03 · 값을 매길 수 없는 것",
                                      L"CH04 · 거짓된 등가",
                                      L"CH05 · 끝나지 않는 우리의 이야기"};
    const int ChapterXP[] = {25, 40, 55, 75, 120};
    const int ChapterFragments[] = {4, 6, 8, 12, 20};
    const int ChapterCores[] = {0, 0, 0, 1, 2};
    const int DailyRequired[] = {1, 5, 1};
    const int DailyXP[] = {20, 25, 20};
    const int DailyFragments[] = {4, 6, 5};

    QuestScene Lines(std::initializer_list<QuestLine> lines)
    {
        QuestScene result;
        result.lines = lines;
        return result;
    }

    bool ValidDay(int day)
    {
        if (day == 0)
        {
            return true;
        }
        const int year = day / 10000;
        const int month = (day / 100) % 100;
        const int date = day % 100;
        if (year < 1970 || year > 9999 || month < 1 || month > 12)
        {
            return false;
        }
        const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
        return date >= 1 && date <= days[month - 1] + (month == 2 && leap ? 1 : 0);
    }

    bool ValidCounter(int value)
    {
        return value >= 0 && value <= 1000000000;
    }

    int TargetRealm(QuestTarget target)
    {
        if (target == QuestTarget::Echo)
        {
            return 1;
        }
        for (const auto& anchor : echo::QuestAnchors())
        {
            if (anchor.target == target)
            {
                return anchor.realm;
            }
        }
        return -1;
    }
} // namespace

namespace echo
{
    const std::vector<QuestAnchor>& QuestAnchors()
    {
        static const std::vector<QuestAnchor> anchors = {
            {QuestTarget::Sea, 0, 1.0, -1.3, L"세아", true},
            {QuestTarget::Mion, 0, -1.8, -1.8, L"미온 · 식당", true},
            {QuestTarget::Signal, 1, -1.2, -1.5, L"이상 송출 지점", false},
            {QuestTarget::Doyun, 1, -4.0, 0.0, L"도윤 · 기록 거리", true},
            {QuestTarget::Photo, 1, -4.0, 2.5, L"남아 있는 사진", false},
            {QuestTarget::Trace, 1, -6.0, 3.5, L"잔향의 흔적", false},
            {QuestTarget::Yunseo, 0, -4.0, 0.0, L"윤서 · 방송 스튜디오", true},
            {QuestTarget::Ihyeon, 0, 0.0, 4.0, L"이현 · 연구시설", true},
            {QuestTarget::PrivateLogs, 1, 0.0, -5.0, L"비공개 송출 기록", false},
            {QuestTarget::Control, 1, 2.0, -5.0, L"기억 연결 관리 단말", false},
            {QuestTarget::Harin, 1, -4.0, -5.0, L"하린 · 방송 연결", true},
            {QuestTarget::Grove, 1, -7.0, -3.0, L"빛이 머무는 숲", false}};
        return anchors;
    }

    bool QuestState::IsMainTarget(QuestTarget target) const
    {
        return m_MainStep < MainStepCount && MainSteps[m_MainStep].target == target;
    }

    QuestScene QuestState::MainScene() const
    {
        QuestScene result;
        switch (m_MainStep)
        {
        case 0:
            return Lines(
                {{L"미온", L"어서 와요! 오늘도 늘 먹던 걸로? 세아 씨는 안 매운 걸로 주면 되죠?"},
                 {L"세아", L"아니거든요! 저 매운 거 잘 먹거든요!"},
                 {L"미온", L"지난번엔 우유 두 통 마셨잖아요."},
                 {L"시청자 채팅", L"ㅋㅋㅋㅋ 맵찔이 검거"},
                 {L"플레이어", L"방송 제목을 매운맛 챌린지로 바꿀까?"},
                 {L"세아", L"야! 너까지 그러기야? 잠깐, 채팅에 무슨 일이 난 것 같은데……."}});
        case 1:
            result = Lines(
                {{L"시청자 채팅", L"잠깐, 하린 채널 켜졌는데?"},
                 {L"세아", L"……뭐? 그럴 리가 없어. 하린 언니는……."},
                 {L"하린", L"다들 안녕! 오늘은 어디부터 가볼까? 어? 세아도 보고 있네?"},
                 {L"세아", L"언니…… 진짜야? 녹화 영상이 우리 채팅에 대답할 리가 없는데."},
                 {L"하린", L"세아야…… 나, 지금 어디에 있는 거야?"},
                 {L"기록",
                  L"화면에 노이즈가 번진다. 송출 경로를 확인할지, 먼저 세아를 살필지 결정하자."}});
            result.hasChoice = true;
            result.choices[0] = L"송출 위치부터 확인하자";
            result.choices[1] = L"세아, 잠깐 쉬어도 괜찮아";
            result.responses[0] = {
                {L"플레이어", L"방송의 송출 위치부터 확인하자."},
                {L"세아", L"……그래. 누가 이런 방송을 켰는지부터 알아보자."},
                {L"기록", L"방송 기록에서 루미아의 비정상 송출 좌표를 확보했다."}};
            result.responses[1] = {
                {L"플레이어", L"세아, 괜찮아? 잠깐 쉬어도 돼."},
                {L"세아", L"고마워. 조금 놀랐을 뿐이야. 그래도 확인하고 싶어."},
                {L"기록", L"방송 기록에서 루미아의 비정상 송출 좌표를 확보했다."}};
            return result;
        case 2:
            return Lines(
                {{L"기록",
                  L"송출 좌표는 폐쇄된 기록 거리로 이어진다. 영상은 새로운 채팅에 반응하고 있다."},
                 {L"세아",
                  m_IntroChoice == 1
                      ? L"기다려 줘서 고마워. 이제 차근차근 살펴보자."
                      : L"좌표는 맞아. 그래도 이 방송을 누가 켰는지는 아직 모르겠어."},
                 {L"하린", L"분명 익숙한 길인데…… 여기서 나가는 방법이 떠오르지 않아."},
                 {L"세아", L"저기 길가에 누가 있어. 우리에게 할 말이 있는 것 같은데?"}});
        case 3:
            return Lines(
                {{L"도윤", L"저기요…… 혹시 사람을 찾는 방법을 아세요?"},
                 {L"플레이어", L"누구를 찾으시는데요? 마지막으로 기억나는 게 뭔가요?"},
                 {L"도윤",
                  L"그게…… 누구였는지 기억이 안 나요. 어제 누군가랑 하린의 방송을 봤어요."},
                 {L"도윤", L"분명 소중한 사람이었는데…… 얼굴이 떠오르지 않아요."},
                 {L"세아", L"그 방송을 보고 나서부터요?"},
                 {L"도윤",
                  L"네. 그런데 사진은 그대로 남아 있어요. 제 사진과 거리의 기록을 비교해 주실래요?"}});
        case 4:
            return Lines(
                {{L"기록",
                  L"사진 속에는 도윤과 다른 한 사람이 함께 웃고 있다. 촬영 시각과 파일은 온전하다."},
                 {L"이현", L"통신 연결됐습니다. 기록 자체에는 문제가 없습니다."},
                 {L"세아", L"그럼 기억만 사라졌다는 거예요?"},
                 {L"이현",
                  L"원인은 불명확합니다. 사진이 남아 있다는 사실과 기억의 빈자리를 구분해서 기록하죠."},
                 {L"이현", L"거리 끝에서 잔향 반응이 검출됐어요. 흔적을 직접 확인해 주세요."}});
        case 5:
            return Lines(
                {{L"기록",
                  L"잔향은 남은 기록과 비슷한 리듬으로 빛난다. 누군가의 이름을 단정할 증거는 없다."},
                 {L"이현",
                  L"반응은 확보했습니다. 다만 이것이 기억 손실의 원인인지 결과인지는 아직 알 수 없어요."},
                 {L"플레이어",
                  L"도윤의 사진은 남고, 기억은 어긋나 있어요. 방송 쪽의 기록도 조사해 봐야겠어요."},
                 {L"세아",
                  L"윤서라면 하린 언니 방송의 현재 상황을 알 거야. 현실 스튜디오로 돌아가자."}});
        case 6:
            return Lines(
                {{L"윤서", L"하린 언니 방송, 기다리던 사람들이 정말 많이 모였어!"},
                 {L"세아", L"다들 언니가 돌아왔다고 믿는 거야?"},
                 {L"윤서",
                  L"그만큼 기다린 사람이 많았던 거지. 누군가에겐 정말 행복한 재회일 수도 있잖아."},
                 {L"플레이어", L"그 방송을 본 뒤 소중한 사람을 기억하지 못하는 시민을 만났어."},
                 {L"윤서", L"……그게 방송 때문이라는 증거는 있어?"},
                 {L"이현",
                  L"아직 인과관계는 확인되지 않았습니다. 연구시설에서 노출을 줄일 대응부터 함께 정하죠."}});
        case 7:
            result = Lines(
                {{L"이현",
                  L"방송 접속 이후 기억 손실 신고가 증가했습니다. 하지만 원인을 확정할 단계는 아닙니다."},
                 {L"세아", L"그럼 사람들을 더 모으기 전에 확인해야지!"},
                 {L"하린",
                  L"윤서야. 나 하나 때문에 누군가 아파야 한다면…… 그건 정말 행복한 재회일까?"},
                 {L"이현",
                  L"추가 노출을 줄이면서 비공개 송출 기록을 조사해야 합니다. 어떤 방침을 요청할까요?"}});
            result.hasChoice = true;
            result.choices[0] = L"방송 중단을 요청하고 조사한다";
            result.choices[1] = L"신규 접속을 제한하고 조사한다";
            result.responses[0] = {
                {L"플레이어", L"방송을 잠시 중단하고 조사하자."},
                {L"세아", L"나도 그게 맞는 것 같아. 언니도 이해해 줄 거야."},
                {L"이현",
                 L"중단을 요청하겠습니다. 아직 무단 연결이 차단된 것은 아닙니다. 실제 경로를 찾아 검증해야 합니다."}};
            result.responses[1] = {
                {L"플레이어", L"접속 규모를 제한하고 원인을 확인하자."},
                {L"이현",
                 L"추가 노출은 막겠습니다. 안전성이 확인되기 전에는 연결을 확대하지 않죠."},
                {L"윤서", L"조사에 협력할게. 기뻐하는 사람들만 보느라 다른 사람의 불안을 놓쳤어."}};
            return result;
        case 8:
            return Lines(
                {{L"윤서",
                  m_PolicyChoice == 0
                      ? L"방송 중단 요청을 접수했어. 원인을 확인하기 전까지 홍보도 멈출게."
                      : L"신규 접속 제한을 적용했어. 연결을 더 확대하지 않고 조사에 협력할게."},
                 {L"윤서",
                  L"비공개 송출 기록을 볼 수 있는 조사 권한을 준비했어. 기록은 루미아의 단말에서 열 수 있어."},
                 {L"이현",
                  L"동의하지 않은 이용자와 연결되는 경로가 있는지 확인하겠습니다. 선택만으로 해결됐다고 생각하면 안 됩니다."},
                 {L"세아",
                  L"언니를 탓하려는 게 아니야. 누가 다치고 있다면, 먼저 멈춰야 할 일을 찾는 거야."}});
        case 9:
            return Lines(
                {{L"이현", L"찾았습니다. 방송 안정화 기록이 삭제되지 않고 남아 있었어요."},
                 {L"세아", L"뭐가 적혀 있는데요?"},
                 {L"이현", L"동의하지 않은 이용자의 기억 연결까지 사용하고 있습니다."},
                 {L"윤서", L"그런 일이…… 가능하다고요?"},
                 {L"이현", L"죽음과 탄생의 규칙 때문이 아닙니다. 누군가 만든 시스템이에요."},
                 {L"세아", L"그럼 누가 이런 걸 만든 거죠?"},
                 {L"이현",
                  L"책임자는 더 조사해야 합니다. 관리 단말을 검증할 보스의 송신 코어 증거도 필요해요. 루미아 보스 한 체를 처치한 뒤 단말을 조사해 주세요."}});
        case 10:
            if (m_KnownBosses < 1)
            {
                return Lines(
                    {{L"관리 단말",
                      L"송신 코어의 전투 기록이 확인되지 않습니다. 접근은 조사 전용으로 제한됩니다."},
                     {L"이현",
                      L"루미아 보스 한 체를 처치해 실제 송신 코어 기록을 확보한 뒤 다시 조사해 주세요. 시작 전투 구역의 보스는 (6, 4) 부근에 있습니다."}});
            }
            return Lines(
                {{L"관리 단말",
                  L"실제 보스 처치 기록과 조사 권한을 확인했습니다. 무단 기억 연결의 관리 기능을 열람합니다."},
                 {L"이현",
                  L"피해를 일으키는 경로는 찾았어요. 이제 실행 전에 현실의 연구시설에서 차단 범위를 검증하죠."},
                 {L"하린", L"그럼…… 누군가 나 때문에 기억을 잃은 거야?"},
                 {L"플레이어", L"당신에게 책임이 있다는 뜻은 아니에요."},
                 {L"하린", L"……고마워. 하지만 계속 방송할지는 내가 선택하고 싶어."}});
        case 11:
            return Lines(
                {{L"이현",
                  L"검증했습니다. 이 관리 기능은 동의 없는 기억 연결 경로만 차단할 수 있습니다."},
                 {L"이현",
                  L"이미 발생한 기억 손실이 자동으로 회복되는 것은 아닙니다. 유실체의 생명을 지우는 명령도 아니에요."},
                 {L"윤서", L"탄생 자체를 잘못이라고 해서는 안 되겠네요."},
                 {L"세아",
                  L"하린 언니와 먼저 이야기하자. 우리가 원하는 모습으로 남으라고 강요하고 싶지는 않아."}});
        case 12:
            return Lines(
                {{L"세아", L"언니…… 난 아직도 언니가 진짜인지 모르겠어."},
                 {L"하린", L"나도 그래. 그런데 지금 너랑 이야기하는 건 좋아."},
                 {L"윤서", L"기다리는 사람들한테 뭐라고 해야 할까……."},
                 {L"하린", L"그냥 있는 그대로 말해 줘."},
                 {L"이현", L"무단 연결은 차단할 수 있습니다. 방송을 계속할지는 별도 문제예요."},
                 {L"하린", L"그럼 내 이야기는…… 내가 정해도 될까?"},
                 {L"플레이어",
                  L"물론이죠. 당신의 삶이니까요. 우선 검증된 단말에서 다른 사람에게 이어진 무단 연결을 끊을게요."}});
        case 13:
            return Lines(
                {{L"관리 단말",
                  L"조사 권한 · 송신 코어 증거 · 이현의 차단 범위 검증을 확인했습니다."},
                 {L"이현", L"명령 대상을 다시 확인했습니다. 동의 없는 기억 연결만 중단합니다."},
                 {L"플레이어", L"무단 기억 연결 차단을 실행합니다."},
                 {L"관리 단말", L"대상 경로 응답 확인. 무단 기억 연결을 차단했습니다."},
                 {L"이현",
                  L"차단 적용 상태를 확인했습니다. 하린의 정체나 기억의 회복 여부는 아직 별도의 조사 과제입니다."}});
        case 14:
            result = Lines(
                {{L"이현",
                  L"무단 기억 연결은 차단된 상태입니다. 피해 없는 연결을 검증할 연구와 방송의 향후 운영은 별도로 정해야 합니다."},
                 {L"하린",
                  L"내가 누구인지 아직 다 알지는 못해. 그래도 누군가의 기대에 맞추기 위해서만 존재하고 싶지는 않아."},
                 {L"세아",
                  L"언니가 생각할 시간을 가졌으면 좋겠어. 우리도 피해를 입은 사람들을 잊지 않을게."}});
            result.hasChoice = true;
            result.choices[0] = L"동의와 안전을 검증하는 연구";
            result.choices[1] = L"방송 연결 중단과 피해 회복";
            result.responses[0] = {
                {L"플레이어",
                 L"당사자의 동의와 안전성을 확인하는 제한 연구를 진행해요. 공개 방송에 바로 복귀할 필요는 없어요."},
                {L"하린",
                 L"이번엔 누군가가 정해 준 내가 아니라, 내가 원하는 모습으로 만나고 싶어."},
                {L"이현",
                 L"피해 없는 연결 방식부터 검증하겠습니다. 이미 잃은 기억이 돌아온다고 약속하지는 않겠습니다."}};
            result.responses[1] = {
                {L"플레이어", L"하린의 방송 연결은 중단하고, 피해 회복과 기록 보존에 집중해요."},
                {L"세아",
                 L"널 다시 볼 수 있을지는 모르겠어. 그래도 우리가 웃었던 날들은 사라지지 않아."},
                {L"이현",
                 L"문제의 연결 기능을 중단합니다. 어떤 유실체도 이 선택 때문에 소멸시키지 않습니다."}};
            return result;
        case 15:
            return Lines(
                {{L"하린", L"세아야. 다음에 만나면…… 우리 방송 말고 그냥 놀자."},
                 {L"미온", L"돌아왔군요. 오늘도 늘 먹던 걸로?"},
                 {L"플레이어", L"네. 세아 것도 같이 부탁드려요."},
                 {L"미온", L"좋아요. 따뜻하게 준비해 둘게요. 오늘은 천천히 먹고 가요."},
                 {L"세아", L"……오늘은 그냥 그게 먹고 싶네. 돌아올 곳이 있어서 다행이야."},
                 {L"기록",
                  m_EndingChoice == 0
                      ? L"동의와 안전성을 검증하는 연구가 시작됐다. 하린의 기원과 잔향의 근본 원리는 다음 이야기로 남는다."
                      : L"방송 연결을 중단하고 피해 회복과 기록 보존을 시작했다. 하린의 향후 상태와 잔향의 원리는 다음 이야기로 남는다."}});
        default:
            return result;
        }
    }

    QuestScene QuestState::SideScene(QuestTarget target) const
    {
        if (target == QuestTarget::Sea)
        {
            if (m_SideSteps[0] == 0)
            {
                return Lines(
                    {{L"세아",
                      L"우리 첫 합방, 거창하게 말고 동네 소개부터 해 볼까? 멋진 오프닝은 내가 맡을게!"},
                     {L"플레이어", L"오프닝 세 번 연속 혀 꼬인 건 편집해 줄게."},
                     {L"세아",
                      L"그건 시청자들이 좋아하는 부분이거든! 현실에서 [B]로 방송을 켜고 20초쯤 함께 동네를 보여 준 뒤 다시 와 줘."}});
            }
            if (m_SideSteps[0] == 1)
            {
                return Lines(
                    {{L"세아",
                      L"아직 인트로 찍는 중이야? 현실에서 방송을 켜고 조금 더 돌아보자. 실수해도 그게 우리 방송이지!"}});
            }
            if (m_SideSteps[0] == 2)
            {
                return Lines(
                    {{L"시청자 채팅", L"오프닝보다 엔딩 인사가 더 긴 방송 ㅋㅋㅋ"},
                     {L"세아",
                      L"완벽하진 않았지만 같이 웃었잖아. 다음엔 실수부터 방송 제목에 넣자!"},
                     {L"플레이어", L"함께 실패해도 괜찮다는 건 꽤 좋은 발견인 것 같아."}});
            }
        }
        if (target == QuestTarget::Echo && m_KnownDeaths > 0)
        {
            if (m_SideSteps[1] == 0)
            {
                return Lines(
                    {{L"유실체", L"너에게서 시작했지만, 나도 좋아하는 곳을 찾아보고 싶어."},
                     {L"플레이어", L"어떤 곳이 궁금해?"},
                     {L"유실체",
                      L"루미아 서쪽에 빛이 머무는 숲이 있대. 조금 돌아가더라도, 그 길을 보고 싶어."},
                     {L"플레이어",
                      L"오늘은 네가 가고 싶은 데부터 가자. 숲을 살펴보고 네게 알려 줄게."}});
            }
            if (m_SideSteps[1] == 1)
            {
                return Lines(
                    {{L"유실체",
                      L"그 숲에는 어떤 색의 빛이 있을까? 나를 위해 알아봐 주는 사람이 있다는 게 신기해."}});
            }
            if (m_SideSteps[1] == 2)
            {
                return Lines(
                    {{L"플레이어", L"숲을 보고 왔어. 길 끝에서 빛이 천천히 머물더라."},
                     {L"유실체",
                      L"그럼 내 거처에도 작은 숲 장식을 놓고 싶어. 내가 좋아하는 걸 기억할 수 있게."},
                     {L"기록",
                      L"유실체의 거처에 숲을 닮은 장식이 남는다. 이 바람은 플레이어의 복제가 아니라, 그 존재가 고른 삶의 한 부분이다."}});
            }
        }
        if (target == QuestTarget::Grove && m_SideSteps[1] == 1 && m_KnownDeaths > 0)
        {
            return Lines(
                {{L"기록",
                  L"나뭇잎 같은 빛이 고요히 흔들린다. 유실체에게 전할 숲의 풍경을 기록했다."},
                 {L"플레이어", L"돌아가면 어떤 장식을 놓고 싶은지 물어봐야겠다."}});
        }
        if (target == QuestTarget::Ihyeon && m_MainStep >= 12)
        {
            if (m_SideSteps[2] == 0)
            {
                return Lines(
                    {{L"이현",
                      L"플랫폼의 피해 경로는 찾았지만, 모든 잔향이 그 시스템으로 설명되지는 않습니다."},
                     {L"이현",
                      L"아직 발견하지 않은 기록 두 곳을 조사해 주세요. 기존 데이터베이스에 없는 목소리가 있는지 비교해 보고 싶어요."},
                     {L"플레이어",
                      L"하린의 정체를 섣불리 정하지 않고, 새로운 흔적부터 살펴볼게요."}});
            }
            if (m_SideSteps[2] == 1)
            {
                return Lines(
                    {{L"이현",
                      L"새로 발견한 기록이 더 필요합니다. 이미 조사한 장소를 다시 확인하는 것은 새 증거로 세지 않겠습니다."}});
            }
            if (m_SideSteps[2] == 2)
            {
                return Lines(
                    {{L"이현", L"두 기록 모두 기존 목록에 없는 반응을 포함하고 있군요."},
                     {L"이현",
                      L"플랫폼 문제와 잔향의 근본 원인이 같다고 단정할 수는 없습니다. 다음 조사를 위한 자료로 보존하죠."},
                     {L"플레이어", L"설명할 수 없다고 해서, 그 목소리가 없었던 건 아니니까요."}});
            }
        }
        return QuestScene();
    }

    QuestScene QuestState::Scene(QuestTarget target) const
    {
        if (IsMainTarget(target))
        {
            return MainScene();
        }
        QuestScene side = SideScene(target);
        if (!side.lines.empty())
        {
            return side;
        }
        switch (target)
        {
        case QuestTarget::Mion:
            return Lines(
                {{L"미온",
                  EpisodeFinished()
                      ? L"오늘도 늘 먹던 걸로? 세아 씨 몫도 따뜻하게 준비해 둘게요. 천천히 먹고 가요."
                      : L"밥 먹는 동안만큼은 너무 서두르지 말아요. 세아 씨가 좋아하는 음료도 챙겨 둘게요."}});
        case QuestTarget::Sea:
            return Lines(
                {{L"세아",
                  m_MainStep >= 12
                      ? L"방송보다 중요한 게 있다는 걸 배웠어. 그래도 함께 웃는 일까지 그만둘 필요는 없겠지?"
                      : L"오늘 방송은 무조건 성공한다! ……실수하면 그것도 우리만의 장면이지!"}});
        case QuestTarget::Doyun:
            return Lines(
                {{L"도윤",
                  m_MainStep >= 6
                      ? L"아직 기억나지 않아요. 그래도 포기하고 싶진 않아요. 사진을 없애지 않고 간직할게요."
                      : L"이 거리를 걷다 보면 무언가 떠오를 것 같은데…… 지금은 조용히 생각하고 싶어요."}});
        case QuestTarget::Yunseo:
            return Lines(
                {{L"윤서",
                  m_MainStep >= 12
                      ? L"이제 즐거움을 누가 책임지는지도 생각하려고. 사람의 마음을 숫자로만 볼 수는 없으니까."
                      : L"시청자가 즐거우면 성공한 방송이지! 오늘은 어떤 이야기를 같이 보여 줄까?"}});
        case QuestTarget::Ihyeon:
            return Lines(
                {{L"이현",
                  EpisodeFinished()
                      ? L"기록만으로 설명하지 못하는 삶도 있군요. 알지 못하는 부분은 알지 못한다고 남겨 두겠습니다."
                      : L"기록과 증거부터 확인해야 합니다. 아직 모르는 원인을 먼저 결론 내리지는 않겠습니다."}});
        case QuestTarget::Harin:
            if (m_MainStep >= 12)
            {
                return Lines(
                    {{L"하린",
                      L"내가 누구인지 아직 다 알지는 못해. 그래도 지금 누군가와 이야기하는 이 순간은 소중해."}});
            }
            return Lines(
                {{L"방송 단말",
                  L"안정적인 대화 연결은 아직 열리지 않았습니다. 현재 메인 목표의 송출 기록부터 조사해 주세요."}});
        case QuestTarget::Echo:
            if (m_KnownDeaths > 0)
            {
                return Lines(
                    {{L"유실체",
                      GroveDecorated()
                          ? L"그 작은 숲 장식이 마음에 들어. 언젠가는 내가 고른 다음 길도 이야기해 줄게."
                          : L"나도 아직 내가 누구인지 알아가는 중이야. 서두르지 않고 이야기해 줘서 고마워."}});
            }
            return QuestScene();
        default:
            return QuestScene();
        }
    }

    bool QuestState::Complete(QuestTarget target, int choice, QuestReward& reward)
    {
        reward = QuestReward();
        notification.clear();
        if (!m_MetricsInitialized || TargetRealm(target) != m_Realm)
        {
            return false;
        }
        const QuestScene scene = Scene(target);
        if (scene.lines.empty() || (scene.hasChoice && (choice < 0 || choice > 1))
            || (!scene.hasChoice && choice != -1))
        {
            return false;
        }
        bool changed = false;
        if (IsMainTarget(target))
        {
            if ((m_MainStep == 10 || m_MainStep == 13) && m_KnownBosses < 1)
            {
                notification =
                    L"보스 처치 증거가 필요합니다 · 루미아 보스 한 체를 처치하고 단말로 돌아오세요.";
                return false;
            }
            if (m_MainStep == 1)
            {
                m_IntroChoice = choice;
            }
            else if (m_MainStep == 7)
            {
                m_PolicyChoice = choice;
            }
            else if (m_MainStep == 13)
            {
                m_LinksDisabled = true;
            }
            else if (m_MainStep == 14)
            {
                if (!m_LinksDisabled)
                {
                    return false;
                }
                m_EndingChoice = choice;
            }
            ++m_MainStep;
            changed = true;
            notification =
                L"메인 목표를 기록했습니다 · "
                + std::wstring(m_MainStep < MainStepCount ? MainSteps[m_MainStep].objective
                                                          : L"첫 번째 에피소드 완료");
            for (int chapter = 0; chapter < 5; ++chapter)
            {
                if (m_MainStep == ChapterEnds[chapter + 1])
                {
                    reward.xp += ChapterXP[chapter];
                    reward.fragments += ChapterFragments[chapter];
                    reward.cores += ChapterCores[chapter];
                    notification =
                        std::wstring(ChapterTitles[chapter]) + L" 완료 · 보상을 받았습니다.";
                }
            }
        }
        else if (target == QuestTarget::Sea && m_SideSteps[0] == 0)
        {
            m_SideSteps[0] = 1;
            changed = true;
            notification = L"외전 시작 · 세아의 첫 합방 · 현실에서 [B] 방송 20초";
        }
        else if (target == QuestTarget::Sea && m_SideSteps[0] == 2)
        {
            m_SideSteps[0] = 3;
            reward = {35, 8, 0};
            changed = true;
            notification = L"외전 완료 · 세아의 첫 합방 · 함께한 실수도 우리의 이야기입니다.";
        }
        else if (target == QuestTarget::Echo && m_KnownDeaths > 0 && m_SideSteps[1] == 0)
        {
            m_SideSteps[1] = 1;
            changed = true;
            notification = L"외전 시작 · 내가 선택한 길 · 빛이 머무는 숲 조사";
        }
        else if (target == QuestTarget::Grove && m_KnownDeaths > 0 && m_SideSteps[1] == 1)
        {
            m_SideSteps[1] = 2;
            changed = true;
            notification = L"숲의 풍경을 기록했습니다 · 유실체에게 돌아가 이야기하세요.";
        }
        else if (target == QuestTarget::Echo && m_KnownDeaths > 0 && m_SideSteps[1] == 2)
        {
            m_SideSteps[1] = 3;
            reward = {50, 10, 1};
            changed = true;
            notification = L"외전 완료 · 내가 선택한 길 · 숲 장식이 세계에 남습니다.";
        }
        else if (target == QuestTarget::Ihyeon && m_MainStep >= 12 && m_SideSteps[2] == 0)
        {
            m_SideSteps[2] = 1;
            changed = true;
            notification = L"외전 시작 · 기록되지 않은 목소리 · 새로운 발견 두 곳 조사";
        }
        else if (target == QuestTarget::Ihyeon && m_MainStep >= 12 && m_SideSteps[2] == 2)
        {
            m_SideSteps[2] = 3;
            reward = {50, 8, 1};
            changed = true;
            notification = L"외전 완료 · 기록되지 않은 목소리 · 밝혀지지 않은 단서를 보존합니다.";
        }
        if (m_Realm == 0 && (target == QuestTarget::Sea || target == QuestTarget::Mion)
            && m_DailyProgress[0] == 0)
        {
            m_DailyProgress[0] = 1;
            changed = true;
            if (notification.empty())
            {
                notification = L"오늘의 인사 완료 · [K] 일일 의뢰에서 보상을 받으세요.";
            }
        }
        return changed;
    }

    bool QuestState::Update(const QuestMetrics& metrics, QuestReward& reward)
    {
        reward = QuestReward();
        notification.clear();
        const int today = metrics.today > 0 ? metrics.today : CurrentDay();
        if ((metrics.realm != 0 && metrics.realm != 1) || !ValidCounter(metrics.kills)
            || !ValidCounter(metrics.bosses) || metrics.bosses > metrics.kills
            || !ValidCounter(metrics.discoveries) || !ValidCounter(metrics.deaths)
            || !std::isfinite(metrics.dt) || metrics.dt < 0.0 || !ValidDay(today) || today == 0)
        {
            return false;
        }
        int freshKills = 0;
        int freshDiscoveries = 0;
        if (m_MetricsInitialized)
        {
            freshKills = std::max(0, metrics.kills - m_KnownKills);
            freshDiscoveries = std::max(0, metrics.discoveries - m_KnownDiscoveries);
        }
        m_MetricsInitialized = true;
        m_Realm = metrics.realm;
        m_KnownKills = std::max(m_KnownKills, metrics.kills);
        m_KnownBosses = std::max(m_KnownBosses, metrics.bosses);
        m_KnownDiscoveries = std::max(m_KnownDiscoveries, metrics.discoveries);
        m_KnownDeaths = std::max(m_KnownDeaths, metrics.deaths);
        bool changed = false;
        if (today > m_Day)
        {
            m_Day = today;
            m_DailyProgress = {};
            m_DailyClaimed = {};
            changed = true;
            notification = L"새로운 하루의 의뢰가 도착했습니다 · [K] 퀘스트";
        }
        const int dailyKills = std::min(DailyRequired[1], m_DailyProgress[1] + freshKills);
        const int dailyDiscoveries =
            std::min(DailyRequired[2], m_DailyProgress[2] + freshDiscoveries);
        changed =
            changed || dailyKills != m_DailyProgress[1] || dailyDiscoveries != m_DailyProgress[2];
        m_DailyProgress[1] = dailyKills;
        m_DailyProgress[2] = dailyDiscoveries;
        if (m_SideSteps[0] == 1 && metrics.realm == 0 && metrics.broadcasting)
        {
            m_CollabSeconds = std::min(20.0, m_CollabSeconds + std::min(20.0, metrics.dt));
            if (m_CollabSeconds >= 20.0)
            {
                m_SideSteps[0] = 2;
                changed = true;
                notification = L"합방 기록을 확보했습니다 · 세아에게 돌아가세요.";
            }
        }
        if (m_SideSteps[2] == 1 && freshDiscoveries > 0)
        {
            m_VoiceDiscoveries = std::min(2, m_VoiceDiscoveries + freshDiscoveries);
            changed = true;
            if (m_VoiceDiscoveries >= 2)
            {
                m_SideSteps[2] = 2;
                notification = L"두 개의 새로운 목소리를 기록했습니다 · 이현에게 돌아가세요.";
            }
        }
        return changed;
    }

    std::vector<QuestEntry> QuestState::Entries(int category) const
    {
        std::vector<QuestEntry> entries;
        if (category == 0)
        {
            for (int chapter = 0; chapter < 5; ++chapter)
            {
                QuestEntry entry;
                const int begin = ChapterEnds[chapter];
                const int end = ChapterEnds[chapter + 1];
                entry.title = m_MainStep >= begin ? ChapterTitles[chapter]
                                                  : L"CH0" + std::to_wstring(chapter + 1)
                                                        + L" · 아직 닿지 않은 이야기";
                entry.required = end - begin;
                entry.progress = std::clamp(m_MainStep - begin, 0, entry.required);
                entry.complete = m_MainStep >= end;
                entry.rewardXP = ChapterXP[chapter];
                entry.rewardFragments = ChapterFragments[chapter];
                entry.rewardCores = ChapterCores[chapter];
                if (entry.complete)
                {
                    entry.objective = L"완료 · 보상을 받았습니다.";
                    entry.detail = L"이 장에서 확인한 사실과 선택은 저장됩니다.";
                    if (chapter == 0)
                    {
                        entry.detail = m_IntroChoice == 0 ? L"선택 기록 · 송출 위치 추적"
                                                          : L"선택 기록 · 세아의 마음 먼저 살피기";
                    }
                    else if (chapter == 2)
                    {
                        entry.detail = m_PolicyChoice == 0 ? L"선택 기록 · 방송 중단 요청"
                                                           : L"선택 기록 · 신규 접속 제한";
                    }
                    else if (chapter == 4)
                    {
                        entry.detail = m_EndingChoice == 0
                                           ? L"후일담 · 동의와 안전을 검증하는 제한 연구"
                                           : L"후일담 · 방송 연결 중단과 피해 회복";
                    }
                }
                else if (m_MainStep >= begin)
                {
                    entry.objective = MainSteps[m_MainStep].objective;
                    entry.target = MainSteps[m_MainStep].target;
                    entry.realm = MainSteps[m_MainStep].realm;
                    entry.detail =
                        L"대화를 끝까지 듣고 선택하면 진행됩니다. [E] 상호작용 · [TAB] 세계 전환";
                    if (m_MainStep == 10 && m_KnownBosses == 0)
                    {
                        entry.detail =
                            L"루미아 보스 1체 처치 필요 · 시작 구역 보스 (6, 4) · 사망은 필수가 아닙니다.";
                    }
                }
                else
                {
                    entry.objective = L"앞선 이야기를 진행하면 열립니다.";
                    entry.detail = L"아직 조사하지 않은 사건의 내용은 공개되지 않습니다.";
                }
                entries.push_back(entry);
            }
            return entries;
        }
        if (category == 1)
        {
            QuestEntry collab;
            collab.title = L"외전 · 세아의 첫 합방";
            collab.progress = m_SideSteps[0];
            collab.required = 3;
            collab.complete = m_SideSteps[0] == 3;
            collab.target = QuestTarget::Sea;
            collab.realm = 0;
            collab.rewardXP = 35;
            collab.rewardFragments = 8;
            collab.objective = m_SideSteps[0] == 0 ? L"세아에게 첫 합방 이야기 듣기"
                               : m_SideSteps[0] == 1
                                   ? L"현실에서 방송하기 · "
                                         + std::to_wstring(static_cast<int>(m_CollabSeconds))
                                         + L" / 20초"
                               : m_SideSteps[0] == 2 ? L"세아에게 돌아가 합방 마무리하기"
                                                     : L"완료 · 함께한 실수도 우리의 이야기";
            collab.detail = L"[B]로 방송을 켭니다. 현실에서 보낸 방송 시간은 저장됩니다.";
            entries.push_back(collab);

            QuestEntry path;
            path.title = L"외전 · 내가 선택한 길";
            path.progress = m_SideSteps[1];
            path.required = 3;
            path.complete = GroveDecorated();
            path.target = m_SideSteps[1] == 1 ? QuestTarget::Grove : QuestTarget::Echo;
            path.realm = 1;
            path.rewardXP = 50;
            path.rewardFragments = 10;
            path.rewardCores = 1;
            path.objective = m_KnownDeaths == 0    ? L"만난 유실체가 있다면 교류하기 · 선택형 외전"
                             : m_SideSteps[1] == 0 ? L"유실체가 바라는 길 이야기 듣기"
                             : m_SideSteps[1] == 1 ? L"빛이 머무는 숲 조사하기"
                             : m_SideSteps[1] == 2 ? L"유실체에게 숲의 풍경 전하기"
                                                   : L"완료 · 숲 장식이 거처에 남았습니다.";
            path.detail =
                L"실제 태어난 유실체만 등장합니다. 메인 진행에 사망이나 이 외전은 필요하지 않습니다.";
            entries.push_back(path);

            QuestEntry voice;
            voice.title = L"외전 · 기록되지 않은 목소리";
            voice.progress = m_SideSteps[2];
            voice.required = 3;
            voice.complete = m_SideSteps[2] == 3;
            voice.realm = 0;
            voice.target = m_MainStep >= 12 ? QuestTarget::Ihyeon : QuestTarget::None;
            voice.rewardXP = 50;
            voice.rewardFragments = 8;
            voice.rewardCores = 1;
            voice.objective =
                m_MainStep < 12       ? L"메인 4장을 마친 뒤 이현과 이야기하기"
                : m_SideSteps[2] == 0 ? L"이현에게 설명되지 않은 잔향 묻기"
                : m_SideSteps[2] == 1
                    ? L"새로운 기록 조사 · " + std::to_wstring(m_VoiceDiscoveries) + L" / 2"
                : m_SideSteps[2] == 2 ? L"이현에게 새 기록 전달하기"
                                      : L"완료 · 설명되지 않은 목소리를 보존했습니다.";
            voice.detail =
                L"의뢰를 받은 뒤의 새로운 발견만 셉니다. 이미 조사한 장소의 재방문은 제외됩니다.";
            entries.push_back(voice);
            return entries;
        }
        if (category == 2)
        {
            const wchar_t* titles[] = {L"일일 · 오늘의 인사",
                                       L"일일 · 기록 거리 정리",
                                       L"일일 · 오늘의 발견"};
            const wchar_t* objectives[] = {L"현실에서 세아 또는 미온과 대화하기",
                                           L"루미아에서 몬스터 5체 새로 처치하기",
                                           L"어느 세계에서든 새로운 기록 1곳 발견하기"};
            for (size_t index = 0; index < 3; ++index)
            {
                QuestEntry entry;
                entry.title = titles[index];
                entry.objective = objectives[index];
                entry.detail = L"한국 시간 자정에 갱신 · [F] 준비된 일일 보상 받기";
                entry.progress = m_DailyProgress[index];
                entry.required = DailyRequired[index];
                entry.complete = m_DailyClaimed[index];
                entry.claimable = !entry.complete && entry.progress >= entry.required;
                entry.realm = index == 1 ? 1 : 0;
                entry.target = index == 0 ? QuestTarget::Sea : QuestTarget::None;
                entry.rewardXP = DailyXP[index];
                entry.rewardFragments = DailyFragments[index];
                if (entry.complete)
                {
                    entry.detail = L"오늘의 보상을 받았습니다. 다음 날 새 의뢰가 도착합니다.";
                }
                entries.push_back(entry);
            }
        }
        return entries;
    }

    QuestEntry QuestState::Tracked() const
    {
        for (const QuestEntry& entry : Entries(0))
        {
            if (!entry.complete)
            {
                return entry;
            }
        }
        const auto sides = Entries(1);
        for (size_t index = 0; index < sides.size(); ++index)
        {
            if (!sides[index].complete && (index != 1 || m_KnownDeaths > 0))
            {
                return sides[index];
            }
        }
        for (const QuestEntry& entry : Entries(2))
        {
            if (!entry.complete)
            {
                return entry;
            }
        }
        QuestEntry entry;
        entry.title = L"다음 이야기까지";
        entry.objective = L"두 세계를 자유롭게 탐험하며 새로운 기록을 남기세요.";
        entry.detail = L"하린의 기원과 잔향의 근본 원리는 아직 밝혀지지 않았습니다.";
        entry.complete = true;
        return entry;
    }

    bool QuestState::ClaimDaily(size_t index, QuestReward& reward)
    {
        reward = QuestReward();
        notification.clear();
        if (index >= m_DailyProgress.size() || m_DailyClaimed[index]
            || m_DailyProgress[index] < DailyRequired[index])
        {
            return false;
        }
        m_DailyClaimed[index] = true;
        reward = {DailyXP[index], DailyFragments[index], 0};
        notification = L"일일 의뢰 보상을 받았습니다 · 같은 의뢰는 오늘 다시 받을 수 없습니다.";
        return true;
    }

    bool QuestState::EpisodeFinished() const
    {
        return m_MainStep == MainStepCount;
    }

    bool QuestState::LinksDisabled() const
    {
        return m_LinksDisabled;
    }

    bool QuestState::GroveDecorated() const
    {
        return m_SideSteps[1] == 3;
    }

    int QuestState::CurrentDay()
    {
        const std::time_t korea = std::time(nullptr) + 9 * 60 * 60;
        std::tm date = {};
#ifdef _WIN32
        if (gmtime_s(&date, &korea) != 0)
        {
            return 0;
        }
#else
        if (!gmtime_r(&korea, &date))
        {
            return 0;
        }
#endif
        return (date.tm_year + 1900) * 10000 + (date.tm_mon + 1) * 100 + date.tm_mday;
    }

    bool QuestState::Valid() const
    {
        if (m_MainStep < 0 || m_MainStep > MainStepCount || !ValidDay(m_Day)
            || !std::isfinite(m_CollabSeconds) || m_CollabSeconds < 0.0 || m_CollabSeconds > 20.0
            || m_VoiceDiscoveries < 0 || m_VoiceDiscoveries > 2 || (m_Realm != 0 && m_Realm != 1)
            || !ValidCounter(m_KnownKills) || !ValidCounter(m_KnownBosses)
            || m_KnownBosses > m_KnownKills || !ValidCounter(m_KnownDiscoveries)
            || !ValidCounter(m_KnownDeaths))
        {
            return false;
        }
        const auto validChoice = [](int choice, bool reached)
        {
            return reached ? choice >= 0 && choice <= 1 : choice == -1;
        };
        if (!validChoice(m_IntroChoice, m_MainStep >= 2)
            || !validChoice(m_PolicyChoice, m_MainStep >= 8)
            || !validChoice(m_EndingChoice, m_MainStep >= 15)
            || m_LinksDisabled != (m_MainStep >= 14))
        {
            return false;
        }
        for (size_t index = 0; index < 3; ++index)
        {
            if (m_SideSteps[index] < 0 || m_SideSteps[index] > 3 || m_DailyProgress[index] < 0
                || m_DailyProgress[index] > DailyRequired[index]
                || (m_DailyClaimed[index] && m_DailyProgress[index] != DailyRequired[index]))
            {
                return false;
            }
        }
        if ((m_SideSteps[0] == 0 && m_CollabSeconds != 0.0)
            || (m_SideSteps[0] == 1 && m_CollabSeconds >= 20.0)
            || (m_SideSteps[0] >= 2 && m_CollabSeconds != 20.0)
            || (m_SideSteps[2] == 0 && m_VoiceDiscoveries != 0)
            || (m_SideSteps[2] == 1 && m_VoiceDiscoveries >= 2)
            || (m_SideSteps[2] >= 2 && m_VoiceDiscoveries != 2)
            || (m_SideSteps[1] > 0 && m_KnownDeaths == 0) || (m_SideSteps[2] > 0 && m_MainStep < 12)
            || (m_MainStep >= 11 && m_KnownBosses == 0))
        {
            return false;
        }
        if (!m_MetricsInitialized
            && (m_MainStep != 0 || m_SideSteps != std::array<int, 3>{} || m_KnownKills != 0
                || m_KnownBosses != 0 || m_KnownDiscoveries != 0 || m_KnownDeaths != 0))
        {
            return false;
        }
        return m_Day != 0
               || (m_DailyProgress == std::array<int, 3>{}
                   && m_DailyClaimed == std::array<bool, 3>{});
    }

    void QuestState::Write(std::ostream& output) const
    {
        output << "QUESTS 1\n"
               << m_MainStep << ' ' << m_IntroChoice << ' ' << m_PolicyChoice << ' '
               << m_EndingChoice << ' ' << (m_LinksDisabled ? 1 : 0) << '\n';
        output << m_SideSteps[0] << ' ' << m_SideSteps[1] << ' ' << m_SideSteps[2] << ' '
               << std::setprecision(17) << m_CollabSeconds << ' ' << m_VoiceDiscoveries << '\n';
        output << m_Day;
        for (int progress : m_DailyProgress)
        {
            output << ' ' << progress;
        }
        for (bool claimed : m_DailyClaimed)
        {
            output << ' ' << (claimed ? 1 : 0);
        }
        output << '\n'
               << (m_MetricsInitialized ? 1 : 0) << ' ' << m_Realm << ' ' << m_KnownKills << ' '
               << m_KnownBosses << ' ' << m_KnownDiscoveries << ' ' << m_KnownDeaths
               << "\nQUEST_END\n";
    }

    bool QuestState::Read(std::istream& input)
    {
        QuestState candidate;
        std::string token;
        int version = 0;
        int links = 0;
        int initialized = 0;
        if (!(input >> token >> version) || token != "QUESTS" || version != 1
            || !(input >> candidate.m_MainStep >> candidate.m_IntroChoice
                 >> candidate.m_PolicyChoice >> candidate.m_EndingChoice >> links)
            || (links != 0 && links != 1)
            || !(input >> candidate.m_SideSteps[0] >> candidate.m_SideSteps[1]
                 >> candidate.m_SideSteps[2] >> candidate.m_CollabSeconds
                 >> candidate.m_VoiceDiscoveries >> candidate.m_Day))
        {
            return false;
        }
        candidate.m_LinksDisabled = links == 1;
        for (int& progress : candidate.m_DailyProgress)
        {
            if (!(input >> progress))
            {
                return false;
            }
        }
        for (size_t index = 0; index < candidate.m_DailyClaimed.size(); ++index)
        {
            int claimed = 0;
            if (!(input >> claimed) || (claimed != 0 && claimed != 1))
            {
                return false;
            }
            candidate.m_DailyClaimed[index] = claimed == 1;
        }
        if (!(input >> initialized >> candidate.m_Realm >> candidate.m_KnownKills
              >> candidate.m_KnownBosses >> candidate.m_KnownDiscoveries >> candidate.m_KnownDeaths
              >> token)
            || (initialized != 0 && initialized != 1) || token != "QUEST_END")
        {
            return false;
        }
        candidate.m_MetricsInitialized = initialized == 1;
        if (!candidate.Valid())
        {
            return false;
        }
        *this = std::move(candidate);
        return true;
    }
} // namespace echo
