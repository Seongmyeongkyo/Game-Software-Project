#include "stdafx.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include "Renderer.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <iostream>

namespace
{
    const float CanvasWidth = 1440.0f;
    const float CanvasHeight = 900.0f;
    const float Pi = 3.14159265358979323846f;

    GLuint MakeShader(GLenum type, const char* source)
    {
        GLuint shader = glCreateShader(type);
        if (!shader)
        {
            return 0;
        }
        glShaderSource(shader, 1, &source, nullptr);
        glCompileShader(shader);
        GLint compiled = GL_FALSE;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
        if (!compiled)
        {
            char log[2048] = {};
            glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
            std::cerr << "Renderer shader compilation failed: " << log << std::endl;
            glDeleteShader(shader);
            return 0;
        }
        return shader;
    }
} // namespace

Renderer::Renderer(int width, int height)
{
    m_Program = CompileProgram();
    if (!m_Program)
    {
        return;
    }
    glGenVertexArrays(1, &m_VertexArray);
    glGenBuffers(1, &m_VertexBuffer);
    glBindVertexArray(m_VertexArray);
    glBindBuffer(GL_ARRAY_BUFFER, m_VertexBuffer);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(0,
                          2,
                          GL_FLOAT,
                          GL_FALSE,
                          sizeof(Vertex),
                          reinterpret_cast<void*>(offsetof(Vertex, x)));
    glVertexAttribPointer(1,
                          2,
                          GL_FLOAT,
                          GL_FALSE,
                          sizeof(Vertex),
                          reinterpret_cast<void*>(offsetof(Vertex, u)));
    glVertexAttribPointer(2,
                          4,
                          GL_FLOAT,
                          GL_FALSE,
                          sizeof(Vertex),
                          reinterpret_cast<void*>(offsetof(Vertex, r)));
    glVertexAttribPointer(3,
                          1,
                          GL_FLOAT,
                          GL_FALSE,
                          sizeof(Vertex),
                          reinterpret_cast<void*>(offsetof(Vertex, emission)));
    const unsigned char white[] = {255, 255, 255, 255};
    glGenTextures(1, &m_WhiteTexture);
    glBindTexture(GL_TEXTURE_2D, m_WhiteTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glUseProgram(m_Program);
    glUniform1i(glGetUniformLocation(m_Program, "u_Texture"), 0);
    m_LinearSceneUniform = glGetUniformLocation(m_Program, "u_LinearScene");
    m_Vertices.reserve(24576);
    m_Initialized = m_VertexArray != 0 && m_VertexBuffer != 0 && m_WhiteTexture != 0;
    m_PostProcess.reset(new PostProcess());
    if (!m_PostProcess->Initialize())
    {
        std::cerr << "Post-processing is unavailable; using direct rendering." << std::endl;
    }
    Resize(width, height);
}

Renderer::~Renderer()
{
    m_PostProcess.reset();
    ClearTextCache();
    ClearImageCache();
    if (m_ImageDecoderToken)
    {
        Gdiplus::GdiplusShutdown(static_cast<ULONG_PTR>(m_ImageDecoderToken));
    }
    if (m_WhiteTexture)
    {
        glDeleteTextures(1, &m_WhiteTexture);
    }
    if (m_VertexBuffer)
    {
        glDeleteBuffers(1, &m_VertexBuffer);
    }
    if (m_VertexArray)
    {
        glDeleteVertexArrays(1, &m_VertexArray);
    }
    if (m_Program)
    {
        glDeleteProgram(m_Program);
    }
}

GLuint Renderer::CompileProgram()
{
    const char* vertexSource = R"GLSL(#version 330 core
layout(location = 0) in vec2 a_Position;
layout(location = 1) in vec2 a_UV;
layout(location = 2) in vec4 a_Color;
layout(location = 3) in float a_Emission;

out vec2 v_UV;
out vec4 v_Color;
out float v_Emission;

void main()
{
    v_UV = a_UV;
    v_Color = a_Color;
    v_Emission = a_Emission;
    gl_Position = vec4(a_Position.x / 720.0 - 1.0, 1.0 - a_Position.y / 450.0, 0.0, 1.0);
}
)GLSL";

    const char* fragmentSource = R"GLSL(#version 330 core
in vec2 v_UV;
in vec4 v_Color;
in float v_Emission;

uniform sampler2D u_Texture;
uniform bool u_LinearScene;

out vec4 FragColor;

vec3 linearize(vec3 c)
{
    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c));
}

void main()
{
    vec4 c = v_Color * texture(u_Texture, v_UV);

    if (u_LinearScene)
    {
        c.rgb = linearize(clamp(c.rgb, 0.0, 1.0)) * (1.0 + max(v_Emission, 0.0));
    }

    FragColor = c;
}
)GLSL";
    GLuint vertex = MakeShader(GL_VERTEX_SHADER, vertexSource);
    GLuint fragment = MakeShader(GL_FRAGMENT_SHADER, fragmentSource);
    if (!vertex || !fragment)
    {
        if (vertex)
        {
            glDeleteShader(vertex);
        }
        if (fragment)
        {
            glDeleteShader(fragment);
        }
        return 0;
    }
    GLuint program = glCreateProgram();
    if (!program)
    {
        glDeleteShader(vertex);
        glDeleteShader(fragment);
        return 0;
    }
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    GLint linked = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked)
    {
        char log[2048] = {};
        glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        std::cerr << "Renderer shader link failed: " << log << std::endl;
        glDeleteProgram(program);
        return 0;
    }
    return program;
}

void Renderer::Resize(int width, int height)
{
    m_WindowWidth = std::max(1, width);
    m_WindowHeight = std::max(1, height);
    float scale = std::min(m_WindowWidth / CanvasWidth, m_WindowHeight / CanvasHeight);
    m_ViewportWidth = std::max(1, static_cast<int>(std::round(CanvasWidth * scale)));
    m_ViewportHeight = std::max(1, static_cast<int>(std::round(CanvasHeight * scale)));
    m_ViewportX = (m_WindowWidth - m_ViewportWidth) / 2;
    m_ViewportY = (m_WindowHeight - m_ViewportHeight) / 2;
    if (m_PostProcess)
    {
        m_PostProcess->Resize(m_ViewportWidth, m_ViewportHeight);
    }
    glViewport(m_ViewportX, m_ViewportY, m_ViewportWidth, m_ViewportHeight);
}

void Renderer::BeginFrame(Color background)
{
    if (!m_Initialized)
    {
        return;
    }
    m_Vertices.clear();
    m_BatchTexture = m_WhiteTexture;
    m_InOverlay = false;
    m_SceneOffscreen = false;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDisable(GL_FRAMEBUFFER_SRGB);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glClearColor(background.r, background.g, background.b, background.a);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (m_PostProcess && m_PostProcess->BeginScene())
    {
        m_SceneOffscreen = true;
        // BeginScene selected the full offscreen viewport. Clear in linear space.
        auto linearize = [](float c)
        {
            return c <= .04045f ? c / 12.92f : std::pow((c + .055f) / 1.055f, 2.4f);
        };
        glClearColor(linearize(background.r),
                     linearize(background.g),
                     linearize(background.b),
                     background.a);
        glClear(GL_COLOR_BUFFER_BIT);
    }
    else
    {
        glViewport(m_ViewportX, m_ViewportY, m_ViewportWidth, m_ViewportHeight);
    }
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(m_Program);
    glUniform1i(m_LinearSceneUniform, m_SceneOffscreen ? 1 : 0);
    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(m_VertexArray);
}

void Renderer::BeginOverlay()
{
    if (!m_Initialized || m_InOverlay)
    {
        return;
    }
    Flush();
    if (m_SceneOffscreen)
    {
        m_PostProcess->Composite(m_ViewportX,
                                 m_ViewportY,
                                 m_ViewportWidth,
                                 m_ViewportHeight,
                                 m_PostSettings);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(m_ViewportX, m_ViewportY, m_ViewportWidth, m_ViewportHeight);
    glDisable(GL_FRAMEBUFFER_SRGB);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(m_Program);
    glUniform1i(m_LinearSceneUniform, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(m_VertexArray);
    m_BatchTexture = m_WhiteTexture;
    m_InOverlay = true;
}

void Renderer::EndFrame()
{
    if (!m_Initialized)
    {
        return;
    }
    BeginOverlay();
    Flush();
    glBindVertexArray(0);
    glUseProgram(0);
}

void Renderer::Flush()
{
    if (m_Vertices.empty())
    {
        return;
    }
    glBindTexture(GL_TEXTURE_2D, m_BatchTexture);
    glBindBuffer(GL_ARRAY_BUFFER, m_VertexBuffer);
    glBufferData(GL_ARRAY_BUFFER,
                 m_Vertices.size() * sizeof(Vertex),
                 m_Vertices.data(),
                 GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(m_Vertices.size()));
    m_Vertices.clear();
}

void Renderer::UseTexture(GLuint texture)
{
    if (m_BatchTexture != texture)
    {
        Flush();
        m_BatchTexture = texture;
    }
}

void Renderer::Triangle(Vec2 a, Vec2 b, Vec2 c, Color color)
{
    m_Vertices.push_back({a.x, a.y, .5f, .5f, color.r, color.g, color.b, color.a, color.emission});
    m_Vertices.push_back({b.x, b.y, .5f, .5f, color.r, color.g, color.b, color.a, color.emission});
    m_Vertices.push_back({c.x, c.y, .5f, .5f, color.r, color.g, color.b, color.a, color.emission});
}

void Renderer::Polygon(const std::vector<Vec2>& vertices, Color color)
{
    if (!m_Initialized || vertices.size() < 3 || color.a <= 0)
    {
        return;
    }
    UseTexture(m_WhiteTexture);
    for (size_t i = 1; i + 1 < vertices.size(); ++i)
    {
        Triangle(vertices[0], vertices[i], vertices[i + 1], color);
    }
}

void Renderer::Rect(float x, float y, float width, float height, Color color)
{
    if (!m_Initialized || width <= 0 || height <= 0 || color.a <= 0)
    {
        return;
    }
    UseTexture(m_WhiteTexture);
    Triangle(Vec2(x, y), Vec2(x + width, y), Vec2(x + width, y + height), color);
    Triangle(Vec2(x, y), Vec2(x + width, y + height), Vec2(x, y + height), color);
}

void Renderer::RoundedRect(float x, float y, float width, float height, float radius, Color color)
{
    if (width <= 0 || height <= 0)
    {
        return;
    }
    radius = std::max(0.0f, std::min(radius, std::min(width, height) * .5f));
    if (radius < .5f)
    {
        Rect(x, y, width, height, color);
        return;
    }
    std::vector<Vec2> vertices;
    vertices.reserve(36);
    const Vec2 centers[] = {Vec2(x + width - radius, y + radius),
                            Vec2(x + width - radius, y + height - radius),
                            Vec2(x + radius, y + height - radius),
                            Vec2(x + radius, y + radius)};
    for (int corner = 0; corner < 4; ++corner)
    {
        float first = -Pi * .5f + corner * Pi * .5f;
        for (int step = 0; step <= 8; ++step)
        {
            float angle = first + step * Pi / 16.0f;
            vertices.push_back(Vec2(centers[corner].x + std::cos(angle) * radius,
                                    centers[corner].y + std::sin(angle) * radius));
        }
    }
    Polygon(vertices, color);
}

void Renderer::Line(Vec2 start, Vec2 end, float thickness, Color color)
{
    if (thickness <= 0)
    {
        return;
    }
    float dx = end.x - start.x;
    float dy = end.y - start.y;
    float length = std::sqrt(dx * dx + dy * dy);
    if (length < .001f)
    {
        Circle(start.x, start.y, thickness * .5f, color);
        return;
    }
    float nx = -dy / length * thickness * .5f;
    float ny = dx / length * thickness * .5f;
    Polygon({Vec2(start.x + nx, start.y + ny),
             Vec2(end.x + nx, end.y + ny),
             Vec2(end.x - nx, end.y - ny),
             Vec2(start.x - nx, start.y - ny)},
            color);
}

void Renderer::Circle(float x, float y, float radius, Color color)
{
    if (!m_Initialized || radius <= 0 || color.a <= 0)
    {
        return;
    }
    UseTexture(m_WhiteTexture);
    int segments = std::max(16, std::min(80, static_cast<int>(radius * 1.5f)));
    for (int segment = 0; segment < segments; ++segment)
    {
        float a = segment * (2 * Pi / segments);
        float b = (segment + 1) * (2 * Pi / segments);
        Triangle(Vec2(x, y),
                 Vec2(x + std::cos(a) * radius, y + std::sin(a) * radius),
                 Vec2(x + std::cos(b) * radius, y + std::sin(b) * radius),
                 color);
    }
}

void Renderer::TexturedRect(float x, float y, float width, float height, Color color)
{
    const Vertex vertices[] = {{x, y, 0, 0, color.r, color.g, color.b, color.a},
                               {x + width, y, 1, 0, color.r, color.g, color.b, color.a},
                               {x + width, y + height, 1, 1, color.r, color.g, color.b, color.a},
                               {x, y, 0, 0, color.r, color.g, color.b, color.a},
                               {x + width, y + height, 1, 1, color.r, color.g, color.b, color.a},
                               {x, y + height, 0, 1, color.r, color.g, color.b, color.a}};
    m_Vertices.insert(m_Vertices.end(), vertices, vertices + 6);
}

Renderer::TextTexture Renderer::CreateTextTexture(const std::wstring& text, int pixels, int weight)
{
    TextTexture result = {0, 0, 0};
    HDC dc = CreateCompatibleDC(nullptr);
    if (!dc)
    {
        return result;
    }
    HFONT font = CreateFontW(-pixels,
                             0,
                             0,
                             0,
                             weight,
                             FALSE,
                             FALSE,
                             FALSE,
                             DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS,
                             CLIP_DEFAULT_PRECIS,
                             ANTIALIASED_QUALITY,
                             DEFAULT_PITCH | FF_DONTCARE,
                             L"Malgun Gothic");
    if (!font)
    {
        DeleteDC(dc);
        return result;
    }
    HGDIOBJ previousFont = SelectObject(dc, font);
    RECT bounds = {0, 0, 0, 0};
    DrawTextW(dc,
              text.c_str(),
              static_cast<int>(text.size()),
              &bounds,
              DT_CALCRECT | DT_LEFT | DT_TOP | DT_NOPREFIX);
    result.width = std::max(1L, bounds.right - bounds.left + 3L);
    result.height = std::max(1L, bounds.bottom - bounds.top + 2L);
    BITMAPINFO bitmapInfo = {};
    bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmapInfo.bmiHeader.biWidth = result.width;
    bitmapInfo.bmiHeader.biHeight = -result.height;
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;
    void* bitmapPixels = nullptr;
    HBITMAP bitmap = CreateDIBSection(dc, &bitmapInfo, DIB_RGB_COLORS, &bitmapPixels, nullptr, 0);
    if (bitmap && bitmapPixels)
    {
        HGDIOBJ previousBitmap = SelectObject(dc, bitmap);
        std::memset(bitmapPixels, 0, result.width * result.height * 4);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(255, 255, 255));
        RECT drawBounds = {0, 0, result.width, result.height};
        DrawTextW(dc,
                  text.c_str(),
                  static_cast<int>(text.size()),
                  &drawBounds,
                  DT_LEFT | DT_TOP | DT_NOPREFIX);
        GdiFlush();
        unsigned char* data = static_cast<unsigned char*>(bitmapPixels);
        for (int i = 0; i < result.width * result.height; ++i)
        {
            const unsigned char alpha =
                std::max(data[i * 4], std::max(data[i * 4 + 1], data[i * 4 + 2]));
            data[i * 4] = data[i * 4 + 1] = data[i * 4 + 2] = 255;
            data[i * 4 + 3] = alpha;
        }
        glGenTextures(1, &result.texture);
        glBindTexture(GL_TEXTURE_2D, result.texture);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D,
                     0,
                     GL_RGBA8,
                     result.width,
                     result.height,
                     0,
                     GL_RGBA,
                     GL_UNSIGNED_BYTE,
                     data);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        SelectObject(dc, previousBitmap);
        DeleteObject(bitmap);
    }
    SelectObject(dc, previousFont);
    DeleteObject(font);
    DeleteDC(dc);
    return result;
}

void Renderer::ClearTextCache()
{
    for (const auto& entry : m_TextCache)
    {
        if (entry.second.texture)
        {
            glDeleteTextures(1, &entry.second.texture);
        }
    }
    m_TextCache.clear();
}

void Renderer::Text(float x,
                    float y,
                    const std::wstring& text,
                    float pixels,
                    Color color,
                    int weight)
{
    if (!m_Initialized || text.empty() || pixels <= 0 || color.a <= 0)
    {
        return;
    }
    int pixelHeight = std::max(1, static_cast<int>(std::round(pixels)));
    auto key = std::make_tuple(text, pixelHeight, weight);
    auto found = m_TextCache.find(key);
    if (found == m_TextCache.end())
    {
        Flush();
        if (m_TextCache.size() >= 1024)
        {
            ClearTextCache();
            m_BatchTexture = m_WhiteTexture;
        }
        TextTexture created = CreateTextTexture(text, pixelHeight, weight);
        if (!created.texture)
        {
            return;
        }
        found = m_TextCache.emplace(key, created).first;
    }
    UseTexture(found->second.texture);
    TexturedRect(x,
                 y,
                 static_cast<float>(found->second.width),
                 static_cast<float>(found->second.height),
                 color);
}

Renderer::TextTexture Renderer::CreateImageTexture(const std::wstring& relativePath)
{
    TextTexture result = {0, 0, 0};

    if (!m_ImageDecoderToken)
    {
        Gdiplus::GdiplusStartupInput startup;
        ULONG_PTR token = 0;

        if (Gdiplus::GdiplusStartup(&token, &startup, nullptr) != Gdiplus::Ok)
        {
            return result;
        }

        m_ImageDecoderToken = static_cast<uintptr_t>(token);
    }

    std::vector<wchar_t> executablePath(32768, L'\0');
    const DWORD pathLength = GetModuleFileNameW(nullptr,
                                                executablePath.data(),
                                                static_cast<DWORD>(executablePath.size()));

    if (pathLength == 0 || pathLength >= executablePath.size())
    {
        return result;
    }

    std::wstring imagePath(executablePath.data(), pathLength);
    const size_t separator = imagePath.find_last_of(L"\\/");

    if (separator == std::wstring::npos)
    {
        return result;
    }

    imagePath = imagePath.substr(0, separator + 1) + relativePath;
    Gdiplus::Bitmap bitmap(imagePath.c_str(), FALSE);

    if (bitmap.GetLastStatus() != Gdiplus::Ok)
    {
        return result;
    }

    const UINT width = bitmap.GetWidth();
    const UINT height = bitmap.GetHeight();
    constexpr size_t maximumPixels = 16 * 1024 * 1024;

    if (width == 0 || height == 0 || width > 8192 || height > 8192
        || static_cast<size_t>(width) * height > maximumPixels)
    {
        return result;
    }

    Gdiplus::Rect rectangle(0, 0, static_cast<INT>(width), static_cast<INT>(height));
    Gdiplus::BitmapData locked = {};

    // ARGB, unlike PARGB, keeps straight alpha for GL_SRC_ALPHA blending.
    if (bitmap.LockBits(&rectangle, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &locked)
        != Gdiplus::Ok)
    {
        return result;
    }

    if (!locked.Scan0 || std::abs(static_cast<long long>(locked.Stride)) < width * 4LL)
    {
        bitmap.UnlockBits(&locked);
        return result;
    }

    std::vector<unsigned char> pixels(static_cast<size_t>(width) * height * 4);

    for (UINT row = 0; row < height; ++row)
    {
        // Scan0 is the logical first row. Signed stepping also supports negative stride.
        const unsigned char* source = static_cast<const unsigned char*>(locked.Scan0)
                                      + static_cast<ptrdiff_t>(row) * locked.Stride;
        unsigned char* destination = pixels.data() + static_cast<size_t>(row) * width * 4;

        for (UINT column = 0; column < width; ++column)
        {
            const size_t offset = static_cast<size_t>(column) * 4;
            destination[offset] = source[offset + 2];
            destination[offset + 1] = source[offset + 1];
            destination[offset + 2] = source[offset];
            destination[offset + 3] = source[offset + 3];
        }
    }

    bitmap.UnlockBits(&locked);
    glGenTextures(1, &result.texture);

    if (!result.texture)
    {
        return result;
    }

    glBindTexture(GL_TEXTURE_2D, result.texture);
    GLint previousUnpackAlignment = 4;
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &previousUnpackAlignment);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D,
                 0,
                 GL_RGBA8,
                 static_cast<GLsizei>(width),
                 static_cast<GLsizei>(height),
                 0,
                 GL_RGBA,
                 GL_UNSIGNED_BYTE,
                 pixels.data());
    glPixelStorei(GL_UNPACK_ALIGNMENT, previousUnpackAlignment);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    GLint uploadedWidth = 0;
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &uploadedWidth);

    if (uploadedWidth != static_cast<GLint>(width))
    {
        glDeleteTextures(1, &result.texture);
        result.texture = 0;
        return result;
    }

    result.width = static_cast<int>(width);
    result.height = static_cast<int>(height);
    return result;
}

void Renderer::ClearImageCache()
{
    for (const auto& entry : m_ImageCache)
    {
        if (entry.second.texture)
        {
            glDeleteTextures(1, &entry.second.texture);
        }
    }

    m_ImageCache.clear();
}

bool Renderer::Image(const std::wstring& relativePath,
                     float x,
                     float y,
                     float width,
                     float height,
                     Color tint,
                     bool cover,
                     bool mirror)
{
    if (!m_Initialized || relativePath.empty() || width <= 0 || height <= 0 || !std::isfinite(x)
        || !std::isfinite(y) || !std::isfinite(width) || !std::isfinite(height))
    {
        return false;
    }

    if (tint.a <= 0)
    {
        return true;
    }

    auto found = m_ImageCache.find(relativePath);

    if (found == m_ImageCache.end())
    {
        // Texture creation may change the current GL binding, so finish the old batch first.
        Flush();
        const TextTexture loaded = CreateImageTexture(relativePath);
        found = m_ImageCache.emplace(relativePath, loaded).first;

        if (!loaded.texture)
        {
            std::wcerr << L"Portrait image could not be loaded: " << relativePath << std::endl;
        }
    }

    const TextTexture& image = found->second;

    if (!image.texture)
    {
        return false;
    }

    float u0 = 0;
    float u1 = 1;
    float v0 = 0;
    float v1 = 1;
    const float sourceAspect = static_cast<float>(image.width) / image.height;
    const float frameAspect = width / height;

    if (cover)
    {
        if (sourceAspect > frameAspect)
        {
            const float fraction = frameAspect / sourceAspect;
            u0 = (1 - fraction) * .5f;
            u1 = u0 + fraction;
        }
        else
        {
            const float fraction = sourceAspect / frameAspect;
            v0 = (1 - fraction) * .5f;
            v1 = v0 + fraction;
        }
    }
    else
    {
        const float scale = std::min(width / image.width, height / image.height);
        const float fittedWidth = image.width * scale;
        const float fittedHeight = image.height * scale;
        x += (width - fittedWidth) * .5f;
        y += (height - fittedHeight) * .5f;
        width = fittedWidth;
        height = fittedHeight;
    }

    if (mirror)
    {
        std::swap(u0, u1);
    }

    UseTexture(image.texture);
    const Vertex vertices[] = {
        {x, y, u0, v0, tint.r, tint.g, tint.b, tint.a, tint.emission},
        {x + width, y, u1, v0, tint.r, tint.g, tint.b, tint.a, tint.emission},
        {x + width, y + height, u1, v1, tint.r, tint.g, tint.b, tint.a, tint.emission},
        {x, y, u0, v0, tint.r, tint.g, tint.b, tint.a, tint.emission},
        {x + width, y + height, u1, v1, tint.r, tint.g, tint.b, tint.a, tint.emission},
        {x, y + height, u0, v1, tint.r, tint.g, tint.b, tint.a, tint.emission}};
    m_Vertices.insert(m_Vertices.end(), vertices, vertices + 6);
    return true;
}

void Renderer::DrawSolidRect(float x,
                             float y,
                             float,
                             float size,
                             float r,
                             float g,
                             float b,
                             float a)
{
    Rect(CanvasWidth * .5f + x - size * .5f,
         CanvasHeight * .5f - y - size * .5f,
         size,
         size,
         Color(r, g, b, a));
}
