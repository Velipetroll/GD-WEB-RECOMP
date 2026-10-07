#include "color-manager.h"
#include <vector>

ColorManager::ColorManager() {
    reset();
}

ColorManager::~ColorManager() {}

void ColorManager::reset() {
    _colors.clear();
    _actions.clear();

    // Default colors (Stereo Madness)
    _colors[COLOR_BG]     = {0, 102, 255};
    _colors[COLOR_GROUND] = {0,  68, 170};
}

void ColorManager::triggerColor(int channelId, const ColorRGB& targetColor, float duration) {
    ColorRGB startColor = getColor(channelId);

    if (duration <= 0.0f) {
        _colors[channelId] = targetColor;
        _actions.erase(channelId);
    } else {
        _actions[channelId] = TweenValue(startColor, targetColor, duration);
    }
}

void ColorManager::step(float dt) {
    if (_actions.empty()) return;
    std::vector<int> finished;

    for (auto& pair : _actions) {
        int channel = pair.first;
        TweenValue& tween = pair.second;

        tween.step(dt);
        _colors[channel] = tween.current;

        if (tween.done) {
            finished.push_back(channel);
        }
    }

    for (int channel : finished) {
        _actions.erase(channel);
    }
}

ColorRGB ColorManager::getColor(int channelId) const {
    auto it = _colors.find(channelId);
    if (it != _colors.end()) {
        return it->second;
    }
    return {255, 255, 255};
}

unsigned int ColorManager::getHex(int channelId) const {
    ColorRGB c = getColor(channelId);
    return ((c.r & 0xFF) << 16) | ((c.g & 0xFF) << 8) | (c.b & 0xFF);
}

void ColorManager::getGLColor(int channelId, float& r, float& g, float& b) const {
    ColorRGB c = getColor(channelId);
    r = c.r / 255.0f;
    g = c.g / 255.0f;
    b = c.b / 255.0f;
}
