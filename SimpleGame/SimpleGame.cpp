/*
Copyright 2022 Lee Taek Hee (Tech University of Korea)

This program is free software: you can redistribute it and/or modify
it under the terms of the What The Hell License. Do it plz.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY.
*/

#include "stdafx.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <shellapi.h>
#include <objidl.h>
#include <gdiplus.h>
#include "Dependencies/glew.h"
#include "Dependencies/freeglut.h"
#include "Renderer.h"
#include "GameState.h"
#include "PrototypeScene.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#pragma comment(lib, "gdiplus.lib")

namespace
{
    std::unique_ptr<Renderer> renderer;
    std::unique_ptr<echo::GameState> state;
    prototype::View view;
    bool keys[256] = {};
    bool arrowLeft = false, arrowRight = false, arrowUp = false, arrowDown = false;
    bool shiftPressed = false;
    bool captureMode = false;
    bool shuttingDown = false;
    int captureRealm = 0;
    int captureFrames = 0;
    int resultCode = 0;
    int previousTime = 0;
    std::wstring capturePath;

    double Distance(double ax, double ay, double bx, double by)
    {
        const double dx = ax - bx;
        const double dy = ay - by;
        return std::sqrt(dx * dx + dy * dy);
    }

    void Cleanup()
    {
        // Destroy GL resources while GLUT's context still exists.
        renderer.reset();
    }

    void ExitGame()
    {
        if (shuttingDown) return;
        shuttingDown = true;
        if (state && !captureMode) state->Save();
        Cleanup();
        glutLeaveMainLoop();
    }

    void Closed()
    {
        if (!shuttingDown && state && !captureMode) state->Save();
        shuttingDown = true;
        Cleanup();
    }

    bool CapturePng(const std::wstring& path)
    {
        const int width = glutGet(GLUT_WINDOW_WIDTH);
        const int height = glutGet(GLUT_WINDOW_HEIGHT);
        if (width < 1 || height < 1 || width > 16384 || height > 16384) return false;
        std::vector<unsigned char> bottomUp(static_cast<size_t>(width) * height * 4);
        std::vector<unsigned char> topDown(bottomUp.size());
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadBuffer(GL_BACK);
        glReadPixels(0, 0, width, height, GL_BGRA, GL_UNSIGNED_BYTE, bottomUp.data());
        if (glGetError() != GL_NO_ERROR) return false;
        const size_t stride = static_cast<size_t>(width) * 4;
        for (int y = 0; y < height; ++y)
        {
            const unsigned char* source = bottomUp.data() + static_cast<size_t>(height - y - 1) * stride;
            unsigned char* destination = topDown.data() + static_cast<size_t>(y) * stride;
            std::copy(source, source + stride, destination);
            for (int x = 0; x < width; ++x) destination[x * 4 + 3] = 255;
        }
        Gdiplus::GdiplusStartupInput startup;
        ULONG_PTR token = 0;
        if (Gdiplus::GdiplusStartup(&token, &startup, nullptr) != Gdiplus::Ok) return false;
        bool saved = false;
        {
            UINT encoderCount = 0, encoderBytes = 0;
            if (Gdiplus::GetImageEncodersSize(&encoderCount, &encoderBytes) == Gdiplus::Ok && encoderBytes > 0)
            {
                std::vector<unsigned char> encoderStorage(encoderBytes);
                Gdiplus::ImageCodecInfo* encoders = reinterpret_cast<Gdiplus::ImageCodecInfo*>(encoderStorage.data());
                if (Gdiplus::GetImageEncoders(encoderCount, encoderBytes, encoders) == Gdiplus::Ok)
                {
                    for (UINT i = 0; i < encoderCount; ++i)
                    {
                        if (wcscmp(encoders[i].MimeType, L"image/png") == 0)
                        {
                            Gdiplus::Bitmap bitmap(width, height, width * 4, PixelFormat32bppARGB, topDown.data());
                            saved = bitmap.GetLastStatus() == Gdiplus::Ok
                                && bitmap.Save(path.c_str(), &encoders[i].Clsid, nullptr) == Gdiplus::Ok;
                            break;
                        }
                    }
                }
            }
        }
        Gdiplus::GdiplusShutdown(token);
        return saved;
    }

    void RenderScene()
    {
        if (!renderer || !state || shuttingDown) return;
        renderer->BeginFrame(state->realm == 0 ? Color(0.025f, 0.035f, 0.065f, 1.0f)
                                              : Color(0.026f, 0.025f, 0.065f, 1.0f));
        prototype::Render(*renderer, *state, view);
        renderer->EndFrame();
        if (captureMode && ++captureFrames == 3)
        {
            const bool saved = CapturePng(capturePath);
            std::wcout << (saved ? L"Captured: " : L"Capture failed: ") << capturePath << std::endl;
            resultCode = saved ? 0 : 2;
            ExitGame();
            return;
        }
        glutSwapBuffers();
    }

    void Resize(int width, int height)
    {
        if (renderer) renderer->Resize(std::max(1, width), std::max(1, height));
        glutPostRedisplay();
    }

    bool PanelOpen()
    {
        return view.dialogueOpen || view.journalOpen || view.helpOpen;
    }

    void OpenDialogue(const std::wstring& text)
    {
        view.dialogue = text;
        view.dialogueOpen = true;
        view.journalOpen = false;
        view.helpOpen = false;
    }

    void SwitchRealmSafely()
    {
        state->SwitchRealm();
        if (!prototype::IsBlocked(state->x, state->y, state->realm)) return;
        const double originalX = state->x;
        const double originalY = state->y;
        for (double radius = 0.5; radius <= 6.0; radius += 0.5)
        {
            for (int direction = 0; direction < 16; ++direction)
            {
                const double angle = direction * 6.283185307179586 / 16.0;
                const double candidateX = originalX + std::cos(angle) * radius;
                const double candidateY = originalY + std::sin(angle) * radius;
                if (!prototype::IsBlocked(candidateX, candidateY, state->realm))
                {
                    state->x = candidateX;
                    state->y = candidateY;
                    state->Save();
                    return;
                }
            }
        }
        // The authored hub origin is always open, including after world switching.
        state->x = state->y = 0.0;
        state->Save();
    }

    void Interact()
    {
        if (view.dialogueOpen)
        {
            view.dialogueOpen = false;
            return;
        }
        if (state->realm == 0 && Distance(state->x, state->y, 1.0, -1.3) < 2.2)
        {
            if (state->deaths > 0)
                OpenDialogue(L"세아: 돌아왔구나. 오늘은 내가 기다릴게.\n늘 먹던 걸로 할까? 천천히 이야기해도 돼.");
            else if (state->broadcasting)
                OpenDialogue(L"세아: 시청자보다 네가 먼저 웃어야지!\n오늘도 같이 갈까? 길을 잃어도 방송은 되잖아.");
            else
                OpenDialogue(L"세아: 또 접속하러 가? 도시도 꽤 재밌는데.\n돌아오면 이야기해 줘. 늘 먹던 걸로 준비할게.");
            return;
        }
        if (state->realm == 1)
        {
            const echo::EchoLife* nearest = nullptr;
            double distance = 2.2;
            for (const echo::EchoLife& life : state->echoes)
            {
                const double candidate = Distance(state->x, state->y, life.x, life.y);
                if (candidate < distance)
                {
                    nearest = &life;
                    distance = candidate;
                }
            }
            if (nearest)
            {
                if (nearest->protection >= nearest->aggression && nearest->protection >= nearest->curiosity)
                    OpenDialogue(L"유실체: 위험한 길이면 내가 먼저 가볼게.\n그런데... 오늘은 저 빛나는 숲에 가보고 싶어.");
                else if (nearest->aggression >= nearest->curiosity)
                    OpenDialogue(L"유실체: 앞을 막으면 뚫으면 되는 줄 알았어.\n잠깐 멈춰도 괜찮다는 건, 이제 배우는 중이야.");
                else
                    OpenDialogue(L"유실체: 저 길 끝에는 뭐가 있을까?\n네게서 시작했지만, 내가 고른 길도 걷고 싶어.");
                return;
            }
        }
        const int centerX = static_cast<int>(std::floor(state->x / 8.0));
        const int centerY = static_cast<int>(std::floor(state->y / 8.0));
        double best = 2.4;
        int foundX = 0, foundY = 0;
        bool found = false;
        for (int cx = centerX - 1; cx <= centerX + 1; ++cx)
        {
            for (int cy = centerY - 1; cy <= centerY + 1; ++cy)
            {
                const double distance = Distance(state->x, state->y, cx * 8.0 + 2.5, cy * 8.0 + 2.5);
                if (distance < best)
                {
                    found = true;
                    foundX = cx;
                    foundY = cy;
                    best = distance;
                }
            }
        }
        if (found)
        {
            const bool fresh = state->Discover(state->realm, foundX, foundY);
            if (state->realm == 0)
                OpenDialogue(fresh
                    ? L"오래된 방송 기록을 발견했다.\n마지막 인사 뒤에도, 누군가의 웃음이 남아 있다."
                    : L"이곳에서 발견한 방송 기록은 보관되어 있다.\n아직 의미를 알 수 없는 한 문장이 눈에 밟힌다.");
            else
                OpenDialogue(fresh
                    ? L"이름 없는 잔향이 조용히 빛난다.\n처음 온 장소인데, 누군가 나를 기다린 것 같다."
                    : L"이 장소는 네가 남긴 발견을 기억하고 있다.\n잔향은 여전히 같은 자리에서 빛난다.");
            return;
        }
        if (Distance(state->x, state->y, -2.0, 0.8) < 2.4)
        {
            SwitchRealmSafely();
            return;
        }
        OpenDialogue(L"가까운 인물이나 빛나는 흔적을 찾아보자.\n[TAB] 세계 전환 · [J] 발견과 생명의 기록");
    }

    void KeyDown(unsigned char rawKey, int, int)
    {
        if (captureMode || shuttingDown) return;
        shiftPressed = (glutGetModifiers() & GLUT_ACTIVE_SHIFT) != 0;
        const unsigned char key = rawKey >= 'A' && rawKey <= 'Z'
            ? static_cast<unsigned char>(rawKey - 'A' + 'a') : rawKey;
        if (keys[key]) return;
        keys[key] = true;
        switch (key)
        {
        case 27:
            if (PanelOpen()) view.dialogueOpen = view.journalOpen = view.helpOpen = false;
            else ExitGame();
            break;
        case '\t':
            if (!PanelOpen()) SwitchRealmSafely();
            break;
        case 'e': Interact(); break;
        case 'q':
            if (!PanelOpen()) state->Act(true);
            break;
        case ' ':
            if (!PanelOpen())
            {
                state->Act(false);
                view.attack = 1.0f;
            }
            break;
        case 'r':
            if (!PanelOpen() && state->realm == 1)
            {
                state->Die();
                view.deathFlash = 1.0f;
            }
            else if (state->realm == 0) state->message = L"유실체 탄생 체험은 루미아에서 [R]로 진행합니다.";
            break;
        case 'b':
            if (!PanelOpen())
            {
                state->broadcasting = !state->broadcasting;
                state->message = state->broadcasting
                    ? L"방송을 시작했습니다 · 오늘의 발견을 함께 나눠요."
                    : L"방송을 마쳤습니다 · 이제 우리끼리 이야기할 시간입니다.";
                state->Save();
            }
            break;
        case 'j':
            view.journalOpen = !view.journalOpen;
            view.dialogueOpen = view.helpOpen = false;
            break;
        case '1':
            if (view.dialogueOpen)
            {
                state->Act(true);
                view.dialogue = L"“오늘은 네가 가고 싶은 데부터 가자.”\n함께 걷겠다는 선택이 보호 성향으로 기록되었다.";
            }
            break;
        case '2':
            if (view.dialogueOpen)
            {
                state->curiosity += 0.08f;
                const float total = state->protection + state->aggression + state->curiosity;
                state->protection /= total;
                state->aggression /= total;
                state->curiosity /= total;
                state->message = L"관찰하는 선택을 기록했습니다 · 탐험 성향이 조금 자랍니다.";
                state->Save();
                view.dialogue = L"서두르지 않고 주변과 상대의 표정을 살핀다.\n말보다 오래 남는 작은 변화가 있을지도 모른다.";
            }
            break;
        default: break;
        }
        glutPostRedisplay();
    }

    void KeyUp(unsigned char rawKey, int, int)
    {
        shiftPressed = (glutGetModifiers() & GLUT_ACTIVE_SHIFT) != 0;
        const unsigned char key = rawKey >= 'A' && rawKey <= 'Z'
            ? static_cast<unsigned char>(rawKey - 'A' + 'a') : rawKey;
        keys[key] = false;
    }

    void SpecialDown(int key, int, int)
    {
        if (captureMode || shuttingDown) return;
        shiftPressed = (glutGetModifiers() & GLUT_ACTIVE_SHIFT) != 0;
        if (key == GLUT_KEY_LEFT) arrowLeft = true;
        if (key == GLUT_KEY_RIGHT) arrowRight = true;
        if (key == GLUT_KEY_UP) arrowUp = true;
        if (key == GLUT_KEY_DOWN) arrowDown = true;
        if (key == GLUT_KEY_F1)
        {
            view.helpOpen = !view.helpOpen;
            view.dialogueOpen = view.journalOpen = false;
        }
        if (key == GLUT_KEY_F5)
        {
            if (state->Save()) state->message = L"현재 세계와 생명의 기록을 저장했습니다.";
        }
    }

    void SpecialUp(int key, int, int)
    {
        shiftPressed = (glutGetModifiers() & GLUT_ACTIVE_SHIFT) != 0;
        if (key == GLUT_KEY_LEFT) arrowLeft = false;
        if (key == GLUT_KEY_RIGHT) arrowRight = false;
        if (key == GLUT_KEY_UP) arrowUp = false;
        if (key == GLUT_KEY_DOWN) arrowDown = false;
    }

    void Timer(int)
    {
        if (shuttingDown) return;
        const int now = glutGet(GLUT_ELAPSED_TIME);
        double dt = std::min(0.05, std::max(0.0, (now - previousTime) / 1000.0));
        previousTime = now;
        if (captureMode) view.time = 12.0;
        else
        {
            view.time += dt;
            float moveX = 0.0f, moveY = 0.0f;
            if (!PanelOpen())
            {
                const float screenX = (keys['d'] || arrowRight ? 1.0f : 0.0f)
                    - (keys['a'] || arrowLeft ? 1.0f : 0.0f);
                const float screenY = (keys['s'] || arrowDown ? 1.0f : 0.0f)
                    - (keys['w'] || arrowUp ? 1.0f : 0.0f);
                moveX = screenX + screenY;
                moveY = screenY - screenX;
                const double length = std::sqrt(moveX * moveX + moveY * moveY);
                if (length > 0.0)
                {
                    const double distance = (shiftPressed ? 5.4 : 3.0) * dt;
                    const double candidateX = state->x + moveX / length * distance;
                    const double candidateY = state->y + moveY / length * distance;
                    if (prototype::IsBlocked(candidateX, candidateY, state->realm))
                    {
                        const double axisX = state->x + (moveX > 0.0f ? distance : -distance);
                        const double axisY = state->y + (moveY > 0.0f ? distance : -distance);
                        if (moveX != 0.0f && !prototype::IsBlocked(axisX, state->y, state->realm)) moveY = 0.0f;
                        else if (moveY != 0.0f && !prototype::IsBlocked(state->x, axisY, state->realm)) moveX = 0.0f;
                        else moveX = moveY = 0.0f;
                    }
                }
            }
            state->Update(dt, moveX, moveY, shiftPressed);
            const double smoothing = 1.0 - std::exp(-7.0 * dt);
            view.cameraX += (state->x - view.cameraX) * smoothing;
            view.cameraY += (state->y - view.cameraY) * smoothing;
            view.attack = std::max(0.0f, view.attack - static_cast<float>(dt * 2.4));
            view.deathFlash = std::max(0.0f, view.deathFlash - static_cast<float>(dt * 1.1));
        }
        glutPostRedisplay();
        glutTimerFunc(16, Timer, 0);
    }

    bool ParseCommandLine()
    {
        int count = 0;
        LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &count);
        if (!arguments) return false;
        bool valid = true;
        for (int i = 1; i < count; ++i)
        {
            if (std::wstring(arguments[i]) == L"--capture")
            {
                if (i + 2 >= count)
                {
                    valid = false;
                    break;
                }
                const std::wstring realmArgument = arguments[++i];
                if (realmArgument != L"city" && realmArgument != L"lumia")
                {
                    valid = false;
                    break;
                }
                captureMode = true;
                captureRealm = realmArgument == L"lumia" ? 1 : 0;
                capturePath = arguments[++i];
            }
        }
        LocalFree(arguments);
        if (!valid) std::cerr << "Usage: SimpleGame.exe --capture city|lumia output.png\n";
        return valid;
    }
}

int main(int argc, char** argv)
{
    (void)argc;
    if (!ParseCommandLine()) return 2;
    SetProcessDPIAware();
    int glutArgc = 1;
    char* glutArgv[] = { argv[0], nullptr };
    glutInit(&glutArgc, glutArgv);
    glutInitContextVersion(3, 3);
    glutInitContextProfile(GLUT_CORE_PROFILE);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA | GLUT_DEPTH);
    glutInitWindowPosition(80, 50);
    glutInitWindowSize(1440, 900);
    glutCreateWindow("ECHO | Lumia - Open World RPG Prototype");
    glutSetOption(GLUT_ACTION_ON_WINDOW_CLOSE, GLUT_ACTION_CONTINUE_EXECUTION);
    glewExperimental = GL_TRUE;
    const GLenum glewStatus = glewInit();
    if (glewStatus != GLEW_OK)
    {
        std::cerr << "GLEW initialization failed: " << glewGetErrorString(glewStatus) << '\n';
        return 1;
    }
    // Some GLEW builds leave an extension-query error during core-context initialization.
    glGetError();
    if (!glewIsSupported("GL_VERSION_3_3"))
    {
        std::cerr << "OpenGL 3.3 is required for the prototype renderer.\n";
        return 1;
    }
    renderer.reset(new Renderer(1440, 900));
    if (!renderer->IsInitialized())
    {
        std::cerr << "Prototype renderer initialization failed.\n";
        Cleanup();
        return 1;
    }
    state.reset(new echo::GameState(!captureMode));
    if (captureMode)
    {
        state->realm = captureRealm;
        state->x = state->y = 0.0;
        state->protection = 7.0f / 14.0f;
        state->aggression = 2.0f / 14.0f;
        state->curiosity = 5.0f / 14.0f;
        state->broadcasting = true;
        state->deaths = 1;
        state->discoveries.clear();
        state->echoes.clear();
        echo::EchoLife life;
        life.id = 1;
        life.x = 1.6;
        life.y = 0.5;
        life.protection = state->protection;
        life.aggression = state->aggression;
        life.curiosity = state->curiosity;
        state->echoes.push_back(life);
        state->message = captureRealm == 0
            ? L"2050년, 익숙한 도시에서 시작하는 낯선 이야기."
            : L"하나의 죽음은 하나의 생명을 만든다 · 당신의 선택에서 태어난 유실체.";
        view.time = 12.0;
    }
    view.cameraX = state->x;
    view.cameraY = state->y;
    glutDisplayFunc(RenderScene);
    glutReshapeFunc(Resize);
    glutKeyboardFunc(KeyDown);
    glutKeyboardUpFunc(KeyUp);
    glutSpecialFunc(SpecialDown);
    glutSpecialUpFunc(SpecialUp);
    glutCloseFunc(Closed);
    glutIgnoreKeyRepeat(1);
    previousTime = glutGet(GLUT_ELAPSED_TIME);
    glutTimerFunc(16, Timer, 0);
    std::cout << "ECHO prototype | WASD/arrows move | TAB realm | E interact | Q protect\n"
              << "SPACE act | B broadcast | R Lumia death | J journal | F1 help | F5 save\n";
    glutMainLoop();
    Cleanup();
    return resultCode;
}
