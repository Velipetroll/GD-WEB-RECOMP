#include "boot-scene.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include "level-data-helpers.h"

// Forward declaration from font-helpers
void defineFontFromFnt(const std::string& fontKey, const std::string& fntText);

std::unordered_map<std::string, Texture> BootScene::textures;
std::unordered_map<std::string, std::string> BootScene::textCache;

BootScene::BootScene() {}
BootScene::~BootScene() {}

std::string BootScene::loadTextFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "[BootScene] Failed to open file: " << path << std::endl;
        return "";
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

Texture BootScene::loadTextureGL(const std::string& path) {
    Texture tex;
    int channels;
    stbi_set_flip_vertically_on_load(false);
    unsigned char* data = stbi_load(path.c_str(), &tex.width, &tex.height, &channels, 4);
    if (!data) {
        std::cerr << "[BootScene] Failed to load texture: " << path << std::endl;
        return tex;
    }

    glGenTextures(1, &tex.id);
    glBindTexture(GL_TEXTURE_2D, tex.id);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tex.width, tex.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    stbi_image_free(data);
    return tex;
}

void BootScene::renderProgressBar(float progress) {
    glClear(GL_COLOR_BUFFER_BIT);

    float barTotalWidth = 0.6f * screenWidth;
    float barHeight = 8.0f;
    float currentWidth = barTotalWidth * progress;

    float centerX = screenWidth * 0.5f;
    float centerY = screenHeight * 0.5f;

    float startX = centerX - (barTotalWidth * 0.5f);
    float startY = centerY - (barHeight * 0.5f);

    RenderDevice::get().drawRect(startX, startY, currentWidth, barHeight, 0.0f, 1.0f, 0.0f, 1.0f);
}

void BootScene::preload(SDL_Window* window) {
    struct AssetTask {
        std::string key;
        std::string path;
        bool isTexture;
    };

    std::vector<AssetTask> tasks = {
        {"GJ_WebSheet",    "assets/GJ_WebSheet.png",     true},
        {"bigFont",        "assets/bigFont.png",         true},
        {"bigFontFnt",     "assets/bigFont.fnt",         false},
        {"goldFont",       "assets/goldFont.png",        true},
        {"goldFontFnt",    "assets/goldFont.fnt",        false},
        {"game_bg_01",     "assets/game_bg_01_001.png",  true},
        {"sliderBar",      "assets/sliderBar.png",       true},
        {"square04_001",   "assets/square04_001.png",    true},
        {"GJ_square02",    "assets/GJ_square02.png",     true},
        {"GJ_WebSheetJson","assets/GJ_WebSheet.json",    false},
        {"level_1",        "assets/1.txt",               false}
    };

    for (size_t i = 0; i < tasks.size(); ++i) {
        if (tasks[i].isTexture) {
            textures[tasks[i].key] = loadTextureGL(tasks[i].path);
        } else {
            textCache[tasks[i].key] = loadTextFile(tasks[i].path);
        }

        float progress = (float)(i + 1) / (float)tasks.size();
        renderProgressBar(progress);
        SDL_GL_SwapWindow(window);
    }
}

void BootScene::create() {
    if (textCache.find("bigFontFnt") != textCache.end()) {
        defineFontFromFnt("bigFont", textCache["bigFontFnt"]);
    }
    if (textCache.find("goldFontFnt") != textCache.end()) {
        defineFontFromFnt("goldFont", textCache["goldFontFnt"]);
    }

    std::cout << "[BootScene] Preload completed successfully." << std::endl;
}
