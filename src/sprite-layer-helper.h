#pragma once
#include <string>
#include "constants.h"
#include "level-data-helpers.h"

struct LayeredSprite {
    std::string frameName = "";
    const AtlasFrame* cachedFrame = nullptr;
    uint32_t cachedTexID = 0;
    float x = 0.0f;
    float y = 0.0f;
    float offsetX = 0.0f;
    float offsetY = 0.0f;
    int depth = 0;
    bool visible = true;
    float rotation = 0.0f;
    float scaleX = 1.0f;
    float scaleY = 1.0f;
    float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;
    BlendMode blend = BLEND_NORMAL;

    void setPosition(float nx, float ny) { x = nx; y = ny; }
    void setOffset(float ox, float oy) { offsetX = ox; offsetY = oy; }
    void setAngle(float angle) { rotation = angle; }
    void setScale(float s) { scaleX = s; scaleY = s; }
    void setScale(float sx, float sy) { scaleX = sx; scaleY = sy; }
    void setTint(float nr, float ng, float nb) { r = nr; g = ng; b = nb; }
    void setAlpha(float na) { a = na; }
    void setVisible(bool v) { visible = v; }
    void setDepth(int d) { depth = d; }
    void setBlendMode(BlendMode m) { blend = m; }

    void render(float extraOffsetX = 0.0f, float extraOffsetY = 0.0f);
};

LayeredSprite createLayeredSprite(float x, float y, const std::string& frameName, int depth = 0, bool visible = true);
