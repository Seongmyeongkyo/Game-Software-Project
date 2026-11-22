#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "Dependencies/glew.h"

struct PostProcessSettings
{
    bool enabled = true;
    bool bloomEnabled = true;
    bool vignetteEnabled = true;
    bool edgeBlurEnabled = true;
    float exposure = 1.15f;
    float bloomStrength = 0.18f;
    float bloomThreshold = 1.0f;
    float vignetteStrength = 0.35f;
    // Radii are measured from the image center in units of image height.
    // For a 1440:900 viewport, the side midpoint is radius 0.8 and the top is 0.5.
    float vignetteInner = 0.38f;
    float vignetteOuter = 0.78f;
    float edgeBlurStrength = 1.0f;
    float edgeBlurInner = 0.30f;
    float edgeBlurOuter = 0.68f;
};

// Linear-light HDR scene capture and SDR presentation. Draw the UI after Composite().
// All methods and destruction require a current OpenGL 3.3 context.
class PostProcess
{
public:
    PostProcess() = default;
    ~PostProcess();
    PostProcess(const PostProcess&) = delete;
    PostProcess& operator=(const PostProcess&) = delete;

    bool Initialize();
    bool Resize(int width, int height);
    bool BeginScene();
    void Composite(int viewportX, int viewportY, int viewportWidth, int viewportHeight,
                   const PostProcessSettings& settings);
    bool IsReady() const { return m_Initialized && m_TargetsReady; }

private:
    struct Target
    {
        GLuint framebuffer = 0;
        GLuint texture = 0;
        int width = 0;
        int height = 0;
    };
    struct FilterUniforms { GLint source = -1, texel = -1, extract = -1, threshold = -1; };
    struct BlurUniforms { GLint source = -1, direction = -1; };
    struct CompositeUniforms
    {
        GLint scene = -1, bloom = -1, blurred = -1, enabled = -1;
        GLint aspect = -1, settings = -1, vignetteRange = -1, edgeRange = -1;
    };

    static bool CreateTarget(Target& target, int width, int height);
    static void DeleteTarget(Target& target);
    void ReleaseTargets();
    void Filter(const Target& source, const Target& destination, bool extract, float threshold);
    void Blur(const Target& source, const Target& destination, bool horizontal, float radius);
    void DrawFullscreen();

    bool m_Initialized = false;
    bool m_TargetsReady = false;
    GLuint m_VertexArray = 0;
    GLuint m_FilterProgram = 0, m_BlurProgram = 0, m_CompositeProgram = 0;
    Target m_Scene, m_Bloom[2], m_BlurredScene[2];
    FilterUniforms m_FilterUniforms;
    BlurUniforms m_BlurUniforms;
    CompositeUniforms m_CompositeUniforms;
};
