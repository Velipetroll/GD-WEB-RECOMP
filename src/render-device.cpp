#include "render-device.h"
#include "boot-scene.h"
#include "font-helpers.h"
#include "stb_image.h"
#include <SDL2/SDL_opengl.h>
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

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

void setEngineBlendMode(BlendMode mode) {
    RenderDevice::get().setBlendMode(mode);
}

// External declarations implemented in render-d3d8.cpp
extern bool d3d8_init(SDL_Window* window, int windowW, int windowH, bool vsync);
extern void d3d8_shutdown();
extern void d3d8_setViewport(int vpX, int vpY, int vpW, int vpH);
extern void d3d8_setVSync(bool enabled);
extern void d3d8_beginFrame();
extern void d3d8_clear(float r, float g, float b, float a);
extern void d3d8_endFrame();
extern void d3d8_flushBatch(const D3DVertex* buffer, size_t count, uint32_t currentTexID);
extern void d3d8_drawCircle(float cx, float cy, float radius, uint32_t color, bool filled, int segments, float transX, float transY, float scaleX, float scaleY);
extern void d3d8_drawTriangleStrip(const float* coordsXY, const float* colorsRGBA, size_t vertCount, bool additive, float transX, float transY, float scaleX, float scaleY);
extern void d3d8_drawRepeatedBackground(uint32_t texID, float uvOffsetX, float uvOffsetY, float uvW, float uvH, float logicalW, float logicalH, float bgR, float bgG, float bgB, float scaleX, float scaleY);
extern uint32_t d3d8_registerTexture(uint32_t handle, const PreparedTexture& t);
extern void d3d8_reloadTexture(uint32_t handle, const PreparedTexture& t);

#if defined(__EMSCRIPTEN__) || defined(__ANDROID__)
#define HAS_GLES2 1
#include "render-webgl.h"
#endif

#ifdef _WIN32
#include <d3d9.h>
#include <SDL2/SDL_syswm.h>

#define D3DFVF_D3D9_2D (D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1)
typedef IDirect3D9* (WINAPI *Direct3DCreate9_Fn)(UINT SDKVersion);

static inline DWORD toD3D9Color(float r, float g, float b, float a) {
    auto ca = (DWORD)(std::clamp(a, 0.0f, 1.0f) * 255.0f);
    auto cr = (DWORD)(std::clamp(r, 0.0f, 1.0f) * 255.0f);
    auto cg = (DWORD)(std::clamp(g, 0.0f, 1.0f) * 255.0f);
    auto cb = (DWORD)(std::clamp(b, 0.0f, 1.0f) * 255.0f);
    return (ca << 24) | (cr << 16) | (cg << 8) | cb;
}
#endif

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
            // e.g. the 1046x1046 atlas -> 1024x1024 (2% smaller, 4x less memory than padding to 2048)
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
    if (_backend == RENDERER_WEBGL) return "WebGL 2.0 FastPath";
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
        if (!_initD3D9()) {
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
        if (!_initOpenGL()) return false;
    }

    setViewport(0, 0, windowW, windowH, (float)screenWidth, (float)screenHeight);
    return true;
}

#ifndef HAS_GLES2
void RenderDevice::_bindGLBatchPointers() {
    glVertexPointer(2, GL_FLOAT, sizeof(GLVertex), &_batchBuffer.gl[0].x);
    glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(GLVertex), &_batchBuffer.gl[0].color);
    glTexCoordPointer(2, GL_FLOAT, sizeof(GLVertex), &_batchBuffer.gl[0].u);
    _glPointersBound = true;
}

bool RenderDevice::_initOpenGL() {
    _glContext = SDL_GL_CreateContext(_window);
    if (!_glContext) return false;

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
    // Fully transparent texels add exactly 0, so the image is unchanged.
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_LIGHTING);
    glDisable(GL_FOG);
    glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_FASTEST);   // 2D only: GMA can skip per-pixel perspective divide

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    _bindGLBatchPointers();

    uint32_t whitePixels[4] = { 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF };
    glGenTextures(1, &_glWhiteTex);
    glBindTexture(GL_TEXTURE_2D, _glWhiteTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, whitePixels);
    _lastGLTex = _glWhiteTex;

    return true;
}
#else
void RenderDevice::_bindGLBatchPointers() {}
bool RenderDevice::_initOpenGL() { return true; }
#endif

#ifdef _WIN32
bool RenderDevice::_initD3D9() {
    _hD3D9Module = (void*)LoadLibraryA("d3d9.dll");
    if (!_hD3D9Module) return false;

    auto pCreate = (Direct3DCreate9_Fn)GetProcAddress((HMODULE)_hD3D9Module, "Direct3DCreate9");
    if (!pCreate) { FreeLibrary((HMODULE)_hD3D9Module); _hD3D9Module = nullptr; return false; }

    _d3d9 = (void*)pCreate(D3D_SDK_VERSION);
    if (!_d3d9) { FreeLibrary((HMODULE)_hD3D9Module); _hD3D9Module = nullptr; return false; }

    SDL_SysWMinfo wmInfo;
    SDL_VERSION(&wmInfo.version);
    if (!SDL_GetWindowWMInfo(_window, &wmInfo)) return false;
    HWND hWnd = wmInfo.info.win.window;

    auto* d3d = (IDirect3D9*)_d3d9;
    D3DDISPLAYMODE d3ddm;
    if (FAILED(d3d->GetAdapterDisplayMode(D3DADAPTER_DEFAULT, &d3ddm))) return false;

    // ---- GPU detection ----
    GpuCaps& caps = gpu::caps();
    D3DADAPTER_IDENTIFIER9 ident;
    if (SUCCEEDED(d3d->GetAdapterIdentifier(D3DADAPTER_DEFAULT, 0, &ident))) {
        caps.name = ident.Description;
        int t = gpu::tierFromPciId(ident.VendorId, ident.DeviceId, &caps.isIntelGMA);
        caps.tier = (t >= 0) ? t : gpu::tierFromName(gpu::lower(ident.Description), &caps.isIntelGMA);
    }
    D3DCAPS9 dcaps;
    if (SUCCEEDED(d3d->GetDeviceCaps(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, &dcaps))) {
        bool pow2 = (dcaps.TextureCaps & D3DPTEXTURECAPS_POW2) != 0;
        bool cond = (dcaps.TextureCaps & D3DPTEXTURECAPS_NONPOW2CONDITIONAL) != 0;
        caps.npot = !pow2 || cond;   // conditional NPOT is enough: clamp addressing, no mipmaps
    }
    caps.detected = true;

    auto* d3dpp = (D3DPRESENT_PARAMETERS*)_d3dpp9;
    ZeroMemory(d3dpp, sizeof(D3DPRESENT_PARAMETERS));
    d3dpp->Windowed = TRUE;
    d3dpp->SwapEffect = D3DSWAPEFFECT_DISCARD;
    d3dpp->BackBufferFormat = d3ddm.Format;
    d3dpp->BackBufferWidth = _vpW;
    d3dpp->BackBufferHeight = _vpH;
    d3dpp->BackBufferCount = 1;
    d3dpp->EnableAutoDepthStencil = FALSE;
    d3dpp->hDeviceWindow = hWnd;
    d3dpp->Flags = 0;
    d3dpp->PresentationInterval = _vsync ? D3DPRESENT_INTERVAL_ONE : D3DPRESENT_INTERVAL_IMMEDIATE;

    // GMA 900/950/3100/3150 have no hardware T&L: go straight to software VP (XYZRHW bypasses it anyway)
    DWORD order[2] = { D3DCREATE_HARDWARE_VERTEXPROCESSING, D3DCREATE_SOFTWARE_VERTEXPROCESSING };
    if (caps.isIntelGMA && caps.tier == 0) std::swap(order[0], order[1]);

    IDirect3DDevice9* dev = nullptr;
    HRESULT hr = d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hWnd, order[0], d3dpp, &dev);
    if (FAILED(hr)) {
        hr = d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hWnd, order[1], d3dpp, &dev);
        if (FAILED(hr)) return false;
    }
    _d3d9Device = (void*)dev;

    _d3d9CanA8L8 = SUCCEEDED(d3d->CheckDeviceFormat(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, d3dpp->BackBufferFormat,
                                                    0, D3DRTYPE_TEXTURE, D3DFMT_A8L8));
    _d3d9CanL8   = SUCCEEDED(d3d->CheckDeviceFormat(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, d3dpp->BackBufferFormat,
                                                    0, D3DRTYPE_TEXTURE, D3DFMT_L8));
    _d3d9CanA4R4G4B4 = SUCCEEDED(d3d->CheckDeviceFormat(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, d3dpp->BackBufferFormat,
                                                        0, D3DRTYPE_TEXTURE, D3DFMT_A4R4G4B4));

    _applyD3D9RenderStates();
    _createD3D9WhiteTexture();
    _createD3D9BatchBuffers();
    std::cout << "[RenderDevice] Direct3D 9 initialized (" << caps.name << " | tier " << caps.tier
              << " | NPOT " << (caps.npot ? "yes" : "no") << " | quality " << gpu::presetName(gpu::resolvedPreset()) << ").\n";
    return true;
}

void RenderDevice::_applyD3D9RenderStates() {
    auto* dev = (IDirect3DDevice9*)_d3d9Device;
    if (!dev) return;
    dev->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetRenderState(D3DRS_LIGHTING, FALSE);
    dev->SetRenderState(D3DRS_DITHERENABLE, FALSE);
    dev->SetRenderState(D3DRS_SPECULARENABLE, FALSE);
    dev->SetRenderState(D3DRS_FOGENABLE, FALSE);
    // INTEL GMA OPTIMIZATION: Disable CPU software clipping for pre-transformed vertices
    dev->SetRenderState(D3DRS_CLIPPING, FALSE);

    // Premultiplied alpha: one fixed blend equation for normal + additive (no per-batch state changes)
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
    dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);   // additive vertices carry alpha 0

    dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    dev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
    dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    dev->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
    dev->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    dev->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);

    dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    dev->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    _lastD3D9Tex = nullptr;
}

void RenderDevice::_bindD3D9Stream() {
    if (_d3d9Bound) return;
    auto* dev = (IDirect3DDevice9*)_d3d9Device;
    dev->SetFVF(D3DFVF_D3D9_2D);
    dev->SetStreamSource(0, (IDirect3DVertexBuffer9*)_d3d9VB, 0, sizeof(D3DVertex));
    dev->SetIndices((IDirect3DIndexBuffer9*)_d3d9IB);
    _d3d9Bound = true;
}

void RenderDevice::_onResizeD3D9(int newW, int newH) {
    auto* dev = (IDirect3DDevice9*)_d3d9Device;
    if (!dev || newW <= 0 || newH <= 0) return;
    flushBatch();

    if (_d3d9VB) { ((IDirect3DVertexBuffer9*)_d3d9VB)->Release(); _d3d9VB = nullptr; }
    _d3d9Bound = false;
    auto* d3dpp = (D3DPRESENT_PARAMETERS*)_d3dpp9;
    d3dpp->BackBufferWidth = newW;
    d3dpp->BackBufferHeight = newH;
    if (FAILED(dev->Reset(d3dpp))) return;

    _createD3D9BatchBuffers();
    _applyD3D9RenderStates();
}

void RenderDevice::_createD3D9WhiteTexture() {
    auto* dev = (IDirect3DDevice9*)_d3d9Device;
    if (!dev) return;
    IDirect3DTexture9* tex = nullptr;
    HRESULT hr = dev->CreateTexture(2, 2, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex, NULL);
    if (FAILED(hr)) hr = dev->CreateTexture(2, 2, 1, 0, D3DFMT_X8R8G8B8, D3DPOOL_MANAGED, &tex, NULL);
    if (tex) {
        D3DLOCKED_RECT lr;
        if (SUCCEEDED(tex->LockRect(0, &lr, NULL, 0))) {
            auto* p = (uint8_t*)lr.pBits;
            for (int y = 0; y < 2; ++y) {
                auto* row = (uint32_t*)(p + y * lr.Pitch);
                row[0] = 0xFFFFFFFF; row[1] = 0xFFFFFFFF;
            }
            tex->UnlockRect(0);
            _d3d9WhiteTex = (void*)tex;
        }
    }
}

void RenderDevice::_createD3D9BatchBuffers() {
    auto* dev = (IDirect3DDevice9*)_d3d9Device;
    if (!dev) return;
    _d3d9VbOffset = 0;
    _d3d9Bound = false;

    if (!_d3d9VB) {
        IDirect3DVertexBuffer9* vb = nullptr;
        dev->CreateVertexBuffer(D3D9_RING_VERTS * sizeof(D3DVertex), D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, D3DFVF_D3D9_2D, D3DPOOL_DEFAULT, &vb, NULL);
        _d3d9VB = (void*)vb;
    }
    if (!_d3d9IB) {
        IDirect3DIndexBuffer9* ib = nullptr;
        dev->CreateIndexBuffer(MAX_BATCH_QUADS * 6 * sizeof(uint16_t), D3DUSAGE_WRITEONLY, D3DFMT_INDEX16, D3DPOOL_MANAGED, &ib, NULL);
        if (ib) {
            uint16_t* idx = nullptr;
            if (SUCCEEDED(ib->Lock(0, 0, (void**)&idx, 0))) {
                for (uint16_t q = 0; q < MAX_BATCH_QUADS; ++q) {
                    uint16_t base = q * 4;
                    idx[q * 6 + 0] = base + 0; idx[q * 6 + 1] = base + 1; idx[q * 6 + 2] = base + 2;
                    idx[q * 6 + 3] = base + 0; idx[q * 6 + 4] = base + 2; idx[q * 6 + 5] = base + 3;
                }
                ib->Unlock();
                _d3d9IB = (void*)ib;
            }
        }
    }
}
#endif

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
    if (_d3d9WhiteTex) { ((IDirect3DTexture9*)_d3d9WhiteTex)->Release(); _d3d9WhiteTex = nullptr; }
    if (_d3d9VB) { ((IDirect3DVertexBuffer9*)_d3d9VB)->Release(); _d3d9VB = nullptr; }
    if (_d3d9IB) { ((IDirect3DIndexBuffer9*)_d3d9IB)->Release(); _d3d9IB = nullptr; }
    for (auto& pair : _d3d9Textures) { if (pair.second) ((IDirect3DTexture9*)pair.second)->Release(); }
    _d3d9Textures.clear();
    if (_d3d9Device) { ((IDirect3DDevice9*)_d3d9Device)->Release(); _d3d9Device = nullptr; }
    if (_d3d9) { ((IDirect3D9*)_d3d9)->Release(); _d3d9 = nullptr; }
    if (_hD3D9Module) { FreeLibrary((HMODULE)_hD3D9Module); _hD3D9Module = nullptr; }
    #endif

    if (_glWhiteTex) {
        glDeleteTextures(1, &_glWhiteTex);
        _glWhiteTex = 0;
    }

    if (_glContext) {
        SDL_GL_DeleteContext(_glContext);
        _glContext = nullptr;
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
    if (_backend == RENDERER_D3D9 && _d3d9Device) {
        auto* d3dpp = (D3DPRESENT_PARAMETERS*)_d3dpp9;
        if (vpW != (int)d3dpp->BackBufferWidth || vpH != (int)d3dpp->BackBufferHeight) {
            _onResizeD3D9(vpW, vpH);
        }
        D3DVIEWPORT9 vp;
        vp.X = vpX; vp.Y = vpY; vp.Width = vpW; vp.Height = vpH; vp.MinZ = 0.0f; vp.MaxZ = 1.0f;
        ((IDirect3DDevice9*)_d3d9Device)->SetViewport(&vp);
        return;
    }
    #endif

    #ifndef HAS_GLES2
    glViewport(vpX, vpY, vpW, vpH);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, logicalW, logicalH, 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    #endif
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
    if (_backend == RENDERER_D3D9 && _d3d9Device) {
        auto* d3dpp = (D3DPRESENT_PARAMETERS*)_d3dpp9;
        DWORD target = enabled ? D3DPRESENT_INTERVAL_ONE : D3DPRESENT_INTERVAL_IMMEDIATE;
        if (d3dpp->PresentationInterval != target) {
            d3dpp->PresentationInterval = target;
            _onResizeD3D9(_vpW, _vpH);
        }
        return;
    }
    #endif
    SDL_GL_SetSwapInterval(enabled ? 1 : 0);
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
    if (_backend == RENDERER_D3D9) { _inScene = false; return; }
    #endif
}

void RenderDevice::clear(float r, float g, float b, float a) {
    #ifdef HAS_GLES2
    if (_backend == RENDERER_WEBGL) { webgl_clear(r, g, b, a); return; }
    #endif
    #ifdef _WIN32
    if (_backend == RENDERER_D3D8) { d3d8_clear(r, g, b, a); return; }
    if (_backend == RENDERER_D3D9 && _d3d9Device) {
        auto* dev = (IDirect3DDevice9*)_d3d9Device;
        dev->Clear(0, NULL, D3DCLEAR_TARGET, toD3D9Color(r, g, b, a), 1.0f, 0);
        if (!_inScene) { dev->BeginScene(); _inScene = true; }
        return;
    }
    #endif
    glClearColor(r, g, b, a);
    glClear(GL_COLOR_BUFFER_BIT);
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
    if (_backend == RENDERER_D3D9 && _d3d9Device) {
        auto* dev = (IDirect3DDevice9*)_d3d9Device;
        if (_inScene) { dev->EndScene(); _inScene = false; }
        dev->Present(NULL, NULL, NULL, NULL);
        return;
    }
    #endif
    SDL_GL_SwapWindow(_window);
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
    if (_backend == RENDERER_D3D9 && _d3d9Device && _d3d9VB && _d3d9IB) {
        auto* dev = (IDirect3DDevice9*)_d3d9Device;
        if (!_inScene) {
            dev->BeginScene();
            _inScene = true;
        }

        IDirect3DTexture9* tex = (IDirect3DTexture9*)_d3d9WhiteTex;
        if (_currentTexID != 0) {
            auto it = _d3d9Textures.find(_currentTexID);
            if (it != _d3d9Textures.end() && it->second) tex = (IDirect3DTexture9*)it->second;
        }

        if (_lastD3D9Tex != tex) {
            dev->SetTexture(0, tex);
            _lastD3D9Tex = tex;
        }

        // Stall-free ring buffer: continuous D3DLOCK_NOOVERWRITE, DISCARD when restarting cycle
        DWORD lockFlags = D3DLOCK_NOOVERWRITE;
        if (_d3d9VbOffset + _batchVertCount > D3D9_RING_VERTS) {
            _d3d9VbOffset = 0;
            lockFlags = D3DLOCK_DISCARD;
        }

        D3DVertex* pLock = nullptr;
        auto* vb = (IDirect3DVertexBuffer9*)_d3d9VB;
        if (SUCCEEDED(vb->Lock(_d3d9VbOffset * sizeof(D3DVertex), _batchVertCount * sizeof(D3DVertex), (void**)&pLock, lockFlags))) {
            // Direct contiguous memory copy - Vertices already precalculated with scale and color
            memcpy(pLock, _batchBuffer.d3d, _batchVertCount * sizeof(D3DVertex));
            vb->Unlock();

            _bindD3D9Stream();   // FVF / stream 0 / index buffer never change: bound once
            UINT numPrimitives = (UINT)(_batchVertCount / 4) * 2;
            dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, (INT)_d3d9VbOffset, 0, _batchVertCount, 0, numPrimitives);
            _d3d9VbOffset += _batchVertCount;
        }
        _batchVertCount = 0;
        return;
    }
    #endif

    #ifndef HAS_GLES2
    // OpenGL: blend func is constant (premultiplied alpha); pointers only rebound after an immediate path
    GLuint bindTex = (_currentTexID != 0) ? _currentTexID : _glWhiteTex;
    if (_lastGLTex != bindTex) {
        glBindTexture(GL_TEXTURE_2D, bindTex);
        _lastGLTex = bindTex;
    }
    if (!_glPointersBound) _bindGLBatchPointers();

    glDrawArrays(GL_QUADS, 0, (GLsizei)_batchVertCount);
    _batchVertCount = 0;
    #endif
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
    if (_backend == RENDERER_D3D9 && _d3d9Device && _d3d9VB) {
        auto* dev = (IDirect3DDevice9*)_d3d9Device;
        if (!_inScene) { dev->BeginScene(); _inScene = true; }
        if (_lastD3D9Tex != _d3d9WhiteTex) {
            dev->SetTexture(0, (IDirect3DTexture9*)_d3d9WhiteTex);
            _lastD3D9Tex = _d3d9WhiteTex;
        }
        _bindD3D9Stream();

        DWORD color = packColorPMA(r, g, b, a, additive, false);
        size_t count = filled ? (segments + 2) : (segments + 1);

        DWORD lockFlags = D3DLOCK_NOOVERWRITE;
        if (_d3d9VbOffset + count > D3D9_RING_VERTS) {
            _d3d9VbOffset = 0;
            lockFlags = D3DLOCK_DISCARD;
        }

        D3DVertex* pLock = nullptr;
        auto* vb = (IDirect3DVertexBuffer9*)_d3d9VB;
        if (SUCCEEDED(vb->Lock(_d3d9VbOffset * sizeof(D3DVertex), count * sizeof(D3DVertex), (void**)&pLock, lockFlags))) {
            size_t o = 0;
            if (filled) pLock[o++] = { (cx + _transX) * _scaleX, (cy + _transY) * _scaleY, 0.5f, 1.0f, color, 0.5f, 0.5f };
            for (int i = 0; i <= segments; ++i) {
                float ang = (i / (float)segments) * 6.2831853f;
                pLock[o++] = { (cx + _transX + std::cos(ang) * radius) * _scaleX,
                               (cy + _transY + std::sin(ang) * radius) * _scaleY,
                               0.5f, 1.0f, color, 0.5f, 0.5f };
            }
            vb->Unlock();
            dev->DrawPrimitive(filled ? D3DPT_TRIANGLEFAN : D3DPT_LINESTRIP, (UINT)_d3d9VbOffset, segments);
            _d3d9VbOffset += count;
        }
        return;
    }
    #endif

    #ifndef HAS_GLES2
    glDisable(GL_TEXTURE_2D);
    float verts[18 * 2];
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
    float ac = std::clamp(a, 0.0f, 1.0f);
    glColor4f(r * ac, g * ac, b * ac, additive ? 0.0f : ac);
    float ox = cx + _transX, oy = cy + _transY;
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
    _glPointersBound = false;   // lazily rebound by the next batch flush
    #endif
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
    if (_backend == RENDERER_D3D9 && _d3d9Device && _d3d9VB) {
        auto* dev = (IDirect3DDevice9*)_d3d9Device;
        if (!_inScene) { dev->BeginScene(); _inScene = true; }
        if (_lastD3D9Tex != _d3d9WhiteTex) {
            dev->SetTexture(0, (IDirect3DTexture9*)_d3d9WhiteTex);
            _lastD3D9Tex = _d3d9WhiteTex;
        }
        _bindD3D9Stream();

        DWORD lockFlags = D3DLOCK_NOOVERWRITE;
        if (_d3d9VbOffset + vertCount > D3D9_RING_VERTS) {
            _d3d9VbOffset = 0;
            lockFlags = D3DLOCK_DISCARD;
        }

        D3DVertex* pLock = nullptr;
        auto* vb = (IDirect3DVertexBuffer9*)_d3d9VB;
        if (SUCCEEDED(vb->Lock(_d3d9VbOffset * sizeof(D3DVertex), vertCount * sizeof(D3DVertex), (void**)&pLock, lockFlags))) {
            for (size_t i = 0; i < vertCount; ++i) {
                pLock[i].x = (coordsXY[i * 2 + 0] + _transX) * _scaleX;
                pLock[i].y = (coordsXY[i * 2 + 1] + _transY) * _scaleY;
                pLock[i].z = 0.5f; pLock[i].rhw = 1.0f;
                pLock[i].color = packColorPMA(colorsRGBA[i * 4 + 0], colorsRGBA[i * 4 + 1], colorsRGBA[i * 4 + 2], colorsRGBA[i * 4 + 3], additive, false);
                pLock[i].u = 0.5f; pLock[i].v = 0.5f;
            }
            vb->Unlock();
            dev->DrawPrimitive(D3DPT_TRIANGLESTRIP, (UINT)_d3d9VbOffset, (UINT)vertCount - 2);
            _d3d9VbOffset += vertCount;
        }
        return;
    }
    #endif

    #ifndef HAS_GLES2
    // GL: translate + premultiply on the CPU (no matrix push/pop, no float colour arrays)
    static std::vector<GLVertex> strip;
    strip.resize(vertCount);
    for (size_t i = 0; i < vertCount; ++i) {
        strip[i].x = coordsXY[i * 2 + 0] + _transX;
        strip[i].y = coordsXY[i * 2 + 1] + _transY;
        strip[i].color = packColorPMA(colorsRGBA[i * 4 + 0], colorsRGBA[i * 4 + 1], colorsRGBA[i * 4 + 2], colorsRGBA[i * 4 + 3], additive, true);
        strip[i].u = strip[i].v = 0.5f;
    }
    if (_lastGLTex != _glWhiteTex) { glBindTexture(GL_TEXTURE_2D, _glWhiteTex); _lastGLTex = _glWhiteTex; }
    glVertexPointer(2, GL_FLOAT, sizeof(GLVertex), &strip[0].x);
    glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(GLVertex), &strip[0].color);
    glTexCoordPointer(2, GL_FLOAT, sizeof(GLVertex), &strip[0].u);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, (GLsizei)vertCount);
    _glPointersBound = false;
    #endif
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
    float uvOffsetX = scrollX / baseBgW;
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
    if (_backend == RENDERER_D3D9 && _d3d9Device && _d3d9VB) {
        auto* dev = (IDirect3DDevice9*)_d3d9Device;
        if (!_inScene) {
            dev->BeginScene();
            _inScene = true;
        }

        IDirect3DTexture9* tex = (IDirect3DTexture9*)_d3d9WhiteTex;
        auto it = _d3d9Textures.find(actualTexID);
        if (it != _d3d9Textures.end() && it->second) tex = (IDirect3DTexture9*)it->second;

        dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);   // full-screen opaque pass: no blending
        if (_lastD3D9Tex != tex) { dev->SetTexture(0, tex); _lastD3D9Tex = tex; }
        dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
        dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
        _bindD3D9Stream();

        DWORD color = toD3D9Color(bgR, bgG, bgB, 1.0f);
        DWORD lockFlags = D3DLOCK_NOOVERWRITE;
        if (_d3d9VbOffset + 4 > D3D9_RING_VERTS) {
            _d3d9VbOffset = 0;
            lockFlags = D3DLOCK_DISCARD;
        }

        D3DVertex* pLock = nullptr;
        auto* vb = (IDirect3DVertexBuffer9*)_d3d9VB;
        if (SUCCEEDED(vb->Lock(_d3d9VbOffset * sizeof(D3DVertex), 4 * sizeof(D3DVertex), (void**)&pLock, lockFlags))) {
            pLock[0] = { 0.0f,               0.0f,               0.5f, 1.0f, color, uvOffsetX,       uvOffsetY };
            pLock[1] = { _logicalW * _scaleX, 0.0f,               0.5f, 1.0f, color, uvOffsetX + uvW, uvOffsetY };
            pLock[2] = { 0.0f,               _logicalH * _scaleY, 0.5f, 1.0f, color, uvOffsetX,       uvOffsetY + uvH };
            pLock[3] = { _logicalW * _scaleX, _logicalH * _scaleY, 0.5f, 1.0f, color, uvOffsetX + uvW, uvOffsetY + uvH };
            vb->Unlock();
            dev->DrawPrimitive(D3DPT_TRIANGLESTRIP, (UINT)_d3d9VbOffset, 2);
            _d3d9VbOffset += 4;
        }

        dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
        dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
        dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
        return;
    }
    #endif

    #ifndef HAS_GLES2
    glDisable(GL_BLEND);   // full-screen opaque pass: no blending
    if (_lastGLTex != actualTexID) {
        glBindTexture(GL_TEXTURE_2D, actualTexID);
        _lastGLTex = actualTexID;
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    uint32_t colorGL = packColorPMA(bgR, bgG, bgB, 1.0f, false, true);

    _batchBuffer.gl[0] = { 0.0f,       0.0f,       colorGL, uvOffsetX,       uvOffsetY };
    _batchBuffer.gl[1] = { _logicalW,  0.0f,       colorGL, uvOffsetX + uvW, uvOffsetY };
    _batchBuffer.gl[2] = { _logicalW,  _logicalH,  colorGL, uvOffsetX + uvW, uvOffsetY + uvH };
    _batchBuffer.gl[3] = { 0.0f,       _logicalH,  colorGL, uvOffsetX,       uvOffsetY + uvH };

    if (!_glPointersBound) _bindGLBatchPointers();
    glDrawArrays(GL_QUADS, 0, 4);
    glEnable(GL_BLEND);
    #endif
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
    if (_backend == RENDERER_D3D9 && _d3d9Device) {
        auto* dev = (IDirect3DDevice9*)_d3d9Device;
        _masterTextures[handle] = { name, width, height, std::vector<uint8_t>((const uint8_t*)rgbaPixels, (const uint8_t*)rgbaPixels + (size_t)width * height * 4) };
        PreparedTexture t = prepareTexture(rgbaPixels, width, height, needPOT, true, isBackgroundTextureName(name));

        // d3dMode: 0 = A8R8G8B8, 1 = A8L8 (2 B/texel), 2 = L8 (1 B/texel), 3 = A4R4G4B4 (2 B/texel)
        int mode = 0;
        D3DFORMAT fmt = D3DFMT_A8R8G8B8;
        if (t.format == PF_L8 && _d3d9CanL8) { mode = 2; fmt = D3DFMT_L8; }
        else if ((t.format == PF_LA8 || t.format == PF_I8) && _d3d9CanA8L8) { mode = 1; fmt = D3DFMT_A8L8; }
        else if (t.format == PF_RGBA4444 && _d3d9CanA4R4G4B4) { mode = 3; fmt = D3DFMT_A4R4G4B4; }

        IDirect3DTexture9* tex = nullptr;
        if (FAILED(dev->CreateTexture(t.width, t.height, 1, 0, fmt, D3DPOOL_MANAGED, &tex, NULL))) {
            tex = nullptr; mode = 0;
            if (FAILED(dev->CreateTexture(t.width, t.height, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex, NULL))) tex = nullptr;
        }
        if (tex) {
            D3DLOCKED_RECT lr;
            if (SUCCEEDED(tex->LockRect(0, &lr, NULL, 0))) {
                writeD3DTexels(t, mode, lr.pBits, lr.Pitch);
                tex->UnlockRect(0);
                _d3d9Textures[handle] = (void*)tex;
            } else {
                tex->Release();
            }
        }
        _recordTexUV(handle, t);
        registerNames(handle);
        return handle;
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

    #ifndef HAS_GLES2
    flushBatch();   // never change the bound texture underneath pending quads

    const bool allowIntensity = true;
    PreparedTexture t = prepareTexture(rgbaPixels, width, height, needPOT, allowIntensity, isBackgroundTextureName(name));

    GLuint glID = 0;
    glGenTextures(1, &glID);
    glBindTexture(GL_TEXTURE_2D, glID);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    GLint wrapMode = isBackgroundTextureName(name) ? GL_REPEAT : GL_CLAMP_TO_EDGE;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapMode);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapMode);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    switch (t.format) {
        case PF_L8:   // opaque grayscale: (L,L,L,1)
            glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, t.width, t.height, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, t.data.data());
            break;
        case PF_I8:   // premultiplied white mask: (I,I,I,I), 1 byte per texel
            glTexImage2D(GL_TEXTURE_2D, 0, GL_INTENSITY8, t.width, t.height, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, t.data.data());
            break;
        case PF_LA8:  // premultiplied gray + alpha: 2 bytes per texel
            glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE_ALPHA, t.width, t.height, 0, GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE, t.data.data());
            break;
        case PF_RGBA4444:
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA4, t.width, t.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, t.data.data());
            break;
        default:
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, t.width, t.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, t.data.data());
            break;
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

    _lastGLTex = 0;   // bind cache is stale after touching GL_TEXTURE_2D
    _recordTexUV(glID, t);
    registerNames(glID);
    _masterTextures[glID] = { name, width, height, std::vector<uint8_t>((const uint8_t*)rgbaPixels, (const uint8_t*)rgbaPixels + (size_t)width * height * 4) };
    return glID;
    #else
    return 0;
    #endif
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
    if (_backend == RENDERER_D3D9 && _d3d9Device) {
        auto* dev = (IDirect3DDevice9*)_d3d9Device;
        for (const auto& pair : _masterTextures) {
            uint32_t handle = pair.first;
            const MasterTexture& master = pair.second;
            PreparedTexture t = prepareTexture(master.rgba.data(), master.width, master.height, needPOT, true, isBackgroundTextureName(master.name));
            _recordTexUV(handle, t);

            auto it = _d3d9Textures.find(handle);
            if (it != _d3d9Textures.end() && it->second) {
                if (_lastD3D9Tex == it->second) {
                    _lastD3D9Tex = nullptr;
                    dev->SetTexture(0, (IDirect3DTexture9*)_d3d9WhiteTex);
                }
                ((IDirect3DTexture9*)it->second)->Release();
                it->second = nullptr;
            }

            int mode = 0;
            D3DFORMAT fmt = D3DFMT_A8R8G8B8;
            if (t.format == PF_L8 && _d3d9CanL8) { mode = 2; fmt = D3DFMT_L8; }
            else if ((t.format == PF_LA8 || t.format == PF_I8) && _d3d9CanA8L8) { mode = 1; fmt = D3DFMT_A8L8; }
            else if (t.format == PF_RGBA4444 && _d3d9CanA4R4G4B4) { mode = 3; fmt = D3DFMT_A4R4G4B4; }

            IDirect3DTexture9* tex = nullptr;
            if (FAILED(dev->CreateTexture(t.width, t.height, 1, 0, fmt, D3DPOOL_MANAGED, &tex, NULL))) {
                tex = nullptr; mode = 0;
                if (FAILED(dev->CreateTexture(t.width, t.height, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex, NULL))) tex = nullptr;
            }
            if (tex) {
                D3DLOCKED_RECT lr;
                if (SUCCEEDED(tex->LockRect(0, &lr, NULL, 0))) {
                    writeD3DTexels(t, mode, lr.pBits, lr.Pitch);
                    tex->UnlockRect(0);
                    _d3d9Textures[handle] = (void*)tex;
                } else {
                    tex->Release();
                }
            }
        }
        _lastD3D9Tex = nullptr;
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
        std::cout << "[RenderDevice] Dynamic texture reload (WebGL | " << gpu::presetName(gpu::resolvedPreset())
                  << ", 16bit=" << (gpu::knobs().texture16bit ? "yes" : "no")
                  << "): " << _masterTextures.size() << " textures updated.\n";
        return;
    }
    #endif

    #ifndef HAS_GLES2
    const bool allowIntensity = true;

    for (const auto& pair : _masterTextures) {
        uint32_t handle = pair.first;
        const MasterTexture& master = pair.second;
        PreparedTexture t = prepareTexture(master.rgba.data(), master.width, master.height, needPOT, allowIntensity, isBackgroundTextureName(master.name));
        _recordTexUV(handle, t);

        glBindTexture(GL_TEXTURE_2D, handle);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        GLint wrapMode = isBackgroundTextureName(master.name) ? GL_REPEAT : GL_CLAMP_TO_EDGE;
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapMode);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapMode);

        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        switch (t.format) {
            case PF_L8:
                glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, t.width, t.height, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, t.data.data());
                break;
            case PF_I8:
                glTexImage2D(GL_TEXTURE_2D, 0, GL_INTENSITY8, t.width, t.height, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, t.data.data());
                break;
            case PF_LA8:
                glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE_ALPHA, t.width, t.height, 0, GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE, t.data.data());
                break;
            case PF_RGBA4444:
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA4, t.width, t.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, t.data.data());
                break;
            default:
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, t.width, t.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, t.data.data());
                break;
        }
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    }

    _lastGLTex = 0;
    if (_currentTexID != 0) {
        auto it = _texUV.find(_currentTexID);
        if (it != _texUV.end()) { _uvScaled = true; _curUS = it->second.first; _curVS = it->second.second; }
        else { _uvScaled = false; _curUS = _curVS = 1.0f; }
    }
    std::cout << "[RenderDevice] Dynamic texture reload (OpenGL | " << gpu::presetName(gpu::resolvedPreset())
              << ", 16bit=" << (gpu::knobs().texture16bit ? "yes" : "no")
              << "): " << _masterTextures.size() << " textures updated.\n";
    #endif
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
