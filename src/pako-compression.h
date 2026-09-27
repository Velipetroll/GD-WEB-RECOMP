#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include "constants.h"

struct ObjectChild {
    std::string frame = "";
    float localDx = 0.0f;
    float localDy = 0.0f;
    std::string blend = "normal";
    unsigned int tint = 0;
    int z = 0;
    bool audioScale = false;
};

struct ObjectDefinition {
    std::string type = "solid";
    std::string frame = "";
    float gridW = 1.0f;
    float gridH = 1.0f;
    float spriteW = 0.0f;
    float spriteH = 0.0f;
    float hitboxScaleX = 1.0f;
    float hitboxScaleY = 1.0f;
    std::string sub = "";
    bool black = false;
    bool glow = false;
    bool portalParticle = false;
    unsigned int portalParticleColor = 0;
    std::string blend = "normal";
    unsigned int tint = 0;
    int z = 0;
    int enterEffect = -1;
    int colorIdx = -1;
    std::vector<std::string> randomFrames;
    std::vector<ObjectChild> children;
};

struct LevelObjectRaw {
    int id = 0;
    float x = 0.0f;
    float y = 0.0f;
    bool flipX = false;
    bool flipY = false;
    float rot = 0.0f;
    float scale = 1.0f;
    int zLayer = 0;
    int zOrder = 0;
    std::string groups = "";
    int color1 = 0;
    int color2 = 0;
    std::unordered_map<int, std::string> rawMap;
};

struct ParsedLevel {
    std::string settings;
    std::vector<LevelObjectRaw> objects;
};

class PakoCompression {
public:
    static std::string inflate(const uint8_t* data, size_t size);
    static std::vector<uint8_t> base64Decode(const std::string& input);

    static void initCatalog();
    static const ObjectDefinition* helperFn18(int id);

    static LevelObjectRaw helperFn16(const std::string& objectStr);
    static ParsedLevel helperFn17(const std::string& rawLevelString);
};
