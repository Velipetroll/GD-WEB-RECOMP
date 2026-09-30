#include "render-device.h"
#include "boot-scene.h"
#include "font-helpers.h"
#include "stb_image.h"
#include <SDL2/SDL_opengl.h>
#include <iostream>
#include <iomanip>
#include <cmath>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

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
extern void d3d8_setBlendMode(BlendMode mode);
extern void d3d8_flushBatch(const D3DVertex* buffer, size_t count, uint32_t currentTexID, BlendMode currentBlend);
extern void d3d8_drawCircle(float cx, float cy, float radius, float r, float g, float b, float a, bool filled, BlendMode blend, float transX, float transY, float scaleX, float scaleY);
extern void d3d8_drawTriangleStrip(const float* coordsXY, const float* colorsRGBA, size_t vertCount, BlendMode blend, float transX, float transY, float scaleX, float scaleY);
extern void d3d8_drawRepeatedBackground(uint32_t texID, float uvOffsetX, float uvOffsetY, float uvW, float uvH, float logicalW, float logicalH, float bgR, float bgG, float bgB, float scaleX, float scaleY);
extern uint32_t d3d8_registerTexture(uint32_t handle, int width, int height, const void* rgbaPixels);

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

RenderDevice::RenderDevice() {}
RenderDevice::~RenderDevice() { shutdown(); }

const char* RenderDevice::getBackendName() const {
    if (_backend == RENDERER_D3D8) return "DirectX 8";
    if (_backend == RENDERER_D3D9) return "DirectX 9";
    return "OpenGL 1.1";
}

bool RenderDevice::init(SDL_Window* window, RenderBackendType backend, int windowW, int windowH) {
    _window = window;
    _backend = backend;
    _vpW = windowW;
    _vpH = windowH;

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

bool RenderDevice::_initOpenGL() {
    _glContext = SDL_GL_CreateContext(_window);
    if (!_glContext) return false;

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_TEXTURE_2D);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_DITHER);

    // INTEL GMA OPTIMIZATION: Early discard of 100% transparent fragments before blend to save memory bus bandwidth
    glEnable(GL_ALPHA_TEST);
    glAlphaFunc(GL_GREATER, 0.005f);

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);

    glVertexPointer(2, GL_FLOAT, sizeof(GLVertex), &_batchBuffer.gl[0].x);
    glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(GLVertex), &_batchBuffer.gl[0].color);
    glTexCoordPointer(2, GL_FLOAT, sizeof(GLVertex), &_batchBuffer.gl[0].u);

    uint32_t whitePixels[4] = { 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF };
    glGenTextures(1, &_glWhiteTex);
    glBindTexture(GL_TEXTURE_2D, _glWhiteTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, whitePixels);
    _lastGLTex = _glWhiteTex;

    return true;
}

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

    IDirect3DDevice9* dev = nullptr;
    HRESULT hr = d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hWnd, D3DCREATE_HARDWARE_VERTEXPROCESSING, d3dpp, &dev);
    if (FAILED(hr)) {
        hr = d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hWnd, D3DCREATE_SOFTWARE_VERTEXPROCESSING, d3dpp, &dev);
        if (FAILED(hr)) return false;
    }
    _d3d9Device = (void*)dev;

    _d3d9CanA8L8 = SUCCEEDED(d3d->CheckDeviceFormat(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, d3dpp->BackBufferFormat,
                                                    0, D3DRTYPE_TEXTURE, D3DFMT_A8L8));
    _d3d9CanL8   = SUCCEEDED(d3d->CheckDeviceFormat(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, d3dpp->BackBufferFormat,
                                                    0, D3DRTYPE_TEXTURE, D3DFMT_L8));

    _applyD3D9RenderStates();
    _createD3D9WhiteTexture();
    _createD3D9BatchBuffers();
    std::cout << "[RenderDevice] Direct3D 9 initialized successfully (Intel GMA Circular Ring FastPath).\n";
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

    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    dev->SetRenderState(D3DRS_DESTBLEND, (_currentBlend == BLEND_ADD) ? D3DBLEND_ONE : D3DBLEND_INVSRCALPHA);
    dev->SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);
    dev->SetRenderState(D3DRS_ALPHAREF, 0x01);
    dev->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATER);

    dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    dev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
    dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    dev->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);

    dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    _lastD3D9Tex = nullptr;
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
}

void RenderDevice::setViewport(int vpX, int vpY, int vpW, int vpH, float logicalW, float logicalH) {
    if (vpW <= 0 || vpH <= 0) return;
    _vpX = vpX; _vpY = vpY; _vpW = vpW; _vpH = vpH;
    _logicalW = logicalW; _logicalH = logicalH;
    _scaleX = (logicalW > 0.0f) ? ((float)vpW / logicalW) : 1.0f;
    _scaleY = (logicalH > 0.0f) ? ((float)vpH / logicalH) : 1.0f;

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

    glViewport(vpX, vpY, vpW, vpH);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, logicalW, logicalH, 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

void RenderDevice::setVSync(bool enabled) {
    _vsync = enabled;
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
    #ifdef _WIN32
    if (_backend == RENDERER_D3D8) { d3d8_beginFrame(); return; }
    if (_backend == RENDERER_D3D9) { _inScene = false; return; }
    #endif
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

void RenderDevice::clear(float r, float g, float b, float a) {
    #ifdef _WIN32
    if (_backend == RENDERER_D3D8) { d3d8_clear(r, g, b, a); return; }
    if (_backend == RENDERER_D3D9 && _d3d9Device) {
        auto* dev = (IDirect3DDevice9*)_d3d9Device;
        dev->Clear(0, NULL, D3DCLEAR_TARGET, toD3D9Color(r, g, b, a), 1.0f, 0);
        dev->BeginScene();
        _inScene = true;
        return;
    }
    #endif
    glClearColor(r, g, b, a);
    glClear(GL_COLOR_BUFFER_BIT);
}

void RenderDevice::endFrame() {
    flushBatch();
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

void RenderDevice::setBlendMode(BlendMode mode) {
    if (_currentBlend == mode) return;
    flushBatch();
    _currentBlend = mode;
    #ifdef _WIN32
    if (_backend == RENDERER_D3D8) { d3d8_setBlendMode(mode); return; }
    if (_backend == RENDERER_D3D9 && _d3d9Device) {
        ((IDirect3DDevice9*)_d3d9Device)->SetRenderState(D3DRS_DESTBLEND, (mode == BLEND_ADD) ? D3DBLEND_ONE : D3DBLEND_INVSRCALPHA);
        return;
    }
    #endif
    if (mode == BLEND_ADD) glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    else glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void RenderDevice::beginBatch() {
    flushBatch();
    _batchVertCount = 0;
}

void RenderDevice::flushBatch() {
    if (_batchVertCount == 0) return;

    #ifdef _WIN32
    if (_backend == RENDERER_D3D8) {
        d3d8_flushBatch(_batchBuffer.d3d, _batchVertCount, _currentTexID, _currentBlend);
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

            // FVF / stream 0 / index buffer never change between batches: bind them once
            if (!_d3d9Bound) {
                dev->SetFVF(D3DFVF_D3D9_2D);
                dev->SetStreamSource(0, vb, 0, sizeof(D3DVertex));
                dev->SetIndices((IDirect3DIndexBuffer9*)_d3d9IB);
                _d3d9Bound = true;
            }
            UINT numPrimitives = (UINT)(_batchVertCount / 4) * 2;
            dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, (INT)_d3d9VbOffset, 0, _batchVertCount, 0, numPrimitives);
            _d3d9VbOffset += _batchVertCount;
        }
        _batchVertCount = 0;
        return;
    }
    #endif

    if (_currentBlend == BLEND_ADD) glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    else glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    GLuint bindTex = (_currentTexID != 0) ? _currentTexID : _glWhiteTex;
    if (_lastGLTex != bindTex) {
        glBindTexture(GL_TEXTURE_2D, bindTex);
        _lastGLTex = bindTex;
    }

    glVertexPointer(2, GL_FLOAT, sizeof(GLVertex), &_batchBuffer.gl[0].x);
    glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(GLVertex), &_batchBuffer.gl[0].color);
    glTexCoordPointer(2, GL_FLOAT, sizeof(GLVertex), &_batchBuffer.gl[0].u);

    glDrawArrays(GL_QUADS, 0, (GLsizei)_batchVertCount);
    _batchVertCount = 0;
}

static inline uint32_t fastPackColor(float r, float g, float b, float a, bool isOpenGL) {
    if (r >= 1.0f && g >= 1.0f && b >= 1.0f && a >= 1.0f) return 0xFFFFFFFF;
    uint32_t ca = (uint32_t)(a <= 0.0f ? 0 : (a >= 1.0f ? 255 : (int)(a * 255.0f)));
    uint32_t cr = (uint32_t)(r <= 0.0f ? 0 : (r >= 1.0f ? 255 : (int)(r * 255.0f)));
    uint32_t cg = (uint32_t)(g <= 0.0f ? 0 : (g >= 1.0f ? 255 : (int)(g * 255.0f)));
    uint32_t cb = (uint32_t)(b <= 0.0f ? 0 : (b >= 1.0f ? 255 : (int)(b * 255.0f)));
    return isOpenGL ? ((ca << 24) | (cb << 16) | (cg << 8) | cr)
    : ((ca << 24) | (cr << 16) | (cg << 8) | cb);
}

void RenderDevice::batchQuad(uint32_t texID, float x0, float y0, float u0, float v0,
                             float x1, float y1, float u1, float v1,
                             float x2, float y2, float u2, float v2,
                             float x3, float y3, float u3, float v3,
                             float r, float g, float b, float a, BlendMode blend)
{
    if (_batchVertCount + 4 > MAX_BATCH_VERTS || texID != _currentTexID || blend != _currentBlend) {
        flushBatch();
        _currentTexID = texID;
        if (blend != _currentBlend) {
            _currentBlend = blend;
            #ifdef _WIN32
            // Keep D3D9 hardware state in sync when blend changes implicitly (not via setBlendMode)
            if (_backend == RENDERER_D3D9 && _d3d9Device) {
                ((IDirect3DDevice9*)_d3d9Device)->SetRenderState(D3DRS_DESTBLEND,
                                                                 (blend == BLEND_ADD) ? D3DBLEND_ONE : D3DBLEND_INVSRCALPHA);
            }
            #endif
        }
    }

    uint32_t packedColor = fastPackColor(r, g, b, a, _backend == RENDERER_OPENGL);
    float ox = _transX, oy = _transY;

    if (_backend != RENDERER_OPENGL) {
        float sx = _scaleX, sy = _scaleY;
        D3DVertex* v = &_batchBuffer.d3d[_batchVertCount];
        if (ox == 0.0f && oy == 0.0f) {
            v[0] = { x0 * sx, y0 * sy, 0.5f, 1.0f, packedColor, u0, v0 };
            v[1] = { x1 * sx, y1 * sy, 0.5f, 1.0f, packedColor, u1, v1 };
            v[2] = { x2 * sx, y2 * sy, 0.5f, 1.0f, packedColor, u2, v2 };
            v[3] = { x3 * sx, y3 * sy, 0.5f, 1.0f, packedColor, u3, v3 };
        } else {
            v[0] = { (x0 + ox) * sx, (y0 + oy) * sy, 0.5f, 1.0f, packedColor, u0, v0 };
            v[1] = { (x1 + ox) * sx, (y1 + oy) * sy, 0.5f, 1.0f, packedColor, u1, v1 };
            v[2] = { (x2 + ox) * sx, (y2 + oy) * sy, 0.5f, 1.0f, packedColor, u2, v2 };
            v[3] = { (x3 + ox) * sx, (y3 + oy) * sy, 0.5f, 1.0f, packedColor, u3, v3 };
        }
    } else {
        GLVertex* v = &_batchBuffer.gl[_batchVertCount];
        if (ox == 0.0f && oy == 0.0f) {
            v[0] = { x0, y0, packedColor, u0, v0 };
            v[1] = { x1, y1, packedColor, u1, v1 };
            v[2] = { x2, y2, packedColor, u2, v2 };
            v[3] = { x3, y3, packedColor, u3, v3 };
        } else {
            v[0] = { x0 + ox, y0 + oy, packedColor, u0, v0 };
            v[1] = { x1 + ox, y1 + oy, packedColor, u1, v1 };
            v[2] = { x2 + ox, y2 + oy, packedColor, u2, v2 };
            v[3] = { x3 + ox, y3 + oy, packedColor, u3, v3 };
        }
    }
    _batchVertCount += 4;
}

void RenderDevice::batchAxisAlignedQuad(uint32_t texID, float x0, float y0, float x1, float y1,
                                        float u0, float v0, float u1, float v1,
                                        float r, float g, float b, float a, BlendMode blend)
{
    if (_batchVertCount + 4 > MAX_BATCH_VERTS || texID != _currentTexID || blend != _currentBlend) {
        flushBatch();
        _currentTexID = texID;
        if (blend != _currentBlend) {
            _currentBlend = blend;
            #ifdef _WIN32
            // Keep D3D9 hardware state in sync when blend changes implicitly (not via setBlendMode)
            if (_backend == RENDERER_D3D9 && _d3d9Device) {
                ((IDirect3DDevice9*)_d3d9Device)->SetRenderState(D3DRS_DESTBLEND,
                                                                 (blend == BLEND_ADD) ? D3DBLEND_ONE : D3DBLEND_INVSRCALPHA);
            }
            #endif
        }
    }

    uint32_t packedColor = fastPackColor(r, g, b, a, _backend == RENDERER_OPENGL);
    float ox = _transX, oy = _transY;

    if (_backend != RENDERER_OPENGL) {
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
    #ifdef _WIN32
    if (_backend == RENDERER_D3D8) {
        d3d8_drawCircle(cx, cy, radius, r, g, b, a, filled, blend, _transX, _transY, _scaleX, _scaleY);
        return;
    }
    if (_backend == RENDERER_D3D9 && _d3d9Device && _d3d9VB) {
        auto* dev = (IDirect3DDevice9*)_d3d9Device;
        setBlendMode(blend);
        if (_lastD3D9Tex != _d3d9WhiteTex) {
            dev->SetTexture(0, (IDirect3DTexture9*)_d3d9WhiteTex);
            _lastD3D9Tex = _d3d9WhiteTex;
        }
        dev->SetFVF(D3DFVF_D3D9_2D);

        const int segments = 16;
        DWORD color = toD3D9Color(r, g, b, a);
        size_t count = filled ? (segments + 2) : (segments + 1);

        DWORD lockFlags = D3DLOCK_NOOVERWRITE;
        if (_d3d9VbOffset + count > D3D9_RING_VERTS) {
            _d3d9VbOffset = 0;
            lockFlags = D3DLOCK_DISCARD;
        }

        D3DVertex* pLock = nullptr;
        auto* vb = (IDirect3DVertexBuffer9*)_d3d9VB;
        if (SUCCEEDED(vb->Lock(_d3d9VbOffset * sizeof(D3DVertex), count * sizeof(D3DVertex), (void**)&pLock, lockFlags))) {
            if (filled) {
                pLock[0] = { (cx + _transX) * _scaleX, (cy + _transY) * _scaleY, 0.5f, 1.0f, color, 0.5f, 0.5f };
                for (int i = 0; i <= segments; ++i) {
                    float ang = (i / (float)segments) * 6.2831853f;
                    pLock[i + 1] = { (cx + _transX + std::cos(ang) * radius) * _scaleX,
                        (cy + _transY + std::sin(ang) * radius) * _scaleY,
                        0.5f, 1.0f, color, 0.5f, 0.5f };
                }
                vb->Unlock();

                dev->SetStreamSource(0, vb, 0, sizeof(D3DVertex));
                dev->DrawPrimitive(D3DPT_TRIANGLEFAN, (UINT)_d3d9VbOffset, segments);
            } else {
                for (int i = 0; i <= segments; ++i) {
                    float ang = (i / (float)segments) * 6.2831853f;
                    pLock[i] = { (cx + _transX + std::cos(ang) * radius) * _scaleX,
                        (cy + _transY + std::sin(ang) * radius) * _scaleY,
                        0.5f, 1.0f, color, 0.5f, 0.5f };
                }
                vb->Unlock();

                dev->SetStreamSource(0, vb, 0, sizeof(D3DVertex));
                dev->DrawPrimitive(D3DPT_LINESTRIP, (UINT)_d3d9VbOffset, segments);
            }
            _d3d9VbOffset += count;
        }
        return;
    }
    #endif

    glDisable(GL_TEXTURE_2D);
    setBlendMode(blend);
    glPushMatrix();
    glTranslatef(_transX, _transY, 0.0f);
    const int segments = 16;
    float verts[18 * 2];
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
    glColor4f(r, g, b, a);
    if (filled) {
        verts[0] = cx; verts[1] = cy;
        for (int i = 0; i <= segments; ++i) {
            float ang = (i / (float)segments) * 6.2831853f;
            verts[(i + 1) * 2 + 0] = cx + std::cos(ang) * radius;
            verts[(i + 1) * 2 + 1] = cy + std::sin(ang) * radius;
        }
        glVertexPointer(2, GL_FLOAT, 0, verts);
        glDrawArrays(GL_TRIANGLE_FAN, 0, segments + 2);
    } else {
        for (int i = 0; i < segments; ++i) {
            float ang = (i / (float)segments) * 6.2831853f;
            verts[i * 2 + 0] = cx + std::cos(ang) * radius;
            verts[i * 2 + 1] = cy + std::sin(ang) * radius;
        }
        glLineWidth(3.0f);
        glVertexPointer(2, GL_FLOAT, 0, verts);
        glDrawArrays(GL_LINE_LOOP, 0, segments);
        glLineWidth(1.0f);
    }
    glEnableClientState(GL_COLOR_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glPopMatrix();
    glEnable(GL_TEXTURE_2D);

    glVertexPointer(2, GL_FLOAT, sizeof(GLVertex), &_batchBuffer.gl[0].x);
    glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(GLVertex), &_batchBuffer.gl[0].color);
    glTexCoordPointer(2, GL_FLOAT, sizeof(GLVertex), &_batchBuffer.gl[0].u);
}

void RenderDevice::drawTriangleStrip(const float* coordsXY, const float* colorsRGBA, size_t vertCount, BlendMode blend) {
    if (vertCount < 3) return;
    flushBatch();
    #ifdef _WIN32
    if (_backend == RENDERER_D3D8) {
        d3d8_drawTriangleStrip(coordsXY, colorsRGBA, vertCount, blend, _transX, _transY, _scaleX, _scaleY);
        return;
    }
    if (_backend == RENDERER_D3D9 && _d3d9Device && _d3d9VB) {
        auto* dev = (IDirect3DDevice9*)_d3d9Device;
        setBlendMode(blend);
        if (_lastD3D9Tex != _d3d9WhiteTex) {
            dev->SetTexture(0, (IDirect3DTexture9*)_d3d9WhiteTex);
            _lastD3D9Tex = _d3d9WhiteTex;
        }
        dev->SetFVF(D3DFVF_D3D9_2D);

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
                pLock[i].color = toD3D9Color(colorsRGBA[i * 4 + 0], colorsRGBA[i * 4 + 1], colorsRGBA[i * 4 + 2], colorsRGBA[i * 4 + 3]);
                pLock[i].u = 0.5f; pLock[i].v = 0.5f;
            }
            vb->Unlock();

            dev->SetStreamSource(0, vb, 0, sizeof(D3DVertex));
            dev->DrawPrimitive(D3DPT_TRIANGLESTRIP, (UINT)_d3d9VbOffset, (UINT)vertCount - 2);
            _d3d9VbOffset += vertCount;
        }
        return;
    }
    #endif

    glDisable(GL_TEXTURE_2D);
    setBlendMode(blend);
    glPushMatrix();
    glTranslatef(_transX, _transY, 0.0f);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glVertexPointer(2, GL_FLOAT, 0, coordsXY);
    glColorPointer(4, GL_FLOAT, 0, colorsRGBA);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, (GLsizei)vertCount);
    glEnableClientState(GL_COLOR_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glPopMatrix();
    glEnable(GL_TEXTURE_2D);

    glVertexPointer(2, GL_FLOAT, sizeof(GLVertex), &_batchBuffer.gl[0].x);
    glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(GLVertex), &_batchBuffer.gl[0].color);
    glTexCoordPointer(2, GL_FLOAT, sizeof(GLVertex), &_batchBuffer.gl[0].u);
}

void RenderDevice::drawRepeatedBackground(uint32_t texID, float scrollX, float camY, float bgR, float bgG, float bgB) {
    flushBatch();

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

        dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);   // full-screen opaque pass: no per-pixel alpha test
        dev->SetTexture(0, tex);
        _lastD3D9Tex = tex;
        dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
        dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
        dev->SetFVF(D3DFVF_D3D9_2D);

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

            dev->SetStreamSource(0, vb, 0, sizeof(D3DVertex));
            dev->DrawPrimitive(D3DPT_TRIANGLESTRIP, (UINT)_d3d9VbOffset, 2);
            _d3d9VbOffset += 4;
        }

        dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
        dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
        // Restore full blend pipeline state (not just ALPHABLENDENABLE) so the
        // cube, generic floor and particles that render afterwards use the
        // correct DESTBLEND equation and don't appear transparent/wrong.
        dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
        dev->SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);
        dev->SetRenderState(D3DRS_DESTBLEND,
                            (_currentBlend == BLEND_ADD) ? D3DBLEND_ONE : D3DBLEND_INVSRCALPHA);
        return;
    }
    #endif

    glDisable(GL_BLEND);
    glDisable(GL_ALPHA_TEST);   // full-screen opaque pass: no per-pixel alpha test
    glEnable(GL_TEXTURE_2D);
    if (_lastGLTex != actualTexID) {
        glBindTexture(GL_TEXTURE_2D, actualTexID);
        _lastGLTex = actualTexID;
    }
    static uint32_t lastBoundBg = 0;
    if (lastBoundBg != actualTexID) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        lastBoundBg = actualTexID;
    }

    uint32_t ca = 255;
    uint32_t cr = (uint32_t)(std::clamp(bgR, 0.0f, 1.0f) * 255.0f);
    uint32_t cg = (uint32_t)(std::clamp(bgG, 0.0f, 1.0f) * 255.0f);
    uint32_t cb = (uint32_t)(std::clamp(bgB, 0.0f, 1.0f) * 255.0f);
    uint32_t colorGL = (ca << 24) | (cb << 16) | (cg << 8) | cr;

    _batchBuffer.gl[0] = { 0.0f,       0.0f,       colorGL, uvOffsetX,       uvOffsetY };
    _batchBuffer.gl[1] = { _logicalW,  0.0f,       colorGL, uvOffsetX + uvW, uvOffsetY };
    _batchBuffer.gl[2] = { _logicalW,  _logicalH,  colorGL, uvOffsetX + uvW, uvOffsetY + uvH };
    _batchBuffer.gl[3] = { 0.0f,       _logicalH,  colorGL, uvOffsetX,       uvOffsetY + uvH };

    glVertexPointer(2, GL_FLOAT, sizeof(GLVertex), &_batchBuffer.gl[0].x);
    glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(GLVertex), &_batchBuffer.gl[0].color);
    glTexCoordPointer(2, GL_FLOAT, sizeof(GLVertex), &_batchBuffer.gl[0].u);

    glDrawArrays(GL_QUADS, 0, 4);
    glEnable(GL_ALPHA_TEST);
    glEnable(GL_BLEND);
}

uint32_t RenderDevice::registerTexture(const std::string& name, int width, int height, const void* rgbaPixels) {
    uint32_t handle = _nextTexHandle++;
    _textureRegistry[name] = handle;

    #ifdef _WIN32
    if (_backend == RENDERER_D3D8) {
        return d3d8_registerTexture(handle, width, height, rgbaPixels);
    }
    if (_backend == RENDERER_D3D9 && _d3d9Device) {
        auto* dev = (IDirect3DDevice9*)_d3d9Device;
        const size_t texels = (size_t)width * (size_t)height;
        const int kind = classifyRGBA(rgbaPixels, texels);

        // 0 = A8R8G8B8, 1 = A8L8 (2 B/texel), 2 = L8 (1 B/texel)
        int mode = 0;
        IDirect3DTexture9* tex = nullptr;
        if ((kind & TEXKIND_GRAY_OPAQUE) && _d3d9CanL8 &&
            SUCCEEDED(dev->CreateTexture(width, height, 1, 0, D3DFMT_L8, D3DPOOL_MANAGED, &tex, NULL))) {
            mode = 2;
            } else if (kind != 0 && _d3d9CanA8L8 &&
                SUCCEEDED(dev->CreateTexture(width, height, 1, 0, D3DFMT_A8L8, D3DPOOL_MANAGED, &tex, NULL))) {
                mode = 1;
                } else {
                    tex = nullptr;
                    if (FAILED(dev->CreateTexture(width, height, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex, NULL))) tex = nullptr;
                }

                if (tex) {
                    D3DLOCKED_RECT lr;
                    if (SUCCEEDED(tex->LockRect(0, &lr, NULL, 0))) {
                        const auto* src = (const uint8_t*)rgbaPixels;
                        auto* dst = (uint8_t*)lr.pBits;
                        for (int y = 0; y < height; ++y) {
                            const auto* srcRow = (const uint32_t*)(src + (size_t)y * width * 4);
                            if (mode == 2) {
                                auto* dstRow = dst + (size_t)y * lr.Pitch;
                                for (int x = 0; x < width; ++x) dstRow[x] = (uint8_t)(srcRow[x] & 0xFF);
                            } else if (mode == 1) {
                                auto* dstRow = (uint16_t*)(dst + (size_t)y * lr.Pitch);
                                for (int x = 0; x < width; ++x) {
                                    uint32_t c = srcRow[x];
                                    dstRow[x] = (uint16_t)(((c >> 24) << 8) | (c & 0xFF));   // A8L8: A in high byte
                                }
                            } else {
                                auto* dstRow = (uint32_t*)(dst + (size_t)y * lr.Pitch);
                                for (int x = 0; x < width; ++x) {
                                    uint32_t c = srcRow[x];
                                    dstRow[x] = (c & 0xFF00FF00u) | ((c & 0xFFu) << 16) | ((c >> 16) & 0xFFu);  // RGBA -> ARGB
                                }
                            }
                        }
                        tex->UnlockRect(0);
                        _d3d9Textures[handle] = (void*)tex;
                    } else {
                        tex->Release();
                    }
                }
                return handle;
    }
    #endif

    flushBatch();   // never change the bound texture underneath pending quads

    GLuint glID = 0;
    glGenTextures(1, &glID);
    glBindTexture(GL_TEXTURE_2D, glID);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    const size_t texels = (size_t)width * (size_t)height;
    const int kind = classifyRGBA(rgbaPixels, texels);
    const uint32_t* px = (const uint32_t*)rgbaPixels;
    if (kind & TEXKIND_GRAY_OPAQUE) {
        // opaque grayscale: 1 byte per texel, GL_LUMINANCE gives (L,L,L,1)
        std::vector<uint8_t> l(texels);
        for (size_t i = 0; i < texels; ++i) l[i] = (uint8_t)(px[i] & 0xFF);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, width, height, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, l.data());
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    } else if (kind & TEXKIND_WHITE_ALPHA) {
        // RGB == white everywhere: 1 byte per texel (GL_ALPHA + GL_MODULATE keeps the vertex color untouched)
        std::vector<uint8_t> a(texels);
        for (size_t i = 0; i < texels; ++i) a[i] = (uint8_t)(px[i] >> 24);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, width, height, 0, GL_ALPHA, GL_UNSIGNED_BYTE, a.data());
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    } else if (kind & TEXKIND_GRAY_ALPHA) {
        // gray + alpha: 2 bytes per texel
        std::vector<uint8_t> la(texels * 2);
        for (size_t i = 0; i < texels; ++i) {
            la[i * 2 + 0] = (uint8_t)(px[i] & 0xFF);
            la[i * 2 + 1] = (uint8_t)(px[i] >> 24);
        }
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE_ALPHA, width, height, 0, GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE, la.data());
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    } else {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgbaPixels);
    }
    _lastGLTex = 0;   // bind cache is stale after touching GL_TEXTURE_2D
    return glID;
}

void RenderDevice::syncTexturesFromBootScene() {
    if (_backend == RENDERER_OPENGL) return;

    std::string assetsDir = "assets";
    if (!fs::exists(assetsDir)) assetsDir = "build/assets";

    int count = 0;
    if (fs::exists(assetsDir) && fs::is_directory(assetsDir)) {
        for (const auto& entry : fs::directory_iterator(assetsDir)) {
            if (entry.is_regular_file()) {
                std::string path = entry.path().string();
                std::string ext = entry.path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                if (ext == ".png") {
                    std::string stem = entry.path().stem().string();
                    std::string filename = entry.path().filename().string();
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
    }
    std::cout << "[RenderDevice] Synchronization complete: " << count << " textures loaded.\n";
}

uint32_t RenderDevice::getTextureID(const std::string& name) {
    auto it = _textureRegistry.find(name);
    return (it != _textureRegistry.end()) ? it->second : 0;
}
