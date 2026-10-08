#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>
#include <algorithm>
#include <SDL2/SDL_opengl.h>
#include "utils/constants.h"
#include "render/render-device.h"

struct LayeredSprite;

struct AtlasFrame {
    std::string name;
    std::string atlas = "";
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 0.0f;
    float v1 = 0.0f;
    float offsetX = 0.0f;
    float offsetY = 0.0f;
    float sourceW = 0.0f;
    float sourceH = 0.0f;
    bool rotated = false;
    uint32_t textureId = 0;
};

enum ObjectType : uint8_t {
    OBJ_NONE = 0,
    OBJ_SOLID,
    OBJ_HAZARD,
    OBJ_PORTAL_FLY,
    OBJ_PORTAL_CUBE,
    OBJ_PAD,
    OBJ_PORTAL_SPEED,
    OBJ_PORTAL_MINI,
    OBJ_PORTAL_NORMAL
};

class LevelObject {
public:
    std::string type;
    ObjectType objType = OBJ_NONE;
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    bool activated = false;

    int id = 0;
    int padType = 8; // 8: Yellow, 9: Pink, 34: Red
    float speedValue = 1.0f; // Speed multiplier for speed portals
    float rotation = 0.0f;
    bool flipX = false;
    bool flipY = false;
    float portalY = 0.0f;

    LevelObject(const std::string& type = "", float x = 0.0f, float y = 0.0f, float w = 0.0f, float h = 0.0f);
};

template <typename T>
void zeroArray(std::vector<T>& array) {
    std::fill(array.begin(), array.end(), T(0));
}

template <typename T, size_t N>
void zeroArray(T (&array)[N]) {
    std::fill(array, array + N, T(0));
}

constexpr int DEF_NUM11 = 256;
constexpr int DEF_K = 286;
constexpr int DEF_NUM12 = 30;
constexpr int DEF_NUM13 = 15;

extern const uint8_t Uint8Array2[29];
extern const uint8_t Uint8Array3[30];
extern const uint8_t Uint8Array4[19];
extern const uint8_t Uint8Array5[19];

extern const std::vector<std::string> atlasTable;

class AtlasManager {
public:
    static std::unordered_map<std::string, AtlasFrame> frames;
    static const AtlasFrame* squareFrame;
    static float atlasScale;
    static void loadAtlasPlist(const std::string& plistContent, const std::string& textureKey, int texW, int texH);
    static void loadAtlasJson(const std::string& jsonContent, int texW, int texH);
    static const AtlasFrame* findAtlasFrame(const std::string& frameName);
};

const AtlasFrame* findAtlasFrame(const std::string& frameName);
LayeredSprite addImageFromAtlas(float x, float y, const std::string& frameName);

void beginSpriteBatch();
void flushSpriteBatch();

void batchAtlasFrame(uint32_t texID, const AtlasFrame* frame, float x, float y,
                     float w, float h, float rotation,
                     float r, float g, float b, float a,
                     bool flipX = false, bool flipY = false,
                     BlendMode blend = BLEND_NORMAL);

void drawAtlasFrame(const std::string& frameName, float x, float y,
                    float w = 0.0f, float h = 0.0f, float rotation = 0.0f,
                    float r = 1.0f, float g = 1.0f, float b = 1.0f, float a = 1.0f,
                    bool flipX = false, bool flipY = false,
                    BlendMode blend = BLEND_NORMAL);

void drawScale9(const std::string& textureKey, float x, float y, float w, float h, float cornerSize,
                float r = 1.0f, float g = 1.0f, float b = 1.0f, float a = 1.0f);
