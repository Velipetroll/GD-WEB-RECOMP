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
    ls.cachedTexID = 0;
    if (frame) {
        if (frame->textureId != 0) {
            ls.cachedTexID = frame->textureId;
        } else if (!frame->atlas.empty()) {
            auto itA = BootScene::textures.find(frame->atlas);
            if (itA != BootScene::textures.end()) ls.cachedTexID = itA->second.id;
        }
    }
    if (ls.cachedTexID == 0) {
        auto itWs = BootScene::textures.find("GJ_WebSheet");
        if (itWs != BootScene::textures.end()) ls.cachedTexID = itWs->second.id;
    }

    ls.frameName = frameName;
    ls.x = x;
    ls.y = y;
    ls.depth = depth;
    ls.visible = visible;
    ls.offsetX = frame ? frame->offsetX : 0.0f;
    ls.offsetY = frame ? frame->offsetY : 0.0f;

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
            if (af->textureId != 0) {
                texID = af->textureId;
            } else if (!af->atlas.empty()) {
                auto itA = BootScene::textures.find(af->atlas);
                if (itA != BootScene::textures.end()) texID = itA->second.id;
            }
        }
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

    bool flipX = (scaleX < 0.0f);
    bool flipY = (scaleY < 0.0f);
    float drawW = baseW * std::abs(scaleX);
    float drawH = baseH * std::abs(scaleY);

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
        batchAtlasFrame(texID, af, x + extraOffsetX + rx, y + extraOffsetY + ry, drawW, drawH, rotation, r, g, b, a, flipX, flipY, blend);
    } else {
        drawAtlasFrame(frameName, x + extraOffsetX + rx, y + extraOffsetY + ry, drawW, drawH, rotation, r, g, b, a, flipX, flipY);
    }
}
