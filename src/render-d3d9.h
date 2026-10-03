#pragma once
#include <cstdint>
#include <cstddef>
#include <SDL2/SDL.h>
#include "constants.h"

struct D3DVertex;
struct PreparedTexture;

bool d3d9_init(SDL_Window* window, int windowW, int windowH, bool vsync);
void d3d9_shutdown();
void d3d9_setViewport(int vpX, int vpY, int vpW, int vpH);
void d3d9_setVSync(bool enabled);
void d3d9_beginFrame();
void d3d9_clear(float r, float g, float b, float a);
void d3d9_endFrame();
void d3d9_flushBatch(const D3DVertex* buffer, size_t count, uint32_t currentTexID);
void d3d9_drawCircle(float cx, float cy, float radius, uint32_t color, bool filled, int segments, float transX, float transY, float scaleX, float scaleY);
void d3d9_drawTriangleStrip(const float* coordsXY, const float* colorsRGBA, size_t vertCount, bool additive, float transX, float transY, float scaleX, float scaleY);
void d3d9_drawRepeatedBackground(uint32_t texID, float uvOffsetX, float uvOffsetY, float uvW, float uvH, float logicalW, float logicalH, float bgR, float bgG, float bgB, float scaleX, float scaleY);
uint32_t d3d9_registerTexture(uint32_t handle, const PreparedTexture& t);
void d3d9_reloadTexture(uint32_t handle, const PreparedTexture& t);
