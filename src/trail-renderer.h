#pragma once
#include <vector>
#include <cmath>
#include <SDL2/SDL_opengl.h>
#include "constants.h"

struct TrailPoint {
    float x = 0.0f;
    float y = 0.0f;
    float state = 1.0f; // 1.0 (new) to 0.0 (faded)
};

class TrailRenderer {
public:
    TrailRenderer(float lifetime = 0.3f, float minSeg = 5.0f, float stroke = 4.0f,
                  float maxSeg = 150.0f, unsigned int color = 16777215, float opacity = 1.0f);
    ~TrailRenderer();

    void setPosition(float x, float y);
    void start();
    void stop();
    void reset();

    void update(float dt);
    void render(float cameraX, float cameraY);

private:
    unsigned int _color;
    float _opacity;
    float _fadeDelta;
    float _minSegSq;
    float _maxSeg;
    int _maxPoints;
    float _stroke;

    std::vector<TrailPoint> _pts;
    float _posX = 0.0f;
    float _posY = 0.0f;
    bool _posInit = false;
    bool _active = false;
};
