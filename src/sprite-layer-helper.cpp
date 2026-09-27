#include "sprite-layer-helper.h"
#include "boot-scene.h"
#include <cmath>

LayeredSprite createLayeredSprite(float x, float y, const std::string& frameName, int depth, bool visible) {
    LayeredSprite ls;
    const AtlasFrame* frame = findAtlasFrame(frameName);

    if (!frame) {
        std::string key = frameName;
        auto it = BootScene::textures.find(key);
        if (it == BootScene::textures.end() && key.size() > 4 && key.substr(key.size() - 4) == ".png") {
            it = BootScene::textures.find(key.substr(0, key.size() - 4));
        }
        if (it == BootScene::textures.end()) {
            it = BootScene::textures.find(key + ".png");
        }
        if (it == BootScene::textures.end()) {
            ls.visible = false;
            ls.frameName = "";
            return ls;
        }
    }

    ls.cachedFrame = frame;
    auto itWs = BootScene::textures.find("GJ_WebSheet");
    if (itWs != BootScene::textures.end()) ls.cachedTexID = itWs->second.id;

    ls.frameName = frameName;
    ls.x = x;
    ls.y = y;
    ls.depth = depth;
    ls.visible = visible;
    ls.offsetX = 0.0f;
    ls.offsetY = 0.0f;

    // Read spriteSourceSize and sourceSize from JSON to calculate exact offset
    // (Aligns ship cockpit, cube eyes and vehicles)
    if (BootScene::textCache.find("GJ_WebSheetJson") != BootScene::textCache.end()) {
        const std::string& json = BootScene::textCache["GJ_WebSheetJson"];
        size_t fnPos = json.find("\"" + frameName + "\"");
        if (fnPos != std::string::npos) {
            size_t nextPos = json.find(".png\"", fnPos + frameName.size() + 2);
            size_t chunkLen = (nextPos != std::string::npos) ? (nextPos - fnPos) : 1000;
            std::string chunk = json.substr(fnPos, chunkLen);

            auto getChunkVal = [&](const std::string& key, size_t start) -> float {
                size_t kPos = chunk.find("\"" + key + "\"", start);
                if (kPos == std::string::npos) return 0.0f;
                size_t colon = chunk.find(":", kPos);
                if (colon == std::string::npos) return 0.0f;
                try { return std::stof(chunk.substr(colon + 1)); } catch (...) { return 0.0f; }
            };

            size_t sssPos = chunk.find("\"spriteSourceSize\"");
            size_t ssPos  = chunk.find("\"sourceSize\"");

            if (sssPos != std::string::npos && ssPos != std::string::npos) {
                float offX = getChunkVal("x", sssPos);
                float offY = getChunkVal("y", sssPos);
                float srcW = getChunkVal("w", ssPos);
                float srcH = getChunkVal("h", ssPos);

                // Restore dimensions to JSON pixels
                float fw = frame ? (frame->w * AtlasManager::atlasScale) : srcW;
                float fh = frame ? (frame->h * AtlasManager::atlasScale) : srcH;

                if (srcW > 0.0f && srcH > 0.0f) {
                    ls.offsetX = (offX + fw * 0.5f) - (srcW * 0.5f);
                    ls.offsetY = (offY + fh * 0.5f) - (srcH * 0.5f);
                }
            }
        }
    }

    if (AtlasManager::atlasScale > 0.0f) {
        ls.offsetX /= AtlasManager::atlasScale;
        ls.offsetY /= AtlasManager::atlasScale;
    }

    return ls;
}

void LayeredSprite::render(float extraOffsetX, float extraOffsetY) {
    if (!visible || (!cachedFrame && frameName.empty())) return;

    const AtlasFrame* af = cachedFrame ? cachedFrame : findAtlasFrame(frameName);
    float baseW = 0.0f;
    float baseH = 0.0f;
    uint32_t texID = cachedTexID;

    if (af) {
        baseW = af->w;
        baseH = af->h;
        if (texID == 0) {
            auto itWs = BootScene::textures.find("GJ_WebSheet");
            if (itWs != BootScene::textures.end()) texID = itWs->second.id;
        }
    } else {
        auto it = BootScene::textures.find(frameName);
        if (it == BootScene::textures.end() && frameName.size() > 4 && frameName.substr(frameName.size() - 4) == ".png") {
            it = BootScene::textures.find(frameName.substr(0, frameName.size() - 4));
        }
        if (it == BootScene::textures.end()) {
            it = BootScene::textures.find(frameName + ".png");
        }
        if (it != BootScene::textures.end()) {
            baseW = (float)it->second.width;
            baseH = (float)it->second.height;
            texID = it->second.id;
        }
    }

    if (baseW <= 0.0f || baseH <= 0.0f || texID == 0) return;

    float drawW = baseW * scaleX;
    float drawH = baseH * scaleY;

    float rx = 0.0f, ry = 0.0f;
    if (offsetX != 0.0f || offsetY != 0.0f) {
        if (rotation == 0.0f) {
            rx = offsetX * scaleX;
            ry = offsetY * scaleY;
        } else {
            float rad = rotation * 0.0174532925f;
            float cosR = std::cos(rad);
            float sinR = std::sin(rad);
            rx = (offsetX * cosR - offsetY * sinR) * scaleX;
            ry = (offsetX * sinR + offsetY * cosR) * scaleY;
        }
    }

    if (af) {
        batchAtlasFrame(texID, af, x + extraOffsetX + rx, y + extraOffsetY + ry, drawW, drawH, rotation, r, g, b, a, false, false, blend);
    } else {
        drawAtlasFrame(frameName, x + extraOffsetX + rx, y + extraOffsetY + ry, drawW, drawH, rotation, r, g, b, a);
    }
}
