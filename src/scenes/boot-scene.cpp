#include "boot-scene.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include "level/level-data-helpers.h"
#include "assets/asset-loader.h"

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
        {"GJ_GameSheet",       "assets/Resources/GJ_GameSheet-hd.png",        true},
        {"GJ_GameSheetPlist",  "assets/Resources/GJ_GameSheet-hd.plist",      false},
        {"GJ_GameSheet02",     "assets/Resources/GJ_GameSheet02-hd.png",      true},
        {"GJ_GameSheet02Plist","assets/Resources/GJ_GameSheet02-hd.plist",    false},
        {"GJ_GameSheet03",     "assets/Resources/GJ_GameSheet03-hd.png",      true},
        {"GJ_GameSheet03Plist","assets/Resources/GJ_GameSheet03-hd.plist",    false},
        {"GJ_GameSheetGlow",   "assets/Resources/GJ_GameSheetGlow-hd.png",    true},
        {"GJ_GameSheetGlowPlist","assets/Resources/GJ_GameSheetGlow-hd.plist",false},
        {"GJ_LaunchSheet",     "assets/Resources/GJ_LaunchSheet-hd.png",      true},
        {"GJ_LaunchSheetPlist","assets/Resources/GJ_LaunchSheet-hd.plist",    false},
        {"bigFont",            "assets/bigFont.png",                          true},
        {"bigFontFnt",         "assets/bigFont.fnt",                          false},
        {"goldFont",           "assets/goldFont.png",                         true},
        {"goldFontFnt",        "assets/goldFont.fnt",                         false},
        {"game_bg_01",         "assets/game_bg_01_001.png",                   true},
        {"sliderBar",          "assets/Resources/sliderBar.png",              true},
        {"slidergroove",       "assets/Resources/slidergroove.png",           true},
        {"sliderthumb",        "assets/Resources/sliderthumb.png",            true},
        {"sliderthumbsel",     "assets/Resources/sliderthumbsel.png",         true},
        {"GJ_progressBar_001", "assets/Resources/GJ_progressBar_001.png",     true},
        {"groundSquare_01_001","assets/Resources/groundSquare_01_001-hd.png",  true},
        {"square",             "assets/Resources/square.png",                 true},
        {"square04_001",       "assets/Resources/square04_001.png",           true},
        {"GJ_square02",        "assets/Resources/GJ_square02.png",            true},
        {"level_1",            "assets/1.txt",                                false}
    };

    for (size_t i = 0; i < tasks.size(); ++i) {
        if (tasks[i].isTexture) {
            Texture tex = loadTexture(tasks[i].key, tasks[i].path);
            textures[tasks[i].key] = tex;
            textures[tasks[i].key + ".png"] = tex;
            if (tasks[i].key == "groundSquare_01_001") {
                textures["groundSquare_01_001-hd"] = tex;
                textures["groundSquare_01_001-hd.png"] = tex;
            }
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

    // Load sprite sheets from Cocos2d plist files
    struct SheetPair {
        std::string plistKey;
        std::string texKey;
    };
    std::vector<SheetPair> sheets = {
        {"GJ_GameSheetPlist",     "GJ_GameSheet"},
        {"GJ_GameSheet02Plist",   "GJ_GameSheet02"},
        {"GJ_GameSheet03Plist",   "GJ_GameSheet03"},
        {"GJ_GameSheetGlowPlist", "GJ_GameSheetGlow"},
        {"GJ_LaunchSheetPlist",   "GJ_LaunchSheet"}
    };

    for (const auto& s : sheets) {
        auto itP = textCache.find(s.plistKey);
        if (itP != textCache.end() && !itP->second.empty()) {
            int tw = 0, th = 0;
            auto itT = textures.find(s.texKey);
            if (itT != textures.end()) {
                tw = itT->second.width;
                th = itT->second.height;
            }
            AtlasManager::loadAtlasPlist(itP->second, s.texKey, tw, th);
        }
    }

    // Register synthetic AtlasFrame for standalone ground texture
    auto itG = textures.find("groundSquare_01_001");
    if (itG != textures.end() && itG->second.id != 0) {
        AtlasFrame gndAf;
        gndAf.name = "groundSquare_01_001.png";
        gndAf.atlas = "groundSquare_01_001";
        gndAf.textureId = itG->second.id;
        gndAf.w = 180.0f;
        gndAf.h = 180.0f;
        gndAf.x = 0.0f;
        gndAf.y = 0.0f;
        gndAf.offsetX = 0.0f;
        gndAf.offsetY = 0.0f;
        gndAf.rotated = false;
        gndAf.u0 = 0.0f;
        gndAf.v0 = 0.0f;
        gndAf.u1 = 1.0f;
        gndAf.v1 = 1.0f;
        AtlasManager::frames["groundSquare_01_001"] = gndAf;
        AtlasManager::frames["groundSquare_01_001.png"] = gndAf;
        AtlasManager::frames["groundSquare_01_001-hd"] = gndAf;
        AtlasManager::frames["groundSquare_01_001-hd.png"] = gndAf;
    }

    std::cout << "[BootScene] Preload completed successfully (" << textures.size() << " texture entries registered, "
              << AtlasManager::frames.size() << " atlas frames loaded from plists)." << std::endl;
}
