#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <cstddef>
#include <SDL2/SDL.h>
#include "constants.h"
#include "gpu-profile.h"

enum RenderBackendType {
    RENDERER_OPENGL = 0,
    RENDERER_D3D8   = 1,
    RENDERER_D3D9   = 2,
    RENDERER_WEBGL  = 3
};

// Native pre-transformed vertex format for Direct3D 8 and Direct3D 9 (D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1) - 28 bytes
struct D3DVertex {
    float x, y, z, rhw; // 16 bytes: pre-scaled screen coordinates
    uint32_t color;     // 4 bytes: 0xAARRGGBB
    float u, v;         // 8 bytes: texture coordinates
};

// High-performance compact format for OpenGL 1.1 (20 bytes - 37.5% bus reduction on Intel GMA)
struct GLVertex {
    float x, y;         // 8 bytes: pre-translated screen coordinates
    uint32_t color;     // 4 bytes: 0xAABBGGRR (GL_UNSIGNED_BYTE little-endian RGBA)
    float u, v;         // 8 bytes: texture coordinates
};


// Lossless texture-format analysis (shared by the GL, D3D8 and D3D9 back-ends).
// Returns a bitmask for tightly packed RGBA8 pixels (byte order R,G,B,A):
//   bit 0 -> every texel has RGB == (255,255,255): only alpha carries information
//   bit 1 -> every texel has R == G == B: luminance + alpha
//   bit 2 -> bit 1 and every texel is fully opaque: pure luminance (e.g. a grayscale background tinted by vertex color)
// These can be stored in 8/16 bits per texel (GL_LUMINANCE / GL_ALPHA / GL_LUMINANCE_ALPHA, D3DFMT_L8 / D3DFMT_A8L8)
// with a bit-exact sampling result, cutting texture fetch traffic by 2x-4x.
// The RGB of fully transparent texels is deliberately part of the test so that bilinear
// filtering at sprite borders stays identical to the RGBA8 path.
enum : int { TEXKIND_WHITE_ALPHA = 1, TEXKIND_GRAY_ALPHA = 2, TEXKIND_GRAY_OPAQUE = 4 };

inline int classifyRGBA(const void* pixels, size_t count) {
    const uint32_t* p = static_cast<const uint32_t*>(pixels);
    bool white = true, gray = true, opaque = true;
    for (size_t i = 0; i < count && gray; ++i) {
        uint32_t c = p[i];
        uint32_t r = c & 0xFFu, g = (c >> 8) & 0xFFu, b = (c >> 16) & 0xFFu;
        if (r != g || g != b) { gray = false; white = false; opaque = false; break; }
        if (r != 0xFFu) white = false;
        if ((c >> 24) != 0xFFu) opaque = false;
    }
    if (count == 0) return 0;
    return (white ? TEXKIND_WHITE_ALPHA : 0) | (gray ? TEXKIND_GRAY_ALPHA : 0) | ((gray && opaque) ? TEXKIND_GRAY_OPAQUE : 0);
}

// -----------------------------------------------------------------------------
// PREMULTIPLIED-ALPHA UNIFIED BLENDING
// Every back-end uses ONE single blend equation:  dst = src + dst * (1 - srcA)
// Textures are premultiplied at load time and vertex colours are premultiplied here.
//   BLEND_NORMAL -> vertex (r*a, g*a, b*a, a)  == classic SRC_ALPHA / INV_SRC_ALPHA
//   BLEND_ADD    -> vertex (r*a, g*a, b*a, 0)  == classic SRC_ALPHA / ONE
// Consequence: switching between normal and additive sprites never breaks a batch and
// never touches GPU state; only a texture change does. This removes most draw calls
// (the level interleaves additive glow and normal sprites all over the place), which is
// the single biggest cost on Intel GMA drivers running on Atom-class CPUs.
// -----------------------------------------------------------------------------
inline uint32_t packColorPMA(float r, float g, float b, float a, bool additive, bool glOrder) {
    if (r >= 1.0f && g >= 1.0f && b >= 1.0f && a >= 1.0f) return additive ? 0x00FFFFFFu : 0xFFFFFFFFu;
    float ac = a <= 0.0f ? 0.0f : (a >= 1.0f ? 1.0f : a);
    auto q = [ac](float c) -> uint32_t {
        c = c <= 0.0f ? 0.0f : (c >= 1.0f ? 1.0f : c);
        return (uint32_t)(c * ac * 255.0f + 0.5f);
    };
    uint32_t cr = q(r), cg = q(g), cb = q(b);
    uint32_t ca = additive ? 0u : (uint32_t)(ac * 255.0f + 0.5f);
    return glOrder ? ((ca << 24) | (cb << 16) | (cg << 8) | cr)
                   : ((ca << 24) | (cr << 16) | (cg << 8) | cb);
}

// Storage formats produced by prepareTexture(); all of them are premultiplied.
enum PreparedFormat : int {
    PF_RGBA8 = 0,     // 4 B/texel  R,G,B,A
    PF_RGBA4444 = 1,  // data still R,G,B,A 8-bit; back-end stores it as 16-bit 4444 (quality preset LOW)
    PF_LA8 = 2,       // 2 B/texel  L,A     (gray + alpha)
    PF_I8 = 3,        // 1 B/texel  I = L = A (white sprite masks, glow, particles, fonts)
    PF_L8 = 4         // 1 B/texel  opaque gray (background)
};

struct PreparedTexture {
    int width = 0, height = 0;          // storage size (may be POT padded / downscaled)
    float uScale = 1.0f, vScale = 1.0f; // UV remap applied by RenderDevice when the storage was padded
    PreparedFormat format = PF_RGBA8;
    std::vector<uint8_t> data;
};

// Raw uncompressed RGBA pixel cache kept in RAM for instantaneous dynamic compression switching
struct MasterTexture {
    std::string name;
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgba;
};

// Premultiplies, handles NPOT on POT-only GPUs (pad or downscale) and picks the smallest lossless format.
PreparedTexture prepareTexture(const void* rgbaPixels, int width, int height, bool needPOT, bool allowIntensity, bool isBackground = false);

// Writes a prepared texture into a locked Direct3D surface.
// d3dMode: 0 = A8R8G8B8, 1 = A8L8, 2 = L8, 3 = A4R4G4B4
void writeD3DTexels(const PreparedTexture& t, int d3dMode, void* bits, int pitch);

class RenderDevice {
public:
    static RenderDevice& get() {
        static RenderDevice instance;
        return instance;
    }

    bool init(SDL_Window* window, RenderBackendType backend, int windowW, int windowH);
    void shutdown();

    void setViewport(int vpX, int vpY, int vpW, int vpH, float logicalW, float logicalH);
    int getViewportX() const { return _vpX; }
    int getViewportY() const { return _vpY; }
    int getViewportW() const { return _vpW; }
    int getViewportH() const { return _vpH; }
    float getLogicalW() const { return _logicalW; }
    float getLogicalH() const { return _logicalH; }
    void setVSync(bool enabled);
    bool getVSync() const { return _vsync; }

    void beginFrame();
    void endFrame();
    void clear(float r, float g, float b, float a = 1.0f);

    // With premultiplied alpha the blend mode is only a per-vertex flag: no flush, no GPU state change.
    void setBlendMode(BlendMode mode) { _currentBlend = mode; }
    BlendMode getBlendMode() const { return _currentBlend; }

    // 2D transformation handling without batch breaks (Zero batch-break matrix stack)
    inline void pushMatrix() {
        _matrixStack.push_back({_transX, _transY});
    }

    inline void popMatrix() {
        if (!_matrixStack.empty()) {
            _transX = _matrixStack.back().first;
            _transY = _matrixStack.back().second;
            _matrixStack.pop_back();
        }
    }

    inline void translate(float dx, float dy) {
        _transX += dx;
        _transY += dy;
    }

    void beginBatch();
    void flushBatch();
    void batchQuad(uint32_t texID, float x0, float y0, float u0, float v0,
                   float x1, float y1, float u1, float v1,
                   float x2, float y2, float u2, float v2,
                   float x3, float y3, float u3, float v3,
                   float r, float g, float b, float a, BlendMode blend);

    void batchAxisAlignedQuad(uint32_t texID, float x0, float y0, float x1, float y1,
                              float u0, float v0, float u1, float v1,
                              float r, float g, float b, float a, BlendMode blend);

    void drawRect(float x, float y, float w, float h, float r, float g, float b, float a, BlendMode blend = BLEND_NORMAL);
    void drawColorQuad(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3,
                       float r, float g, float b, float a, BlendMode blend = BLEND_NORMAL);
    void drawCircle(float cx, float cy, float radius, float r, float g, float b, float a, bool filled, BlendMode blend = BLEND_NORMAL);
    void drawTriangleStrip(const float* coordsXY, const float* colorsRGBA, size_t vertCount, BlendMode blend = BLEND_NORMAL);
    void drawRepeatedBackground(uint32_t texID, float scrollX, float camY, float bgR, float bgG, float bgB);

    uint32_t registerTexture(const std::string& name, int width, int height, const void* rgbaPixels);
    void syncTexturesFromBootScene();
    uint32_t getTextureID(const std::string& name);
    void reloadTextures();

    RenderBackendType getBackend() const { return _backend; }
    const char* getBackendName() const;

    // Render statistics (draw calls issued during the last completed frame)
    uint32_t getLastFrameDrawCalls() const { return _lastDrawCalls; }
    // Texture-format choice (updated dynamically in real-time when quality changes)
    bool texturesLoaded16bit() const { return _tex16Active; }

private:
    RenderDevice();
    ~RenderDevice();

    bool _initOpenGL();
    void _bindGLBatchPointers();
    void _selectTexture(uint32_t texID);
    void _recordTexUV(uint32_t handle, const PreparedTexture& t);
    #ifdef _WIN32
    bool _initD3D9();
    void _createD3D9WhiteTexture();
    void _createD3D9BatchBuffers();
    void _applyD3D9RenderStates();
    void _bindD3D9Stream();
    void _onResizeD3D9(int newW, int newH);
    #endif

    SDL_Window* _window = nullptr;
    SDL_GLContext _glContext = nullptr;
    RenderBackendType _backend = RENDERER_OPENGL;
    bool _vsync = false;

    int _vpX = 0, _vpY = 0, _vpW = 800, _vpH = 600;
    float _logicalW = 800.0f, _logicalH = 600.0f;
    float _scaleX = 1.0f, _scaleY = 1.0f;

    std::vector<std::pair<float, float>> _matrixStack;
    float _transX = 0.0f, _transY = 0.0f;

    BlendMode _currentBlend = BLEND_NORMAL;
    uint32_t _currentTexID = 0;

    // Per-texture UV remap (only for textures padded to a power of two)
    std::unordered_map<uint32_t, std::pair<float, float>> _texUV;
    bool _uvScaled = false;
    float _curUS = 1.0f, _curVS = 1.0f;

    // 1024 quads * 4 verts * 28 B = 112 KB (was 448 KB): the staging buffer stays cache-resident
    static constexpr size_t MAX_BATCH_QUADS = 1024;
    static constexpr size_t MAX_BATCH_VERTS = MAX_BATCH_QUADS * 4;
    union {
        D3DVertex d3d[MAX_BATCH_VERTS];
        GLVertex  gl[MAX_BATCH_VERTS];
    } _batchBuffer;
    size_t _batchVertCount = 0;

    GLuint _glWhiteTex = 0;
    GLuint _lastGLTex = 0;
    bool _glPointersBound = false;   // client arrays point at _batchBuffer.gl

    uint32_t _drawCalls = 0, _lastDrawCalls = 0;
    uint32_t _statsFrames = 0, _statsTick = 0;
    bool _statsEnabled = false;
    bool _tex16Active = false;

    #ifdef _WIN32
    // Direct3D 9 members with dynamic ring buffer and texture cache
    static constexpr size_t D3D9_RING_VERTS = 32768; // 8192 quads
    size_t _d3d9VbOffset = 0;
    void* _lastD3D9Tex = nullptr;
    void* _hD3D9Module = nullptr;
    void* _d3d9 = nullptr;
    void* _d3d9Device = nullptr;
    void* _d3d9VB = nullptr;
    void* _d3d9IB = nullptr;
    void* _d3d9WhiteTex = nullptr;
    uint8_t _d3dpp9[128] = { 0 }; // Opaque buffer for D3DPRESENT_PARAMETERS of D3D9
    std::unordered_map<uint32_t, void*> _d3d9Textures;
    bool _inScene = false;
    bool _d3d9Bound = false;      // FVF / stream 0 / index buffer already bound
    bool _d3d9CanA8L8 = false;    // device can sample D3DFMT_A8L8 (lossless 16-bit mask textures)
    bool _d3d9CanL8 = false;      // device can sample D3DFMT_L8 (lossless 8-bit opaque gray textures)
    bool _d3d9CanA4R4G4B4 = false;// device can sample D3DFMT_A4R4G4B4 (quality preset LOW)
    #endif

    std::unordered_map<std::string, uint32_t> _textureRegistry;
    std::unordered_map<uint32_t, MasterTexture> _masterTextures;
    uint32_t _nextTexHandle = 1;
};
