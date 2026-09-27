#pragma once
#include <vector>
#include <cmath>
#include <SDL2/SDL_opengl.h>
#include "constants.h"

struct ExpandingRing {
    float x = 0.0f;
    float y = 0.0f;
    float startR = 0.0f;
    float endR = 0.0f;
    float duration = 0.5f;
    float elapsed = 0.0f;
    float delay = 0.0f;
    bool filled = false;
    bool pulse = false;
    unsigned int color = 16777215;
    bool done = false;
};

struct WinParticle {
    float x = 0.0f, y = 0.0f;
    float vx = 0.0f, vy = 0.0f;
    float startScale = 0.4f, endScale = 0.13f;
    float life = 0.0f, maxLife = 0.5f;
    float delay = 0.0f;
    unsigned int color = 16777215;
    bool done = false;
};

class WinEffects {
public:
    static void drawExpandingRing(float x, float y, float startR, float endR,
                                  float durationMs, bool filled = false,
                                  bool pulse = false, unsigned int color = 16777215,
                                  float delayMs = 0.0f);

    static void spawnFinishParticles(unsigned int color1 = 16777215, unsigned int color2 = 16777215, float delayMs = 0.0f);
    static void spawnStarParticles(float x, float y, int count = 30);

    static void update(float dt);
    static void render();
    static void reset();

private:
    static std::vector<ExpandingRing> rings;
    static std::vector<WinParticle> particles;
};

void drawExpandingRing(float x, float y, float startR, float endR,
                       float durationMs, bool filled = false,
                       bool pulse = false, unsigned int color = 16777215,
                       float delayMs = 0.0f);

void spawnFinishParticles(unsigned int color1 = 16777215, unsigned int color2 = 16777215, float delayMs = 0.0f);
