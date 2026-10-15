#pragma once
#include <cstdint>
#include <cstddef>
#include <SDL2/SDL.h>
#include "constants.h"

struct GLVertex;
struct PreparedTexture;

#if defined(__EMSCRIPTEN__) || defined(__ANDROID__)

bool webgl_init(SDL_Window* window, int windowW, int windowH, bool vsync);
void webgl_shutdown();
void webgl_setViewport(int vpX, int vpY, int vpW, int vpH, float logicalW, float logicalH);
void webgl_setVSync(bool enabled);
void webgl_beginFrame();
void webgl_clear(float r, float g, float b, float a);
void webgl_endFrame();
void webgl_flushBatch(const GLVertex* buffer, size_t count, uint32_t currentTexID);
void webgl_drawCircle(float cx, float cy, float radius, float r, float g, float b, float a, bool filled, int segments, float transX, float transY, BlendMode blend);
void webgl_drawTriangleStrip(const float* coordsXY, const float* colorsRGBA, size_t vertCount, bool additive, float transX, float transY);
void webgl_drawRepeatedBackground(uint32_t texID, float uvOffsetX, float uvOffsetY, float uvW, float uvH, float logicalW, float logicalH, float bgR, float bgG, float bgB);
uint32_t webgl_registerTexture(const PreparedTexture& t, bool isBackground);
void webgl_reloadTexture(uint32_t handle, const PreparedTexture& t, bool isBackground);

#endif
