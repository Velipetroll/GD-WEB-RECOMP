#include "level-data-helpers.h"
#include "sprite-layer-helper.h"
#include "boot-scene.h"
#include <iostream>
#include <algorithm>
#include <cmath>

const std::vector<std::string> atlasTable = { "GJ_WebSheet" };

const uint8_t Uint8Array2[29] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0
};
const uint8_t Uint8Array3[30] = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13
};
const uint8_t Uint8Array4[19] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 3, 7
};
const uint8_t Uint8Array5[19] = {
    16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
};

std::unordered_map<std::string, AtlasFrame> AtlasManager::frames;
const AtlasFrame* AtlasManager::squareFrame = nullptr;
float AtlasManager::atlasScale = 1.0f;

LevelObject::LevelObject(const std::string& type, float x, float y, float w, float h)
: type(type), x(x), y(y), w(w), h(h), activated(false)
{
    if (type == "solid") objType = OBJ_SOLID;
    else if (type == "hazard") objType = OBJ_HAZARD;
    else if (type == portalFly) objType = OBJ_PORTAL_FLY;
    else if (type == portalCube) objType = OBJ_PORTAL_CUBE;
    else if (type == portalMini) objType = OBJ_PORTAL_MINI;
    else if (type == portalNormal) objType = OBJ_PORTAL_NORMAL;
    else if (type == "pad") objType = OBJ_PAD;
    else if (type == "speed") objType = OBJ_PORTAL_SPEED;
    else objType = OBJ_NONE;
}

void beginSpriteBatch() {
    // No-op to allow the batch to continue without artificial breaks
}

void flushSpriteBatch() {
    RenderDevice::get().flushBatch();
}

void batchAtlasFrame(uint32_t texID, const AtlasFrame* frame, float x, float y,
                     float w, float h, float rotation,
                     float r, float g, float b, float a,
                     bool flipX, bool flipY,
                     BlendMode blend)
{
    if (!frame || texID == 0 || w == 0.0f || h == 0.0f || a <= 0.001f) return;
    if (w < 0.0f) { flipX = !flipX; w = -w; }
    if (h < 0.0f) { flipY = !flipY; h = -h; }

    float u0 = frame->u0, v0 = frame->v0;
    float u1 = frame->u1, v1 = frame->v1;

    float qu0, qv0, qu1, qv1, qu2, qv2, qu3, qv3;
    if (!frame->rotated) {
        if (flipX) std::swap(u0, u1);
        if (flipY) std::swap(v0, v1);

        qu0 = u0; qv0 = v0;
        qu1 = u1; qv1 = v0;
        qu2 = u1; qv2 = v1;
        qu3 = u0; qv3 = v1;
    } else {
        // Rotated 90 deg clockwise in sprite sheet
        if (flipX) std::swap(v0, v1);
        if (flipY) std::swap(u0, u1);

        qu0 = u1; qv0 = v0;
        qu1 = u1; qv1 = v1;
        qu2 = u0; qv2 = v1;
        qu3 = u0; qv3 = v0;
    }

    if (rotation == 0.0f && !frame->rotated) {
        float halfW = w * 0.5f;
        float halfH = h * 0.5f;
        RenderDevice::get().batchAxisAlignedQuad(
            texID,
            x - halfW, y - halfH,
            x + halfW, y + halfH,
            qu0, qv0, qu2, qv2,
            r, g, b, a, blend
        );
        return;
    }

    float halfW = w * 0.5f;
    float halfH = h * 0.5f;

    float x0, y0, x1, y1, x2, y2, x3, y3;

    if (rotation == 0.0f) {
        x0 = x - halfW; y0 = y - halfH;
        x1 = x + halfW; y1 = y - halfH;
        x2 = x + halfW; y2 = y + halfH;
        x3 = x - halfW; y3 = y + halfH;
    } else {
        float cosR, sinR;
        if (rotation == 90.0f || rotation == -270.0f) {
            cosR = 0.0f; sinR = 1.0f;
        } else if (rotation == 180.0f || rotation == -180.0f) {
            cosR = -1.0f; sinR = 0.0f;
        } else if (rotation == 270.0f || rotation == -90.0f) {
            cosR = 0.0f; sinR = -1.0f;
        } else {
            float rad = rotation * 0.0174532925f;
            cosR = std::cos(rad);
            sinR = std::sin(rad);
        }

        float rx0 = -halfW * cosR + halfH * sinR;
        float ry0 = -halfW * sinR - halfH * cosR;
        float rx1 =  halfW * cosR + halfH * sinR;
        float ry1 =  halfW * sinR - halfH * cosR;

        x0 = x + rx0; y0 = y + ry0;
        x1 = x + rx1; y1 = y + ry1;
        x2 = x - rx0; y2 = y - ry0;
        x3 = x - rx1; y3 = y - ry1;
    }

    RenderDevice::get().batchQuad(
        texID,
        x0, y0, qu0, qv0,
        x1, y1, qu1, qv1,
        x2, y2, qu2, qv2,
        x3, y3, qu3, qv3,
        r, g, b, a, blend
    );
}

void AtlasManager::loadAtlasPlist(const std::string& plistContent, const std::string& textureKey, int texW, int texH) {
    if (plistContent.empty()) return;

    float sheetW = (float)texW;
    float sheetH = (float)texH;

    // Fallback sheet dimensions from plist metadata if not passed
    if (sheetW <= 0.0f || sheetH <= 0.0f) {
        size_t mp = plistContent.find("<key>metadata</key>");
        if (mp != std::string::npos) {
            size_t szp = plistContent.find("<key>size</key>", mp);
            if (szp != std::string::npos) {
                size_t sp = plistContent.find("<string>{", szp);
                if (sp != std::string::npos) {
                    float mw = 0.0f, mh = 0.0f;
                    if (sscanf(plistContent.c_str() + sp, "<string>{%f,%f}</string>", &mw, &mh) == 2) {
                        if (mw > 0.0f) sheetW = mw;
                        if (mh > 0.0f) sheetH = mh;
                    }
                }
            }
        }
    }

    if (sheetW <= 0.0f) sheetW = 1024.0f;
    if (sheetH <= 0.0f) sheetH = 1024.0f;

    uint32_t resolvedTexID = 0;
    auto itTex = BootScene::textures.find(textureKey);
    if (itTex != BootScene::textures.end()) {
        resolvedTexID = itTex->second.id;
    }

    size_t fStart = plistContent.find("<key>frames</key>");
    if (fStart == std::string::npos) return;
    size_t dictStart = plistContent.find("<dict>", fStart);
    if (dictStart == std::string::npos) return;

    size_t pos = dictStart + 6;
    while (pos < plistContent.size()) {
        size_t kp = plistContent.find("<key>", pos);
        if (kp == std::string::npos) break;
        size_t ke = plistContent.find("</key>", kp);
        if (ke == std::string::npos) break;
        std::string frameName = plistContent.substr(kp + 5, ke - (kp + 5));
        if (frameName == "metadata") break;

        size_t dp = plistContent.find("<dict>", ke);
        size_t de = plistContent.find("</dict>", dp);
        if (dp == std::string::npos || de == std::string::npos) break;

        std::string chunk = plistContent.substr(dp, de - dp + 7);
        pos = de + 7;

        float fx = 0.0f, fy = 0.0f, fw = 0.0f, fh = 0.0f;
        float ox = 0.0f, oy = 0.0f;
        float sw = 0.0f, sh = 0.0f;
        bool rotated = false;

        size_t fp = chunk.find("<key>frame</key>");
        if (fp != std::string::npos) {
            size_t sp = chunk.find("<string>{{", fp);
            if (sp != std::string::npos) {
                sscanf(chunk.c_str() + sp, "<string>{{%f,%f},{%f,%f}}</string>", &fx, &fy, &fw, &fh);
            }
        }

        size_t op = chunk.find("<key>offset</key>");
        if (op != std::string::npos) {
            size_t sp = chunk.find("<string>{", op);
            if (sp != std::string::npos) {
                sscanf(chunk.c_str() + sp, "<string>{%f,%f}</string>", &ox, &oy);
            }
        }

        size_t rp = chunk.find("<key>rotated</key>");
        if (rp != std::string::npos) {
            size_t tp = chunk.find("<true/>", rp);
            if (tp != std::string::npos && tp < de) {
                rotated = true;
            }
        }

        size_t ssp = chunk.find("<key>sourceSize</key>");
        if (ssp != std::string::npos) {
            size_t sp = chunk.find("<string>{", ssp);
            if (sp != std::string::npos) {
                sscanf(chunk.c_str() + sp, "<string>{%f,%f}</string>", &sw, &sh);
            }
        } else {
            sw = fw; sh = fh;
        }

        if (fw > 0.0f && fh > 0.0f) {
            AtlasFrame af;
            af.name = frameName;
            af.atlas = textureKey;
            af.textureId = resolvedTexID;
            af.x = fx;
            af.y = fy;
            af.w = fw;
            af.h = fh;
            af.offsetX = ox;
            af.offsetY = -oy; // Cocos2d bottom-up to top-down screen Y
            af.sourceW = (sw > 0.0f) ? sw : fw;
            af.sourceH = (sh > 0.0f) ? sh : fh;
            af.rotated = rotated;

            float texW_rect = rotated ? fh : fw;
            float texH_rect = rotated ? fw : fh;

            af.u0 = fx / sheetW;
            af.v0 = fy / sheetH;
            af.u1 = (fx + texW_rect) / sheetW;
            af.v1 = (fy + texH_rect) / sheetH;

            frames[frameName] = af;
            if (frameName.size() > 4 && frameName.compare(frameName.size() - 4, 4, ".png") == 0) {
                frames[frameName.substr(0, frameName.size() - 4)] = af;
            } else {
                frames[frameName + ".png"] = af;
            }
        }
    }

    squareFrame = findAtlasFrame("square.png");
}

void AtlasManager::loadAtlasJson(const std::string& jsonContent, int texW, int texH) {
    if (jsonContent.empty()) return;
    frames.clear();

    float jsonW = (texW > 0) ? (float)texW : 1046.0f;
    float jsonH = (texH > 0) ? (float)texH : 1046.0f;

    size_t sizePos = jsonContent.find("\"size\"");
    if (sizePos != std::string::npos) {
        auto getDim = [&](const std::string& key) -> float {
            size_t kPos = jsonContent.find("\"" + key + "\"", sizePos);
            if (kPos == std::string::npos || kPos - sizePos > 200) return 0.0f;
            size_t colon = jsonContent.find(":", kPos);
            if (colon == std::string::npos) return 0.0f;
            try { return std::stof(jsonContent.substr(colon + 1)); } catch (...) { return 0.0f; }
        };
        float gw = getDim("w");
        float gh = getDim("h");
        if (gw > 0.0f) jsonW = gw;
        if (gh > 0.0f) jsonH = gh;
    }

    atlasScale = (jsonW > 1046.0f) ? (jsonW / 1046.0f) : 1.0f;
    if (atlasScale <= 0.0f) atlasScale = 1.0f;

    size_t pos = 0;
    while (pos < jsonContent.size()) {
        size_t fnPos = jsonContent.find("\"filename\"", pos);
        size_t framePos = jsonContent.find("\"frame\"", pos);
        if (fnPos == std::string::npos && framePos == std::string::npos) break;

        std::string frameName = "";
        size_t currentFrameBlock = std::string::npos;

        if (fnPos != std::string::npos && (framePos == std::string::npos || fnPos < framePos)) {
            size_t colon = jsonContent.find(":", fnPos);
            size_t q1 = jsonContent.find("\"", colon);
            size_t q2 = (q1 != std::string::npos) ? jsonContent.find("\"", q1 + 1) : std::string::npos;
            if (q1 != std::string::npos && q2 != std::string::npos) {
                frameName = jsonContent.substr(q1 + 1, q2 - (q1 + 1));
            }
            currentFrameBlock = jsonContent.find("\"frame\"", q2 != std::string::npos ? q2 : fnPos);
            pos = (currentFrameBlock != std::string::npos) ? currentFrameBlock : fnPos + 10;
        } else {
            size_t nameEnd = jsonContent.rfind(".png\"", framePos);
            if (nameEnd != std::string::npos) {
                size_t nameStart = jsonContent.rfind("\"", nameEnd - 1);
                if (nameStart != std::string::npos) {
                    frameName = jsonContent.substr(nameStart + 1, (nameEnd + 4) - (nameStart + 1));
                }
            }
            currentFrameBlock = framePos;
            pos = framePos + 7;
        }

        if (frameName.empty() || currentFrameBlock == std::string::npos) {
            pos += 7;
            continue;
        }

        auto getVal = [&](const std::string& key) -> float {
            size_t kPos = jsonContent.find("\"" + key + "\"", currentFrameBlock);
            if (kPos == std::string::npos || kPos - currentFrameBlock > 300) return 0.0f;
            size_t colon = jsonContent.find(":", kPos);
            if (colon == std::string::npos) return 0.0f;
            try { return std::stof(jsonContent.substr(colon + 1)); } catch (...) { return 0.0f; }
        };

        float fx = getVal("x");
        float fy = getVal("y");
        float fw = getVal("w");
        float fh = getVal("h");

        if (fw > 0.0f && fh > 0.0f) {
            AtlasFrame af;
            af.name = frameName;
            af.atlas = "GJ_WebSheet";
            af.x = fx / atlasScale;
            af.y = fy / atlasScale;
            af.w = fw / atlasScale;
            af.h = fh / atlasScale;
            af.u0 = fx / jsonW;
            af.v0 = fy / jsonH;
            af.u1 = (fx + fw) / jsonW;
            af.v1 = (fy + fh) / jsonH;

            frames[frameName] = af;
            if (frameName.size() > 4 && frameName.substr(frameName.size() - 4) == ".png") {
                frames[frameName.substr(0, frameName.size() - 4)] = af;
            } else {
                frames[frameName + ".png"] = af;
            }
        }
    }
    squareFrame = findAtlasFrame("square.png");
}

const AtlasFrame* AtlasManager::findAtlasFrame(const std::string& frameName) {
    if (squareFrame && (frameName == "square.png" || frameName == "square")) {
        return squareFrame;
    }

    auto it = frames.find(frameName);
    if (it != frames.end()) return &it->second;

    if (frameName.size() > 4 && frameName.compare(frameName.size() - 4, 4, ".png") == 0) {
        auto it2 = frames.find(frameName.substr(0, frameName.size() - 4));
        if (it2 != frames.end()) return &it2->second;
    } else {
        auto it3 = frames.find(frameName + ".png");
        if (it3 != frames.end()) return &it3->second;
    }
    return nullptr;
}

const AtlasFrame* findAtlasFrame(const std::string& frameName) {
    return AtlasManager::findAtlasFrame(frameName);
}

LayeredSprite addImageFromAtlas(float x, float y, const std::string& frameName) {
    return createLayeredSprite(x, y, frameName, 0, true);
}

void drawAtlasFrame(const std::string& frameName, float x, float y,
                    float w, float h, float rotation,
                    float r, float g, float b, float a,
                    bool flipX, bool flipY,
                    BlendMode blend)
{
    const AtlasFrame* frame = (AtlasManager::squareFrame && (frameName == "square.png" || frameName == "square"))
    ? AtlasManager::squareFrame
    : findAtlasFrame(frameName);
    uint32_t texID = 0;
    float drawW = w;
    float drawH = h;
    AtlasFrame dummyFrame;

    if (frame) {
        if (frame->textureId != 0) {
            texID = frame->textureId;
        } else if (!frame->atlas.empty()) {
            auto itA = BootScene::textures.find(frame->atlas);
            if (itA != BootScene::textures.end()) texID = itA->second.id;
        }
        if (texID == 0) {
            auto itWs = BootScene::textures.find("GJ_WebSheet");
            if (itWs != BootScene::textures.end()) texID = itWs->second.id;
        }
        if (texID == 0) return;
        if (drawW == 0.0f) drawW = frame->w;
        if (drawH == 0.0f) drawH = frame->h;
    } else {
        std::string key = frameName;
        auto it = BootScene::textures.find(key);
        if (it == BootScene::textures.end() && key.size() > 4 && key.substr(key.size() - 4) == ".png") {
            key = key.substr(0, key.size() - 4);
            it = BootScene::textures.find(key);
        }
        if (it == BootScene::textures.end()) {
            key = frameName + ".png";
            it = BootScene::textures.find(key);
        }

        if (it != BootScene::textures.end()) {
            texID = it->second.id;
            dummyFrame.name = frameName;
            dummyFrame.u0 = 0.0f; dummyFrame.v0 = 0.0f;
            dummyFrame.u1 = 1.0f; dummyFrame.v1 = 1.0f;
            if (drawW == 0.0f) drawW = (float)it->second.width;
            if (drawH == 0.0f) drawH = (float)it->second.height;
            frame = &dummyFrame;
        } else {
            return;
        }
    }

    batchAtlasFrame(texID, frame, x, y, drawW, drawH, rotation, r, g, b, a, flipX, flipY, blend);
}

void drawScale9(const std::string& textureKey, float x, float y, float w, float h, float cornerSize,
                float r, float g, float b, float a)
{
    std::string key = textureKey;
    auto it = BootScene::textures.find(key);
    if (it == BootScene::textures.end() && key.size() > 4 && key.substr(key.size() - 4) == ".png") {
        key = key.substr(0, key.size() - 4);
        it = BootScene::textures.find(key);
    }
    if (it == BootScene::textures.end()) {
        key = textureKey + ".png";
        it = BootScene::textures.find(key);
    }

    uint32_t texID = 0;
    float texW = 0.0f, texH = 0.0f;
    float uBase0 = 0.0f, vBase0 = 0.0f, uBase1 = 1.0f, vBase1 = 1.0f;

    if (it != BootScene::textures.end()) {
        texID = it->second.id;
        texW = (float)it->second.width;
        texH = (float)it->second.height;
    } else {
        const AtlasFrame* af = findAtlasFrame(textureKey);
        if (af) {
            if (af->textureId != 0) {
                texID = af->textureId;
            } else if (!af->atlas.empty()) {
                auto itA = BootScene::textures.find(af->atlas);
                if (itA != BootScene::textures.end()) texID = itA->second.id;
            }
            if (texID == 0) {
                auto itWs = BootScene::textures.find("GJ_WebSheet");
                if (itWs != BootScene::textures.end()) texID = itWs->second.id;
            }
            if (texID != 0) {
                texW = af->w;
                texH = af->h;
                uBase0 = af->u0; vBase0 = af->v0;
                uBase1 = af->u1; vBase1 = af->v1;
            } else {
                return;
            }
        } else {
            return;
        }
    }

    if (texW <= 0.0f || texH <= 0.0f || w <= 0.0f || h <= 0.0f) return;

    // Corner ratio for square04_001 and GJ_square02 is 0.325 of texture width (52px for 160px)
    const float cornerRatio = 0.325f;

    float cSize = cornerSize;
    if (cSize <= 0.0f) {
        cSize = cornerRatio * 160.0f; // 52px standard 1:1
    } else if (cSize <= 1.0f) {
        cSize = cSize * 160.0f;
    }

    float cW = std::min(cSize, w * 0.5f);
    float cH = std::min(cSize, h * 0.5f);

    // Screen coordinates (9 quadrants corresponding to table4 in JS)
    float px[4] = {
        x - w * 0.5f,
        x - w * 0.5f + cW,
        x + w * 0.5f - cW,
        x + w * 0.5f
    };
    float py[4] = {
        y - h * 0.5f,
        y - h * 0.5f + cH,
        y + h * 0.5f - cH,
        y + h * 0.5f
    };

    // Universal UV coordinates (compatible with textures of any resolution)
    float uSpan = uBase1 - uBase0;
    float vSpan = vBase1 - vBase0;
    float pu[4] = {
        uBase0,
        uBase0 + uSpan * cornerRatio,
        uBase1 - uSpan * cornerRatio,
        uBase1
    };
    float pv[4] = {
        vBase0,
        vBase0 + vSpan * cornerRatio,
        vBase1 - vSpan * cornerRatio,
        vBase1
    };

    BlendMode curBlend = RenderDevice::get().getBlendMode();
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            RenderDevice::get().batchAxisAlignedQuad(
                texID,
                px[col],     py[row],
                px[col + 1], py[row + 1],
                pu[col],     pv[row],
                pu[col + 1], pv[row + 1],
                r, g, b, a, curBlend
            );
        }
    }
}
