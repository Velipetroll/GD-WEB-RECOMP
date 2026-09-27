#include "render-device.h"

#if defined(_WIN32) && !defined(_WIN64)
#include <d3d8.h>
#include <SDL2/SDL_syswm.h>
#include <iostream>
#include <algorithm>
#include <cstring>

#define D3DFVF_D3D8_2D (D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1)
typedef IDirect3D8* (WINAPI *Direct3DCreate8_Fn)(UINT SDKVersion);

static constexpr size_t D3D8_RING_VERTS = 32768; // 8192 quads in dynamic ring buffer
static constexpr size_t D3D8_MAX_QUADS = 4096;

struct D3D8State {
    HMODULE hModule = nullptr;
    IDirect3D8* d3d = nullptr;
    IDirect3DDevice8* device = nullptr;
    IDirect3DVertexBuffer8* vb = nullptr;
    IDirect3DIndexBuffer8* ib = nullptr;
    IDirect3DTexture8* whiteTex = nullptr;
    IDirect3DTexture8* lastBoundTex = nullptr;
    BlendMode lastBlend = (BlendMode)-1;
    size_t vbOffset = 0;
    D3DPRESENT_PARAMETERS d3dpp;
    std::unordered_map<uint32_t, IDirect3DTexture8*> textures;
    bool vsync = false;
    bool inScene = false;
};

static D3D8State s_d3d8;

static inline DWORD toD3D8Color(float r, float g, float b, float a) {
    auto ca = (DWORD)(std::clamp(a, 0.0f, 1.0f) * 255.0f);
    auto cr = (DWORD)(std::clamp(r, 0.0f, 1.0f) * 255.0f);
    auto cg = (DWORD)(std::clamp(g, 0.0f, 1.0f) * 255.0f);
    auto cb = (DWORD)(std::clamp(b, 0.0f, 1.0f) * 255.0f);
    return (ca << 24) | (cr << 16) | (cg << 8) | cb;
}

static void applyD3D8States() {
    if (!s_d3d8.device) return;
    s_d3d8.device->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
    s_d3d8.device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    s_d3d8.device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    s_d3d8.device->SetRenderState(D3DRS_LIGHTING, FALSE);
    s_d3d8.device->SetRenderState(D3DRS_DITHERENABLE, FALSE);
    s_d3d8.device->SetRenderState(D3DRS_SPECULARENABLE, FALSE);
    s_d3d8.device->SetRenderState(D3DRS_FOGENABLE, FALSE);
    // INTEL GMA OPTIMIZATION: Disable CPU software clipping for pre-transformed vertices
    s_d3d8.device->SetRenderState(D3DRS_CLIPPING, FALSE);

    s_d3d8.device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    s_d3d8.device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    s_d3d8.device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    s_d3d8.device->SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);
    s_d3d8.device->SetRenderState(D3DRS_ALPHAREF, 0x01);
    s_d3d8.device->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATER);

    s_d3d8.device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    s_d3d8.device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    s_d3d8.device->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    s_d3d8.device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
    s_d3d8.device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    s_d3d8.device->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);

    s_d3d8.device->SetTextureStageState(0, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
    s_d3d8.device->SetTextureStageState(0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
    s_d3d8.device->SetTextureStageState(0, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP);
    s_d3d8.device->SetTextureStageState(0, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);
    s_d3d8.lastBoundTex = nullptr;
    s_d3d8.lastBlend = (BlendMode)-1;
}

static void createD3D8Buffers() {
    if (!s_d3d8.device) return;
    s_d3d8.vbOffset = 0;
    if (!s_d3d8.vb) {
        s_d3d8.device->CreateVertexBuffer(D3D8_RING_VERTS * sizeof(D3DVertex),
                                          D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY,
                                          D3DFVF_D3D8_2D,
                                          D3DPOOL_DEFAULT,
                                          &s_d3d8.vb);
    }
    if (!s_d3d8.ib) {
        s_d3d8.device->CreateIndexBuffer(D3D8_MAX_QUADS * 6 * sizeof(uint16_t),
                                         D3DUSAGE_WRITEONLY,
                                         D3DFMT_INDEX16,
                                         D3DPOOL_MANAGED,
                                         &s_d3d8.ib);
        if (s_d3d8.ib) {
            uint16_t* idx = nullptr;
            if (SUCCEEDED(s_d3d8.ib->Lock(0, 0, (BYTE**)&idx, 0))) {
                for (uint16_t q = 0; q < D3D8_MAX_QUADS; ++q) {
                    uint16_t base = q * 4;
                    idx[q * 6 + 0] = base + 0; idx[q * 6 + 1] = base + 1; idx[q * 6 + 2] = base + 2;
                    idx[q * 6 + 3] = base + 0; idx[q * 6 + 4] = base + 2; idx[q * 6 + 5] = base + 3;
                }
                s_d3d8.ib->Unlock();
            }
        }
    }
}

static void createD3D8WhiteTex() {
    if (!s_d3d8.device) return;
    s_d3d8.device->CreateTexture(2, 2, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &s_d3d8.whiteTex);
    if (!s_d3d8.whiteTex) s_d3d8.device->CreateTexture(2, 2, 1, 0, D3DFMT_X8R8G8B8, D3DPOOL_MANAGED, &s_d3d8.whiteTex);
    if (s_d3d8.whiteTex) {
        D3DLOCKED_RECT lr;
        if (SUCCEEDED(s_d3d8.whiteTex->LockRect(0, &lr, NULL, 0))) {
            auto* p = (uint8_t*)lr.pBits;
            for (int y = 0; y < 2; ++y) {
                auto* row = (uint32_t*)(p + y * lr.Pitch);
                row[0] = 0xFFFFFFFF; row[1] = 0xFFFFFFFF;
            }
            s_d3d8.whiteTex->UnlockRect(0);
        }
    }
}

static void onResizeD3D8(int newW, int newH) {
    if (!s_d3d8.device || newW <= 0 || newH <= 0) return;
    if (s_d3d8.vb) { s_d3d8.vb->Release(); s_d3d8.vb = nullptr; }
    s_d3d8.d3dpp.BackBufferWidth = newW;
    s_d3d8.d3dpp.BackBufferHeight = newH;
    if (FAILED(s_d3d8.device->Reset(&s_d3d8.d3dpp))) return;
    createD3D8Buffers();
    applyD3D8States();
}

bool d3d8_init(SDL_Window* window, int windowW, int windowH, bool vsync) {
    s_d3d8.hModule = LoadLibraryA("d3d8.dll");
    if (!s_d3d8.hModule) return false;

    auto pCreate = (Direct3DCreate8_Fn)GetProcAddress(s_d3d8.hModule, "Direct3DCreate8");
    if (!pCreate) { FreeLibrary(s_d3d8.hModule); s_d3d8.hModule = nullptr; return false; }

    s_d3d8.d3d = pCreate(D3D_SDK_VERSION);
    if (!s_d3d8.d3d) { FreeLibrary(s_d3d8.hModule); s_d3d8.hModule = nullptr; return false; }

    SDL_SysWMinfo wmInfo;
    SDL_VERSION(&wmInfo.version);
    if (!SDL_GetWindowWMInfo(window, &wmInfo)) return false;
    HWND hWnd = wmInfo.info.win.window;

    D3DDISPLAYMODE d3ddm;
    if (FAILED(s_d3d8.d3d->GetAdapterDisplayMode(D3DADAPTER_DEFAULT, &d3ddm))) return false;

    ZeroMemory(&s_d3d8.d3dpp, sizeof(s_d3d8.d3dpp));
    s_d3d8.vsync = vsync;
    s_d3d8.d3dpp.Windowed = TRUE;
    s_d3d8.d3dpp.SwapEffect = vsync ? D3DSWAPEFFECT_COPY_VSYNC : D3DSWAPEFFECT_DISCARD;
    s_d3d8.d3dpp.BackBufferFormat = d3ddm.Format;
    s_d3d8.d3dpp.BackBufferWidth = windowW;
    s_d3d8.d3dpp.BackBufferHeight = windowH;
    s_d3d8.d3dpp.BackBufferCount = 1;
    s_d3d8.d3dpp.EnableAutoDepthStencil = FALSE;
    s_d3d8.d3dpp.hDeviceWindow = hWnd;
    s_d3d8.d3dpp.Flags = 0;
    s_d3d8.d3dpp.FullScreen_RefreshRateInHz = 0;
    s_d3d8.d3dpp.FullScreen_PresentationInterval = 0;

    // Try Hardware Vertex Processing first (GMA 4500 / HD Graphics) and fall back to Software (GMA 950 / 3100 / 3150)
    DWORD behavior = D3DCREATE_HARDWARE_VERTEXPROCESSING;
    HRESULT hr = s_d3d8.d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hWnd, behavior, &s_d3d8.d3dpp, &s_d3d8.device);
    if (FAILED(hr)) {
        behavior = D3DCREATE_SOFTWARE_VERTEXPROCESSING;
        hr = s_d3d8.d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hWnd, behavior, &s_d3d8.d3dpp, &s_d3d8.device);
        if (FAILED(hr)) {
            hr = s_d3d8.d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_REF, hWnd, behavior, &s_d3d8.d3dpp, &s_d3d8.device);
            if (FAILED(hr)) return false;
        }
    }

    applyD3D8States();
    createD3D8WhiteTex();
    createD3D8Buffers();
    std::cout << "[RenderDevice] Direct3D 8 initialized successfully (Intel GMA Circular Ring FastPath 32-bit).\n";
    return true;
}

void d3d8_shutdown() {
    if (s_d3d8.whiteTex) { s_d3d8.whiteTex->Release(); s_d3d8.whiteTex = nullptr; }
    if (s_d3d8.vb) { s_d3d8.vb->Release(); s_d3d8.vb = nullptr; }
    if (s_d3d8.ib) { s_d3d8.ib->Release(); s_d3d8.ib = nullptr; }
    for (auto& pair : s_d3d8.textures) { if (pair.second) pair.second->Release(); }
    s_d3d8.textures.clear();
    s_d3d8.lastBoundTex = nullptr;
    if (s_d3d8.device) { s_d3d8.device->Release(); s_d3d8.device = nullptr; }
    if (s_d3d8.d3d) { s_d3d8.d3d->Release(); s_d3d8.d3d = nullptr; }
    if (s_d3d8.hModule) { FreeLibrary(s_d3d8.hModule); s_d3d8.hModule = nullptr; }
}

void d3d8_setViewport(int vpX, int vpY, int vpW, int vpH) {
    if (!s_d3d8.device || vpW <= 0 || vpH <= 0) return;
    if (vpW != (int)s_d3d8.d3dpp.BackBufferWidth || vpH != (int)s_d3d8.d3dpp.BackBufferHeight) {
        onResizeD3D8(vpW, vpH);
    }
    D3DVIEWPORT8 vp;
    vp.X = vpX; vp.Y = vpY; vp.Width = vpW; vp.Height = vpH; vp.MinZ = 0.0f; vp.MaxZ = 1.0f;
    s_d3d8.device->SetViewport(&vp);
}

void d3d8_setVSync(bool enabled) {
    s_d3d8.vsync = enabled;
    if (!s_d3d8.device) return;
    D3DSWAPEFFECT target = enabled ? D3DSWAPEFFECT_COPY_VSYNC : D3DSWAPEFFECT_DISCARD;
    if (s_d3d8.d3dpp.SwapEffect != target) {
        s_d3d8.d3dpp.SwapEffect = target;
        onResizeD3D8(s_d3d8.d3dpp.BackBufferWidth, s_d3d8.d3dpp.BackBufferHeight);
    }
}

void d3d8_beginFrame() { s_d3d8.inScene = false; }

void d3d8_clear(float r, float g, float b, float a) {
    if (!s_d3d8.device) return;
    s_d3d8.device->Clear(0, NULL, D3DCLEAR_TARGET, toD3D8Color(r, g, b, a), 1.0f, 0);
    s_d3d8.device->BeginScene();
    s_d3d8.inScene = true;
}

void d3d8_endFrame() {
    if (!s_d3d8.device) return;
    if (s_d3d8.inScene) { s_d3d8.device->EndScene(); s_d3d8.inScene = false; }
    s_d3d8.device->Present(NULL, NULL, NULL, NULL);
}

void d3d8_setBlendMode(BlendMode mode) {
    if (!s_d3d8.device || s_d3d8.lastBlend == mode) return;
    s_d3d8.device->SetRenderState(D3DRS_DESTBLEND, (mode == BLEND_ADD) ? D3DBLEND_ONE : D3DBLEND_INVSRCALPHA);
    s_d3d8.lastBlend = mode;
}

void d3d8_flushBatch(const D3DVertex* buffer, size_t count, uint32_t currentTexID, BlendMode currentBlend) {
    if (!s_d3d8.device || !s_d3d8.vb || !s_d3d8.ib || count == 0) return;

    if (!s_d3d8.inScene) {
        s_d3d8.device->BeginScene();
        s_d3d8.inScene = true;
    }

    IDirect3DTexture8* tex = s_d3d8.whiteTex;
    if (currentTexID != 0) {
        auto it = s_d3d8.textures.find(currentTexID);
        if (it != s_d3d8.textures.end() && it->second) tex = it->second;
    }

    if (s_d3d8.lastBoundTex != tex) {
        s_d3d8.device->SetTexture(0, tex);
        s_d3d8.lastBoundTex = tex;
    }

    d3d8_setBlendMode(currentBlend);

    // Stall-free ring buffer: continuous D3DLOCK_NOOVERWRITE, DISCARD when restarting cycle
    DWORD lockFlags = D3DLOCK_NOOVERWRITE;
    if (s_d3d8.vbOffset + count > D3D8_RING_VERTS) {
        s_d3d8.vbOffset = 0;
        lockFlags = D3DLOCK_DISCARD;
    }

    D3DVertex* pLock = nullptr;
    if (SUCCEEDED(s_d3d8.vb->Lock(s_d3d8.vbOffset * sizeof(D3DVertex), count * sizeof(D3DVertex), (BYTE**)&pLock, lockFlags))) {
        // Direct ultra-fast SIMD copy: Coordinates and colors are already prepared from batchQuad
        memcpy(pLock, buffer, count * sizeof(D3DVertex));
        s_d3d8.vb->Unlock();

        s_d3d8.device->SetVertexShader(D3DFVF_D3D8_2D);
        s_d3d8.device->SetStreamSource(0, s_d3d8.vb, sizeof(D3DVertex));
        s_d3d8.device->SetIndices(s_d3d8.ib, s_d3d8.vbOffset);
        UINT numPrimitives = (UINT)(count / 4) * 2;
        s_d3d8.device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, count, 0, numPrimitives);
        s_d3d8.vbOffset += count;
    }
}

void d3d8_drawCircle(float cx, float cy, float radius, float r, float g, float b, float a, bool filled, BlendMode blend, float transX, float transY, float scaleX, float scaleY) {
    if (!s_d3d8.device) return;
    d3d8_setBlendMode(blend);
    if (s_d3d8.lastBoundTex != s_d3d8.whiteTex) {
        s_d3d8.device->SetTexture(0, s_d3d8.whiteTex);
        s_d3d8.lastBoundTex = s_d3d8.whiteTex;
    }
    s_d3d8.device->SetVertexShader(D3DFVF_D3D8_2D);

    const int segments = 16;
    DWORD color = toD3D8Color(r, g, b, a);

    if (filled) {
        D3DVertex v[18];
        v[0] = { (cx + transX) * scaleX, (cy + transY) * scaleY, 0.5f, 1.0f, color, 0.5f, 0.5f };
        for (int i = 0; i <= segments; ++i) {
            float ang = (i / (float)segments) * 6.2831853f;
            v[i + 1] = { (cx + transX + std::cos(ang) * radius) * scaleX,
                         (cy + transY + std::sin(ang) * radius) * scaleY,
                         0.5f, 1.0f, color, 0.5f, 0.5f };
        }
        s_d3d8.device->DrawPrimitiveUP(D3DPT_TRIANGLEFAN, segments, v, sizeof(D3DVertex));
    } else {
        D3DVertex v[17];
        for (int i = 0; i <= segments; ++i) {
            float ang = (i / (float)segments) * 6.2831853f;
            v[i] = { (cx + transX + std::cos(ang) * radius) * scaleX,
                     (cy + transY + std::sin(ang) * radius) * scaleY,
                     0.5f, 1.0f, color, 0.5f, 0.5f };
        }
        s_d3d8.device->DrawPrimitiveUP(D3DPT_LINESTRIP, segments, v, sizeof(D3DVertex));
    }
}

void d3d8_drawTriangleStrip(const float* coordsXY, const float* colorsRGBA, size_t vertCount, BlendMode blend, float transX, float transY, float scaleX, float scaleY) {
    if (!s_d3d8.device || vertCount < 3) return;
    d3d8_setBlendMode(blend);
    if (s_d3d8.lastBoundTex != s_d3d8.whiteTex) {
        s_d3d8.device->SetTexture(0, s_d3d8.whiteTex);
        s_d3d8.lastBoundTex = s_d3d8.whiteTex;
    }
    s_d3d8.device->SetVertexShader(D3DFVF_D3D8_2D);

    DWORD lockFlags = D3DLOCK_NOOVERWRITE;
    if (s_d3d8.vbOffset + vertCount > D3D8_RING_VERTS) {
        s_d3d8.vbOffset = 0;
        lockFlags = D3DLOCK_DISCARD;
    }

    D3DVertex* pLock = nullptr;
    if (SUCCEEDED(s_d3d8.vb->Lock(s_d3d8.vbOffset * sizeof(D3DVertex), vertCount * sizeof(D3DVertex), (BYTE**)&pLock, lockFlags))) {
        for (size_t i = 0; i < vertCount; ++i) {
            pLock[i].x = (coordsXY[i * 2 + 0] + transX) * scaleX;
            pLock[i].y = (coordsXY[i * 2 + 1] + transY) * scaleY;
            pLock[i].z = 0.5f;
            pLock[i].rhw = 1.0f;
            pLock[i].color = toD3D8Color(colorsRGBA[i * 4 + 0], colorsRGBA[i * 4 + 1], colorsRGBA[i * 4 + 2], colorsRGBA[i * 4 + 3]);
            pLock[i].u = 0.5f;
            pLock[i].v = 0.5f;
        }
        s_d3d8.vb->Unlock();

        s_d3d8.device->SetStreamSource(0, s_d3d8.vb, sizeof(D3DVertex));
        s_d3d8.device->DrawPrimitive(D3DPT_TRIANGLESTRIP, s_d3d8.vbOffset, (UINT)vertCount - 2);
        s_d3d8.vbOffset += vertCount;
    }
}

void d3d8_drawRepeatedBackground(uint32_t texID, float uvOffsetX, float uvOffsetY, float uvW, float uvH, float logicalW, float logicalH, float bgR, float bgG, float bgB, float scaleX, float scaleY) {
    if (!s_d3d8.device) return;

    if (!s_d3d8.inScene) {
        s_d3d8.device->BeginScene();
        s_d3d8.inScene = true;
    }

    IDirect3DTexture8* tex = s_d3d8.whiteTex;
    auto it = s_d3d8.textures.find(texID);
    if (it != s_d3d8.textures.end() && it->second) tex = it->second;

    s_d3d8.device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    s_d3d8.device->SetTexture(0, tex);
    s_d3d8.lastBoundTex = tex;
    s_d3d8.device->SetTextureStageState(0, D3DTSS_ADDRESSU, D3DTADDRESS_WRAP);
    s_d3d8.device->SetTextureStageState(0, D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);
    s_d3d8.device->SetVertexShader(D3DFVF_D3D8_2D);

    DWORD color = toD3D8Color(bgR, bgG, bgB, 1.0f);

    DWORD lockFlags = D3DLOCK_NOOVERWRITE;
    if (s_d3d8.vbOffset + 4 > D3D8_RING_VERTS) {
        s_d3d8.vbOffset = 0;
        lockFlags = D3DLOCK_DISCARD;
    }

    D3DVertex* pLock = nullptr;
    if (SUCCEEDED(s_d3d8.vb->Lock(s_d3d8.vbOffset * sizeof(D3DVertex), 4 * sizeof(D3DVertex), (BYTE**)&pLock, lockFlags))) {
        pLock[0] = { 0.0f,              0.0f,              0.5f, 1.0f, color, uvOffsetX,       uvOffsetY };
        pLock[1] = { logicalW * scaleX, 0.0f,              0.5f, 1.0f, color, uvOffsetX + uvW, uvOffsetY };
        pLock[2] = { 0.0f,              logicalH * scaleY, 0.5f, 1.0f, color, uvOffsetX,       uvOffsetY + uvH };
        pLock[3] = { logicalW * scaleX, logicalH * scaleY, 0.5f, 1.0f, color, uvOffsetX + uvW, uvOffsetY + uvH };
        s_d3d8.vb->Unlock();

        s_d3d8.device->SetStreamSource(0, s_d3d8.vb, sizeof(D3DVertex));
        s_d3d8.device->DrawPrimitive(D3DPT_TRIANGLESTRIP, s_d3d8.vbOffset, 2);
        s_d3d8.vbOffset += 4;
    }

    s_d3d8.device->SetTextureStageState(0, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP);
    s_d3d8.device->SetTextureStageState(0, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);
    s_d3d8.device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
}

uint32_t d3d8_registerTexture(uint32_t handle, int width, int height, const void* rgbaPixels) {
    if (!s_d3d8.device) return handle;
    IDirect3DTexture8* tex = nullptr;
    if (SUCCEEDED(s_d3d8.device->CreateTexture(width, height, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex))) {
        D3DLOCKED_RECT lr;
        if (SUCCEEDED(tex->LockRect(0, &lr, NULL, 0))) {
            const auto* src = (const uint8_t*)rgbaPixels;
            auto* dst = (uint8_t*)lr.pBits;
            for (int y = 0; y < height; ++y) {
                const auto* srcRow = (const uint32_t*)(src + y * width * 4);
                auto* dstRow = (uint32_t*)(dst + y * lr.Pitch);
                for (int x = 0; x < width; ++x) {
                    uint32_t c = srcRow[x];
                    uint32_t r = (c) & 0xFF;
                    uint32_t g = (c >> 8) & 0xFF;
                    uint32_t b = (c >> 16) & 0xFF;
                    uint32_t a = (c >> 24) & 0xFF;
                    dstRow[x] = (a << 24) | (r << 16) | (g << 8) | b;
                }
            }
            tex->UnlockRect(0);
            s_d3d8.textures[handle] = tex;
        }
    }
    return handle;
}

#else
// Stub implementations for 64-bit builds or Linux
bool d3d8_init(SDL_Window*, int, int, bool) { return false; }
void d3d8_shutdown() {}
void d3d8_setViewport(int, int, int, int) {}
void d3d8_setVSync(bool) {}
void d3d8_beginFrame() {}
void d3d8_clear(float, float, float, float) {}
void d3d8_endFrame() {}
void d3d8_setBlendMode(BlendMode) {}
void d3d8_flushBatch(const D3DVertex*, size_t, uint32_t, BlendMode) {}
void d3d8_drawCircle(float, float, float, float, float, float, float, bool, BlendMode, float, float, float, float) {}
void d3d8_drawTriangleStrip(const float*, const float*, size_t, BlendMode, float, float, float, float) {}
void d3d8_drawRepeatedBackground(uint32_t, float, float, float, float, float, float, float, float, float, float, float) {}
uint32_t d3d8_registerTexture(uint32_t handle, int, int, const void*) { return handle; }
#endif
