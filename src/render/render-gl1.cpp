#include "render-gl1.h"
#include "render-device.h"
#include "gpu-profile.h"

#if !defined(__EMSCRIPTEN__) && !defined(__ANDROID__)
#include <SDL2/SDL_opengl.h>
#include <iostream>
#include <vector>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <algorithm>

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

struct GL1State {
    SDL_Window* window = nullptr;
    SDL_GLContext context = nullptr;
    GLuint whiteTex = 0;
    GLuint lastBoundTex = 0;
    bool pointersBound = false;
    bool vsync = false;
};

static GL1State s_gl1;

static void bindGL1Pointers(const GLVertex* buffer) {
    glVertexPointer(2, GL_FLOAT, sizeof(GLVertex), &buffer[0].x);
    glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(GLVertex), &buffer[0].color);
    glTexCoordPointer(2, GL_FLOAT, sizeof(GLVertex), &buffer[0].u);
    s_gl1.pointersBound = true;
}

static void uploadGL1Texture(const PreparedTexture& t, bool isBackground) {
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    GLint wrapMode = isBackground ? GL_REPEAT : GL_CLAMP_TO_EDGE;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapMode);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapMode);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    switch (t.format) {
        case PF_L8: // Grayscale (1 byte/texel)
            glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, t.width, t.height, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, t.data.data());
            break;
        case PF_I8: // Intensity (1 byte/texel)
            glTexImage2D(GL_TEXTURE_2D, 0, GL_INTENSITY8, t.width, t.height, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, t.data.data());
            break;
        case PF_LA8: // Grayscale + Alpha (2 bytes/texel)
            glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE_ALPHA, t.width, t.height, 0, GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE, t.data.data());
            break;
        case PF_RGBA4444: // 16-bit RGBA4
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA4, t.width, t.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, t.data.data());
            break;
        default: // RGBA8 (4 bytes/texel)
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, t.width, t.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, t.data.data());
            break;
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
}

bool gl1_init(SDL_Window* window, int windowW, int windowH, bool vsync) {
    s_gl1.window = window;
    s_gl1.vsync = vsync;
    s_gl1.context = SDL_GL_CreateContext(window);
    if (!s_gl1.context) return false;

    // ---- GPU detection (Intel GMA tiers, NPOT support) ----
    GpuCaps& caps = gpu::caps();
    const char* glRenderer = (const char*)glGetString(GL_RENDERER);
    const char* glVersion  = (const char*)glGetString(GL_VERSION);
    const char* glExt      = (const char*)glGetString(GL_EXTENSIONS);
    caps.name = glRenderer ? glRenderer : "unknown";
    caps.tier = gpu::tierFromName(gpu::lower(glRenderer), &caps.isIntelGMA);
    int major = glVersion ? std::atoi(glVersion) : 1;
    caps.npot = major >= 2 || (glExt && std::strstr(glExt, "GL_ARB_texture_non_power_of_two"));
    caps.detected = true;
    std::cout << "[RenderDevice] GL renderer: " << caps.name << " | tier " << caps.tier
              << (caps.isIntelGMA ? " (Intel GMA)" : "") << " | NPOT " << (caps.npot ? "yes" : "no")
              << " | quality " << gpu::presetName(gpu::resolvedPreset()) << "\n";

    // Premultiplied alpha: ONE blend equation for normal + additive, set once for the whole session
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_TEXTURE_2D);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_DITHER);
    // Alpha test must stay off: additive vertices carry alpha 0 under premultiplied blending.
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_LIGHTING);
    glDisable(GL_FOG);
    glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_FASTEST); // 2D only: GMA can skip per-pixel perspective divide

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);

    uint32_t whitePixels[4] = { 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF };
    glGenTextures(1, &s_gl1.whiteTex);
    glBindTexture(GL_TEXTURE_2D, s_gl1.whiteTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, whitePixels);
    s_gl1.lastBoundTex = s_gl1.whiteTex;
    s_gl1.pointersBound = false;

    return true;
}

void gl1_shutdown() {
    if (s_gl1.whiteTex) {
        glDeleteTextures(1, &s_gl1.whiteTex);
        s_gl1.whiteTex = 0;
    }
    if (s_gl1.context) {
        SDL_GL_DeleteContext(s_gl1.context);
        s_gl1.context = nullptr;
    }
}

void gl1_setViewport(int vpX, int vpY, int vpW, int vpH, float logicalW, float logicalH) {
    glViewport(vpX, vpY, vpW, vpH);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, logicalW, logicalH, 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

void gl1_setVSync(bool enabled) {
    s_gl1.vsync = enabled;
    SDL_GL_SetSwapInterval(enabled ? 1 : 0);
}

void gl1_beginFrame() {
    // Nothing needed for fixed pipeline OpenGL 1.1 beginFrame
}

void gl1_clear(float r, float g, float b, float a) {
    glClearColor(r, g, b, a);
    glClear(GL_COLOR_BUFFER_BIT);
}

void gl1_endFrame(SDL_Window* window) {
    SDL_GL_SwapWindow(window);
}

void gl1_flushBatch(const GLVertex* buffer, size_t count, uint32_t currentTexID) {
    if (count == 0) return;
    GLuint bindTex = (currentTexID != 0) ? currentTexID : s_gl1.whiteTex;
    if (s_gl1.lastBoundTex != bindTex) {
        glBindTexture(GL_TEXTURE_2D, bindTex);
        s_gl1.lastBoundTex = bindTex;
    }
    if (!s_gl1.pointersBound) {
        bindGL1Pointers(buffer);
    }
    glDrawArrays(GL_QUADS, 0, (GLsizei)count);
}

void gl1_drawCircle(float cx, float cy, float radius, float r, float g, float b, float a, bool filled, int segments, float transX, float transY, BlendMode blend) {
    glDisable(GL_TEXTURE_2D);
    float verts[18 * 2];
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
    const bool additive = blend == BLEND_ADD;
    float ac = std::clamp(a, 0.0f, 1.0f);
    glColor4f(r * ac, g * ac, b * ac, additive ? 0.0f : ac);
    float ox = cx + transX, oy = cy + transY;
    if (filled) {
        verts[0] = ox; verts[1] = oy;
        for (int i = 0; i <= segments; ++i) {
            float ang = (i / (float)segments) * 6.2831853f;
            verts[(i + 1) * 2 + 0] = ox + std::cos(ang) * radius;
            verts[(i + 1) * 2 + 1] = oy + std::sin(ang) * radius;
        }
        glVertexPointer(2, GL_FLOAT, 0, verts);
        glDrawArrays(GL_TRIANGLE_FAN, 0, segments + 2);
    } else {
        for (int i = 0; i < segments; ++i) {
            float ang = (i / (float)segments) * 6.2831853f;
            verts[i * 2 + 0] = ox + std::cos(ang) * radius;
            verts[i * 2 + 1] = oy + std::sin(ang) * radius;
        }
        glLineWidth(3.0f);
        glVertexPointer(2, GL_FLOAT, 0, verts);
        glDrawArrays(GL_LINE_LOOP, 0, segments);
        glLineWidth(1.0f);
    }
    glEnableClientState(GL_COLOR_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnable(GL_TEXTURE_2D);
    s_gl1.pointersBound = false; // lazily rebound on next batch flush
}

void gl1_drawTriangleStrip(const float* coordsXY, const float* colorsRGBA, size_t vertCount, bool additive, float transX, float transY) {
    if (vertCount < 3) return;
    static std::vector<GLVertex> strip;
    strip.resize(vertCount);
    for (size_t i = 0; i < vertCount; ++i) {
        strip[i].x = coordsXY[i * 2 + 0] + transX;
        strip[i].y = coordsXY[i * 2 + 1] + transY;
        strip[i].color = packColorPMA(colorsRGBA[i * 4 + 0], colorsRGBA[i * 4 + 1], colorsRGBA[i * 4 + 2], colorsRGBA[i * 4 + 3], additive, true);
        strip[i].u = strip[i].v = 0.5f;
    }
    if (s_gl1.lastBoundTex != s_gl1.whiteTex) {
        glBindTexture(GL_TEXTURE_2D, s_gl1.whiteTex);
        s_gl1.lastBoundTex = s_gl1.whiteTex;
    }
    glVertexPointer(2, GL_FLOAT, sizeof(GLVertex), &strip[0].x);
    glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(GLVertex), &strip[0].color);
    glTexCoordPointer(2, GL_FLOAT, sizeof(GLVertex), &strip[0].u);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, (GLsizei)vertCount);
    s_gl1.pointersBound = false;
}

void gl1_drawRepeatedBackground(uint32_t texID, float uvOffsetX, float uvOffsetY, float uvW, float uvH, float logicalW, float logicalH, float bgR, float bgG, float bgB) {
    glDisable(GL_BLEND); // full-screen opaque pass: no blending
    if (s_gl1.lastBoundTex != texID) {
        glBindTexture(GL_TEXTURE_2D, texID);
        s_gl1.lastBoundTex = texID;
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    uint32_t colorGL = packColorPMA(bgR, bgG, bgB, 1.0f, false, true);

    GLVertex bgVerts[4];
    bgVerts[0] = { 0.0f,     0.0f,     colorGL, uvOffsetX,       uvOffsetY };
    bgVerts[1] = { logicalW, 0.0f,     colorGL, uvOffsetX + uvW, uvOffsetY };
    bgVerts[2] = { logicalW, logicalH, colorGL, uvOffsetX + uvW, uvOffsetY + uvH };
    bgVerts[3] = { 0.0f,     logicalH, colorGL, uvOffsetX,       uvOffsetY + uvH };

    glVertexPointer(2, GL_FLOAT, sizeof(GLVertex), &bgVerts[0].x);
    glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(GLVertex), &bgVerts[0].color);
    glTexCoordPointer(2, GL_FLOAT, sizeof(GLVertex), &bgVerts[0].u);
    glDrawArrays(GL_QUADS, 0, 4);
    glEnable(GL_BLEND);
    s_gl1.pointersBound = false;
}

uint32_t gl1_registerTexture(const PreparedTexture& t, bool isBackground) {
    GLuint glID = 0;
    glGenTextures(1, &glID);
    glBindTexture(GL_TEXTURE_2D, glID);
    uploadGL1Texture(t, isBackground);
    s_gl1.lastBoundTex = glID;
    return glID;
}

void gl1_reloadTexture(uint32_t handle, const PreparedTexture& t, bool isBackground) {
    if (handle == 0) return;
    glBindTexture(GL_TEXTURE_2D, handle);
    uploadGL1Texture(t, isBackground);
    s_gl1.lastBoundTex = handle;
}

#else
// Stubs for Web/Android
bool gl1_init(SDL_Window*, int, int, bool) { return false; }
void gl1_shutdown() {}
void gl1_setViewport(int, int, int, int, float, float) {}
void gl1_setVSync(bool) {}
void gl1_beginFrame() {}
void gl1_clear(float, float, float, float) {}
void gl1_endFrame(SDL_Window*) {}
void gl1_flushBatch(const GLVertex*, size_t, uint32_t) {}
void gl1_drawCircle(float, float, float, float, float, float, float, bool, int, float, float, BlendMode) {}
void gl1_drawTriangleStrip(const float*, const float*, size_t, bool, float, float) {}
void gl1_drawRepeatedBackground(uint32_t, float, float, float, float, float, float, float, float, float) {}
uint32_t gl1_registerTexture(const PreparedTexture&, bool) { return 0; }
void gl1_reloadTexture(uint32_t, const PreparedTexture&, bool) {}
#endif
