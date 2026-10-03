#if defined(__EMSCRIPTEN__) || defined(__ANDROID__)

#include "render-webgl.h"
#include "render-device.h"
#include "gpu-profile.h"
#include <GLES2/gl2.h>
#include <iostream>
#include <vector>
#include <cmath>
#include <cstring>
#include <algorithm>

namespace {

// Dynamic ring buffer sizing
// 32768 vertices = 8192 quads (640 KB buffer). Cache-resident, zero GPU-CPU synchronization stalls.
constexpr size_t RING_VERTS = 32768;
constexpr size_t RING_QUADS = RING_VERTS / 4;

struct WebGLRendererState {
    SDL_Window* window = nullptr;
    SDL_GLContext context = nullptr;

    GLuint program = 0;
    GLint u_resolution = -1;
    GLint u_texture = -1;

    GLuint vbo = 0;
    GLuint ibo = 0;
    size_t vbOffset = 0; // Current vertex offset in ring buffer (kept aligned to 4)

    GLuint whiteTex = 0;
    GLuint lastBoundTex = 0;

    float logicalW = 800.0f;
    float logicalH = 600.0f;
    bool vsync = false;
};

WebGLRendererState s_webgl;

// High-performance shaders for WebGL 1.0 & WebGL 2.0
static const char* kVertexShaderSource =
    "precision highp float;\n"
    "attribute vec2 a_pos;\n"
    "attribute vec4 a_color;\n"
    "attribute vec2 a_texCoord;\n"
    "uniform vec2 u_resolution;\n"
    "varying vec4 v_color;\n"
    "varying vec2 v_texCoord;\n"
    "void main() {\n"
    "    vec2 zeroToOne = a_pos / u_resolution;\n"
    "    vec2 zeroToTwo = zeroToOne * 2.0;\n"
    "    vec2 clipSpace = zeroToTwo - 1.0;\n"
    "    gl_Position = vec4(clipSpace.x, -clipSpace.y, 0.0, 1.0);\n"
    "    v_color = a_color;\n"
    "    v_texCoord = a_texCoord;\n"
    "}\n";

static const char* kFragmentShaderSource =
    "precision mediump float;\n"
    "varying vec4 v_color;\n"
    "varying vec2 v_texCoord;\n"
    "uniform sampler2D u_texture;\n"
    "void main() {\n"
    "    gl_FragColor = texture2D(u_texture, v_texCoord) * v_color;\n"
    "}\n";

GLuint compileShader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    if (!shader) return 0;
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint status = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (!status) {
        char log[1024];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        std::cerr << "[WebGL] Shader compile error: " << log << std::endl;
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

GLuint createProgram(const char* vsSrc, const char* fsSrc) {
    GLuint vs = compileShader(GL_VERTEX_SHADER, vsSrc);
    if (!vs) return 0;
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, fsSrc);
    if (!fs) {
        glDeleteShader(vs);
        return 0;
    }

    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);

    // Explicit attribute locations to ensure consistency
    glBindAttribLocation(prog, 0, "a_pos");
    glBindAttribLocation(prog, 1, "a_color");
    glBindAttribLocation(prog, 2, "a_texCoord");

    glLinkProgram(prog);
    GLint status = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &status);
    if (!status) {
        char log[1024];
        glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
        std::cerr << "[WebGL] Program link error: " << log << std::endl;
        glDeleteProgram(prog);
        glDeleteShader(vs);
        glDeleteShader(fs);
        return 0;
    }

    glDeleteShader(vs);
    glDeleteShader(fs);
    return prog;
}

void uploadTexturePixels(const PreparedTexture& t, bool isBackground) {
    GLint wrapMode = isBackground ? GL_REPEAT : GL_CLAMP_TO_EDGE;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapMode);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapMode);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    switch (t.format) {
        case PF_L8: // Grayscale background (1 byte/texel)
            glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, t.width, t.height, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, t.data.data());
            break;
        case PF_LA8: // Grayscale + Alpha (2 bytes/texel)
            glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE_ALPHA, t.width, t.height, 0, GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE, t.data.data());
            break;
        case PF_RGBA4444: { // 16-bit packed RGBA (2 bytes/texel - cuts mobile VRAM in half)
            const size_t n = (size_t)t.width * t.height;
            std::vector<uint16_t> packed(n);
            const uint8_t* p = t.data.data();
            for (size_t i = 0; i < n; ++i) {
                uint16_t r = (p[i * 4 + 0] >> 4) & 0x0F;
                uint16_t g = (p[i * 4 + 1] >> 4) & 0x0F;
                uint16_t b = (p[i * 4 + 2] >> 4) & 0x0F;
                uint16_t a = (p[i * 4 + 3] >> 4) & 0x0F;
                packed[i] = (r << 12) | (g << 8) | (b << 4) | a;
            }
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, t.width, t.height, 0, GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4, packed.data());
            break;
        }
        default: // PF_RGBA8 (4 bytes/texel)
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, t.width, t.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, t.data.data());
            break;
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
}

} // anonymous namespace

bool webgl_init(SDL_Window* window, int windowW, int windowH, bool vsync) {
    s_webgl.window = window;
    s_webgl.vsync = vsync;
    s_webgl.context = SDL_GL_CreateContext(window);
    if (!s_webgl.context) {
        std::cerr << "[WebGL] SDL_GL_CreateContext failed: " << SDL_GetError() << std::endl;
        return false;
    }

    // Set GPU caps for web environment
    GpuCaps& caps = gpu::caps();
    const char* glVendor   = (const char*)glGetString(GL_VENDOR);
    const char* glRenderer = (const char*)glGetString(GL_RENDERER);
    const char* glVersion  = (const char*)glGetString(GL_VERSION);
    caps.name = glRenderer ? glRenderer : "WebGL";
    caps.tier = gpu::tierFromName(gpu::lower(caps.name.c_str()), &caps.isIntelGMA);
    caps.npot = true; // WebGL 1/2 has full NPOT support with CLAMP_TO_EDGE
    caps.detected = true;

    std::cout << "[RenderDevice] FastPath WebGL initialized (" << caps.name
              << " | tier " << caps.tier << " | quality "
              << gpu::presetName(gpu::resolvedPreset()) << ")\n";

    // Setup shaders
    s_webgl.program = createProgram(kVertexShaderSource, kFragmentShaderSource);
    if (!s_webgl.program) {
        std::cerr << "[WebGL] Failed to build shader program!\n";
        return false;
    }
    glUseProgram(s_webgl.program);
    s_webgl.u_resolution = glGetUniformLocation(s_webgl.program, "u_resolution");
    s_webgl.u_texture = glGetUniformLocation(s_webgl.program, "u_texture");
    glUniform1i(s_webgl.u_texture, 0); // texture unit 0

    // Setup global GL states: Premultiplied alpha
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    // Create dynamic ring buffer VBO
    glGenBuffers(1, &s_webgl.vbo);
    glBindBuffer(GL_ARRAY_BUFFER, s_webgl.vbo);
    glBufferData(GL_ARRAY_BUFFER, RING_VERTS * sizeof(GLVertex), nullptr, GL_DYNAMIC_DRAW);

    // Create static IBO for indexed quads
    glGenBuffers(1, &s_webgl.ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, s_webgl.ibo);
    std::vector<uint16_t> indices(RING_QUADS * 6);
    for (uint16_t q = 0; q < RING_QUADS; ++q) {
        uint16_t base = q * 4;
        indices[q * 6 + 0] = base + 0;
        indices[q * 6 + 1] = base + 1;
        indices[q * 6 + 2] = base + 2;
        indices[q * 6 + 3] = base + 0;
        indices[q * 6 + 4] = base + 2;
        indices[q * 6 + 5] = base + 3;
    }
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(uint16_t), indices.data(), GL_STATIC_DRAW);

    // Setup vertex attribute layout (stays permanently bound to s_webgl.vbo)
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(GLVertex), (const void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(GLVertex), (const void*)8);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(GLVertex), (const void*)12);

    // Create 1x1 white texture for untextured geometry
    uint32_t whitePixel = 0xFFFFFFFF;
    glGenTextures(1, &s_webgl.whiteTex);
    glBindTexture(GL_TEXTURE_2D, s_webgl.whiteTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, &whitePixel);
    s_webgl.lastBoundTex = s_webgl.whiteTex;

    return true;
}

void webgl_shutdown() {
    if (s_webgl.whiteTex) {
        glDeleteTextures(1, &s_webgl.whiteTex);
        s_webgl.whiteTex = 0;
    }
    if (s_webgl.vbo) {
        glDeleteBuffers(1, &s_webgl.vbo);
        s_webgl.vbo = 0;
    }
    if (s_webgl.ibo) {
        glDeleteBuffers(1, &s_webgl.ibo);
        s_webgl.ibo = 0;
    }
    if (s_webgl.program) {
        glDeleteProgram(s_webgl.program);
        s_webgl.program = 0;
    }
    if (s_webgl.context) {
        SDL_GL_DeleteContext(s_webgl.context);
        s_webgl.context = nullptr;
    }
}

void webgl_setViewport(int vpX, int vpY, int vpW, int vpH, float logicalW, float logicalH) {
    s_webgl.logicalW = logicalW;
    s_webgl.logicalH = logicalH;
    glViewport(vpX, vpY, vpW, vpH);
    glUseProgram(s_webgl.program);
    glUniform2f(s_webgl.u_resolution, logicalW, logicalH);
}

void webgl_setVSync(bool enabled) {
    s_webgl.vsync = enabled;
    SDL_GL_SetSwapInterval(enabled ? 1 : 0);
}

void webgl_beginFrame() {
    // Zero CPU-side overhead at frame start
}

void webgl_clear(float r, float g, float b, float a) {
    glClearColor(r, g, b, a);
    glClear(GL_COLOR_BUFFER_BIT);
}

void webgl_endFrame() {
    if (s_webgl.window) {
        SDL_GL_SwapWindow(s_webgl.window);
    }
}

void webgl_flushBatch(const GLVertex* buffer, size_t count, uint32_t currentTexID) {
    if (count == 0) return;

    // Ring buffer advance with zero-stall orphaning when wrapping
    if (s_webgl.vbOffset + count > RING_VERTS) {
        s_webgl.vbOffset = 0;
        glBufferData(GL_ARRAY_BUFFER, RING_VERTS * sizeof(GLVertex), nullptr, GL_DYNAMIC_DRAW);
    }

    glBufferSubData(GL_ARRAY_BUFFER, s_webgl.vbOffset * sizeof(GLVertex), count * sizeof(GLVertex), buffer);

    GLuint tex = (currentTexID != 0) ? currentTexID : s_webgl.whiteTex;
    if (s_webgl.lastBoundTex != tex) {
        glBindTexture(GL_TEXTURE_2D, tex);
        s_webgl.lastBoundTex = tex;
    }

    const size_t numQuads = count / 4;
    const size_t numIndices = numQuads * 6;
    const size_t quadOffset = s_webgl.vbOffset / 4;
    const size_t byteOffset = quadOffset * 6 * sizeof(uint16_t);

    glDrawElements(GL_TRIANGLES, (GLsizei)numIndices, GL_UNSIGNED_SHORT, (const void*)byteOffset);

    s_webgl.vbOffset += count;
}

void webgl_drawCircle(float cx, float cy, float radius, float r, float g, float b, float a, bool filled, int segments, float transX, float transY, BlendMode blend) {
    const size_t count = filled ? ((size_t)segments + 2) : (size_t)segments;

    if (s_webgl.vbOffset + count > RING_VERTS) {
        s_webgl.vbOffset = 0;
        glBufferData(GL_ARRAY_BUFFER, RING_VERTS * sizeof(GLVertex), nullptr, GL_DYNAMIC_DRAW);
    }

    const uint32_t color = packColorPMA(r, g, b, a, blend == BLEND_ADD, true);
    const float ox = cx + transX, oy = cy + transY;

    static std::vector<GLVertex> circleVerts;
    circleVerts.resize(count);

    if (filled) {
        circleVerts[0] = { ox, oy, color, 0.5f, 0.5f };
        for (int i = 0; i <= segments; ++i) {
            float ang = (i / (float)segments) * 6.2831853f;
            circleVerts[i + 1] = { ox + std::cos(ang) * radius, oy + std::sin(ang) * radius, color, 0.5f, 0.5f };
        }
    } else {
        for (int i = 0; i < segments; ++i) {
            float ang = (i / (float)segments) * 6.2831853f;
            circleVerts[i] = { ox + std::cos(ang) * radius, oy + std::sin(ang) * radius, color, 0.5f, 0.5f };
        }
    }

    glBufferSubData(GL_ARRAY_BUFFER, s_webgl.vbOffset * sizeof(GLVertex), count * sizeof(GLVertex), circleVerts.data());

    if (s_webgl.lastBoundTex != s_webgl.whiteTex) {
        glBindTexture(GL_TEXTURE_2D, s_webgl.whiteTex);
        s_webgl.lastBoundTex = s_webgl.whiteTex;
    }

    if (filled) {
        glDrawArrays(GL_TRIANGLE_FAN, (GLint)s_webgl.vbOffset, (GLsizei)count);
    } else {
        glDrawArrays(GL_LINE_LOOP, (GLint)s_webgl.vbOffset, (GLsizei)count);
    }

    s_webgl.vbOffset += count;
    s_webgl.vbOffset = (s_webgl.vbOffset + 3) & ~size_t(3);
}

void webgl_drawTriangleStrip(const float* coordsXY, const float* colorsRGBA, size_t vertCount, bool additive, float transX, float transY) {
    if (vertCount < 3) return;

    if (s_webgl.vbOffset + vertCount > RING_VERTS) {
        s_webgl.vbOffset = 0;
        glBufferData(GL_ARRAY_BUFFER, RING_VERTS * sizeof(GLVertex), nullptr, GL_DYNAMIC_DRAW);
    }

    static std::vector<GLVertex> stripVerts;
    stripVerts.resize(vertCount);
    for (size_t i = 0; i < vertCount; ++i) {
        stripVerts[i].x = coordsXY[i * 2 + 0] + transX;
        stripVerts[i].y = coordsXY[i * 2 + 1] + transY;
        stripVerts[i].color = packColorPMA(colorsRGBA[i * 4 + 0], colorsRGBA[i * 4 + 1], colorsRGBA[i * 4 + 2], colorsRGBA[i * 4 + 3], additive, true);
        stripVerts[i].u = 0.5f;
        stripVerts[i].v = 0.5f;
    }

    glBufferSubData(GL_ARRAY_BUFFER, s_webgl.vbOffset * sizeof(GLVertex), vertCount * sizeof(GLVertex), stripVerts.data());

    if (s_webgl.lastBoundTex != s_webgl.whiteTex) {
        glBindTexture(GL_TEXTURE_2D, s_webgl.whiteTex);
        s_webgl.lastBoundTex = s_webgl.whiteTex;
    }

    glDrawArrays(GL_TRIANGLE_STRIP, (GLint)s_webgl.vbOffset, (GLsizei)vertCount);

    s_webgl.vbOffset += vertCount;
    s_webgl.vbOffset = (s_webgl.vbOffset + 3) & ~size_t(3);
}

void webgl_drawRepeatedBackground(uint32_t texID, float uvOffsetX, float uvOffsetY, float uvW, float uvH, float logicalW, float logicalH, float bgR, float bgG, float bgB) {
    if (texID == 0) return;

    if (s_webgl.vbOffset + 4 > RING_VERTS) {
        s_webgl.vbOffset = 0;
        glBufferData(GL_ARRAY_BUFFER, RING_VERTS * sizeof(GLVertex), nullptr, GL_DYNAMIC_DRAW);
    }

    uint32_t color = packColorPMA(bgR, bgG, bgB, 1.0f, false, true);
    GLVertex bgVerts[4];
    bgVerts[0] = { 0.0f,     0.0f,     color, uvOffsetX,       uvOffsetY };
    bgVerts[1] = { logicalW, 0.0f,     color, uvOffsetX + uvW, uvOffsetY };
    bgVerts[2] = { 0.0f,     logicalH, color, uvOffsetX,       uvOffsetY + uvH };
    bgVerts[3] = { logicalW, logicalH, color, uvOffsetX + uvW, uvOffsetY + uvH };

    glBufferSubData(GL_ARRAY_BUFFER, s_webgl.vbOffset * sizeof(GLVertex), 4 * sizeof(GLVertex), bgVerts);

    if (s_webgl.lastBoundTex != texID) {
        glBindTexture(GL_TEXTURE_2D, texID);
        s_webgl.lastBoundTex = texID;
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    glDisable(GL_BLEND);
    glDrawArrays(GL_TRIANGLE_STRIP, (GLint)s_webgl.vbOffset, 4);
    glEnable(GL_BLEND);

    s_webgl.vbOffset += 4;
}

uint32_t webgl_registerTexture(const PreparedTexture& t, bool isBackground) {
    GLuint glID = 0;
    glGenTextures(1, &glID);
    glBindTexture(GL_TEXTURE_2D, glID);
    uploadTexturePixels(t, isBackground);
    s_webgl.lastBoundTex = glID;
    return glID;
}

void webgl_reloadTexture(uint32_t handle, const PreparedTexture& t, bool isBackground) {
    if (handle == 0) return;
    glBindTexture(GL_TEXTURE_2D, handle);
    uploadTexturePixels(t, isBackground);
    s_webgl.lastBoundTex = handle;
}

#endif // __EMSCRIPTEN__
