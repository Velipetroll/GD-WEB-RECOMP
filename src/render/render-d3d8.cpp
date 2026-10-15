#include "render-device.h"

#if defined(_WIN32) && !defined(_WIN64)
#include <d3d8.h>
#include <SDL2/SDL_syswm.h>
#include <iostream>
#include <algorithm>
#include <cstring>
#include <cmath>

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
    size_t vbOffset = 0;
    D3DPRESENT_PARAMETERS d3dpp;
    std::unordered_map<uint32_t, IDirect3DTexture8*> textures;
    bool vsync = false;
    bool inScene = false;
    bool bound = false;      // FVF + stream 0 already bound to the ring VB
    bool canA8L8 = false;    // lossless 16-bit white/gray + alpha textures
    bool canL8 = false;      // lossless 8-bit opaque gray textures
    bool canA4R4G4B4 = false;// 16-bit colour textures (quality preset LOW)
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

    // Premultiplied alpha: one fixed blend equation for normal + additive (no per-batch state changes)
    s_d3d8.device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    s_d3d8.device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
    s_d3d8.device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    s_d3d8.device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);   // additive vertices carry alpha 0

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
    s_d3d8.device->SetTextureStageState(0, D3DTSS_MIPFILTER, D3DTEXF_NONE);
    s_d3d8.device->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    s_d3d8.device->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
    s_d3d8.lastBoundTex = nullptr;
    s_d3d8.bound = false;
}

static inline void bindD3D8Stream() {
    if (s_d3d8.bound) return;
    s_d3d8.device->SetVertexShader(D3DFVF_D3D8_2D);
    s_d3d8.device->SetStreamSource(0, s_d3d8.vb, sizeof(D3DVertex));
    s_d3d8.bound = true;
}

static inline void ensureD3D8Scene() {
    if (!s_d3d8.inScene) { s_d3d8.device->BeginScene(); s_d3d8.inScene = true; }
}

static void createD3D8Buffers() {
    if (!s_d3d8.device) return;
    s_d3d8.vbOffset = 0;
    s_d3d8.bound = false;
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
    s_d3d8.bound = false;
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

    // ---- GPU detection (Intel GMA tiers, NPOT support) ----
    GpuCaps& caps = gpu::caps();
    D3DADAPTER_IDENTIFIER8 ident;
    if (SUCCEEDED(s_d3d8.d3d->GetAdapterIdentifier(D3DADAPTER_DEFAULT, D3DENUM_NO_WHQL_LEVEL, &ident))) {
        caps.name = ident.Description;
        int t = gpu::tierFromPciId(ident.VendorId, ident.DeviceId, &caps.isIntelGMA);
        caps.tier = (t >= 0) ? t : gpu::tierFromName(gpu::lower(ident.Description), &caps.isIntelGMA);
    }
    D3DCAPS8 dcaps;
    if (SUCCEEDED(s_d3d8.d3d->GetDeviceCaps(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, &dcaps))) {
        bool pow2 = (dcaps.TextureCaps & D3DPTEXTURECAPS_POW2) != 0;
        bool cond = (dcaps.TextureCaps & D3DPTEXTURECAPS_NONPOW2CONDITIONAL) != 0;
        caps.npot = !pow2 || cond;
    }
    caps.detected = true;

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
    // (GMA 900/950/3100/3150 are detected up-front and go straight to software VP)
    DWORD order[2] = { D3DCREATE_HARDWARE_VERTEXPROCESSING, D3DCREATE_SOFTWARE_VERTEXPROCESSING };
    if (caps.isIntelGMA && caps.tier == 0) std::swap(order[0], order[1]);
    DWORD behavior = order[0];
    HRESULT hr = s_d3d8.d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hWnd, behavior, &s_d3d8.d3dpp, &s_d3d8.device);
    if (FAILED(hr)) {
        behavior = order[1];
        hr = s_d3d8.d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hWnd, behavior, &s_d3d8.d3dpp, &s_d3d8.device);
        if (FAILED(hr)) {
            hr = s_d3d8.d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_REF, hWnd, behavior, &s_d3d8.d3dpp, &s_d3d8.device);
            if (FAILED(hr)) return false;
        }
    }

    s_d3d8.canA8L8 = SUCCEEDED(s_d3d8.d3d->CheckDeviceFormat(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, s_d3d8.d3dpp.BackBufferFormat,
                                                             0, D3DRTYPE_TEXTURE, D3DFMT_A8L8));
    s_d3d8.canL8   = SUCCEEDED(s_d3d8.d3d->CheckDeviceFormat(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, s_d3d8.d3dpp.BackBufferFormat,
                                                             0, D3DRTYPE_TEXTURE, D3DFMT_L8));
    s_d3d8.canA4R4G4B4 = SUCCEEDED(s_d3d8.d3d->CheckDeviceFormat(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, s_d3d8.d3dpp.BackBufferFormat,
                                                                 0, D3DRTYPE_TEXTURE, D3DFMT_A4R4G4B4));

    applyD3D8States();
    createD3D8WhiteTex();
    createD3D8Buffers();
    std::cout << "[RenderDevice] Direct3D 8 initialized (" << caps.name << " | tier " << caps.tier
              << " | NPOT " << (caps.npot ? "yes" : "no") << " | quality " << gpu::presetName(gpu::resolvedPreset()) << ").\n";
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
    ensureD3D8Scene();
}

void d3d8_endFrame() {
    if (!s_d3d8.device) return;
    if (s_d3d8.inScene) { s_d3d8.device->EndScene(); s_d3d8.inScene = false; }
    s_d3d8.device->Present(NULL, NULL, NULL, NULL);
}

void d3d8_flushBatch(const D3DVertex* buffer, size_t count, uint32_t currentTexID) {
    if (!s_d3d8.device || !s_d3d8.vb || !s_d3d8.ib || count == 0) return;
    ensureD3D8Scene();

    IDirect3DTexture8* tex = s_d3d8.whiteTex;
    if (currentTexID != 0) {
        auto it = s_d3d8.textures.find(currentTexID);
        if (it != s_d3d8.textures.end() && it->second) tex = it->second;
    }
    if (s_d3d8.lastBoundTex != tex) {
        s_d3d8.device->SetTexture(0, tex);
        s_d3d8.lastBoundTex = tex;
    }

    // Stall-free ring buffer: continuous D3DLOCK_NOOVERWRITE, DISCARD when restarting cycle
    DWORD lockFlags = D3DLOCK_NOOVERWRITE;
    if (s_d3d8.vbOffset + count > D3D8_RING_VERTS) {
        s_d3d8.vbOffset = 0;
        lockFlags = D3DLOCK_DISCARD;
    }

    D3DVertex* pLock = nullptr;
    if (SUCCEEDED(s_d3d8.vb->Lock(s_d3d8.vbOffset * sizeof(D3DVertex), count * sizeof(D3DVertex), (BYTE**)&pLock, lockFlags))) {
        // Coordinates and premultiplied colours are already prepared by batchQuad
        memcpy(pLock, buffer, count * sizeof(D3DVertex));
        s_d3d8.vb->Unlock();

        bindD3D8Stream();
        s_d3d8.device->SetIndices(s_d3d8.ib, s_d3d8.vbOffset);   // base vertex index moves with the ring offset
        UINT numPrimitives = (UINT)(count / 4) * 2;
        s_d3d8.device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, count, 0, numPrimitives);
        s_d3d8.vbOffset += count;
    }
}

static inline void bindWhite() {
    if (s_d3d8.lastBoundTex != s_d3d8.whiteTex) {
        s_d3d8.device->SetTexture(0, s_d3d8.whiteTex);
        s_d3d8.lastBoundTex = s_d3d8.whiteTex;
    }
}

void d3d8_drawCircle(float cx, float cy, float radius, uint32_t color, bool filled, int segments, float transX, float transY, float scaleX, float scaleY) {
    if (!s_d3d8.device || !s_d3d8.vb) return;
    ensureD3D8Scene();
    bindWhite();
    bindD3D8Stream();

    size_t count = filled ? (segments + 2) : (segments + 1);
    DWORD lockFlags = D3DLOCK_NOOVERWRITE;
    if (s_d3d8.vbOffset + count > D3D8_RING_VERTS) {
        s_d3d8.vbOffset = 0;
        lockFlags = D3DLOCK_DISCARD;
    }

    D3DVertex* pLock = nullptr;
    if (SUCCEEDED(s_d3d8.vb->Lock(s_d3d8.vbOffset * sizeof(D3DVertex), count * sizeof(D3DVertex), (BYTE**)&pLock, lockFlags))) {
        size_t o = 0;
        if (filled) pLock[o++] = { (cx + transX) * scaleX, (cy + transY) * scaleY, 0.5f, 1.0f, color, 0.5f, 0.5f };
        for (int i = 0; i <= segments; ++i) {
            float ang = (i / (float)segments) * 6.2831853f;
            pLock[o++] = { (cx + transX + std::cos(ang) * radius) * scaleX,
                           (cy + transY + std::sin(ang) * radius) * scaleY,
                           0.5f, 1.0f, color, 0.5f, 0.5f };
        }
        s_d3d8.vb->Unlock();
        s_d3d8.device->DrawPrimitive(filled ? D3DPT_TRIANGLEFAN : D3DPT_LINESTRIP, s_d3d8.vbOffset, segments);
        s_d3d8.vbOffset += count;
    }
}

void d3d8_drawTriangleStrip(const float* coordsXY, const float* colorsRGBA, size_t vertCount, bool additive, float transX, float transY, float scaleX, float scaleY) {
    if (!s_d3d8.device || !s_d3d8.vb || vertCount < 3) return;
    ensureD3D8Scene();
    bindWhite();
    bindD3D8Stream();

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
            pLock[i].color = packColorPMA(colorsRGBA[i * 4 + 0], colorsRGBA[i * 4 + 1], colorsRGBA[i * 4 + 2], colorsRGBA[i * 4 + 3], additive, false);
            pLock[i].u = 0.5f;
            pLock[i].v = 0.5f;
        }
        s_d3d8.vb->Unlock();
        s_d3d8.device->DrawPrimitive(D3DPT_TRIANGLESTRIP, s_d3d8.vbOffset, (UINT)vertCount - 2);
        s_d3d8.vbOffset += vertCount;
    }
}

void d3d8_drawRepeatedBackground(uint32_t texID, float uvOffsetX, float uvOffsetY, float uvW, float uvH, float logicalW, float logicalH, float bgR, float bgG, float bgB, float scaleX, float scaleY) {
    if (!s_d3d8.device || !s_d3d8.vb) return;
    ensureD3D8Scene();

    IDirect3DTexture8* tex = s_d3d8.whiteTex;
    auto it = s_d3d8.textures.find(texID);
    if (it != s_d3d8.textures.end() && it->second) tex = it->second;

    s_d3d8.device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);   // opaque full-screen pass: no blending
    if (s_d3d8.lastBoundTex != tex) { s_d3d8.device->SetTexture(0, tex); s_d3d8.lastBoundTex = tex; }
    s_d3d8.device->SetTextureStageState(0, D3DTSS_ADDRESSU, D3DTADDRESS_WRAP);
    s_d3d8.device->SetTextureStageState(0, D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);
    bindD3D8Stream();

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
        s_d3d8.device->DrawPrimitive(D3DPT_TRIANGLESTRIP, s_d3d8.vbOffset, 2);
        s_d3d8.vbOffset += 4;
    }

    s_d3d8.device->SetTextureStageState(0, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP);
    s_d3d8.device->SetTextureStageState(0, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);
    s_d3d8.device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
}

uint32_t d3d8_registerTexture(uint32_t handle, const PreparedTexture& t) {
    if (!s_d3d8.device) return handle;

    // d3dMode: 0 = A8R8G8B8, 1 = A8L8, 2 = L8, 3 = A4R4G4B4
    int mode = 0;
    D3DFORMAT fmt = D3DFMT_A8R8G8B8;
    if (t.format == PF_L8 && s_d3d8.canL8) { mode = 2; fmt = D3DFMT_L8; }
    else if ((t.format == PF_LA8 || t.format == PF_I8) && s_d3d8.canA8L8) { mode = 1; fmt = D3DFMT_A8L8; }
    else if (t.format == PF_RGBA4444 && s_d3d8.canA4R4G4B4) { mode = 3; fmt = D3DFMT_A4R4G4B4; }

    IDirect3DTexture8* tex = nullptr;
    if (FAILED(s_d3d8.device->CreateTexture(t.width, t.height, 1, 0, fmt, D3DPOOL_MANAGED, &tex))) {
        tex = nullptr; mode = 0;
        if (FAILED(s_d3d8.device->CreateTexture(t.width, t.height, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex))) tex = nullptr;
    }
    if (tex) {
        D3DLOCKED_RECT lr;
        if (SUCCEEDED(tex->LockRect(0, &lr, NULL, 0))) {
            writeD3DTexels(t, mode, lr.pBits, lr.Pitch);
            tex->UnlockRect(0);
            s_d3d8.textures[handle] = tex;
        } else {
            tex->Release();
        }
    }
    return handle;
}

void d3d8_reloadTexture(uint32_t handle, const PreparedTexture& t) {
    if (!s_d3d8.device) return;
    auto it = s_d3d8.textures.find(handle);
    if (it != s_d3d8.textures.end() && it->second) {
        if (s_d3d8.lastBoundTex == it->second) {
            s_d3d8.lastBoundTex = nullptr;
            s_d3d8.device->SetTexture(0, s_d3d8.whiteTex);
        }
        it->second->Release();
        s_d3d8.textures.erase(it);
    }
    d3d8_registerTexture(handle, t);
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
void d3d8_flushBatch(const D3DVertex*, size_t, uint32_t) {}
void d3d8_drawCircle(float, float, float, uint32_t, bool, int, float, float, float, float) {}
void d3d8_drawTriangleStrip(const float*, const float*, size_t, bool, float, float, float, float) {}
void d3d8_drawRepeatedBackground(uint32_t, float, float, float, float, float, float, float, float, float, float, float) {}
uint32_t d3d8_registerTexture(uint32_t handle, const PreparedTexture&) { return handle; }
void d3d8_reloadTexture(uint32_t, const PreparedTexture&) {}
#endif
