#include "boot-scene.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include "level-data-helpers.h"
#include "asset-loader.h"

// Forward declaration from font-helpers
void defineFontFromFnt(const std::string& fontKey, const std::string& fntText);

std::unordered_map<std::string, Texture> BootScene::textures;
std::unordered_map<std::string, std::string> BootScene::textCache;

BootScene::BootScene() {}
BootScene::~BootScene() {}

std::string BootScene::loadTextFile(const std::string& path) {
    std::string text = loadAssetText(path);
    if (text.empty()) {
        std::cerr << "[BootScene] Failed to open file: " << path << std::endl;
    }
    return text;
}

Texture BootScene::loadTexture(const std::string& key, const std::string& path) {
    Texture tex;
    int channels = 0;
    stbi_set_flip_vertically_on_load(false);
    std::vector<uint8_t> bin = loadAssetBinary(path);
    if (bin.empty()) {
        std::cerr << "[BootScene] Failed to load texture file: " << path << std::endl;
        return tex;
    }
    unsigned char* data = stbi_load_from_memory(bin.data(), static_cast<int>(bin.size()), &tex.width, &tex.height, &channels, 4);
    if (!data) {
        std::cerr << "[BootScene] Failed to decode texture: " << path << std::endl;
        return tex;
    }

    uint32_t texID = RenderDevice::get().registerTexture(key, tex.width, tex.height, data);
    tex.id = (GLuint)texID;
    stbi_image_free(data);
    return tex;
}

void BootScene::renderProgressBar(float progress) {
    RenderDevice::get().beginFrame();
    RenderDevice::get().clear(0.0f, 0.0f, 0.0f, 1.0f);

    float barTotalWidth = 0.6f * screenWidth;
    float barHeight = 8.0f;
    float currentWidth = barTotalWidth * progress;

    float centerX = screenWidth * 0.5f;
    float centerY = screenHeight * 0.5f;

    float startX = centerX - (barTotalWidth * 0.5f);
    float startY = centerY - (barHeight * 0.5f);

    RenderDevice::get().drawRect(startX, startY, currentWidth, barHeight, 0.0f, 1.0f, 0.0f, 1.0f);
    RenderDevice::get().endFrame();
}

void BootScene::preload(SDL_Window* window) {
    (void)window;
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
        {"level_1",        "assets/1.txt"    ,               false}
    };

    for (size_t i = 0; i < tasks.size(); ++i) {
        if (tasks[i].isTexture) {
            Texture tex = loadTexture(tasks[i].key, tasks[i].path);
            textures[tasks[i].key] = tex;
            textures[tasks[i].key + ".png"] = tex;
            if (tasks[i].key == "game_bg_01") {
                textures["game_bg_01_001"] = tex;
                textures["game_bg_01_001.png"] = tex;
            }
        } else {
            textCache[tasks[i].key] = loadTextFile(tasks[i].path);
        }

        float progress = (float)(i + 1) / (float)tasks.size();
        renderProgressBar(progress);
    }
}

void BootScene::create() {
    if (textCache.find("bigFontFnt") != textCache.end()) {
        defineFontFromFnt("bigFont", textCache["bigFontFnt"]);
    }
    if (textCache.find("goldFontFnt") != textCache.end()) {
        defineFontFromFnt("goldFont", textCache["goldFontFnt"]);
    }

    std::cout << "[BootScene] Preload completed successfully (" << textures.size() << " texture entries registered)." << std::endl;
}
