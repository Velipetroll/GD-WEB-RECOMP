#pragma once
#include <unordered_map>
#include "tween-value.h"

class ColorManager {
public:
    static constexpr int COLOR_BG = 1000;
    static constexpr int COLOR_GROUND = 1001;

    ColorManager();
    ~ColorManager();

    void reset();
    void triggerColor(int channelId, const ColorRGB& targetColor, float duration);
    void step(float dt);

    ColorRGB getColor(int channelId) const;
    unsigned int getHex(int channelId) const;

    // Normalized RGB values (0.0f - 1.0f) for OpenGL
    void getGLColor(int channelId, float& r, float& g, float& b) const;

private:
    std::unordered_map<int, ColorRGB> _colors;
    std::unordered_map<int, TweenValue> _actions;
};
