#include "win-effects.h"
#include "level-data-helpers.h"
#include "render-device.h"
#include <algorithm>
#include <cmath>

std::vector<ExpandingRing> WinEffects::rings;
std::vector<WinParticle> WinEffects::particles;

void WinEffects::drawExpandingRing(float x, float y, float startR, float endR,
                                   float durationMs, bool filled, bool pulse, unsigned int color,
                                   float delayMs)
{
    ExpandingRing r;
    r.x = x;
    r.y = y;
    r.startR = startR;
    r.endR = endR;
    r.duration = durationMs / 1000.0f;
    r.elapsed = 0.0f;
    r.delay = delayMs / 1000.0f;
    r.filled = filled;
    r.pulse = pulse;
    r.color = color;
    r.done = false;
    rings.push_back(r);
}

void WinEffects::spawnFinishParticles(unsigned int color1, unsigned int color2, float delayMs) {
    float px = 200.0f + (screenWidth - 400.0f) * ((rand() % 1000) / 1000.0f);
    float py = 200.0f + 240.0f * ((rand() % 1000) / 1000.0f);

    drawExpandingRing(px, py, 40.0f, 140.0f + 60.0f * ((rand() % 100) / 100.0f), 500.0f, true, true, color2, delayMs);

    for (int i = 0; i < 25; ++i) {
        float angle = ((rand() % 360) * 3.14159265f) / 180.0f;
        float speed = 520.0f + 400.0f * ((rand() % 100) / 100.0f);

        WinParticle p;
        p.x = px + ((rand() % 41) - 20.0f);
        p.y = py + ((rand() % 41) - 20.0f);
        p.vx = std::cos(angle) * speed;
        p.vy = std::sin(angle) * speed;
        p.startScale = 0.4f;
        p.endScale = 0.13f;
        p.maxLife = (100.0f + (rand() % 401)) / 1000.0f;
        p.life = 0.0f;
        p.delay = delayMs / 1000.0f;
        p.color = color1;
        p.done = false;
        particles.push_back(p);
    }
}

void WinEffects::spawnStarParticles(float x, float y, int count) {
    for (int i = 0; i < count; ++i) {
        float angle = ((rand() % 360) * 3.14159265f) / 180.0f;
        float speed = 200.0f + (rand() % 401);

        WinParticle p;
        p.x = x;
        p.y = y;
        p.vx = std::cos(angle) * speed;
        p.vy = std::sin(angle) * speed;
        p.startScale = 0.5f;
        p.endScale = 0.0f;
        p.maxLife = (200.0f + (rand() % 401)) / 1000.0f;
        p.life = 0.0f;
        p.delay = 0.0f;
        p.color = 16776960;
        p.done = false;
        particles.push_back(p);
    }
}

void WinEffects::update(float dt) {
    if (rings.empty() && particles.empty()) return;
    for (auto& r : rings) {
        if (r.delay > 0.0f) { r.delay -= dt; continue; }
        r.elapsed += dt;
        if (r.elapsed >= r.duration) r.done = true;
    }
    rings.erase(std::remove_if(rings.begin(), rings.end(), [](const ExpandingRing& r) { return r.done; }), rings.end());

    for (auto& p : particles) {
        if (p.delay > 0.0f) { p.delay -= dt; continue; }
        p.life += dt;
        p.x += p.vx * dt;
        p.y += p.vy * dt;
        if (p.life >= p.maxLife) p.done = true;
    }
    particles.erase(std::remove_if(particles.begin(), particles.end(), [](const WinParticle& p) { return p.done; }), particles.end());
}

void WinEffects::render() {
    if (rings.empty() && particles.empty()) return;

    for (const auto& r : rings) {
        if (r.delay > 0.0f || r.done) continue;

        float p = std::min(r.elapsed / r.duration, 1.0f);
        float curR = r.startR + (r.endR - r.startR) * p;
        float alpha = r.pulse ? ((p < 0.5f) ? (2.0f * p) : (2.0f * (1.0f - p))) : (1.0f - p);
        alpha = std::max(0.0f, alpha);

        float red   = ((r.color >> 16) & 0xFF) / 255.0f;
        float green = ((r.color >> 8)  & 0xFF) / 255.0f;
        float blue  =  (r.color        & 0xFF) / 255.0f;

        RenderDevice::get().drawCircle(r.x, r.y, curR, red, green, blue, alpha, r.filled, BLEND_ADD);
    }

    for (const auto& p : particles) {
        if (p.delay > 0.0f || p.done) continue;

        float t = (p.maxLife > 0.0f) ? std::min(p.life / p.maxLife, 1.0f) : 1.0f;
        float scale = p.startScale + (p.endScale - p.startScale) * t;
        float alpha = 1.0f - t;

        float red   = ((p.color >> 16) & 0xFF) / 255.0f;
        float green = ((p.color >> 8)  & 0xFF) / 255.0f;
        float blue  =  (p.color        & 0xFF) / 255.0f;

        drawAtlasFrame("square.png", p.x, p.y, 20.0f * scale, 20.0f * scale, 0.0f, red, green, blue, alpha);
    }
    applyBlendMode(BLEND_NORMAL);
}

void WinEffects::reset() {
    rings.clear();
    particles.clear();
}
