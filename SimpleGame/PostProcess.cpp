#include "stdafx.h"
#include "PostProcess.h"
#include <algorithm>
#include <cmath>
#include <iostream>

namespace
{
    const char* FullscreenVertex = R"GLSL(#version 330 core
out vec2 v_UV;
void main()
{
    const vec2 positions[3] = vec2[3](vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
    vec2 p = positions[gl_VertexID];
    v_UV = p * 0.5 + 0.5;
    gl_Position = vec4(p, 0.0, 1.0);
}
)GLSL";

    const char* FilterFragment = R"GLSL(#version 330 core
in vec2 v_UV;
uniform sampler2D u_Source;
uniform vec2 u_SourceTexel;
uniform bool u_Extract;
uniform float u_Threshold;
out vec4 FragColor;
void main()
{
    // Four source samples downsample in linear light before either blur chain.
    vec2 offset = u_SourceTexel * 0.5;
    vec3 color = texture(u_Source, v_UV + vec2(-offset.x, -offset.y)).rgb;
    color += texture(u_Source, v_UV + vec2(offset.x, -offset.y)).rgb;
    color += texture(u_Source, v_UV + vec2(-offset.x, offset.y)).rgb;
    color += texture(u_Source, v_UV + vec2(offset.x, offset.y)).rgb;
    color = max(color * 0.25, vec3(0.0));
    if (u_Extract)
    {
        float brightness = max(color.r, max(color.g, color.b));
        float knee = max(u_Threshold * 0.5, 0.0001);
        float soft = clamp(brightness - u_Threshold + knee, 0.0, 2.0 * knee);
        soft = soft * soft / (4.0 * knee);
        float contribution = max(brightness - u_Threshold, soft);
        color *= contribution / max(brightness, 0.0001);
    }
    FragColor = vec4(color, 1.0);
}
)GLSL";

    const char* BlurFragment = R"GLSL(#version 330 core
in vec2 v_UV;
uniform sampler2D u_Source;
uniform vec2 u_Direction;
out vec4 FragColor;
void main()
{
    // Normalized, separable nine-tap Gaussian; clamp-to-edge avoids dark borders.
    vec3 color = texture(u_Source, v_UV).rgb * 0.2270270270;
    color += (texture(u_Source, v_UV + u_Direction).rgb
            + texture(u_Source, v_UV - u_Direction).rgb) * 0.1945945946;
    color += (texture(u_Source, v_UV + u_Direction * 2.0).rgb
            + texture(u_Source, v_UV - u_Direction * 2.0).rgb) * 0.1216216216;
    color += (texture(u_Source, v_UV + u_Direction * 3.0).rgb
            + texture(u_Source, v_UV - u_Direction * 3.0).rgb) * 0.0540540541;
    color += (texture(u_Source, v_UV + u_Direction * 4.0).rgb
            + texture(u_Source, v_UV - u_Direction * 4.0).rgb) * 0.0162162162;
    FragColor = vec4(color, 1.0);
}
)GLSL";

    const char* CompositeFragment = R"GLSL(#version 330 core
in vec2 v_UV;
uniform sampler2D u_Scene;
uniform sampler2D u_Bloom;
uniform sampler2D u_BlurredScene;
uniform bool u_Enabled;
uniform float u_Aspect;
// exposure, bloom strength, vignette strength, edge blur strength.
uniform vec4 u_Settings;
uniform vec2 u_VignetteRange;
uniform vec2 u_EdgeRange;
out vec4 FragColor;

vec3 LinearToSrgb(vec3 color)
{
    color = max(color, vec3(0.0));
    bvec3 linearPart = lessThanEqual(color, vec3(0.0031308));
    vec3 low = color * 12.92;
    vec3 high = 1.055 * pow(color, vec3(1.0 / 2.4)) - 0.055;
    return mix(high, low, linearPart);
}
vec3 Filmic(vec3 color)
{
    // Smooth highlight shoulder: high-energy emissive values survive capture and
    // bloom extraction, then are compressed for an ordinary SDR framebuffer.
    return clamp((color * (2.51 * color + 0.03))
               / (color * (2.43 * color + 0.59) + 0.14), 0.0, 1.0);
}
void main()
{
    vec3 color = max(texture(u_Scene, v_UV).rgb, vec3(0.0));
    if (!u_Enabled)
    {
        FragColor = vec4(LinearToSrgb(clamp(color, 0.0, 1.0)), 1.0);
        return;
    }
    float radius = length((v_UV - 0.5) * vec2(u_Aspect, 1.0));
    if (u_Settings.w > 0.0)
    {
        float mask = smoothstep(u_EdgeRange.x, u_EdgeRange.y, radius) * u_Settings.w;
        color = mix(color, texture(u_BlurredScene, v_UV).rgb, mask);
    }
    if (u_Settings.y > 0.0) color += texture(u_Bloom, v_UV).rgb * u_Settings.y;
    color *= 1.0 - smoothstep(u_VignetteRange.x, u_VignetteRange.y, radius) * u_Settings.z;
    color = Filmic(color * u_Settings.x);
    FragColor = vec4(LinearToSrgb(color), 1.0);
}
)GLSL";

    GLuint CompileShader(GLenum type, const char* source)
    {
        GLuint shader = glCreateShader(type);
        if (!shader) return 0;
        glShaderSource(shader, 1, &source, nullptr);
        glCompileShader(shader);
        GLint success = GL_FALSE;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
        if (success == GL_FALSE)
        {
            char log[4096] = {};
            glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
            std::cerr << "PostProcess shader compile failed: " << log << std::endl;
            glDeleteShader(shader);
            return 0;
        }
        return shader;
    }

    GLuint MakeProgram(const char* fragment)
    {
        GLuint vertexShader = CompileShader(GL_VERTEX_SHADER, FullscreenVertex);
        GLuint fragmentShader = CompileShader(GL_FRAGMENT_SHADER, fragment);
        if (!vertexShader || !fragmentShader)
        {
            if (vertexShader) glDeleteShader(vertexShader);
            if (fragmentShader) glDeleteShader(fragmentShader);
            return 0;
        }
        GLuint program = glCreateProgram();
        if (program)
        {
            glAttachShader(program, vertexShader);
            glAttachShader(program, fragmentShader);
            glLinkProgram(program);
        }
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
        if (!program) return 0;
        GLint success = GL_FALSE;
        glGetProgramiv(program, GL_LINK_STATUS, &success);
        if (success == GL_FALSE)
        {
            char log[4096] = {};
            glGetProgramInfoLog(program, sizeof(log), nullptr, log);
            std::cerr << "PostProcess program link failed: " << log << std::endl;
            glDeleteProgram(program);
            return 0;
        }
        return program;
    }

    float SafeFloat(float value, float fallback, float minimum, float maximum)
    {
        return std::clamp(std::isfinite(value) ? value : fallback, minimum, maximum);
    }
}

PostProcess::~PostProcess()
{
    ReleaseTargets();
    if (m_FilterProgram) glDeleteProgram(m_FilterProgram);
    if (m_BlurProgram) glDeleteProgram(m_BlurProgram);
    if (m_CompositeProgram) glDeleteProgram(m_CompositeProgram);
    if (m_VertexArray) glDeleteVertexArrays(1, &m_VertexArray);
}

bool PostProcess::Initialize()
{
    if (m_Initialized) return true;
    // Repeated initialization after a shader failure does not leak programs.
    if (m_FilterProgram) glDeleteProgram(m_FilterProgram);
    if (m_BlurProgram) glDeleteProgram(m_BlurProgram);
    if (m_CompositeProgram) glDeleteProgram(m_CompositeProgram);
    m_FilterProgram = MakeProgram(FilterFragment);
    m_BlurProgram = MakeProgram(BlurFragment);
    m_CompositeProgram = MakeProgram(CompositeFragment);
    if (!m_FilterProgram || !m_BlurProgram || !m_CompositeProgram) return false;
    if (!m_VertexArray) glGenVertexArrays(1, &m_VertexArray);
    if (!m_VertexArray) return false;

    m_FilterUniforms.source = glGetUniformLocation(m_FilterProgram, "u_Source");
    m_FilterUniforms.texel = glGetUniformLocation(m_FilterProgram, "u_SourceTexel");
    m_FilterUniforms.extract = glGetUniformLocation(m_FilterProgram, "u_Extract");
    m_FilterUniforms.threshold = glGetUniformLocation(m_FilterProgram, "u_Threshold");
    m_BlurUniforms.source = glGetUniformLocation(m_BlurProgram, "u_Source");
    m_BlurUniforms.direction = glGetUniformLocation(m_BlurProgram, "u_Direction");
    m_CompositeUniforms.scene = glGetUniformLocation(m_CompositeProgram, "u_Scene");
    m_CompositeUniforms.bloom = glGetUniformLocation(m_CompositeProgram, "u_Bloom");
    m_CompositeUniforms.blurred = glGetUniformLocation(m_CompositeProgram, "u_BlurredScene");
    m_CompositeUniforms.enabled = glGetUniformLocation(m_CompositeProgram, "u_Enabled");
    m_CompositeUniforms.aspect = glGetUniformLocation(m_CompositeProgram, "u_Aspect");
    m_CompositeUniforms.settings = glGetUniformLocation(m_CompositeProgram, "u_Settings");
    m_CompositeUniforms.vignetteRange = glGetUniformLocation(m_CompositeProgram, "u_VignetteRange");
    m_CompositeUniforms.edgeRange = glGetUniformLocation(m_CompositeProgram, "u_EdgeRange");
    m_Initialized = true;
    return true;
}

bool PostProcess::CreateTarget(Target& target, int width, int height)
{
    target.width = width;
    target.height = height;
    glGenTextures(1, &target.texture);
    glGenFramebuffers(1, &target.framebuffer);
    if (!target.texture || !target.framebuffer) return false;
    glBindTexture(GL_TEXTURE_2D, target.texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_FRAMEBUFFER, target.framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target.texture, 0);
    const GLenum output = GL_COLOR_ATTACHMENT0;
    glDrawBuffers(1, &output);
    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE)
    {
        std::cerr << "PostProcess framebuffer incomplete (" << width << "x" << height
                  << ", status " << status << ")." << std::endl;
        return false;
    }
    return true;
}

void PostProcess::DeleteTarget(Target& target)
{
    if (target.framebuffer) glDeleteFramebuffers(1, &target.framebuffer);
    if (target.texture) glDeleteTextures(1, &target.texture);
    target = Target{};
}

void PostProcess::ReleaseTargets()
{
    DeleteTarget(m_Scene);
    for (int i = 0; i < 2; ++i)
    {
        DeleteTarget(m_Bloom[i]);
        DeleteTarget(m_BlurredScene[i]);
    }
    m_TargetsReady = false;
}

bool PostProcess::Resize(int width, int height)
{
    width = std::max(1, width);
    height = std::max(1, height);
    if (!m_Initialized) return false;
    if (IsReady() && m_Scene.width == width && m_Scene.height == height) return true;
    GLint maximum = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maximum);
    if (width > maximum || height > maximum)
    {
        std::cerr << "PostProcess target exceeds GL_MAX_TEXTURE_SIZE; using direct rendering." << std::endl;
        ReleaseTargets();
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return false;
    }
    Target scene, bloom[2], blurred[2];
    const int halfWidth = std::max(1, (width + 1) / 2);
    const int halfHeight = std::max(1, (height + 1) / 2);
    bool success = CreateTarget(scene, width, height);
    for (int i = 0; success && i < 2; ++i)
        success = CreateTarget(bloom[i], halfWidth, halfHeight)
               && CreateTarget(blurred[i], halfWidth, halfHeight);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    if (!success)
    {
        DeleteTarget(scene);
        for (int i = 0; i < 2; ++i) { DeleteTarget(bloom[i]); DeleteTarget(blurred[i]); }
        ReleaseTargets();
        return false;
    }
    ReleaseTargets();
    m_Scene = scene;
    for (int i = 0; i < 2; ++i) { m_Bloom[i] = bloom[i]; m_BlurredScene[i] = blurred[i]; }
    m_TargetsReady = true;
    return true;
}

bool PostProcess::BeginScene()
{
    if (!IsReady()) return false;
    glBindFramebuffer(GL_FRAMEBUFFER, m_Scene.framebuffer);
    glViewport(0, 0, m_Scene.width, m_Scene.height);
    glDisable(GL_FRAMEBUFFER_SRGB);
    return true;
}

void PostProcess::DrawFullscreen()
{
    glBindVertexArray(m_VertexArray);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

void PostProcess::Filter(const Target& source, const Target& destination, bool extract, float threshold)
{
    glBindFramebuffer(GL_FRAMEBUFFER, destination.framebuffer);
    glViewport(0, 0, destination.width, destination.height);
    glUseProgram(m_FilterProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, source.texture);
    glUniform1i(m_FilterUniforms.source, 0);
    glUniform2f(m_FilterUniforms.texel, 1.0f / source.width, 1.0f / source.height);
    glUniform1i(m_FilterUniforms.extract, extract ? 1 : 0);
    glUniform1f(m_FilterUniforms.threshold, threshold);
    DrawFullscreen();
}

void PostProcess::Blur(const Target& source, const Target& destination, bool horizontal, float radius)
{
    glBindFramebuffer(GL_FRAMEBUFFER, destination.framebuffer);
    glViewport(0, 0, destination.width, destination.height);
    glUseProgram(m_BlurProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, source.texture);
    glUniform1i(m_BlurUniforms.source, 0);
    glUniform2f(m_BlurUniforms.direction, horizontal ? radius / source.width : 0.0f,
                horizontal ? 0.0f : radius / source.height);
    DrawFullscreen();
}

void PostProcess::Composite(int viewportX, int viewportY, int viewportWidth, int viewportHeight,
                            const PostProcessSettings& settings)
{
    if (!IsReady()) return;
    const float exposure = SafeFloat(settings.exposure, 1.15f, 0.05f, 8.0f);
    const float bloom = settings.enabled && settings.bloomEnabled
        ? SafeFloat(settings.bloomStrength, 0.18f, 0.0f, 2.0f) : 0.0f;
    const float edgeBlur = settings.enabled && settings.edgeBlurEnabled
        ? SafeFloat(settings.edgeBlurStrength, 1.0f, 0.0f, 1.0f) : 0.0f;
    const float vignette = settings.enabled && settings.vignetteEnabled
        ? SafeFloat(settings.vignetteStrength, 0.35f, 0.0f, 0.95f) : 0.0f;
    const float threshold = SafeFloat(settings.bloomThreshold, 1.0f, 0.0f, 16.0f);
    const float vignetteInner = SafeFloat(settings.vignetteInner, 0.38f, 0.0f, 2.0f);
    const float vignetteOuter = SafeFloat(settings.vignetteOuter, 0.78f, vignetteInner + 0.001f, 3.0f);
    const float edgeInner = SafeFloat(settings.edgeBlurInner, 0.30f, 0.0f, 2.0f);
    const float edgeOuter = SafeFloat(settings.edgeBlurOuter, 0.68f, edgeInner + 0.001f, 3.0f);

    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glDisable(GL_FRAMEBUFFER_SRGB);
    if (bloom > 0.0f)
    {
        Filter(m_Scene, m_Bloom[0], true, threshold);
        Blur(m_Bloom[0], m_Bloom[1], true, 2.0f);
        Blur(m_Bloom[1], m_Bloom[0], false, 2.0f);
    }
    if (edgeBlur > 0.0f)
    {
        Filter(m_Scene, m_BlurredScene[0], false, 0.0f);
        Blur(m_BlurredScene[0], m_BlurredScene[1], true, 3.0f);
        Blur(m_BlurredScene[1], m_BlurredScene[0], false, 3.0f);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(viewportX, viewportY, std::max(1, viewportWidth), std::max(1, viewportHeight));
    glUseProgram(m_CompositeProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_Scene.texture);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_Bloom[0].texture);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, m_BlurredScene[0].texture);
    glUniform1i(m_CompositeUniforms.scene, 0);
    glUniform1i(m_CompositeUniforms.bloom, 1);
    glUniform1i(m_CompositeUniforms.blurred, 2);
    glUniform1i(m_CompositeUniforms.enabled, settings.enabled ? 1 : 0);
    glUniform1f(m_CompositeUniforms.aspect, static_cast<float>(m_Scene.width) / m_Scene.height);
    glUniform4f(m_CompositeUniforms.settings, exposure, bloom, vignette, edgeBlur);
    glUniform2f(m_CompositeUniforms.vignetteRange, vignetteInner, vignetteOuter);
    glUniform2f(m_CompositeUniforms.edgeRange, edgeInner, edgeOuter);
    DrawFullscreen();

    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindVertexArray(0);
    glUseProgram(0);
}
