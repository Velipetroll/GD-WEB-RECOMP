#pragma once
#include <cmath>
#include <algorithm>

struct ColorRGB {
    int r = 255;
    int g = 255;
    int b = 255;
};

class TweenValue {
public:
    ColorRGB from;
    ColorRGB to;
    float duration;
    float elapsed;
    bool done;
    ColorRGB current;

    TweenValue()
    : duration(0.0f), elapsed(0.0f), done(true)
    {
        from = {255, 255, 255};
        to = {255, 255, 255};
        current = to;
    }

    TweenValue(const ColorRGB& fromVal, const ColorRGB& toVal, float dur)
    : from(fromVal), to(toVal), duration(dur), elapsed(0.0f), done(dur <= 0.0f)
    {
        current = (dur <= 0.0f) ? toVal : fromVal;
    }

    void step(float dt) {
        if (done) return;
        elapsed += dt;

        float t = (duration > 0.0f) ? std::min(elapsed / duration, 1.0f) : 1.0f;

        if (t >= 1.0f) {
            current = to;
            done = true;
        } else {
            current.r = (int)std::round(from.r + (to.r - from.r) * t);
            current.g = (int)std::round(from.g + (to.g - from.g) * t);
            current.b = (int)std::round(from.b + (to.b - from.b) * t);
        }
    }
};
