#pragma once
#include <cstdint>
#include <cstddef>
#include <SDL2/SDL.h>
#include "constants.h"

struct GLVertex;
struct PreparedTexture;

bool gl1_init(SDL_Window* window, int windowW, int windowH, bool vsync);
void gl1_shutdown();
void gl1_setViewport(int vpX, int vpY, int vpW, int vpH, float logicalW, float logicalH);
void gl1_setVSync(bool enabled);
void gl1_beginFrame();
void gl1_clear(float r, float g, float b, float a);
void gl1_endFrame(SDL_Window* window);
void gl1_flushBatch(const GLVertex* buffer, size_t count, uint32_t currentTexID);
void gl1_drawCircle(float cx, float cy, float radius, float r, float g, float b, float a, bool filled, int segments, float transX, float transY, BlendMode blend);
void gl1_drawTriangleStrip(const float* coordsXY, const float* colorsRGBA, size_t vertCount, bool additive, float transX, float transY);
void gl1_drawRepeatedBackground(uint32_t texID, float uvOffsetX, float uvOffsetY, float uvW, float uvH, float logicalW, float logicalH, float bgR, float bgG, float bgB);
uint32_t gl1_registerTexture(const PreparedTexture& t, bool isBackground);
void gl1_reloadTexture(uint32_t handle, const PreparedTexture& t, bool isBackground);
