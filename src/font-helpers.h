#pragma once
#include <string>
#include <unordered_map>
#include <SDL2/SDL_opengl.h>

struct BitmapChar {
    int id = 0;
    int x = 0, y = 0;
    int width = 0, height = 0;
    int xOffset = 0, yOffset = 0;
    int xAdvance = 0;
    float u0 = 0.0f, v0 = 0.0f;
    float u1 = 0.0f, v1 = 0.0f;
    std::unordered_map<int, int> kerning;
};

struct BitmapFont {
    std::string fontKey;
    int size = 0;
    int lineHeight = 0;
    GLuint textureID = 0;
    int texWidth = 0;
    int texHeight = 0;
    std::unordered_map<int, BitmapChar> chars;
    const BitmapChar* charLookup[256] = { nullptr }; // Acceso inmediato O(1)
};

void defineFontFromFnt(const std::string& fontKey, const std::string& fntText);
const BitmapFont* getFont(const std::string& fontKey);

void drawBitmapText(const std::string& fontKey, const std::string& text,
                    float x, float y, float scale = 1.0f,
                    float r = 1.0f, float g = 1.0f, float b = 1.0f, float a = 1.0f,
                    bool centerAlign = false);

void drawGenericText(const std::string& text, float x, float y, float size = 12.0f,
                     float r = 0.0f, float g = 0.0f, float b = 0.0f, float a = 1.0f,
                     int align = 0);
