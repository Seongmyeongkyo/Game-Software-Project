#include "stdafx.h"
#include "GameState.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <set>
#include <sstream>
#include <tuple>

namespace
{
    const size_t MaxEchoes = 100000;
    const size_t MaxDiscoveries = 250000;
    const LONGLONG MaxSaveBytes = 64LL * 1024LL * 1024LL;

    std::wstring SaveDirectory()
    {
        std::vector<wchar_t> path(32768, L'\0');
        const DWORD length =
            GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (length == 0 || length >= path.size())
        {
            return std::wstring();
        }
        std::wstring directory(path.data(), length);
        const size_t slash = directory.find_last_of(L"\\/");
        if (slash == std::wstring::npos)
        {
            return std::wstring();
        }
        return directory.substr(0, slash + 1) + L"Saves";
    }

    bool ValidCoordinate(double value)
    {
        // Prototype tiles currently use 32-bit cell coordinates. This is a numeric
        // safety limit for this renderer, not a final boundary for the game world.
        return std::isfinite(value) && std::abs(value) < 1.0e7;
    }

    bool ValidProfile(float protection, float aggression, float curiosity)
    {
        const float sum = protection + aggression + curiosity;
        return std::isfinite(protection) && std::isfinite(aggression) && std::isfinite(curiosity)
               && protection >= 0.0f && protection <= 1.0f && aggression >= 0.0f
               && aggression <= 1.0f && curiosity >= 0.0f && curiosity <= 1.0f && sum > 0.99f
               && sum < 1.01f;
    }

    std::wstring BackupSuffix()
    {
        SYSTEMTIME time;
        GetLocalTime(&time);
        std::wostringstream name;
        name << L".unreadable_" << std::setfill(L'0') << std::setw(4) << time.wYear << std::setw(2)
             << time.wMonth << std::setw(2) << time.wDay << L"_" << std::setw(2) << time.wHour
             << std::setw(2) << time.wMinute << std::setw(2) << time.wSecond << L"_" << std::setw(3)
             << time.wMilliseconds << L"_" << GetCurrentProcessId() << L".bak";
        return name.str();
    }
} // namespace

namespace echo
{
    GameState::GameState(bool loadSaved)
        : message(L"2050년, 현실의 하루와 루미아의 모험이 시작됩니다.")
    {
        if (loadSaved)
        {
            Load();
        }
    }

    void GameState::NormalizeProfile()
    {
        protection = std::max(0.0f, protection);
        aggression = std::max(0.0f, aggression);
        curiosity = std::max(0.0f, curiosity);
        const float total = protection + aggression + curiosity;
        if (total > 0.00001f)
        {
            protection /= total;
            aggression /= total;
            curiosity /= total;
        }
        else
        {
            protection = 0.4f;
            aggression = 0.2f;
            curiosity = 0.4f;
        }
    }

    void GameState::Update(double dt, float moveX, float moveY, bool sprint)
    {
        if (!std::isfinite(dt) || !std::isfinite(moveX) || !std::isfinite(moveY))
        {
            return;
        }
        dt = std::max(0.0, std::min(0.1, dt));
        const double length =
            std::sqrt(static_cast<double>(moveX) * moveX + static_cast<double>(moveY) * moveY);
        if (length > 0.00001)
        {
            const double distance = (sprint ? 5.4 : 3.0) * dt;
            const double nextX = x + (moveX / length) * distance;
            const double nextY = y + (moveY / length) * distance;
            if (ValidCoordinate(nextX) && ValidCoordinate(nextY))
            {
                x = nextX;
                y = nextY;
                travelled += distance;
                curiosity += static_cast<float>(distance * 0.002);
                NormalizeProfile();
            }
        }
        saveTimer += dt;
        if (saveTimer >= 10.0)
        {
            saveTimer = 0.0;
            Save();
        }
    }

    void GameState::SwitchRealm()
    {
        realm = realm == 0 ? 1 : 0;
        message = realm == 1 ? L"루미아 접속 완료 · 현실에서 이어진 선택이 이곳에도 남습니다."
                             : L"현실 도시로 돌아왔습니다 · 루미아의 존재와 발견은 보존됩니다.";
        Save();
    }

    void GameState::Die()
    {
        if (echoes.size() >= MaxEchoes || nextEchoId >= (std::numeric_limits<int>::max)())
        {
            message = L"프로토타입 보존 용량에 도달해 사망 처리를 중단했습니다.";
            return;
        }
        NormalizeProfile();
        EchoLife life;
        life.id = nextEchoId++;
        life.x = x;
        life.y = y;
        life.protection = protection;
        life.aggression = aggression;
        life.curiosity = curiosity;
        echoes.push_back(life);
        ++deaths;
        // Keep the new life at the death position and return the player beside it,
        // rather than covering both silhouettes when the death occurred at the hub.
        x = -1.0;
        y = 1.0;
        if (std::hypot(life.x - x, life.y - y) < 1.2)
        {
            x = 1.0;
            y = -1.0;
        }
        message = L"하나의 죽음, 하나의 생명 · 유실체 #" + std::to_wstring(life.id)
                  + L"가 당신의 성향을 이어받아 루미아에 태어났습니다.";
        Save();
    }

    void GameState::Act(bool protect)
    {
        if (protect)
        {
            protection += 0.12f;
            message = L"보호 행동을 기록했습니다 · 다음 생명에 이 성향이 이어집니다.";
        }
        else
        {
            aggression += 0.12f;
            message = L"돌파 행동을 기록했습니다 · 다음 생명에 이 패턴이 이어집니다.";
        }
        NormalizeProfile();
        Save();
    }

    bool GameState::HasDiscovery(int worldRealm, int cx, int cy) const
    {
        for (const Discovery& discovery : discoveries)
        {
            if (discovery.realm == worldRealm && discovery.cellX == cx && discovery.cellY == cy)
            {
                return true;
            }
        }
        return false;
    }

    bool GameState::Discover(int worldRealm, int cx, int cy)
    {
        if ((worldRealm != 0 && worldRealm != 1) || HasDiscovery(worldRealm, cx, cy))
        {
            return false;
        }
        if (discoveries.size() >= MaxDiscoveries)
        {
            message = L"프로토타입 발견 기록 용량에 도달했습니다.";
            return false;
        }
        Discovery discovery;
        discovery.realm = worldRealm;
        discovery.cellX = cx;
        discovery.cellY = cy;
        discoveries.push_back(discovery);
        curiosity += 0.08f;
        NormalizeProfile();
        message = worldRealm == 0
                      ? L"현실의 숨겨진 흔적을 발견했습니다 · 이 발견은 세계에 남습니다."
                      : L"루미아의 잔향을 발견했습니다 · 이 장소와 발견 기록이 보존됩니다.";
        Save();
        return true;
    }

    std::wstring GameState::ProfileName() const
    {
        if (protection >= aggression && protection >= curiosity)
        {
            return L"보호자";
        }
        if (aggression >= curiosity)
        {
            return L"돌파자";
        }
        return L"탐험가";
    }

    bool GameState::Save()
    {
        if (saveBlocked)
        {
            message = L"기존 저장 파일 보호 중 · 파일을 보관하지 못해 자동 저장이 중지되었습니다.";
            return false;
        }
        const std::wstring directory = SaveDirectory();
        if (directory.empty()
            || (!CreateDirectoryW(directory.c_str(), nullptr)
                && GetLastError() != ERROR_ALREADY_EXISTS))
        {
            message = L"저장 폴더에 접근할 수 없습니다. 현재 진행은 메모리에 유지됩니다.";
            return false;
        }
        if (!ValidCoordinate(x) || !ValidCoordinate(y) || !std::isfinite(travelled)
            || travelled < 0.0 || (realm != 0 && realm != 1) || deaths < 0
            || static_cast<size_t>(deaths) != echoes.size() || echoes.size() > MaxEchoes
            || discoveries.size() > MaxDiscoveries
            || !ValidProfile(protection, aggression, curiosity))
        {
            message = L"현재 상태 검증에 실패해 이전 저장 파일을 유지했습니다.";
            return false;
        }

        std::ostringstream output;
        output.imbue(std::locale::classic());
        output << std::setprecision(17) << "ECHO_PROTO_SAVE 1\n"
               << "STATE " << realm << ' ' << x << ' ' << y << ' ' << travelled << ' ' << deaths
               << ' ' << protection << ' ' << aggression << ' ' << curiosity << ' '
               << (broadcasting ? 1 : 0) << ' ' << nextEchoId << '\n'
               << "ECHOES " << echoes.size() << '\n';
        std::set<int> ids;
        for (const EchoLife& life : echoes)
        {
            if (life.id <= 0 || life.id >= nextEchoId || !ids.insert(life.id).second
                || !ValidCoordinate(life.x) || !ValidCoordinate(life.y)
                || !ValidProfile(life.protection, life.aggression, life.curiosity))
            {
                message = L"유실체 기록 검증에 실패해 이전 저장 파일을 유지했습니다.";
                return false;
            }
            output << life.id << ' ' << life.x << ' ' << life.y << ' ' << life.protection << ' '
                   << life.aggression << ' ' << life.curiosity << '\n';
        }
        output << "DISCOVERIES " << discoveries.size() << '\n';
        std::set<std::tuple<int, int, int>> discoveryKeys;
        for (const Discovery& discovery : discoveries)
        {
            if ((discovery.realm != 0 && discovery.realm != 1)
                || !discoveryKeys
                        .insert(std::make_tuple(discovery.realm, discovery.cellX, discovery.cellY))
                        .second)
            {
                message = L"발견 기록 검증에 실패해 이전 저장 파일을 유지했습니다.";
                return false;
            }
            output << discovery.realm << ' ' << discovery.cellX << ' ' << discovery.cellY << '\n';
        }
        output << "END\n";
        const std::string data = output.str();
        if (data.size() > static_cast<size_t>(MaxSaveBytes))
        {
            message = L"프로토타입 저장 용량을 초과해 이전 저장 파일을 유지했습니다.";
            return false;
        }
        const std::wstring destination = directory + L"\\prototype.save";
        const std::wstring temporary =
            destination + L"." + std::to_wstring(GetCurrentProcessId()) + L".tmp";
        HANDLE file = CreateFileW(temporary.c_str(),
                                  GENERIC_WRITE,
                                  0,
                                  nullptr,
                                  CREATE_ALWAYS,
                                  FILE_ATTRIBUTE_NORMAL,
                                  nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            message = L"저장 파일을 열 수 없습니다. 현재 진행은 메모리에 유지됩니다.";
            return false;
        }
        DWORD written = 0;
        const bool writeSucceeded =
            WriteFile(file, data.data(), static_cast<DWORD>(data.size()), &written, nullptr)
            && written == data.size() && FlushFileBuffers(file);
        CloseHandle(file);
        if (!writeSucceeded
            || !MoveFileExW(temporary.c_str(),
                            destination.c_str(),
                            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            DeleteFileW(temporary.c_str());
            message = L"저장을 완료하지 못했습니다. 이전 저장 파일은 유지됩니다.";
            return false;
        }
        saveTimer = 0.0;
        return true;
    }

    void GameState::PreserveUnreadableSave(const std::wstring& path)
    {
        const std::wstring backup = path + BackupSuffix();
        if (MoveFileExW(path.c_str(), backup.c_str(), MOVEFILE_WRITE_THROUGH))
        {
            saveBlocked = false;
            message = L"읽을 수 없는 이전 저장 파일을 .bak으로 보관했습니다. 새 기록을 시작합니다.";
        }
        else
        {
            saveBlocked = true;
            message = L"기존 저장 파일을 보호하기 위해 자동 저장을 중지했습니다.";
        }
    }

    bool GameState::Load()
    {
        const std::wstring directory = SaveDirectory();
        if (directory.empty())
        {
            saveBlocked = true;
            message = L"실행 경로를 확인하지 못해 저장을 중지했습니다.";
            return false;
        }
        const std::wstring path = directory + L"\\prototype.save";
        HANDLE file = CreateFileW(path.c_str(),
                                  GENERIC_READ,
                                  FILE_SHARE_READ,
                                  nullptr,
                                  OPEN_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL,
                                  nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            const DWORD error = GetLastError();
            if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND)
            {
                saveBlocked = true;
                message = L"기존 저장 파일에 접근할 수 없어 자동 저장을 중지했습니다.";
            }
            return false;
        }
        LARGE_INTEGER size;
        if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 || size.QuadPart > MaxSaveBytes)
        {
            CloseHandle(file);
            PreserveUnreadableSave(path);
            return false;
        }
        std::string data(static_cast<size_t>(size.QuadPart), '\0');
        DWORD read = 0;
        const bool readSucceeded =
            ReadFile(file, &data[0], static_cast<DWORD>(data.size()), &read, nullptr)
            && read == data.size();
        CloseHandle(file);
        if (!readSucceeded)
        {
            saveBlocked = true;
            message = L"저장 파일 읽기를 완료하지 못해 자동 저장을 중지했습니다.";
            return false;
        }

        std::istringstream input(data);
        input.imbue(std::locale::classic());
        std::string token;
        int version = 0;
        int savedRealm = 0;
        int savedDeaths = 0;
        int savedBroadcasting = 0;
        int savedNextId = 1;
        double savedX = 0.0;
        double savedY = 0.0;
        double savedTravelled = 0.0;
        float savedProtection = 0.0f;
        float savedAggression = 0.0f;
        float savedCuriosity = 0.0f;
        size_t echoCount = 0;
        size_t discoveryCount = 0;
        std::vector<EchoLife> savedEchoes;
        std::vector<Discovery> savedDiscoveries;
        bool valid = static_cast<bool>(input >> token >> version) && token == "ECHO_PROTO_SAVE"
                     && version == 1;
        valid = valid && static_cast<bool>(input >> token) && token == "STATE"
                && static_cast<bool>(input >> savedRealm >> savedX >> savedY >> savedTravelled
                                     >> savedDeaths >> savedProtection >> savedAggression
                                     >> savedCuriosity >> savedBroadcasting >> savedNextId);
        valid = valid && (savedRealm == 0 || savedRealm == 1) && ValidCoordinate(savedX)
                && ValidCoordinate(savedY) && std::isfinite(savedTravelled) && savedTravelled >= 0.0
                && savedDeaths >= 0 && savedNextId > 0
                && (savedBroadcasting == 0 || savedBroadcasting == 1)
                && ValidProfile(savedProtection, savedAggression, savedCuriosity);
        valid = valid && static_cast<bool>(input >> token >> echoCount) && token == "ECHOES"
                && echoCount <= MaxEchoes && echoCount == static_cast<size_t>(savedDeaths);
        std::set<int> ids;
        if (valid)
        {
            savedEchoes.reserve(echoCount);
        }
        for (size_t i = 0; valid && i < echoCount; ++i)
        {
            EchoLife life;
            valid = static_cast<bool>(input >> life.id >> life.x >> life.y >> life.protection
                                      >> life.aggression >> life.curiosity)
                    && life.id > 0 && life.id < savedNextId && ids.insert(life.id).second
                    && ValidCoordinate(life.x) && ValidCoordinate(life.y)
                    && ValidProfile(life.protection, life.aggression, life.curiosity);
            if (valid)
            {
                savedEchoes.push_back(life);
            }
        }
        valid = valid && static_cast<bool>(input >> token >> discoveryCount)
                && token == "DISCOVERIES" && discoveryCount <= MaxDiscoveries;
        std::set<std::tuple<int, int, int>> discoveryKeys;
        if (valid)
        {
            savedDiscoveries.reserve(discoveryCount);
        }
        for (size_t i = 0; valid && i < discoveryCount; ++i)
        {
            Discovery discovery;
            valid =
                static_cast<bool>(input >> discovery.realm >> discovery.cellX >> discovery.cellY)
                && (discovery.realm == 0 || discovery.realm == 1)
                && discoveryKeys
                       .insert(std::make_tuple(discovery.realm, discovery.cellX, discovery.cellY))
                       .second;
            if (valid)
            {
                savedDiscoveries.push_back(discovery);
            }
        }
        valid = valid && static_cast<bool>(input >> token) && token == "END";
        if (valid)
        {
            input >> std::ws;
            valid = input.eof();
        }
        if (!valid)
        {
            PreserveUnreadableSave(path);
            return false;
        }

        realm = savedRealm;
        x = savedX;
        y = savedY;
        travelled = savedTravelled;
        deaths = savedDeaths;
        protection = savedProtection;
        aggression = savedAggression;
        curiosity = savedCuriosity;
        broadcasting = savedBroadcasting != 0;
        nextEchoId = savedNextId;
        echoes.swap(savedEchoes);
        discoveries.swap(savedDiscoveries);
        saveTimer = 0.0;
        saveBlocked = false;
        message = L"이전 세계 기록을 이어갑니다 · 발견과 유실체가 보존되어 있습니다.";
        return true;
    }
} // namespace echo
