#include "stdafx.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
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
        if (!shader) return 0;
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
}

Renderer::Renderer(int width, int height)
{
    m_Program = CompileProgram();
    if (!m_Program) return;
    glGenVertexArrays(1, &m_VertexArray);
    glGenBuffers(1, &m_VertexBuffer);
    glBindVertexArray(m_VertexArray);
    glBindBuffer(GL_ARRAY_BUFFER, m_VertexBuffer);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, x)));
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, u)));
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, r)));
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
    m_Vertices.reserve(24576);
    m_Initialized = m_VertexArray != 0 && m_VertexBuffer != 0 && m_WhiteTexture != 0;
    Resize(width, height);
}

Renderer::~Renderer()
{
    ClearTextCache();
    if (m_WhiteTexture) glDeleteTextures(1, &m_WhiteTexture);
    if (m_VertexBuffer) glDeleteBuffers(1, &m_VertexBuffer);
    if (m_VertexArray) glDeleteVertexArrays(1, &m_VertexArray);
    if (m_Program) glDeleteProgram(m_Program);
}

GLuint Renderer::CompileProgram()
{
    const char* vertexSource =
        "#version 330 core\n"
        "layout(location=0) in vec2 a_Position;\n"
        "layout(location=1) in vec2 a_UV;\n"
        "layout(location=2) in vec4 a_Color;\n"
        "out vec2 v_UV; out vec4 v_Color;\n"
        "void main(){ v_UV=a_UV; v_Color=a_Color;"
        "gl_Position=vec4(a_Position.x/720.0-1.0,1.0-a_Position.y/450.0,0.0,1.0); }\n";
    const char* fragmentSource =
        "#version 330 core\n"
        "in vec2 v_UV; in vec4 v_Color;\n"
        "uniform sampler2D u_Texture;\n"
        "out vec4 FragColor;\n"
        "void main(){ FragColor=v_Color*texture(u_Texture,v_UV); }\n";
    GLuint vertex = MakeShader(GL_VERTEX_SHADER, vertexSource);
    GLuint fragment = MakeShader(GL_FRAGMENT_SHADER, fragmentSource);
    if (!vertex || !fragment)
    {
        if (vertex) glDeleteShader(vertex);
        if (fragment) glDeleteShader(fragment);
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
    glViewport(m_ViewportX, m_ViewportY, m_ViewportWidth, m_ViewportHeight);
}

void Renderer::BeginFrame(Color background)
{
    if (!m_Initialized) return;
    m_Vertices.clear();
    m_BatchTexture = m_WhiteTexture;
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glClearColor(background.r, background.g, background.b, background.a);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glViewport(m_ViewportX, m_ViewportY, m_ViewportWidth, m_ViewportHeight);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(m_Program);
    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(m_VertexArray);
}

void Renderer::EndFrame()
{
    if (!m_Initialized) return;
    Flush();
    glBindVertexArray(0);
    glUseProgram(0);
}

void Renderer::Flush()
{
    if (m_Vertices.empty()) return;
    glBindTexture(GL_TEXTURE_2D, m_BatchTexture);
    glBindBuffer(GL_ARRAY_BUFFER, m_VertexBuffer);
    glBufferData(GL_ARRAY_BUFFER, m_Vertices.size() * sizeof(Vertex), m_Vertices.data(), GL_STREAM_DRAW);
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
    m_Vertices.push_back({a.x, a.y, .5f, .5f, color.r, color.g, color.b, color.a});
    m_Vertices.push_back({b.x, b.y, .5f, .5f, color.r, color.g, color.b, color.a});
    m_Vertices.push_back({c.x, c.y, .5f, .5f, color.r, color.g, color.b, color.a});
}

void Renderer::Polygon(const std::vector<Vec2>& vertices, Color color)
{
    if (!m_Initialized || vertices.size() < 3 || color.a <= 0) return;
    UseTexture(m_WhiteTexture);
    for (size_t i = 1; i + 1 < vertices.size(); ++i)
        Triangle(vertices[0], vertices[i], vertices[i + 1], color);
}

void Renderer::Rect(float x, float y, float width, float height, Color color)
{
    if (!m_Initialized || width <= 0 || height <= 0 || color.a <= 0) return;
    UseTexture(m_WhiteTexture);
    Triangle(Vec2(x, y), Vec2(x + width, y), Vec2(x + width, y + height), color);
    Triangle(Vec2(x, y), Vec2(x + width, y + height), Vec2(x, y + height), color);
}

void Renderer::RoundedRect(float x, float y, float width, float height, float radius, Color color)
{
    if (width <= 0 || height <= 0) return;
    radius = std::max(0.0f, std::min(radius, std::min(width, height) * .5f));
    if (radius < .5f) { Rect(x, y, width, height, color); return; }
    std::vector<Vec2> vertices;
    vertices.reserve(36);
    const Vec2 centers[] = {
        Vec2(x + width - radius, y + radius),
        Vec2(x + width - radius, y + height - radius),
        Vec2(x + radius, y + height - radius), Vec2(x + radius, y + radius)
    };
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
    if (thickness <= 0) return;
    float dx = end.x - start.x, dy = end.y - start.y;
    float length = std::sqrt(dx * dx + dy * dy);
    if (length < .001f) { Circle(start.x, start.y, thickness * .5f, color); return; }
    float nx = -dy / length * thickness * .5f;
    float ny = dx / length * thickness * .5f;
    Polygon({Vec2(start.x + nx, start.y + ny), Vec2(end.x + nx, end.y + ny),
             Vec2(end.x - nx, end.y - ny), Vec2(start.x - nx, start.y - ny)}, color);
}

void Renderer::Circle(float x, float y, float radius, Color color)
{
    if (!m_Initialized || radius <= 0 || color.a <= 0) return;
    UseTexture(m_WhiteTexture);
    int segments = std::max(16, std::min(80, static_cast<int>(radius * 1.5f)));
    for (int segment = 0; segment < segments; ++segment)
    {
        float a = segment * (2 * Pi / segments);
        float b = (segment + 1) * (2 * Pi / segments);
        Triangle(Vec2(x, y), Vec2(x + std::cos(a) * radius, y + std::sin(a) * radius),
                 Vec2(x + std::cos(b) * radius, y + std::sin(b) * radius), color);
    }
}

void Renderer::TexturedRect(float x, float y, float width, float height, Color color)
{
    const Vertex vertices[] = {
        {x, y, 0, 0, color.r, color.g, color.b, color.a},
        {x + width, y, 1, 0, color.r, color.g, color.b, color.a},
        {x + width, y + height, 1, 1, color.r, color.g, color.b, color.a},
        {x, y, 0, 0, color.r, color.g, color.b, color.a},
        {x + width, y + height, 1, 1, color.r, color.g, color.b, color.a},
        {x, y + height, 0, 1, color.r, color.g, color.b, color.a}
    };
    m_Vertices.insert(m_Vertices.end(), vertices, vertices + 6);
}

Renderer::TextTexture Renderer::CreateTextTexture(const std::wstring& text, int pixels, int weight)
{
    TextTexture result = {0, 0, 0};
    HDC dc = CreateCompatibleDC(nullptr);
    if (!dc) return result;
    HFONT font = CreateFontW(-pixels, 0, 0, 0, weight, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Malgun Gothic");
    if (!font) { DeleteDC(dc); return result; }
    HGDIOBJ previousFont = SelectObject(dc, font);
    RECT bounds = {0, 0, 0, 0};
    DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &bounds,
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
        DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &drawBounds,
            DT_LEFT | DT_TOP | DT_NOPREFIX);
        GdiFlush();
        unsigned char* data = static_cast<unsigned char*>(bitmapPixels);
        for (int i = 0; i < result.width * result.height; ++i)
        {
            const unsigned char alpha = std::max(data[i * 4], std::max(data[i * 4 + 1], data[i * 4 + 2]));
            data[i * 4] = data[i * 4 + 1] = data[i * 4 + 2] = 255;
            data[i * 4 + 3] = alpha;
        }
        glGenTextures(1, &result.texture);
        glBindTexture(GL_TEXTURE_2D, result.texture);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, result.width, result.height, 0,
            GL_RGBA, GL_UNSIGNED_BYTE, data);
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
        if (entry.second.texture) glDeleteTextures(1, &entry.second.texture);
    m_TextCache.clear();
}

void Renderer::Text(float x, float y, const std::wstring& text, float pixels, Color color, int weight)
{
    if (!m_Initialized || text.empty() || pixels <= 0 || color.a <= 0) return;
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
        if (!created.texture) return;
        found = m_TextCache.emplace(key, created).first;
    }
    UseTexture(found->second.texture);
    TexturedRect(x, y, static_cast<float>(found->second.width), static_cast<float>(found->second.height), color);
}

void Renderer::DrawSolidRect(float x, float y, float, float size, float r, float g, float b, float a)
{
    Rect(CanvasWidth * .5f + x - size * .5f, CanvasHeight * .5f - y - size * .5f,
        size, size, Color(r, g, b, a));
}