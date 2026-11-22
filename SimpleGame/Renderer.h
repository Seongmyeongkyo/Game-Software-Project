#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <string>
#include <vector>
#include <map>
#include <tuple>
#include <memory>
#include "PostProcess.h"
#include "Dependencies/glew.h"

struct Color
{
    float r;
    float g;
    float b;
    float a;
    float emission = 0;

    Color(float red = 1, float green = 1, float blue = 1, float alpha = 1)
        : r(red),
          g(green),
          b(blue),
          a(alpha)
    {
    }

    Color Emissive(float intensity) const
    {
        Color color = *this;
        color.emission = intensity;
        return color;
    }
};

struct Vec2
{
    float x;
    float y;

    Vec2(float horizontal = 0, float vertical = 0)
        : x(horizontal),
          y(vertical)
    {
    }
};

// Painter-ordered drawing in a letterboxed 1440 x 900 logical canvas.
class Renderer
{
public:
    Renderer(int width, int height);
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    bool IsInitialized() const
    {
        return m_Initialized;
    }

    void Resize(int width, int height);
    void BeginFrame(Color background);
    // Composite the world before drawing screen-space text and HUD.
    void BeginOverlay();
    void EndFrame();

    PostProcessSettings& PostProcessing()
    {
        return m_PostSettings;
    }

    const PostProcessSettings& PostProcessing() const
    {
        return m_PostSettings;
    }

    void Polygon(const std::vector<Vec2>& vertices, Color color);
    void Rect(float x, float y, float width, float height, Color color);
    void RoundedRect(float x, float y, float width, float height, float radius, Color color);
    void Line(Vec2 start, Vec2 end, float thickness, Color color);
    void Circle(float x, float y, float radius, Color color);
    void Text(float x,
              float y,
              const std::wstring& text,
              float pixels,
              Color color,
              int weight = 400);
    void DrawSolidRect(float x, float y, float z, float size, float r, float g, float b, float a);

private:
    struct Vertex
    {
        float x;
        float y;
        float u;
        float v;
        float r;
        float g;
        float b;
        float a;
        float emission;
    };

    struct TextTexture
    {
        GLuint texture;
        int width;
        int height;
    };

    GLuint CompileProgram();
    void Flush();
    void UseTexture(GLuint texture);
    void Triangle(Vec2 a, Vec2 b, Vec2 c, Color color);
    void TexturedRect(float x, float y, float width, float height, Color color);
    TextTexture CreateTextTexture(const std::wstring& text, int pixels, int weight);
    void ClearTextCache();

    bool m_Initialized = false;
    int m_WindowWidth = 1;
    int m_WindowHeight = 1;
    int m_ViewportX = 0;
    int m_ViewportY = 0;
    int m_ViewportWidth = 1;
    int m_ViewportHeight = 1;
    GLuint m_Program = 0;
    GLuint m_VertexArray = 0;
    GLuint m_VertexBuffer = 0;
    GLuint m_WhiteTexture = 0;
    GLuint m_BatchTexture = 0;
    GLint m_LinearSceneUniform = -1;
    bool m_InOverlay = false;
    bool m_SceneOffscreen = false;
    PostProcessSettings m_PostSettings;
    std::unique_ptr<PostProcess> m_PostProcess;
    std::vector<Vertex> m_Vertices;
    std::map<std::tuple<std::wstring, int, int>, TextTexture> m_TextCache;
};
