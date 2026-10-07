#pragma once
#include <string>
#include <vector>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <cmath>
#include <SDL2/SDL.h>
#include "render/render-device.h"

struct FpsOption {
    std::string label;
    int fps;
    bool vsync;
};

struct RendererOption {
    std::string label;
    RenderBackendType backend;
};

class Settings {
public:
    static Settings& get() {
        static Settings instance;
        return instance;
    }

    float musicVolume = 1.0f;
    float sfxVolume = 1.0f;
    int fpsIndex = 8; // Default: 240 FPS
    int qualityIndex = QUALITY_AUTO; // Default: Auto (detects GMA hardware)
    bool fullscreen = false;
    bool showFps = false;

    std::vector<std::string> qualityOptions = {
        "Auto", "Low", "Medium", "High"
    };

    std::vector<FpsOption> fpsOptions;

    void updateFpsOptions() {
        bool twoOptionsOnly = false;
#if defined(__EMSCRIPTEN__) || defined(__ANDROID__)
        twoOptionsOnly = true;
#else
        if (RenderDevice::get().getBackend() == RENDERER_D3D8 || currentBackend() == RENDERER_D3D8) {
            twoOptionsOnly = true;
        }
#endif
        if (twoOptionsOnly) {
            fpsOptions = {
                { "VSync",     0, true  },
                { "Unlimited", 0, false }
            };
        } else {
            fpsOptions = {
                { "VSync",      0,   true  }, // 0
                { "30 FPS",     30,  false }, // 1
                { "60 FPS",     60,  false }, // 2
                { "75 FPS",     75,  false }, // 3
                { "90 FPS",     90,  false }, // 4
                { "120 FPS",    120, false }, // 5
                { "144 FPS",    144, false }, // 6
                { "165 FPS",    165, false }, // 7
                { "240 FPS",    240, false }, // 8
                { "Unlimited",  0,   false }  // 9
            };
        }
        fpsIndex = std::clamp(fpsIndex, 0, (int)fpsOptions.size() - 1);
    }

    #if defined(_WIN32)
    #if !defined(_WIN64)
    // Windows 32-bit: Direct3D 8, Direct3D 9 and OpenGL 1.1
    std::vector<RendererOption> rendererOptions = {
        { "Direct3D 8",  RENDERER_D3D8   },
        { "Direct3D 9",  RENDERER_D3D9   },
        { "OpenGL 1.1",  RENDERER_OPENGL }
    };
    int rendererIndex = 0; // Default: Direct3D 8
    #else
    // Windows 64-bit: Direct3D 9 and OpenGL 1.1 (Direct3D 8 unavailable)
    std::vector<RendererOption> rendererOptions = {
        { "Direct3D 9",  RENDERER_D3D9   },
        { "OpenGL 1.1",  RENDERER_OPENGL }
    };
    int rendererIndex = 0; // Default: Direct3D 9
    #endif
    #else
    // Linux / Other: No renderer selector (fixed to OpenGL 1.1)
    std::vector<RendererOption> rendererOptions = {};
    int rendererIndex = 0;
    #endif

    const FpsOption& currentFps() const {
        int idx = std::clamp(fpsIndex, 0, (int)fpsOptions.size() - 1);
        return fpsOptions[idx];
    }

    void setFpsIndex(int idx) {
        fpsIndex = std::clamp(idx, 0, (int)fpsOptions.size() - 1);
        applyFpsSettings();
        save();
    }

    void applyFpsSettings() {
        const FpsOption& opt = currentFps();
        SDL_GL_SetSwapInterval(opt.vsync ? 1 : 0);
    }

    RenderBackendType currentBackend() const {
        #if defined(_WIN32)
        if (rendererOptions.empty()) return RENDERER_OPENGL;
        int idx = std::clamp(rendererIndex, 0, (int)rendererOptions.size() - 1);
        return rendererOptions[idx].backend;
        #else
        return RENDERER_OPENGL;
        #endif
    }

    const std::string& currentRendererLabel() const {
        static const std::string defLabel = "OpenGL 1.1";
        #if defined(_WIN32)
        if (rendererOptions.empty()) return defLabel;
        int idx = std::clamp(rendererIndex, 0, (int)rendererOptions.size() - 1);
        return rendererOptions[idx].label;
        #else
        return defLabel;
        #endif
    }

    void nextRenderer() {
        #if defined(_WIN32)
        if (rendererOptions.size() > 1) {
            rendererIndex = (rendererIndex + 1) % (int)rendererOptions.size();
            updateFpsOptions();
            save();
        }
        #endif
    }

    void prevRenderer() {
        #if defined(_WIN32)
        if (rendererOptions.size() > 1) {
            rendererIndex = (rendererIndex - 1 + (int)rendererOptions.size()) % (int)rendererOptions.size();
            updateFpsOptions();
            save();
        }
        #endif
    }

    void setRendererIndex(int idx) {
        #if defined(_WIN32)
        if (!rendererOptions.empty()) {
            rendererIndex = std::clamp(idx, 0, (int)rendererOptions.size() - 1);
            updateFpsOptions();
            save();
        }
        #endif
    }

    const std::string& currentQualityLabel() const {
        int idx = std::clamp(qualityIndex, 0, (int)qualityOptions.size() - 1);
        return qualityOptions[idx];
    }

    void nextQuality() {
        qualityIndex = (qualityIndex + 1) % (int)qualityOptions.size();
        gpu::presetRef() = qualityIndex;
        RenderDevice::get().reloadTextures();
        save();
    }

    void prevQuality() {
        qualityIndex = (qualityIndex - 1 + (int)qualityOptions.size()) % (int)qualityOptions.size();
        gpu::presetRef() = qualityIndex;
        RenderDevice::get().reloadTextures();
        save();
    }

    void setQualityIndex(int idx) {
        qualityIndex = std::clamp(idx, 0, (int)qualityOptions.size() - 1);
        gpu::presetRef() = qualityIndex;
        RenderDevice::get().reloadTextures();
        save();
    }

    static std::string getSettingsPath() {
#ifdef __ANDROID__
        const char* internalDir = SDL_AndroidGetInternalStoragePath();
        if (internalDir) return std::string(internalDir) + "/settings.cfg";
#endif
        return "settings.cfg";
    }

    void load() {
        std::ifstream file(getSettingsPath());
        if (!file.is_open()) {
            gpu::presetRef() = qualityIndex;
            save();
            return;
        }
        std::string line;
        while (std::getline(file, line)) {
            size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string key = line.substr(0, eq);
            std::string val = line.substr(eq + 1);

            key.erase(0, key.find_first_not_of(" \t\r\n"));
            key.erase(key.find_last_not_of(" \t\r\n") + 1);
            val.erase(0, val.find_first_not_of(" \t\r\n"));
            val.erase(val.find_last_not_of(" \t\r\n") + 1);

            try {
                if (key == "musicVolume") musicVolume = std::stof(val);
                else if (key == "sfxVolume") sfxVolume = std::stof(val);
                else if (key == "fpsIndex") fpsIndex = std::stoi(val);
                else if (key == "quality") qualityIndex = std::stoi(val);
                else if (key == "fullscreen") fullscreen = (val == "1" || val == "true");
                else if (key == "showFps") showFps = (val == "1" || val == "true");
                #if defined(_WIN32)
                else if (key == "rendererBackend" || key == "renderer") {
                    int b = std::stoi(val);
                    for (size_t i = 0; i < rendererOptions.size(); ++i) {
                        if ((int)rendererOptions[i].backend == b) {
                            rendererIndex = (int)i;
                            break;
                        }
                    }
                }
                #endif
            } catch (...) {}
        }
        fpsIndex = std::clamp(fpsIndex, 0, (int)fpsOptions.size() - 1);
        qualityIndex = std::clamp(qualityIndex, 0, (int)qualityOptions.size() - 1);
        gpu::presetRef() = qualityIndex;
        musicVolume = std::clamp(musicVolume, 0.0f, 1.0f);
        sfxVolume = std::clamp(sfxVolume, 0.0f, 1.0f);
        #if defined(_WIN32)
        if (!rendererOptions.empty()) {
            rendererIndex = std::clamp(rendererIndex, 0, (int)rendererOptions.size() - 1);
        }
        #endif
        updateFpsOptions();
    }

    void save() {
        std::ofstream file(getSettingsPath());
        if (!file.is_open()) return;
        file << "musicVolume=" << musicVolume << "\n";
        file << "sfxVolume=" << sfxVolume << "\n";
        file << "fpsIndex=" << fpsIndex << "\n";
        file << "quality=" << qualityIndex << "\n";
        file << "fullscreen=" << (fullscreen ? "1" : "0") << "\n";
        file << "showFps=" << (showFps ? "1" : "0") << "\n";
        #if defined(_WIN32)
        if (!rendererOptions.empty()) {
            file << "rendererBackend=" << (int)currentBackend() << "\n";
        }
        #endif
    }

private:
    Settings() {
        updateFpsOptions();
    }
};
