#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <SDL2/SDL.h>
#include "constants.h"

enum RenderBackendType {
    RENDERER_OPENGL = 0,
    RENDERER_D3D8   = 1,
    RENDERER_D3D9   = 2
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

class RenderDevice {
public:
    static RenderDevice& get() {
        static RenderDevice instance;
        return instance;
    }

    bool init(SDL_Window* window, RenderBackendType backend, int windowW, int windowH);
    void shutdown();

    void setViewport(int vpX, int vpY, int vpW, int vpH, float logicalW, float logicalH);
    void setVSync(bool enabled);
    bool getVSync() const { return _vsync; }

    void beginFrame();
    void endFrame();
    void clear(float r, float g, float b, float a = 1.0f);

    void setBlendMode(BlendMode mode);
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

    void drawRect(float x, float y, float w, float h, float r, float g, float b, float a, BlendMode blend = BLEND_NORMAL);
    void drawColorQuad(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3,
                       float r, float g, float b, float a, BlendMode blend = BLEND_NORMAL);
    void drawCircle(float cx, float cy, float radius, float r, float g, float b, float a, bool filled, BlendMode blend = BLEND_NORMAL);
    void drawTriangleStrip(const float* coordsXY, const float* colorsRGBA, size_t vertCount, BlendMode blend = BLEND_NORMAL);
    void drawRepeatedBackground(uint32_t texID, float scrollX, float camY, float bgR, float bgG, float bgB);

    uint32_t registerTexture(const std::string& name, int width, int height, const void* rgbaPixels);
    void syncTexturesFromBootScene();
    uint32_t getTextureID(const std::string& name);

    RenderBackendType getBackend() const { return _backend; }
    const char* getBackendName() const;

private:
    RenderDevice();
    ~RenderDevice();

    bool _initOpenGL();
#ifdef _WIN32
    bool _initD3D9();
    void _createD3D9WhiteTexture();
    void _createD3D9BatchBuffers();
    void _applyD3D9RenderStates();
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

    static constexpr size_t MAX_BATCH_QUADS = 4096;
    static constexpr size_t MAX_BATCH_VERTS = MAX_BATCH_QUADS * 4;
    union {
        D3DVertex d3d[MAX_BATCH_VERTS];
        GLVertex  gl[MAX_BATCH_VERTS];
    } _batchBuffer;
    size_t _batchVertCount = 0;

    GLuint _glWhiteTex = 0;
    GLuint _lastGLTex = 0;

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
#endif

    std::unordered_map<std::string, uint32_t> _textureRegistry;
    uint32_t _nextTexHandle = 1;
};
