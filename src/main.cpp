#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>
#include <iostream>
#include <algorithm>
#include <memory>
#include <fstream>
#include <vector>
#include <cstring>
#include <cstdint>
#include <string>

#include "stb_image.h"
#include "constants.h"
#include "boot-scene.h"
#include "level-data-helpers.h"
#include "render-device.h"
#include "game-scene.h"
#include "win-effects.h"
#include "settings.h"

static void setWindowIcon(SDL_Window* window) {
    stbi_set_flip_vertically_on_load(false);
    int w = 0, h = 0, channels = 0;
    unsigned char* pixels = stbi_load("assets/icon.png", &w, &h, &channels, 4);

    if (pixels) {
        SDL_Surface* iconSurface = SDL_CreateRGBSurfaceWithFormatFrom(
            pixels, w, h, 32, w * 4, SDL_PIXELFORMAT_RGBA32
        );
        if (iconSurface) {
            SDL_SetWindowIcon(window, iconSurface);
            SDL_FreeSurface(iconSurface);
        }
        stbi_image_free(pixels);
    }
}

void updateViewport(int windowWidth, int windowHeight, SDL_Window* window = nullptr) {
    (void)window;
    if (windowHeight <= 0) windowHeight = 1;

    float windowAspect = (float)windowWidth / (float)windowHeight;
    int newLogicalWidth = (int)std::round(screenHeight * windowAspect);
    setScreenWidth(newLogicalWidth);

    RenderDevice::get().setViewport(0, 0, windowWidth, windowHeight, (float)screenWidth, (float)screenHeight);
}

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>

struct EmscriptenContext {
    SDL_Window* window = nullptr;
    std::unique_ptr<GameScene> gameScene;
    int winW = 0;
    int winH = 0;
    Uint64 lastTime = 0;
    Uint64 timerFreq = 0;
};

static EmscriptenContext g_emCtx;

static void emscriptenFrame() {
    int curW = 0, curH = 0;
    SDL_GetWindowSize(g_emCtx.window, &curW, &curH);
    if (curW > 0 && curH > 0 && (curW != g_emCtx.winW || curH != g_emCtx.winH)) {
        g_emCtx.winW = curW;
        g_emCtx.winH = curH;
        updateViewport(curW, curH, g_emCtx.window);
    }
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_WINDOWEVENT) {
            if (event.window.event == SDL_WINDOWEVENT_RESIZED ||
                event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
                event.window.event == SDL_WINDOWEVENT_MAXIMIZED ||
                event.window.event == SDL_WINDOWEVENT_RESTORED)
            {
                SDL_GetWindowSize(g_emCtx.window, &g_emCtx.winW, &g_emCtx.winH);
                updateViewport(g_emCtx.winW, g_emCtx.winH, g_emCtx.window);
            }
        } else {
            g_emCtx.gameScene->handleEvent(event, g_emCtx.winW, g_emCtx.winH, g_emCtx.window);
        }
    }

    Uint64 frameStartTime = SDL_GetPerformanceCounter();
    float dt = (float)(frameStartTime - g_emCtx.lastTime) / (float)g_emCtx.timerFreq;
    g_emCtx.lastTime = frameStartTime;

    if (dt > 0.1f) dt = 0.1f;

    g_emCtx.gameScene->update(dt);
    WinEffects::update(dt);

    RenderDevice::get().beginFrame();
    g_emCtx.gameScene->render();
    RenderDevice::get().endFrame();
}
#endif

int main(int argc, char* argv[]) {
    Settings::get().load();

    RenderBackendType selectedBackend = Settings::get().currentBackend();

    #ifdef _WIN32
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-opengl" || arg == "-gl" || arg == "--opengl") {
            selectedBackend = RENDERER_OPENGL;
        } else if (arg == "-d3d8" || arg == "-dx8" || arg == "--d3d8" || arg == "--dx8") {
            #ifndef _WIN64
            selectedBackend = RENDERER_D3D8;
            #else
            std::cout << "[Main] DirectX 8 does not exist in 64-bit mode. Using DirectX 9.\n";
            selectedBackend = RENDERER_D3D9;
            #endif
        } else if (arg == "-d3d9" || arg == "-dx9" || arg == "--d3d9" || arg == "--dx9") {
            selectedBackend = RENDERER_D3D9;
        }
    }
    #else
    selectedBackend = RENDERER_OPENGL;
    #endif

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) < 0) {
        std::cerr << "[Main] Error in SDL_Init: " << SDL_GetError() << std::endl;
        return -1;
    }

    #ifdef _WIN32
    Uint32 windowFlags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN;
    if (selectedBackend == RENDERER_OPENGL) {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 1);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        windowFlags |= SDL_WINDOW_OPENGL;
    }
    const char* winTitle = "Geometry Dash - Play Level 1";
    #elif defined(__EMSCRIPTEN__)
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    Uint32 windowFlags = SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN;
    const char* winTitle = "Geometry Dash - Play Level 1";
    #else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    Uint32 windowFlags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN;
    const char* winTitle = "Geometry Dash - Play Level 1";
    #endif

    SDL_Window* window = SDL_CreateWindow(
        winTitle,
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        screenWidth, screenHeight,
        windowFlags
    );

    if (!window) {
        std::cerr << "[Main] Could not create window: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return -1;
    }

    if (Settings::get().fullscreen) {
        SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
    }

    setWindowIcon(window);

    int winW, winH;
    SDL_GetWindowSize(window, &winW, &winH);

    if (!RenderDevice::get().init(window, selectedBackend, winW, winH)) {
        #ifdef _WIN32
        std::cout << "[Main] Recreating window for fallback OpenGL 1.1 mode...\n";
        SDL_DestroyWindow(window);

        selectedBackend = RENDERER_OPENGL;
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 1);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

        window = SDL_CreateWindow(
        "Geometry Dash - Play Level 1",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        screenWidth, screenHeight,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN
        );

        if (!window || !RenderDevice::get().init(window, RENDERER_OPENGL, winW, winH)) {
            std::cerr << "[Main] Fallback OpenGL mode also failed.\n";
            if (window) SDL_DestroyWindow(window);
            SDL_Quit();
            return -1;
        }
        setWindowIcon(window);
        #else
        SDL_DestroyWindow(window);
        SDL_Quit();
        return -1;
        #endif
    }

    Settings::get().applyFpsSettings();
    RenderDevice::get().setVSync(Settings::get().currentFps().vsync);
    updateViewport(winW, winH, window);

    BootScene bootScene;
    bootScene.preload(window);

    bootScene.create();
    RenderDevice::get().syncTexturesFromBootScene();

    if (BootScene::textCache.find("GJ_WebSheetJson") != BootScene::textCache.end()) {
        Texture tex = BootScene::textures["GJ_WebSheet"];
        AtlasManager::loadAtlasJson(BootScene::textCache["GJ_WebSheetJson"], tex.width, tex.height);
    }

    std::unique_ptr<GameScene> gameScene = std::make_unique<GameScene>();
    gameScene->init();

    bool running = true;
    SDL_Event event;

    Uint64 lastTime = SDL_GetPerformanceCounter();
    Uint64 timerFreq = SDL_GetPerformanceFrequency();
    bool lastVsyncState = Settings::get().currentFps().vsync;

    int cachedTargetFps = Settings::get().currentFps().fps;
    int lastFpsOptIndex = -1;

#ifdef __EMSCRIPTEN__
    g_emCtx.window = window;
    g_emCtx.gameScene = std::move(gameScene);
    g_emCtx.winW = winW;
    g_emCtx.winH = winH;
    g_emCtx.lastTime = SDL_GetPerformanceCounter();
    g_emCtx.timerFreq = SDL_GetPerformanceFrequency();
    emscripten_set_main_loop(emscriptenFrame, 0, 1);
#else
    while (running) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
            } else if (event.type == SDL_WINDOWEVENT) {
                if (event.window.event == SDL_WINDOWEVENT_RESIZED ||
                    event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
                    event.window.event == SDL_WINDOWEVENT_MAXIMIZED ||
                    event.window.event == SDL_WINDOWEVENT_RESTORED)
                {
                    SDL_GetWindowSize(window, &winW, &winH);
                    updateViewport(winW, winH, window);
                }
            } else {
                gameScene->handleEvent(event, winW, winH, window);
            }
        }

        Uint64 frameStartTime = SDL_GetPerformanceCounter();

        float dt = (float)(frameStartTime - lastTime) / (float)timerFreq;
        lastTime = frameStartTime;

        if (dt > 0.1f) dt = 0.1f;

        gameScene->update(dt);
        WinEffects::update(dt);

        RenderDevice::get().beginFrame();
        gameScene->render();
        RenderDevice::get().endFrame();

        const FpsOption& currentOpt = Settings::get().currentFps();
        if (currentOpt.vsync != lastVsyncState) {
            lastVsyncState = currentOpt.vsync;
            RenderDevice::get().setVSync(currentOpt.vsync);
            lastFpsOptIndex = -1; // Force recalculation
        }

        if (Settings::get().fpsIndex != lastFpsOptIndex) {
            lastFpsOptIndex = Settings::get().fpsIndex;
            cachedTargetFps = currentOpt.fps;
            if (currentOpt.vsync) {
                SDL_DisplayMode dm;
                if (SDL_GetWindowDisplayMode(window, &dm) == 0 && dm.refresh_rate > 0) {
                    cachedTargetFps = dm.refresh_rate;
                } else {
                    cachedTargetFps = 60;
                }
            }
        }

        int targetFps = cachedTargetFps;

        if (targetFps > 0) {
            Uint64 targetTicks = timerFreq / (Uint64)targetFps;
            while (true) {
                Uint64 currentPerf = SDL_GetPerformanceCounter();
                Uint64 elapsedTicks = currentPerf - frameStartTime;
                if (elapsedTicks >= targetTicks) break;

                Uint64 remainingTicks = targetTicks - elapsedTicks;
                double remainingMs = ((double)remainingTicks * 1000.0) / (double)timerFreq;

                if (remainingMs > 2.0) {
                    SDL_Delay((Uint32)(remainingMs - 1.5));
                } else if (remainingMs > 0.5) {
                    SDL_Delay(0);
                }
            }
        }
    }
#endif

    gameScene.reset();
    RenderDevice::get().shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
