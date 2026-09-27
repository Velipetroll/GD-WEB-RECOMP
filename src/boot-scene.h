#pragma once
#include <string>
#include <unordered_map>
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>
#include "constants.h"

struct Texture {
    GLuint id = 0;
    int width = 0;
    int height = 0;
};

class BootScene {
public:
    BootScene();
    ~BootScene();

    void preload(SDL_Window* window);
    void create();

    static std::unordered_map<std::string, Texture> textures;
    static std::unordered_map<std::string, std::string> textCache;

private:
    void renderProgressBar(float progress);
    Texture loadTextureGL(const std::string& path);
    std::string loadTextFile(const std::string& path);
};
