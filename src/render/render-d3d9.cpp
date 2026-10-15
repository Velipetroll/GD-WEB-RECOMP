#include "render-d3d9.h"
#include "render-device.h"
#include "gpu-profile.h"

#ifdef _WIN32
#include <d3d9.h>
#include <SDL2/SDL_syswm.h>
#include <iostream>
#include <algorithm>
#include <cstring>
#include <cmath>

#define D3DFVF_D3D9_2D (D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1)
typedef IDirect3D9* (WINAPI *Direct3DCreate9_Fn)(UINT SDKVersion);

static constexpr size_t D3D9_RING_VERTS = 32768; // 8192 quads in dynamic ring buffer
static constexpr size_t D3D9_MAX_QUADS = 4096;

struct D3D9State {
    void* hModule = nullptr;
    IDirect3D9* d3d = nullptr;
    IDirect3DDevice9* device = nullptr;
    IDirect3DVertexBuffer9* vb = nullptr;
    IDirect3DIndexBuffer9* ib = nullptr;
    IDirect3DTexture9* whiteTex = nullptr;
    IDirect3DTexture9* lastBoundTex = nullptr;
    size_t vbOffset = 0;
    D3DPRESENT_PARAMETERS d3dpp;
    std::unordered_map<uint32_t, IDirect3DTexture9*> textures;
    bool vsync = false;
    bool inScene = false;
    bool bound = false;       // FVF + stream 0 already bound to the ring VB
    bool canA8L8 = false;     // lossless 16-bit white/gray + alpha textures
    bool canL8 = false;       // lossless 8-bit opaque gray textures
    bool canA4R4G4B4 = false; // 16-bit colour textures (quality preset LOW)
};

static D3D9State s_d3d9;

static inline DWORD toD3D9Color(float r, float g, float b, float a) {
    auto ca = (DWORD)(std::clamp(a, 0.0f, 1.0f) * 255.0f);
    auto cr = (DWORD)(std::clamp(r, 0.0f, 1.0f) * 255.0f);
    auto cg = (DWORD)(std::clamp(g, 0.0f, 1.0f) * 255.0f);
    auto cb = (DWORD)(std::clamp(b, 0.0f, 1.0f) * 255.0f);
    return (ca << 24) | (cr << 16) | (cg << 8) | cb;
}

static void applyD3D9States() {
    if (!s_d3d9.device) return;
    s_d3d9.device->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
    s_d3d9.device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    s_d3d9.device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    s_d3d9.device->SetRenderState(D3DRS_LIGHTING, FALSE);
    s_d3d9.device->SetRenderState(D3DRS_DITHERENABLE, FALSE);
    s_d3d9.device->SetRenderState(D3DRS_SPECULARENABLE, FALSE);
    s_d3d9.device->SetRenderState(D3DRS_FOGENABLE, FALSE);
    // INTEL GMA OPTIMIZATION: Disable CPU software clipping for pre-transformed vertices
    s_d3d9.device->SetRenderState(D3DRS_CLIPPING, FALSE);

    // Premultiplied alpha: one fixed blend equation for normal + additive (no per-batch state changes)
    s_d3d9.device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    s_d3d9.device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
    s_d3d9.device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    s_d3d9.device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE); // additive vertices carry alpha 0

    s_d3d9.device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    s_d3d9.device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    s_d3d9.device->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    s_d3d9.device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
    s_d3d9.device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    s_d3d9.device->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
    s_d3d9.device->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    s_d3d9.device->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);

    s_d3d9.device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    s_d3d9.device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    s_d3d9.device->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
    s_d3d9.device->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    s_d3d9.device->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    s_d3d9.lastBoundTex = nullptr;
}

static void createD3D9WhiteTexture() {
    if (!s_d3d9.device) return;
    IDirect3DTexture9* tex = nullptr;
    HRESULT hr = s_d3d9.device->CreateTexture(2, 2, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex, NULL);
    if (FAILED(hr)) hr = s_d3d9.device->CreateTexture(2, 2, 1, 0, D3DFMT_X8R8G8B8, D3DPOOL_MANAGED, &tex, NULL);
    if (tex) {
        D3DLOCKED_RECT lr;
        if (SUCCEEDED(tex->LockRect(0, &lr, NULL, 0))) {
            auto* p = (uint8_t*)lr.pBits;
            for (int y = 0; y < 2; ++y) {
                auto* row = (uint32_t*)(p + y * lr.Pitch);
                row[0] = 0xFFFFFFFF; row[1] = 0xFFFFFFFF;
            }
            tex->UnlockRect(0);
            s_d3d9.whiteTex = tex;
        }
    }
}

static void createD3D9BatchBuffers() {
    if (!s_d3d9.device) return;
    s_d3d9.vbOffset = 0;
    s_d3d9.bound = false;

    if (!s_d3d9.vb) {
        IDirect3DVertexBuffer9* vb = nullptr;
        s_d3d9.device->CreateVertexBuffer(D3D9_RING_VERTS * sizeof(D3DVertex), D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, D3DFVF_D3D9_2D, D3DPOOL_DEFAULT, &vb, NULL);
        s_d3d9.vb = vb;
    }
    if (!s_d3d9.ib) {
        IDirect3DIndexBuffer9* ib = nullptr;
        s_d3d9.device->CreateIndexBuffer(D3D9_MAX_QUADS * 6 * sizeof(uint16_t), D3DUSAGE_WRITEONLY, D3DFMT_INDEX16, D3DPOOL_MANAGED, &ib, NULL);
        if (ib) {
            uint16_t* idx = nullptr;
            if (SUCCEEDED(ib->Lock(0, 0, (void**)&idx, 0))) {
                for (uint16_t q = 0; q < D3D9_MAX_QUADS; ++q) {
                    uint16_t base = q * 4;
                    idx[q * 6 + 0] = base + 0; idx[q * 6 + 1] = base + 1; idx[q * 6 + 2] = base + 2;
                    idx[q * 6 + 3] = base + 0; idx[q * 6 + 4] = base + 2; idx[q * 6 + 5] = base + 3;
                }
                ib->Unlock();
                s_d3d9.ib = ib;
            }
        }
    }
}

static void bindD3D9Stream() {
    if (s_d3d9.bound || !s_d3d9.device) return;
    s_d3d9.device->SetFVF(D3DFVF_D3D9_2D);
    s_d3d9.device->SetStreamSource(0, s_d3d9.vb, 0, sizeof(D3DVertex));
    s_d3d9.device->SetIndices(s_d3d9.ib);
    s_d3d9.bound = true;
}

static void onResizeD3D9(int newW, int newH) {
    if (!s_d3d9.device || newW <= 0 || newH <= 0) return;
    if (s_d3d9.vb) { s_d3d9.vb->Release(); s_d3d9.vb = nullptr; }
    s_d3d9.bound = false;

    s_d3d9.d3dpp.BackBufferWidth = newW;
    s_d3d9.d3dpp.BackBufferHeight = newH;
    if (FAILED(s_d3d9.device->Reset(&s_d3d9.d3dpp))) return;

    createD3D9BatchBuffers();
    applyD3D9States();
}

bool d3d9_init(SDL_Window* window, int windowW, int windowH, bool vsync) {
    s_d3d9.vsync = vsync;
    s_d3d9.hModule = (void*)LoadLibraryA("d3d9.dll");
    if (!s_d3d9.hModule) return false;

    auto pCreate = (Direct3DCreate9_Fn)GetProcAddress((HMODULE)s_d3d9.hModule, "Direct3DCreate9");
    if (!pCreate) { FreeLibrary((HMODULE)s_d3d9.hModule); s_d3d9.hModule = nullptr; return false; }

    s_d3d9.d3d = pCreate(D3D_SDK_VERSION);
    if (!s_d3d9.d3d) { FreeLibrary((HMODULE)s_d3d9.hModule); s_d3d9.hModule = nullptr; return false; }

    SDL_SysWMinfo wmInfo;
    SDL_VERSION(&wmInfo.version);
    if (!SDL_GetWindowWMInfo(window, &wmInfo)) return false;
    HWND hWnd = wmInfo.info.win.window;

    D3DDISPLAYMODE d3ddm;
    if (FAILED(s_d3d9.d3d->GetAdapterDisplayMode(D3DADAPTER_DEFAULT, &d3ddm))) return false;

    GpuCaps& caps = gpu::caps();
    D3DADAPTER_IDENTIFIER9 ident;
    if (SUCCEEDED(s_d3d9.d3d->GetAdapterIdentifier(D3DADAPTER_DEFAULT, 0, &ident))) {
        caps.name = ident.Description;
        int t = gpu::tierFromPciId(ident.VendorId, ident.DeviceId, &caps.isIntelGMA);
        caps.tier = (t >= 0) ? t : gpu::tierFromName(gpu::lower(ident.Description), &caps.isIntelGMA);
    }
    D3DCAPS9 dcaps;
    if (SUCCEEDED(s_d3d9.d3d->GetDeviceCaps(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, &dcaps))) {
        bool pow2 = (dcaps.TextureCaps & D3DPTEXTURECAPS_POW2) != 0;
        bool cond = (dcaps.TextureCaps & D3DPTEXTURECAPS_NONPOW2CONDITIONAL) != 0;
        caps.npot = !pow2 || cond;
    }
    caps.detected = true;

    ZeroMemory(&s_d3d9.d3dpp, sizeof(D3DPRESENT_PARAMETERS));
    s_d3d9.d3dpp.Windowed = TRUE;
    s_d3d9.d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    s_d3d9.d3dpp.BackBufferFormat = d3ddm.Format;
    s_d3d9.d3dpp.BackBufferWidth = windowW;
    s_d3d9.d3dpp.BackBufferHeight = windowH;
    s_d3d9.d3dpp.BackBufferCount = 1;
    s_d3d9.d3dpp.EnableAutoDepthStencil = FALSE;
    s_d3d9.d3dpp.hDeviceWindow = hWnd;
    s_d3d9.d3dpp.Flags = 0;
    s_d3d9.d3dpp.PresentationInterval = vsync ? D3DPRESENT_INTERVAL_ONE : D3DPRESENT_INTERVAL_IMMEDIATE;

    DWORD order[2] = { D3DCREATE_HARDWARE_VERTEXPROCESSING, D3DCREATE_SOFTWARE_VERTEXPROCESSING };
    if (caps.isIntelGMA && caps.tier == 0) std::swap(order[0], order[1]);

    IDirect3DDevice9* dev = nullptr;
    HRESULT hr = s_d3d9.d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hWnd, order[0], &s_d3d9.d3dpp, &dev);
    if (FAILED(hr)) {
        hr = s_d3d9.d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hWnd, order[1], &s_d3d9.d3dpp, &dev);
        if (FAILED(hr)) return false;
    }
    s_d3d9.device = dev;

    s_d3d9.canA8L8 = SUCCEEDED(s_d3d9.d3d->CheckDeviceFormat(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, s_d3d9.d3dpp.BackBufferFormat,
                                                           0, D3DRTYPE_TEXTURE, D3DFMT_A8L8));
    s_d3d9.canL8   = SUCCEEDED(s_d3d9.d3d->CheckDeviceFormat(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, s_d3d9.d3dpp.BackBufferFormat,
                                                           0, D3DRTYPE_TEXTURE, D3DFMT_L8));
    s_d3d9.canA4R4G4B4 = SUCCEEDED(s_d3d9.d3d->CheckDeviceFormat(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, s_d3d9.d3dpp.BackBufferFormat,
                                                               0, D3DRTYPE_TEXTURE, D3DFMT_A4R4G4B4));

    applyD3D9States();
    createD3D9WhiteTexture();
    createD3D9BatchBuffers();

    std::cout << "[RenderDevice] Direct3D 9 initialized (" << caps.name << " | tier " << caps.tier
              << " | NPOT " << (caps.npot ? "yes" : "no") << " | quality " << gpu::presetName(gpu::resolvedPreset()) << ").\n";
    return true;
}

void d3d9_shutdown() {
    if (s_d3d9.whiteTex) { s_d3d9.whiteTex->Release(); s_d3d9.whiteTex = nullptr; }
    if (s_d3d9.vb) { s_d3d9.vb->Release(); s_d3d9.vb = nullptr; }
    if (s_d3d9.ib) { s_d3d9.ib->Release(); s_d3d9.ib = nullptr; }
    for (auto& pair : s_d3d9.textures) {
        if (pair.second) pair.second->Release();
    }
    s_d3d9.textures.clear();
    if (s_d3d9.device) { s_d3d9.device->Release(); s_d3d9.device = nullptr; }
    if (s_d3d9.d3d) { s_d3d9.d3d->Release(); s_d3d9.d3d = nullptr; }
    if (s_d3d9.hModule) { FreeLibrary((HMODULE)s_d3d9.hModule); s_d3d9.hModule = nullptr; }
}

void d3d9_setViewport(int vpX, int vpY, int vpW, int vpH) {
    if (!s_d3d9.device || vpW <= 0 || vpH <= 0) return;
    if (vpW != (int)s_d3d9.d3dpp.BackBufferWidth || vpH != (int)s_d3d9.d3dpp.BackBufferHeight) {
        onResizeD3D9(vpW, vpH);
    }
    D3DVIEWPORT9 vp;
    vp.X = vpX; vp.Y = vpY; vp.Width = vpW; vp.Height = vpH; vp.MinZ = 0.0f; vp.MaxZ = 1.0f;
    s_d3d9.device->SetViewport(&vp);
}

void d3d9_setVSync(bool enabled) {
    s_d3d9.vsync = enabled;
    if (!s_d3d9.device) return;
    DWORD target = enabled ? D3DPRESENT_INTERVAL_ONE : D3DPRESENT_INTERVAL_IMMEDIATE;
    if (s_d3d9.d3dpp.PresentationInterval != target) {
        s_d3d9.d3dpp.PresentationInterval = target;
        onResizeD3D9(s_d3d9.d3dpp.BackBufferWidth, s_d3d9.d3dpp.BackBufferHeight);
    }
}

void d3d9_beginFrame() {
    s_d3d9.inScene = false;
}

void d3d9_clear(float r, float g, float b, float a) {
    if (!s_d3d9.device) return;
    s_d3d9.device->Clear(0, NULL, D3DCLEAR_TARGET, toD3D9Color(r, g, b, a), 1.0f, 0);
    if (!s_d3d9.inScene) {
        s_d3d9.device->BeginScene();
        s_d3d9.inScene = true;
    }
}

void d3d9_endFrame() {
    if (!s_d3d9.device) return;
    if (s_d3d9.inScene) {
        s_d3d9.device->EndScene();
        s_d3d9.inScene = false;
    }
    s_d3d9.device->Present(NULL, NULL, NULL, NULL);
}

void d3d9_flushBatch(const D3DVertex* buffer, size_t count, uint32_t currentTexID) {
    if (!s_d3d9.device || !s_d3d9.vb || !s_d3d9.ib || count == 0) return;

    if (!s_d3d9.inScene) {
        s_d3d9.device->BeginScene();
        s_d3d9.inScene = true;
    }

    IDirect3DTexture9* tex = s_d3d9.whiteTex;
    if (currentTexID != 0) {
        auto it = s_d3d9.textures.find(currentTexID);
        if (it != s_d3d9.textures.end() && it->second) tex = it->second;
    }

    if (s_d3d9.lastBoundTex != tex) {
        s_d3d9.device->SetTexture(0, tex);
        s_d3d9.lastBoundTex = tex;
    }

    DWORD lockFlags = D3DLOCK_NOOVERWRITE;
    if (s_d3d9.vbOffset + count > D3D9_RING_VERTS) {
        s_d3d9.vbOffset = 0;
        lockFlags = D3DLOCK_DISCARD;
    }

    D3DVertex* pLock = nullptr;
    if (SUCCEEDED(s_d3d9.vb->Lock(s_d3d9.vbOffset * sizeof(D3DVertex), count * sizeof(D3DVertex), (void**)&pLock, lockFlags))) {
        memcpy(pLock, buffer, count * sizeof(D3DVertex));
        s_d3d9.vb->Unlock();

        bindD3D9Stream();
        UINT numPrimitives = (UINT)(count / 4) * 2;
        s_d3d9.device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, (INT)s_d3d9.vbOffset, 0, count, 0, numPrimitives);
        s_d3d9.vbOffset += count;
    }
}

void d3d9_drawCircle(float cx, float cy, float radius, uint32_t color, bool filled, int segments, float transX, float transY, float scaleX, float scaleY) {
    if (!s_d3d9.device || !s_d3d9.vb) return;

    if (!s_d3d9.inScene) {
        s_d3d9.device->BeginScene();
        s_d3d9.inScene = true;
    }
    if (s_d3d9.lastBoundTex != s_d3d9.whiteTex) {
        s_d3d9.device->SetTexture(0, s_d3d9.whiteTex);
        s_d3d9.lastBoundTex = s_d3d9.whiteTex;
    }
    bindD3D9Stream();

    size_t count = filled ? (segments + 2) : (segments + 1);
    DWORD lockFlags = D3DLOCK_NOOVERWRITE;
    if (s_d3d9.vbOffset + count > D3D9_RING_VERTS) {
        s_d3d9.vbOffset = 0;
        lockFlags = D3DLOCK_DISCARD;
    }

    D3DVertex* pLock = nullptr;
    if (SUCCEEDED(s_d3d9.vb->Lock(s_d3d9.vbOffset * sizeof(D3DVertex), count * sizeof(D3DVertex), (void**)&pLock, lockFlags))) {
        size_t o = 0;
        if (filled) pLock[o++] = { (cx + transX) * scaleX, (cy + transY) * scaleY, 0.5f, 1.0f, color, 0.5f, 0.5f };
        for (int i = 0; i <= segments; ++i) {
            float ang = (i / (float)segments) * 6.2831853f;
            pLock[o++] = { (cx + transX + std::cos(ang) * radius) * scaleX,
                           (cy + transY + std::sin(ang) * radius) * scaleY,
                           0.5f, 1.0f, color, 0.5f, 0.5f };
        }
        s_d3d9.vb->Unlock();
        s_d3d9.device->DrawPrimitive(filled ? D3DPT_TRIANGLEFAN : D3DPT_LINESTRIP, (UINT)s_d3d9.vbOffset, segments);
        s_d3d9.vbOffset += count;
    }
}

void d3d9_drawTriangleStrip(const float* coordsXY, const float* colorsRGBA, size_t vertCount, bool additive, float transX, float transY, float scaleX, float scaleY) {
    if (!s_d3d9.device || !s_d3d9.vb || vertCount < 3) return;

    if (!s_d3d9.inScene) {
        s_d3d9.device->BeginScene();
        s_d3d9.inScene = true;
    }
    if (s_d3d9.lastBoundTex != s_d3d9.whiteTex) {
        s_d3d9.device->SetTexture(0, s_d3d9.whiteTex);
        s_d3d9.lastBoundTex = s_d3d9.whiteTex;
    }
    bindD3D9Stream();

    DWORD lockFlags = D3DLOCK_NOOVERWRITE;
    if (s_d3d9.vbOffset + vertCount > D3D9_RING_VERTS) {
        s_d3d9.vbOffset = 0;
        lockFlags = D3DLOCK_DISCARD;
    }

    D3DVertex* pLock = nullptr;
    if (SUCCEEDED(s_d3d9.vb->Lock(s_d3d9.vbOffset * sizeof(D3DVertex), vertCount * sizeof(D3DVertex), (void**)&pLock, lockFlags))) {
        for (size_t i = 0; i < vertCount; ++i) {
            pLock[i].x = (coordsXY[i * 2 + 0] + transX) * scaleX;
            pLock[i].y = (coordsXY[i * 2 + 1] + transY) * scaleY;
            pLock[i].z = 0.5f; pLock[i].rhw = 1.0f;
            pLock[i].color = packColorPMA(colorsRGBA[i * 4 + 0], colorsRGBA[i * 4 + 1], colorsRGBA[i * 4 + 2], colorsRGBA[i * 4 + 3], additive, false);
            pLock[i].u = 0.5f; pLock[i].v = 0.5f;
        }
        s_d3d9.vb->Unlock();
        s_d3d9.device->DrawPrimitive(D3DPT_TRIANGLESTRIP, (UINT)s_d3d9.vbOffset, (UINT)vertCount - 2);
        s_d3d9.vbOffset += vertCount;
    }
}

void d3d9_drawRepeatedBackground(uint32_t texID, float uvOffsetX, float uvOffsetY, float uvW, float uvH, float logicalW, float logicalH, float bgR, float bgG, float bgB, float scaleX, float scaleY) {
    if (!s_d3d9.device || !s_d3d9.vb) return;

    if (!s_d3d9.inScene) {
        s_d3d9.device->BeginScene();
        s_d3d9.inScene = true;
    }

    IDirect3DTexture9* tex = s_d3d9.whiteTex;
    auto it = s_d3d9.textures.find(texID);
    if (it != s_d3d9.textures.end() && it->second) tex = it->second;

    s_d3d9.device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE); // opaque pass
    if (s_d3d9.lastBoundTex != tex) {
        s_d3d9.device->SetTexture(0, tex);
        s_d3d9.lastBoundTex = tex;
    }
    s_d3d9.device->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
    s_d3d9.device->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
    bindD3D9Stream();

    DWORD color = toD3D9Color(bgR, bgG, bgB, 1.0f);
    DWORD lockFlags = D3DLOCK_NOOVERWRITE;
    if (s_d3d9.vbOffset + 4 > D3D9_RING_VERTS) {
        s_d3d9.vbOffset = 0;
        lockFlags = D3DLOCK_DISCARD;
    }

    D3DVertex* pLock = nullptr;
    if (SUCCEEDED(s_d3d9.vb->Lock(s_d3d9.vbOffset * sizeof(D3DVertex), 4 * sizeof(D3DVertex), (void**)&pLock, lockFlags))) {
        pLock[0] = { 0.0f,               0.0f,               0.5f, 1.0f, color, uvOffsetX,       uvOffsetY };
        pLock[1] = { logicalW * scaleX, 0.0f,               0.5f, 1.0f, color, uvOffsetX + uvW, uvOffsetY };
        pLock[2] = { 0.0f,               logicalH * scaleY, 0.5f, 1.0f, color, uvOffsetX,       uvOffsetY + uvH };
        pLock[3] = { logicalW * scaleX, logicalH * scaleY, 0.5f, 1.0f, color, uvOffsetX + uvW, uvOffsetY + uvH };
        s_d3d9.vb->Unlock();
        s_d3d9.device->DrawPrimitive(D3DPT_TRIANGLESTRIP, (UINT)s_d3d9.vbOffset, 2);
        s_d3d9.vbOffset += 4;
    }

    s_d3d9.device->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    s_d3d9.device->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    s_d3d9.device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
}

uint32_t d3d9_registerTexture(uint32_t handle, const PreparedTexture& t) {
    if (!s_d3d9.device) return handle;

    int mode = 0;
    D3DFORMAT fmt = D3DFMT_A8R8G8B8;
    if (t.format == PF_L8 && s_d3d9.canL8) { mode = 2; fmt = D3DFMT_L8; }
    else if ((t.format == PF_LA8 || t.format == PF_I8) && s_d3d9.canA8L8) { mode = 1; fmt = D3DFMT_A8L8; }
    else if (t.format == PF_RGBA4444 && s_d3d9.canA4R4G4B4) { mode = 3; fmt = D3DFMT_A4R4G4B4; }

    IDirect3DTexture9* tex = nullptr;
    if (FAILED(s_d3d9.device->CreateTexture(t.width, t.height, 1, 0, fmt, D3DPOOL_MANAGED, &tex, NULL))) {
        tex = nullptr; mode = 0;
        if (FAILED(s_d3d9.device->CreateTexture(t.width, t.height, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex, NULL))) tex = nullptr;
    }
    if (tex) {
        D3DLOCKED_RECT lr;
        if (SUCCEEDED(tex->LockRect(0, &lr, NULL, 0))) {
            writeD3DTexels(t, mode, lr.pBits, lr.Pitch);
            tex->UnlockRect(0);
            s_d3d9.textures[handle] = tex;
        } else {
            tex->Release();
        }
    }
    return handle;
}

void d3d9_reloadTexture(uint32_t handle, const PreparedTexture& t) {
    if (!s_d3d9.device) return;
    auto it = s_d3d9.textures.find(handle);
    if (it != s_d3d9.textures.end() && it->second) {
        if (s_d3d9.lastBoundTex == it->second) {
            s_d3d9.lastBoundTex = nullptr;
            s_d3d9.device->SetTexture(0, s_d3d9.whiteTex);
        }
        it->second->Release();
        s_d3d9.textures.erase(it);
    }
    d3d9_registerTexture(handle, t);
}

#else
// Stubs for non-Windows builds
bool d3d9_init(SDL_Window*, int, int, bool) { return false; }
void d3d9_shutdown() {}
void d3d9_setViewport(int, int, int, int) {}
void d3d9_setVSync(bool) {}
void d3d9_beginFrame() {}
void d3d9_clear(float, float, float, float) {}
void d3d9_endFrame() {}
void d3d9_flushBatch(const D3DVertex*, size_t, uint32_t) {}
void d3d9_drawCircle(float, float, float, uint32_t, bool, int, float, float, float, float) {}
void d3d9_drawTriangleStrip(const float*, const float*, size_t, bool, float, float, float, float) {}
void d3d9_drawRepeatedBackground(uint32_t, float, float, float, float, float, float, float, float, float, float, float) {}
uint32_t d3d9_registerTexture(uint32_t handle, const PreparedTexture&) { return handle; }
void d3d9_reloadTexture(uint32_t, const PreparedTexture&) {}
#endif
