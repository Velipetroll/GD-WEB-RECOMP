#include "render-device.h"
#include "boot-scene.h"
#include "font-helpers.h"
#include "stb_image.h"
#include "render-d3d8.h"
#include "render-d3d9.h"
#include "render-gl1.h"

#if defined(__EMSCRIPTEN__) || defined(__ANDROID__)
#define HAS_GLES2 1
#include "render-webgl.h"
#endif

#include <iostream>
#include <iomanip>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

void setEngineBlendMode(BlendMode mode) {
    RenderDevice::get().setBlendMode(mode);
}

// =============================================================================
// Shared texture preparation (premultiply + NPOT handling + format selection)
// =============================================================================
static inline uint8_t mul255(uint32_t c, uint32_t a) {
    uint32_t t = c * a + 128u;
    return (uint8_t)((t + (t >> 8)) >> 8);   // exact round(c * a / 255)
}

// Bilinear resample of premultiplied RGBA8 (premultiplied data is safe to filter: no dark fringes)
static std::vector<uint8_t> resampleRGBA(const uint8_t* src, int sw, int sh, int dw, int dh) {
    std::vector<uint8_t> dst((size_t)dw * dh * 4);
    const float sx = (float)sw / dw, sy = (float)sh / dh;
    for (int y = 0; y < dh; ++y) {
        float fy = (y + 0.5f) * sy - 0.5f;
        int y0 = (int)std::floor(fy); float ty = fy - y0;
        int y1 = std::clamp(y0 + 1, 0, sh - 1); y0 = std::clamp(y0, 0, sh - 1);
        for (int x = 0; x < dw; ++x) {
            float fx = (x + 0.5f) * sx - 0.5f;
            int x0 = (int)std::floor(fx); float tx = fx - x0;
            int x1 = std::clamp(x0 + 1, 0, sw - 1); x0 = std::clamp(x0, 0, sw - 1);
            const uint8_t* p00 = src + ((size_t)y0 * sw + x0) * 4;
            const uint8_t* p10 = src + ((size_t)y0 * sw + x1) * 4;
            const uint8_t* p01 = src + ((size_t)y1 * sw + x0) * 4;
            const uint8_t* p11 = src + ((size_t)y1 * sw + x1) * 4;
            uint8_t* d = &dst[((size_t)y * dw + x) * 4];
            for (int c = 0; c < 4; ++c) {
                float top = p00[c] + (p10[c] - p00[c]) * tx;
                float bot = p01[c] + (p11[c] - p01[c]) * tx;
                d[c] = (uint8_t)std::clamp(top + (bot - top) * ty + 0.5f, 0.0f, 255.0f);
            }
        }
    }
    return dst;
}

// Pads to (pw, ph) replicating the last row / column, which reproduces CLAMP_TO_EDGE sampling exactly.
static std::vector<uint8_t> padRGBA(const uint8_t* src, int sw, int sh, int pw, int ph) {
    std::vector<uint8_t> dst((size_t)pw * ph * 4);
    for (int y = 0; y < ph; ++y) {
        const uint8_t* srow = src + (size_t)std::min(y, sh - 1) * sw * 4;
        uint8_t* drow = &dst[(size_t)y * pw * 4];
        std::memcpy(drow, srow, (size_t)sw * 4);
        for (int x = sw; x < pw; ++x) std::memcpy(drow + (size_t)x * 4, srow + (size_t)(sw - 1) * 4, 4);
    }
    return dst;
}

PreparedTexture prepareTexture(const void* rgbaPixels, int width, int height, bool needPOT, bool allowIntensity, bool isBackground) {
    PreparedTexture t;
    t.width = width; t.height = height;
    const size_t texels = (size_t)width * (size_t)height;
    std::vector<uint8_t> rgba(texels * 4);
    const uint8_t* src = (const uint8_t*)rgbaPixels;

    // 1) premultiply
    for (size_t i = 0; i < texels; ++i) {
        uint32_t a = src[i * 4 + 3];
        if (a == 255) { std::memcpy(&rgba[i * 4], &src[i * 4], 4); continue; }
        rgba[i * 4 + 0] = mul255(src[i * 4 + 0], a);
        rgba[i * 4 + 1] = mul255(src[i * 4 + 1], a);
        rgba[i * 4 + 2] = mul255(src[i * 4 + 2], a);
        rgba[i * 4 + 3] = (uint8_t)a;
    }

    // Background downscaling based on quality preset (Medium/Low: 512x512, High: 1024x1024)
    if (isBackground && gpu::knobs().bgResolution > 0) {
        int targetRes = gpu::knobs().bgResolution;
        if (width > targetRes && height > targetRes) {
            rgba = resampleRGBA(rgba.data(), width, height, targetRes, targetRes);
            t.width = targetRes;
            t.height = targetRes;
        }
    }

    // 2) NPOT on a POT-only GPU (GMA 950/3100/3150 OpenGL drivers, some D3D caps)
    if (needPOT && t.width > 0 && t.height > 0 && (!gpu::isPow2(t.width) || !gpu::isPow2(t.height))) {
        int pw = gpu::nextPow2(t.width), ph = gpu::nextPow2(t.height);
        int lw = gpu::isPow2(t.width) ? t.width : pw / 2;
        int lh = gpu::isPow2(t.height) ? t.height : ph / 2;
        bool closeBelow = lw >= (int)(t.width * 0.9f) && lh >= (int)(t.height * 0.9f);
        if (gpu::knobs().downscaleNpot && closeBelow) {
            rgba = resampleRGBA(rgba.data(), t.width, t.height, lw, lh);
            t.width = lw; t.height = lh;
        } else {
            rgba = padRGBA(rgba.data(), t.width, t.height, pw, ph);
            t.uScale = (float)t.width / pw; t.vScale = (float)t.height / ph;
            t.width = pw; t.height = ph;
        }
    }

    // 3) format selection and compression
    const size_t n = (size_t)t.width * t.height;
    bool gray = true, opaque = true, intensity = true;
    for (size_t i = 0; i < n && gray; ++i) {
        const uint8_t* p = &rgba[i * 4];
        if (p[0] != p[1] || p[1] != p[2]) { gray = false; break; }
        if (p[3] != 255) opaque = false;
        if (p[0] != p[3]) intensity = false;
    }
    if (gpu::knobs().texture16bit) {
        t.format = PF_RGBA4444;
        for (size_t i = 0; i < n; ++i) {
            rgba[i * 4 + 0] = (rgba[i * 4 + 0] & 0xF0) | (rgba[i * 4 + 0] >> 4);
            rgba[i * 4 + 1] = (rgba[i * 4 + 1] & 0xF0) | (rgba[i * 4 + 1] >> 4);
            rgba[i * 4 + 2] = (rgba[i * 4 + 2] & 0xF0) | (rgba[i * 4 + 2] >> 4);
        }
        t.data = std::move(rgba);
    } else if (gray && opaque) {
        t.format = PF_L8;
        t.data.resize(n);
        for (size_t i = 0; i < n; ++i) t.data[i] = rgba[i * 4];
    } else if (gray && intensity && allowIntensity) {
        t.format = PF_I8;
        t.data.resize(n);
        for (size_t i = 0; i < n; ++i) t.data[i] = rgba[i * 4 + 3];
    } else if (gray) {
        t.format = PF_LA8;
        t.data.resize(n * 2);
        for (size_t i = 0; i < n; ++i) { t.data[i * 2] = rgba[i * 4]; t.data[i * 2 + 1] = rgba[i * 4 + 3]; }
    } else {
        t.format = PF_RGBA8;
        t.data = std::move(rgba);
    }
    return t;
}

static inline void texelRGBA(const PreparedTexture& t, size_t i, uint8_t out[4]) {
    switch (t.format) {
        case PF_L8:  out[0] = out[1] = out[2] = t.data[i]; out[3] = 255; break;
        case PF_I8:  out[0] = out[1] = out[2] = out[3] = t.data[i]; break;
        case PF_LA8: out[0] = out[1] = out[2] = t.data[i * 2]; out[3] = t.data[i * 2 + 1]; break;
        default:     std::memcpy(out, &t.data[i * 4], 4); break;
    }
}

static inline uint32_t q4(uint32_t v) { return (v * 15u + 127u) / 255u; }

void writeD3DTexels(const PreparedTexture& t, int d3dMode, void* bits, int pitch) {
    auto* dst = (uint8_t*)bits;
    for (int y = 0; y < t.height; ++y) {
        uint8_t* row = dst + (size_t)y * pitch;
        size_t base = (size_t)y * t.width;
        for (int x = 0; x < t.width; ++x) {
            uint8_t c[4];
            texelRGBA(t, base + x, c);
            switch (d3dMode) {
                case 2: row[x] = c[0]; break;                                                        // L8
                case 1: ((uint16_t*)row)[x] = (uint16_t)((c[3] << 8) | c[0]); break;                 // A8L8
                case 3: ((uint16_t*)row)[x] = (uint16_t)((q4(c[3]) << 12) | (q4(c[0]) << 8) |      // A4R4G4B4
                                                          (q4(c[1]) << 4) | q4(c[2])); break;
                default: ((uint32_t*)row)[x] = ((uint32_t)c[3] << 24) | ((uint32_t)c[0] << 16) |    // A8R8G8B8
                                               ((uint32_t)c[1] << 8) | c[2]; break;
            }
        }
    }
}

// =============================================================================

RenderDevice::RenderDevice() {}
RenderDevice::~RenderDevice() { shutdown(); }

const char* RenderDevice::getBackendName() const {
    if (_backend == RENDERER_D3D8) return "DirectX 8";
    if (_backend == RENDERER_D3D9) return "DirectX 9";
    #ifdef __ANDROID__
    if (_backend == RENDERER_WEBGL) return "OpenGL ES 2.0";
    #else
    if (_backend == RENDERER_WEBGL) return "WebGL 2.0 FastPath";
    #endif
    return "OpenGL 1.1";
}

bool RenderDevice::init(SDL_Window* window, RenderBackendType backend, int windowW, int windowH) {
    _window = window;
    _backend = backend;
    _vpW = windowW;
    _vpH = windowH;
    _statsEnabled = std::getenv("GD_RENDER_STATS") != nullptr;
    _statsTick = SDL_GetTicks();

    #ifdef HAS_GLES2
    _backend = RENDERER_WEBGL;
    if (!webgl_init(window, windowW, windowH, _vsync)) {
        std::cerr << "[RenderDevice] WebGL initialization failed.\n";
        return false;
    }
    setViewport(0, 0, windowW, windowH, (float)screenWidth, (float)screenHeight);
    return true;
    #endif

    #ifdef _WIN32
    if (_backend == RENDERER_D3D8) {
        #if !defined(_WIN64)
        if (!d3d8_init(window, windowW, windowH, _vsync)) {
            std::cerr << "[RenderDevice] Direct3D 8 failed. Trying Direct3D 9...\n";
            _backend = RENDERER_D3D9;
        } else {
            setViewport(0, 0, windowW, windowH, (float)screenWidth, (float)screenHeight);
            return true;
        }
        #else
        std::cerr << "[RenderDevice] Direct3D 8 does not exist in 64-bit mode. Switching to Direct3D 9...\n";
        _backend = RENDERER_D3D9;
        #endif
    }

    if (_backend == RENDERER_D3D9) {
        if (!d3d9_init(window, windowW, windowH, _vsync)) {
            std::cerr << "[RenderDevice] Direct3D 9 failed. Falling back to OpenGL 1.1...\n";
            _backend = RENDERER_OPENGL;
        } else {
            setViewport(0, 0, windowW, windowH, (float)screenWidth, (float)screenHeight);
            return true;
        }
    }
    #else
    _backend = RENDERER_OPENGL;
    #endif

    if (_backend == RENDERER_OPENGL) {
        if (!gl1_init(window, windowW, windowH, _vsync)) return false;
    }

    setViewport(0, 0, windowW, windowH, (float)screenWidth, (float)screenHeight);
    return true;
}

void RenderDevice::shutdown() {
    flushBatch();
    #ifdef HAS_GLES2
    if (_backend == RENDERER_WEBGL) {
        webgl_shutdown();
        _masterTextures.clear();
        return;
    }
    #endif

    #ifdef _WIN32
    if (_backend == RENDERER_D3D8) {
        d3d8_shutdown();
    }
    if (_backend == RENDERER_D3D9) {
        d3d9_shutdown();
    }
    #endif

    if (_backend == RENDERER_OPENGL) {
        gl1_shutdown();
    }

    _masterTextures.clear();
}

void RenderDevice::setViewport(int vpX, int vpY, int vpW, int vpH, float logicalW, float logicalH) {
    if (vpW <= 0 || vpH <= 0) return;
    _vpX = vpX; _vpY = vpY; _vpW = vpW; _vpH = vpH;
    _logicalW = logicalW; _logicalH = logicalH;
    _scaleX = (logicalW > 0.0f) ? ((float)vpW / logicalW) : 1.0f;
    _scaleY = (logicalH > 0.0f) ? ((float)vpH / logicalH) : 1.0f;

    #ifdef HAS_GLES2
    if (_backend == RENDERER_WEBGL) {
        webgl_setViewport(vpX, vpY, vpW, vpH, logicalW, logicalH);
        return;
    }
    #endif

    #ifdef _WIN32
    if (_backend == RENDERER_D3D8) {
        d3d8_setViewport(vpX, vpY, vpW, vpH);
        return;
    }
    if (_backend == RENDERER_D3D9) {
        d3d9_setViewport(vpX, vpY, vpW, vpH);
        return;
    }
    #endif

    gl1_setViewport(vpX, vpY, vpW, vpH, logicalW, logicalH);
}

void RenderDevice::setVSync(bool enabled) {
    _vsync = enabled;
    #ifdef HAS_GLES2
    if (_backend == RENDERER_WEBGL) {
        webgl_setVSync(enabled);
        return;
    }
    #endif
    #ifdef _WIN32
    if (_backend == RENDERER_D3D8) {
        d3d8_setVSync(enabled);
        return;
    }
    if (_backend == RENDERER_D3D9) {
        d3d9_setVSync(enabled);
        return;
    }
    #endif
    gl1_setVSync(enabled);
}

void RenderDevice::beginFrame() {
    _transX = 0.0f; _transY = 0.0f;
    _matrixStack.clear();
    _drawCalls = 0;
    #ifdef HAS_GLES2
    if (_backend == RENDERER_WEBGL) { webgl_beginFrame(); return; }
    #endif
    #ifdef _WIN32
    if (_backend == RENDERER_D3D8) { d3d8_beginFrame(); return; }
    if (_backend == RENDERER_D3D9) { d3d9_beginFrame(); return; }
    #endif
    gl1_beginFrame();
}

void RenderDevice::clear(float r, float g, float b, float a) {
    #ifdef HAS_GLES2
    if (_backend == RENDERER_WEBGL) { webgl_clear(r, g, b, a); return; }
    #endif
    #ifdef _WIN32
    if (_backend == RENDERER_D3D8) { d3d8_clear(r, g, b, a); return; }
    if (_backend == RENDERER_D3D9) { d3d9_clear(r, g, b, a); return; }
    #endif
    gl1_clear(r, g, b, a);
}

void RenderDevice::endFrame() {
    flushBatch();
    _lastDrawCalls = _drawCalls;
    if (_statsEnabled) {
        ++_statsFrames;
        uint32_t now = SDL_GetTicks();
        if (now - _statsTick >= 1000) {
            std::cout << "[RenderStats] " << getBackendName() << ": " << _statsFrames << " fps, "
                      << _lastDrawCalls << " draw calls/frame\n";
            _statsFrames = 0; _statsTick = now;
        }
    }
    #ifdef HAS_GLES2
    if (_backend == RENDERER_WEBGL) { webgl_endFrame(); return; }
    #endif
    #ifdef _WIN32
    if (_backend == RENDERER_D3D8) { d3d8_endFrame(); return; }
    if (_backend == RENDERER_D3D9) { d3d9_endFrame(); return; }
    #endif
    gl1_endFrame(_window);
}

void RenderDevice::beginBatch() {
    flushBatch();
    _batchVertCount = 0;
}

void RenderDevice::flushBatch() {
    if (_batchVertCount == 0) return;
    ++_drawCalls;

    #ifdef HAS_GLES2
    if (_backend == RENDERER_WEBGL) {
        webgl_flushBatch(_batchBuffer.gl, _batchVertCount, _currentTexID);
        _batchVertCount = 0;
        return;
    }
    #endif

    #ifdef _WIN32
    if (_backend == RENDERER_D3D8) {
        d3d8_flushBatch(_batchBuffer.d3d, _batchVertCount, _currentTexID);
        _batchVertCount = 0;
        return;
    }
    if (_backend == RENDERER_D3D9) {
        d3d9_flushBatch(_batchBuffer.d3d, _batchVertCount, _currentTexID);
        _batchVertCount = 0;
        return;
    }
    #endif

    gl1_flushBatch(_batchBuffer.gl, _batchVertCount, _currentTexID);
    _batchVertCount = 0;
}

void RenderDevice::_selectTexture(uint32_t texID) {
    flushBatch();
    _currentTexID = texID;
    auto it = _texUV.find(texID);
    if (it != _texUV.end()) { _uvScaled = true; _curUS = it->second.first; _curVS = it->second.second; }
    else { _uvScaled = false; _curUS = _curVS = 1.0f; }
}

void RenderDevice::_recordTexUV(uint32_t handle, const PreparedTexture& t) {
    if (t.uScale != 1.0f || t.vScale != 1.0f) _texUV[handle] = { t.uScale, t.vScale };
    else _texUV.erase(handle);
    if (t.format == PF_RGBA4444) _tex16Active = true;
    if (handle == _currentTexID) { _uvScaled = !(t.uScale == 1.0f && t.vScale == 1.0f); _curUS = t.uScale; _curVS = t.vScale; }
}

void RenderDevice::batchQuad(uint32_t texID, float x0, float y0, float u0, float v0,
                             float x1, float y1, float u1, float v1,
                             float x2, float y2, float u2, float v2,
                             float x3, float y3, float u3, float v3,
                             float r, float g, float b, float a, BlendMode blend)
{
    // Only a texture change (or a full buffer) breaks the batch: blend is encoded per vertex
    if (texID != _currentTexID) _selectTexture(texID);
    else if (_batchVertCount + 4 > MAX_BATCH_VERTS) flushBatch();
    _currentBlend = blend;

    if (_uvScaled) {
        u0 *= _curUS; u1 *= _curUS; u2 *= _curUS; u3 *= _curUS;
        v0 *= _curVS; v1 *= _curVS; v2 *= _curVS; v3 *= _curVS;
    }

    const bool isGL = _backend == RENDERER_OPENGL || _backend == RENDERER_WEBGL;
    uint32_t packedColor = packColorPMA(r, g, b, a, blend == BLEND_ADD, isGL);
    float ox = _transX, oy = _transY;

    if (!isGL) {
        float sx = _scaleX, sy = _scaleY;
        D3DVertex* v = &_batchBuffer.d3d[_batchVertCount];
        v[0] = { (x0 + ox) * sx, (y0 + oy) * sy, 0.5f, 1.0f, packedColor, u0, v0 };
        v[1] = { (x1 + ox) * sx, (y1 + oy) * sy, 0.5f, 1.0f, packedColor, u1, v1 };
        v[2] = { (x2 + ox) * sx, (y2 + oy) * sy, 0.5f, 1.0f, packedColor, u2, v2 };
        v[3] = { (x3 + ox) * sx, (y3 + oy) * sy, 0.5f, 1.0f, packedColor, u3, v3 };
    } else {
        GLVertex* v = &_batchBuffer.gl[_batchVertCount];
        v[0] = { x0 + ox, y0 + oy, packedColor, u0, v0 };
        v[1] = { x1 + ox, y1 + oy, packedColor, u1, v1 };
        v[2] = { x2 + ox, y2 + oy, packedColor, u2, v2 };
        v[3] = { x3 + ox, y3 + oy, packedColor, u3, v3 };
    }
    _batchVertCount += 4;
}

void RenderDevice::batchAxisAlignedQuad(uint32_t texID, float x0, float y0, float x1, float y1,
                                        float u0, float v0, float u1, float v1,
                                        float r, float g, float b, float a, BlendMode blend)
{
    if (texID != _currentTexID) _selectTexture(texID);
    else if (_batchVertCount + 4 > MAX_BATCH_VERTS) flushBatch();
    _currentBlend = blend;

    if (_uvScaled) { u0 *= _curUS; u1 *= _curUS; v0 *= _curVS; v1 *= _curVS; }

    const bool isGL = _backend == RENDERER_OPENGL || _backend == RENDERER_WEBGL;
    uint32_t packedColor = packColorPMA(r, g, b, a, blend == BLEND_ADD, isGL);
    float ox = _transX, oy = _transY;

    if (!isGL) {
        float sx = _scaleX, sy = _scaleY;
        float rx0 = (x0 + ox) * sx, ry0 = (y0 + oy) * sy;
        float rx1 = (x1 + ox) * sx, ry1 = (y1 + oy) * sy;
        D3DVertex* v = &_batchBuffer.d3d[_batchVertCount];
        v[0] = { rx0, ry0, 0.5f, 1.0f, packedColor, u0, v0 };
        v[1] = { rx1, ry0, 0.5f, 1.0f, packedColor, u1, v0 };
        v[2] = { rx1, ry1, 0.5f, 1.0f, packedColor, u1, v1 };
        v[3] = { rx0, ry1, 0.5f, 1.0f, packedColor, u0, v1 };
    } else {
        float rx0 = x0 + ox, ry0 = y0 + oy;
        float rx1 = x1 + ox, ry1 = y1 + oy;
        GLVertex* v = &_batchBuffer.gl[_batchVertCount];
        v[0] = { rx0, ry0, packedColor, u0, v0 };
        v[1] = { rx1, ry0, packedColor, u1, v0 };
        v[2] = { rx1, ry1, packedColor, u1, v1 };
        v[3] = { rx0, ry1, packedColor, u0, v1 };
    }
    _batchVertCount += 4;
}

void RenderDevice::drawRect(float x, float y, float w, float h, float r, float g, float b, float a, BlendMode blend) {
    batchAxisAlignedQuad(0, x, y, x + w, y + h, 0.0f, 0.0f, 1.0f, 1.0f, r, g, b, a, blend);
}

void RenderDevice::drawColorQuad(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3, float r, float g, float b, float a, BlendMode blend) {
    batchQuad(0, x0, y0, 0.0f, 0.0f, x1, y1, 1.0f, 0.0f, x2, y2, 1.0f, 1.0f, x3, y3, 0.0f, 1.0f, r, g, b, a, blend);
}

void RenderDevice::drawCircle(float cx, float cy, float radius, float r, float g, float b, float a, bool filled, BlendMode blend) {
    flushBatch();
    ++_drawCalls;
    _currentBlend = blend;
    const int segments = std::clamp(gpu::knobs().circleSegments, 6, 16);

    #ifdef HAS_GLES2
    if (_backend == RENDERER_WEBGL) {
        webgl_drawCircle(cx, cy, radius, r, g, b, a, filled, segments, _transX, _transY, blend);
        return;
    }
    #endif

    const bool additive = blend == BLEND_ADD;
    #ifdef _WIN32
    if (_backend == RENDERER_D3D8) {
        d3d8_drawCircle(cx, cy, radius, packColorPMA(r, g, b, a, additive, false), filled, segments, _transX, _transY, _scaleX, _scaleY);
        return;
    }
    if (_backend == RENDERER_D3D9) {
        d3d9_drawCircle(cx, cy, radius, packColorPMA(r, g, b, a, additive, false), filled, segments, _transX, _transY, _scaleX, _scaleY);
        return;
    }
    #endif

    gl1_drawCircle(cx, cy, radius, r, g, b, a, filled, segments, _transX, _transY, blend);
}

void RenderDevice::drawTriangleStrip(const float* coordsXY, const float* colorsRGBA, size_t vertCount, BlendMode blend) {
    if (vertCount < 3) return;
    flushBatch();
    ++_drawCalls;
    _currentBlend = blend;
    const bool additive = blend == BLEND_ADD;

    #ifdef HAS_GLES2
    if (_backend == RENDERER_WEBGL) {
        webgl_drawTriangleStrip(coordsXY, colorsRGBA, vertCount, additive, _transX, _transY);
        return;
    }
    #endif

    #ifdef _WIN32
    if (_backend == RENDERER_D3D8) {
        d3d8_drawTriangleStrip(coordsXY, colorsRGBA, vertCount, additive, _transX, _transY, _scaleX, _scaleY);
        return;
    }
    if (_backend == RENDERER_D3D9) {
        d3d9_drawTriangleStrip(coordsXY, colorsRGBA, vertCount, additive, _transX, _transY, _scaleX, _scaleY);
        return;
    }
    #endif

    gl1_drawTriangleStrip(coordsXY, colorsRGBA, vertCount, additive, _transX, _transY);
}

static inline bool isBackgroundTextureName(const std::string& name) {
    return name.find("bg") != std::string::npos || name.find("background") != std::string::npos;
}

void RenderDevice::drawRepeatedBackground(uint32_t texID, float scrollX, float camY, float bgR, float bgG, float bgB) {
    flushBatch();
    ++_drawCalls;

    const float baseBgW = 1024.0f;
    const float baseBgH = 1024.0f;
    float uvW = _logicalW / baseBgW;
    float uvH = _logicalH / baseBgH;

    // Wrap scrollX into [0, baseBgW) to eliminate coordinate drift and float precision jitter on mobile/web GPUs
    float wrappedScrollX = std::fmod(scrollX, baseBgW);
    if (wrappedScrollX < 0.0f) wrappedScrollX += baseBgW;
    float uvOffsetX = wrappedScrollX / baseBgW;

    float bgInitY = baseBgH - _logicalH - 180.0f;
    float bgPosY = bgInitY - camY * 0.1f;
    float uvOffsetY = bgPosY / baseBgH;

    uint32_t actualTexID = texID;
    if (actualTexID == 0) actualTexID = getTextureID("game_bg_01");
    if (actualTexID == 0) {
        for (const auto& pair : _textureRegistry) {
            if (pair.first.find("game_bg") != std::string::npos || pair.first.find("bg") != std::string::npos) {
                actualTexID = pair.second;
                break;
            }
        }
    }

    #ifdef HAS_GLES2
    if (_backend == RENDERER_WEBGL) {
        webgl_drawRepeatedBackground(actualTexID, uvOffsetX, uvOffsetY, uvW, uvH, _logicalW, _logicalH, bgR, bgG, bgB);
        return;
    }
    #endif

    #ifdef _WIN32
    if (_backend == RENDERER_D3D8) {
        d3d8_drawRepeatedBackground(actualTexID, uvOffsetX, uvOffsetY, uvW, uvH, _logicalW, _logicalH, bgR, bgG, bgB, _scaleX, _scaleY);
        return;
    }
    if (_backend == RENDERER_D3D9) {
        d3d9_drawRepeatedBackground(actualTexID, uvOffsetX, uvOffsetY, uvW, uvH, _logicalW, _logicalH, bgR, bgG, bgB, _scaleX, _scaleY);
        return;
    }
    #endif

    gl1_drawRepeatedBackground(actualTexID, uvOffsetX, uvOffsetY, uvW, uvH, _logicalW, _logicalH, bgR, bgG, bgB);
}

uint32_t RenderDevice::registerTexture(const std::string& name, int width, int height, const void* rgbaPixels) {
    if (!rgbaPixels || width <= 0 || height <= 0) return 0;
    uint32_t handle = _nextTexHandle++;
    const bool needPOT = !gpu::caps().npot;

    auto registerNames = [this, &name](uint32_t h) {
        _textureRegistry[name] = h;
        _textureRegistry[name + ".png"] = h;
        if (name == "game_bg_01") {
            _textureRegistry["game_bg_01_001"] = h;
            _textureRegistry["game_bg_01_001.png"] = h;
        }
    };

    #ifdef _WIN32
    if (_backend == RENDERER_D3D8) {
        registerNames(handle);
        _masterTextures[handle] = { name, width, height, std::vector<uint8_t>((const uint8_t*)rgbaPixels, (const uint8_t*)rgbaPixels + (size_t)width * height * 4) };
        PreparedTexture t = prepareTexture(rgbaPixels, width, height, needPOT, true, isBackgroundTextureName(name));
        _recordTexUV(handle, t);
        return d3d8_registerTexture(handle, t);
    }
    if (_backend == RENDERER_D3D9) {
        registerNames(handle);
        _masterTextures[handle] = { name, width, height, std::vector<uint8_t>((const uint8_t*)rgbaPixels, (const uint8_t*)rgbaPixels + (size_t)width * height * 4) };
        PreparedTexture t = prepareTexture(rgbaPixels, width, height, needPOT, true, isBackgroundTextureName(name));
        _recordTexUV(handle, t);
        return d3d9_registerTexture(handle, t);
    }
    #endif

    #ifdef HAS_GLES2
    if (_backend == RENDERER_WEBGL) {
        PreparedTexture t = prepareTexture(rgbaPixels, width, height, false, false, isBackgroundTextureName(name));
        uint32_t glID = webgl_registerTexture(t, isBackgroundTextureName(name));
        _recordTexUV(glID, t);
        registerNames(glID);
        _masterTextures[glID] = { name, width, height, std::vector<uint8_t>((const uint8_t*)rgbaPixels, (const uint8_t*)rgbaPixels + (size_t)width * height * 4) };
        return glID;
    }
    #endif

    PreparedTexture t = prepareTexture(rgbaPixels, width, height, needPOT, true, isBackgroundTextureName(name));
    uint32_t glID = gl1_registerTexture(t, isBackgroundTextureName(name));
    _recordTexUV(glID, t);
    registerNames(glID);
    _masterTextures[glID] = { name, width, height, std::vector<uint8_t>((const uint8_t*)rgbaPixels, (const uint8_t*)rgbaPixels + (size_t)width * height * 4) };
    return glID;
}

void RenderDevice::reloadTextures() {
    flushBatch();
    if (_masterTextures.empty()) return;

    const bool needPOT = !gpu::caps().npot;
    _tex16Active = false;

    #ifdef _WIN32
    if (_backend == RENDERER_D3D8) {
        for (const auto& pair : _masterTextures) {
            uint32_t handle = pair.first;
            const MasterTexture& master = pair.second;
            PreparedTexture t = prepareTexture(master.rgba.data(), master.width, master.height, needPOT, true, isBackgroundTextureName(master.name));
            _recordTexUV(handle, t);
            d3d8_reloadTexture(handle, t);
        }
        if (_currentTexID != 0) {
            auto it = _texUV.find(_currentTexID);
            if (it != _texUV.end()) { _uvScaled = true; _curUS = it->second.first; _curVS = it->second.second; }
            else { _uvScaled = false; _curUS = _curVS = 1.0f; }
        }
        std::cout << "[RenderDevice] Dynamic texture reload (DirectX 8 | " << gpu::presetName(gpu::resolvedPreset())
                  << ", 16bit=" << (gpu::knobs().texture16bit ? "yes" : "no")
                  << "): " << _masterTextures.size() << " textures updated.\n";
        return;
    }
    if (_backend == RENDERER_D3D9) {
        for (const auto& pair : _masterTextures) {
            uint32_t handle = pair.first;
            const MasterTexture& master = pair.second;
            PreparedTexture t = prepareTexture(master.rgba.data(), master.width, master.height, needPOT, true, isBackgroundTextureName(master.name));
            _recordTexUV(handle, t);
            d3d9_reloadTexture(handle, t);
        }
        if (_currentTexID != 0) {
            auto it = _texUV.find(_currentTexID);
            if (it != _texUV.end()) { _uvScaled = true; _curUS = it->second.first; _curVS = it->second.second; }
            else { _uvScaled = false; _curUS = _curVS = 1.0f; }
        }
        std::cout << "[RenderDevice] Dynamic texture reload (DirectX 9 | " << gpu::presetName(gpu::resolvedPreset())
                  << ", 16bit=" << (gpu::knobs().texture16bit ? "yes" : "no")
                  << "): " << _masterTextures.size() << " textures updated.\n";
        return;
    }
    #endif

    #ifdef HAS_GLES2
    if (_backend == RENDERER_WEBGL) {
        for (const auto& pair : _masterTextures) {
            uint32_t handle = pair.first;
            const MasterTexture& master = pair.second;
            PreparedTexture t = prepareTexture(master.rgba.data(), master.width, master.height, false, false, isBackgroundTextureName(master.name));
            _recordTexUV(handle, t);
            webgl_reloadTexture(handle, t, isBackgroundTextureName(master.name));
        }
        if (_currentTexID != 0) {
            auto it = _texUV.find(_currentTexID);
            if (it != _texUV.end()) { _uvScaled = true; _curUS = it->second.first; _curVS = it->second.second; }
            else { _uvScaled = false; _curUS = _curVS = 1.0f; }
        }
        #ifdef __ANDROID__
        std::cout << "[RenderDevice] Dynamic texture reload (OpenGL ES 2.0 | " << gpu::presetName(gpu::resolvedPreset())
        #else
        std::cout << "[RenderDevice] Dynamic texture reload (WebGL | " << gpu::presetName(gpu::resolvedPreset())
        #endif
                  << ", 16bit=" << (gpu::knobs().texture16bit ? "yes" : "no")
                  << "): " << _masterTextures.size() << " textures updated.\n";
        return;
    }
    #endif

    const bool allowIntensity = true;
    for (const auto& pair : _masterTextures) {
        uint32_t handle = pair.first;
        const MasterTexture& master = pair.second;
        PreparedTexture t = prepareTexture(master.rgba.data(), master.width, master.height, needPOT, allowIntensity, isBackgroundTextureName(master.name));
        _recordTexUV(handle, t);
        gl1_reloadTexture(handle, t, isBackgroundTextureName(master.name));
    }

    if (_currentTexID != 0) {
        auto it = _texUV.find(_currentTexID);
        if (it != _texUV.end()) { _uvScaled = true; _curUS = it->second.first; _curVS = it->second.second; }
        else { _uvScaled = false; _curUS = _curVS = 1.0f; }
    }
    std::cout << "[RenderDevice] Dynamic texture reload (OpenGL | " << gpu::presetName(gpu::resolvedPreset())
              << ", 16bit=" << (gpu::knobs().texture16bit ? "yes" : "no")
              << "): " << _masterTextures.size() << " textures updated.\n";
}

void RenderDevice::syncTexturesFromBootScene() {
#ifndef __ANDROID__
    std::string assetsDir = "assets";
    if (!fs::exists(assetsDir)) assetsDir = "build/assets";

    if (!fs::exists(assetsDir) || !fs::is_directory(assetsDir)) return;

    int count = 0;
    for (const auto& entry : fs::directory_iterator(assetsDir)) {
        if (entry.is_regular_file()) {
            std::string ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext == ".png") {
                std::string stem = entry.path().stem().string();
                std::string filename = entry.path().filename().string();
                if (_textureRegistry.find(stem) == _textureRegistry.end()) {
                    std::string path = entry.path().string();
                    int w = 0, h = 0, ch = 0;
                    unsigned char* pixels = stbi_load(path.c_str(), &w, &h, &ch, 4);
                    if (pixels) {
                        uint32_t handle = registerTexture(stem, w, h, pixels);
                        stbi_image_free(pixels);

                        BootScene::textures[stem] = { (GLuint)handle, w, h };
                        BootScene::textures[filename] = { (GLuint)handle, w, h };

                        if (stem.find("game_bg") != std::string::npos || stem.find("bg_01") != std::string::npos) {
                            BootScene::textures["game_bg_01"] = { (GLuint)handle, w, h };
                            _textureRegistry["game_bg_01"] = handle;
                        }
                        count++;
                    }
                }
            }
        }
    }

    for (const auto& entry : fs::directory_iterator(assetsDir)) {
        if (entry.is_regular_file()) {
            std::string ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext == ".fnt") {
                std::string stem = entry.path().stem().string();
                std::ifstream f(entry.path());
                if (f.is_open()) {
                    std::stringstream ss;
                    ss << f.rdbuf();
                    defineFontFromFnt(stem, ss.str());
                }
            }
        }
    }
    if (count > 0) {
        std::cout << "[RenderDevice] Extra textures synchronized: " << count << std::endl;
    }
#endif
}

uint32_t RenderDevice::getTextureID(const std::string& name) {
    auto it = _textureRegistry.find(name);
    return (it != _textureRegistry.end()) ? it->second : 0;
}
