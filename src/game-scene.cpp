#include "game-scene.h"
#include "boot-scene.h"
#include "font-helpers.h"
#include "win-effects.h"
#include "settings.h"
#include "render-device.h"
#include <algorithm>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <cstdlib>
#include <cstdio>
#include <cmath>

static constexpr bool DEBUG_SPAWN_AT_END = 0;
static constexpr bool SHOW_FPS = 1;

static void openURL(const std::string& url) {
    SDL_OpenURL(url.c_str());
}

static void toggleFullscreen(SDL_Window* window) {
    if (!window) return;
    Uint32 flags = SDL_GetWindowFlags(window);
    if (flags & SDL_WINDOW_FULLSCREEN_DESKTOP) {
        SDL_SetWindowFullscreen(window, 0);
    } else {
        SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
    }
}

static std::string formatPlayTime(float seconds) {
    int totalSec = (int)std::floor(seconds);
    int hours = totalSec / 3600;
    int mins = (totalSec % 3600) / 60;
    int secs = totalSec % 60;
    char buf[32];
    if (hours > 0) {
        std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d", hours, mins, secs);
    } else {
        std::snprintf(buf, sizeof(buf), "%02d:%02d", mins, secs);
    }
    return std::string(buf);
}

static float easeElasticOut(float t) {
    if (t <= 0.0f) return 0.01f;
    if (t >= 1.0f) return 1.0f;
    float p = 0.6f;
    return std::pow(2.0f, -10.0f * t) * std::sin((t - p / 4.0f) * (2.0f * 3.14159265f) / p) + 1.0f;
}

static float easeBounceOut(float t) {
    if (t < (1.0f / 2.75f)) {
        return 7.5625f * t * t;
    } else if (t < (2.0f / 2.75f)) {
        float p = t - (1.5f / 2.75f);
        return 7.5625f * p * p + 0.75f;
    } else if (t < (2.5f / 2.75f)) {
        float p = t - (2.25f / 2.75f);
        return 7.5625f * p * p + 0.9375f;
    } else {
        float p = t - (2.625f / 2.75f);
        return 7.5625f * p * p + 0.984375f;
    }
}

GameScene::GameScene()
: _cameraX(-groundYOffset),
_cameraY(0.0f),
_prevCameraX(-groundYOffset),
_menuCameraX(-groundYOffset),
_playerWorldX(0.0f),
_slideGroundX(0.0f),
_bgScrollX(0.0f)
{
}

GameScene::~GameScene() {}

void GameScene::init() {
    _level = std::make_unique<LevelRenderer>();
    _player = std::make_unique<Player>(_state, *_level);

    if (BootScene::textCache.find("level_1") != BootScene::textCache.end()) {
        _level->loadLevel(BootScene::textCache["level_1"]);
    }

    _resetGameplayState();
    _slideGroundX = _cameraX;

    Settings::get().load();
    _sfxVolume = Settings::get().sfxVolume;
    _audio.setUserMusicVolume(Settings::get().musicVolume);
    _audio.setSfxVolume(_sfxVolume);
    _isFullscreen = Settings::get().fullscreen;
    Settings::get().applyFpsSettings();

    _menuActive = true;
    _paused = false;
    _showEndLayerUI = false;

    _player->setCubeVisible(false);
    _player->setShipVisible(false);

    _btnAnims[BTN_MENU_PLAY].init(1.0f);
    _btnAnims[BTN_MENU_FS].init(0.64f);
    _btnAnims[BTN_MENU_SETTINGS].init(0.48f);
    _btnAnims[BTN_MENU_INFO].init(0.64f);
    _btnAnims[BTN_MENU_STEAM].init(1.0f / 1.5f);
    _btnAnims[BTN_MENU_GOOGLE].init(1.0f / 1.5f);
    _btnAnims[BTN_MENU_APPLE].init(1.0f / 1.5f);

    _btnAnims[BTN_PAUSE_FS].init(0.64f);
    _btnAnims[BTN_PAUSE_REPLAY].init(1.0f);
    _btnAnims[BTN_PAUSE_PLAY].init(1.0f);
    _btnAnims[BTN_PAUSE_MENU].init(1.0f);

    _btnAnims[BTN_END_REPLAY].init(1.0f);
    _btnAnims[BTN_END_MENU].init(1.0f);
    _btnAnims[BTN_END_APPLE].init(1.0f / 1.5f);
    _btnAnims[BTN_END_GOOGLE].init(1.0f / 1.5f);
    _btnAnims[BTN_END_STEAM].init(1.0f / 1.5f);

    _btnAnims[BTN_INFO_CLOSE].init(0.80f);
    _btnAnims[BTN_INFO_YT].init(0.50f);

    _btnAnims[BTN_SETTINGS_CLOSE].init(0.80f);
    _btnAnims[BTN_SETTINGS_RENDER_PREV].init(1.0f);
    _btnAnims[BTN_SETTINGS_RENDER_NEXT].init(1.0f);
    _btnAnims[BTN_SETTINGS_RENDER_BOX].init(1.0f);

    _pauseBtnVisible = false;
    _pauseBtnAlpha = 0.0f;
    _pauseBtnFading = false;

    if (SHOW_FPS) {
        _lastFpsUpdateTick = SDL_GetTicks();
        _fpsFrameCount = 0;
        _fpsText = "60 FPS";
    }
}

void GameScene::_resetGameplayState() {
    _cameraX = -groundYOffset;
    _cameraY = 0.0f;
    _prevCameraX = _cameraX;
    _playerWorldX = 0.0f;
    _deltaBuffer = 0.0f;
    _deathTimer = 0.0f;
    _deathSoundPlayed = false;
    _newBestShown = false;
    _hadNewBest = false;
    _levelWon = false;
    _showEndLayerUI = false;
    _draggingMusicSlider = false;
    _draggingSfxSlider = false;
    _draggingFpsSlider = false;

    _endCameraOverride = false;
    _endCamTweenActive = false;
    _endCamTweenTime = 0.0f;

    _endSequencePhase = 0;
    _endSequenceTimer = 0.0f;
    _shakeTimer = 0.0f;
    _shakeIntensity = 0.0f;
    _flashAlpha = 0.0f;
    _lightRays.clear();

    _completeBannerVisible = false;
    _completeBannerTimer = 0.0f;
    _completeBannerScale = 0.01f;

    _starAwardStarted = false;
    _starAwardSoundPlayed = false;
    _starAwardTimer = 0.0f;
    _starAwardScale = 3.0f;
    _starAwardAlpha = 0.0f;

    _isMenuAnimatingOut = false;
    _menuAnimTimer = 0.0f;
    _menuPlayTimer = 0.0f;
    _menuGlitterTimer = 0.0f;
    _firstPlay = true;

    _newBestActive = false;
    _newBestTimer = 0.0f;
    _newBestScale = 0.01f;

    _endLayerHiding = false;
    _endLayerHideTimer = 0.0f;
    _endLayerHideCallback = nullptr;

    _showAttemptsLabel = false;
    _showInfoPopup = false;
    _showSettingsPopup = false;
}

void GameScene::_showNewBest() {
    _newBestActive = true;
    _newBestTimer = 0.0f;
    _newBestScale = 0.01f;
}

void GameScene::_hideEndLayer(std::function<void()> onComplete) {
    _endLayerHiding = true;
    _endLayerHideTimer = 0.0f;
    _endLayerHideCallback = onComplete;
}

void GameScene::startGame() {
    if (!_menuActive) return;
    _menuActive = false;

    if (DEBUG_SPAWN_AT_END) {
        _slideIn = false;
        float endX = (_level->endXPos > 0.0f) ? _level->endXPos : 6000.0f;
        _playerWorldX = endX - 1800.0f;
        _cameraX = _playerWorldX - groundYOffset;
        _prevCameraX = _cameraX;
        _slideGroundX = _cameraX;

        _state.y = 200.0f;
        _state.onGround = false;

        _endCameraOverride = false;
        _endCamTweenActive = false;
        _endCamTweenTime = 0.0f;

        _player->reset();
        _player->enterShipMode();
        _audio.startMusic();
        _firstPlay = false;

        _pauseBtnVisible = true;
        _pauseBtnAlpha = 75.0f / 255.0f;
        _pauseBtnFading = false;
        _showAttemptsLabel = false;
        return;
    }

    _slideIn = true;
    _isMenuAnimatingOut = true;
    _menuAnimTimer = 0.0f;

    _cameraX = -groundYOffset;
    _cameraY = 0.0f;
    _prevCameraX = _cameraX;
    _playerWorldX = -groundYOffset - 180.0f;

    _state.y = 30.0f;
    _state.onGround = true;

    _player->setCubeVisible(true);
    _player->reset();

    _showAttemptsLabel = false;
    _pauseBtnVisible = false;
    _pauseBtnAlpha = 0.0f;
    _pauseBtnFading = false;
}

void GameScene::restartLevel() {
    _attempts++;
    _resetGameplayState();
    _state.reset();
    _player->reset();
    _level->resetObjects();
    _level->resetGroundState();
    _level->resetColorTriggers();
    _level->resetEnterEffectTriggers();
    _colorManager.reset();

    if (DEBUG_SPAWN_AT_END) {
        float endX = (_level->endXPos > 0.0f) ? _level->endXPos : 6000.0f;
        _playerWorldX = endX - 1800.0f;
        _cameraX = _playerWorldX - groundYOffset;
        _prevCameraX = _cameraX;
        _state.y = 200.0f;
        _state.onGround = false;

        _endCameraOverride = false;
        _endCamTweenActive = false;
        _endCamTweenTime = 0.0f;

        _player->enterShipMode();
    }

    _audio.reset();
    _audio.startMusic();
    _paused = false;

    _showAttemptsLabel = true;
    _attemptsLabelX = _cameraX + screenWidth * 0.5f + 100.0f;
    _attemptsLabelY = 150.0f;

    _pauseBtnVisible = true;
    _pauseBtnAlpha = 75.0f / 255.0f;
    _pauseBtnFading = false;
}

void GameScene::pushButton() {
    if (_menuActive) return;
    if (_slideIn || _state.isDead || _levelWon) return;

    _state.upKeyDown = true;
    _state.upKeyPressed = true;
    if (!_state.isFlying && _state.canJump) {
        _player->updateJump(0.0f);
        _totalJumps++;
    }
}

void GameScene::releaseButton() {
    _state.upKeyDown = false;
    _state.upKeyPressed = false;
}

void GameScene::pauseGame() {
    if (_paused || _menuActive || _slideIn || _state.isDead || _levelWon) return;
    _paused = true;
    _pauseBtnVisible = false;
    _audio.pauseMusic();
}

void GameScene::resumeGame() {
    if (!_paused) return;
    _paused = false;
    _pauseBtnVisible = true;
    _pauseBtnAlpha = 75.0f / 255.0f;
    _draggingMusicSlider = false;
    _draggingSfxSlider = false;
    _showSettingsPopup = false;
    _audio.resumeMusic();
}

float GameScene::_getBaseScale(ButtonId id) const {
    switch (id) {
        case BTN_MENU_FS:
        case BTN_MENU_INFO:
        case BTN_PAUSE_FS:
            return 0.64f;
        case BTN_MENU_SETTINGS:
            return 0.48f;
        case BTN_MENU_STEAM:
        case BTN_MENU_GOOGLE:
        case BTN_MENU_APPLE:
        case BTN_END_APPLE:
        case BTN_END_GOOGLE:
        case BTN_END_STEAM:
            return 1.0f / 1.5f;
        case BTN_INFO_CLOSE:
        case BTN_SETTINGS_CLOSE:
            return 0.80f;
        case BTN_INFO_YT:
            return 0.50f;
        default:
            return 1.0f;
    }
}

HitBox GameScene::_getButtonHitBox(const std::string& frameName, float cx, float cy, ButtonId id, float guiScale, float expandFactor) {
    const AtlasFrame* af = findAtlasFrame(frameName);
    float origW = (af && af->w > 0.0f) ? af->w : 40.0f;
    float origH = (af && af->h > 0.0f) ? af->h : 40.0f;

    float curScale = _btnAnims[id].scale * guiScale;
    float totalW = origW * expandFactor * curScale;
    float totalH = origH * expandFactor * curScale;

    return { cx, cy, totalW * 0.5f, totalH * 0.5f };
}

ButtonId GameScene::_checkButtonHit(float vx, float vy) {
    float midX = screenWidth * 0.5f;
    float guiScale = getGuiScale();

    // 1. Settings Popup
    if (_showSettingsPopup) {
        #if defined(_WIN32)
        const float basePopupH = 340.0f;
        #else
        const float basePopupH = 260.0f;
        #endif
        const float popupH = basePopupH * guiScale;
        float closeY = 320.0f - (popupH * 0.5f) + 24.0f * guiScale;
        HitBox closeBox = _getButtonHitBox("GJ_closeBtn_001.png", midX - 220.0f * guiScale, closeY, BTN_SETTINGS_CLOSE, guiScale, 2.0f);
        if (closeBox.contains(vx, vy)) return BTN_SETTINGS_CLOSE;

        #if defined(_WIN32)
        if (!Settings::get().rendererOptions.empty()) {
            float rendY = 320.0f - (popupH * 0.5f) + 185.0f * guiScale;
            HitBox prevBox = { midX - 115.0f * guiScale, rendY, 24.0f * guiScale, 22.0f * guiScale };
            if (prevBox.contains(vx, vy)) return BTN_SETTINGS_RENDER_PREV;

            HitBox nextBox = { midX + 115.0f * guiScale, rendY, 24.0f * guiScale, 22.0f * guiScale };
            if (nextBox.contains(vx, vy)) return BTN_SETTINGS_RENDER_NEXT;

            HitBox boxHit = { midX, rendY, 85.0f * guiScale, 18.0f * guiScale };
            if (boxHit.contains(vx, vy)) return BTN_SETTINGS_RENDER_BOX;
        }
        #endif
        return BTN_COUNT;
    }

    // 2. Info / Credits Popup
    if (_showInfoPopup) {
        const float popupH = 336.0f * guiScale;
        float closeY = 320.0f - (popupH * 0.5f) + 24.0f * guiScale;
        HitBox closeBox = _getButtonHitBox("GJ_closeBtn_001.png", midX - 220.0f * guiScale, closeY, BTN_INFO_CLOSE, guiScale, 2.0f);
        if (closeBox.contains(vx, vy)) return BTN_INFO_CLOSE;

        float textW = 145.0f;
        const BitmapFont* gf = getFont("goldFont");
        if (gf) {
            float tw = 0.0f;
            for (char ch : std::string("by ForeverBound")) {
                const BitmapChar* c = gf->charLookup[(unsigned char)ch];
                if (c) tw += c->xAdvance * 0.55f;
            }
            if (tw > 0.0f) textW = tw;
        }

        const AtlasFrame* ytAf = findAtlasFrame("gj_ytIcon_001.png");
        float baseW = (ytAf && ytAf->w > 0.0f) ? ytAf->w : 64.0f;
        float ytDrawW = baseW * 0.50f * guiScale;
        float spacing = 12.0f * guiScale;
        float totalGroupW = textW * guiScale + spacing + ytDrawW;
        float groupStartX = midX - (totalGroupW * 0.5f);
        float ytX = groupStartX + textW * guiScale + spacing + (ytDrawW * 0.5f);
        float creditLine3Y = 320.0f + (334.0f - 320.0f) * guiScale;

        HitBox ytBox = _getButtonHitBox("gj_ytIcon_001.png", ytX, creditLine3Y, BTN_INFO_YT, guiScale, 2.0f);
        if (ytBox.contains(vx, vy)) return BTN_INFO_YT;

        return BTN_COUNT;
    }

    // 3. Main Menu
    if (_menuActive && !_isMenuAnimatingOut) {
        std::string fsTexture = _isFullscreen ? "toggleFullscreenOff_001.png" : "toggleFullscreenOn_001.png";
        float cornerOffset = 33.0f * guiScale;
        HitBox fsBox = _getButtonHitBox(fsTexture, cornerOffset, cornerOffset, BTN_MENU_FS, guiScale, 1.5f);
        if (fsBox.contains(vx, vy)) return BTN_MENU_FS;

        HitBox settingsBox = _getButtonHitBox("GJ_menuBtn_001.png", screenWidth - cornerOffset, cornerOffset, BTN_MENU_SETTINGS, guiScale, 1.5f);
        if (settingsBox.contains(vx, vy)) return BTN_MENU_SETTINGS;

        HitBox infoBox = _getButtonHitBox("GJ_infoIcon_001.png", screenWidth - cornerOffset, 85.0f * guiScale, BTN_MENU_INFO, guiScale, 1.5f);
        if (infoBox.contains(vx, vy)) return BTN_MENU_INFO;

        float bottomY = 320.0f + (555.0f - 320.0f) * guiScale;
        HitBox steamBox = _getButtonHitBox("downloadSteam_001.png", midX + 438.0f * guiScale, bottomY, BTN_MENU_STEAM, guiScale, 1.0f);
        if (steamBox.contains(vx, vy)) return BTN_MENU_STEAM;

        HitBox googleBox = _getButtonHitBox("downloadGoogle_001.png", midX + 228.0f * guiScale, bottomY, BTN_MENU_GOOGLE, guiScale, 1.0f);
        if (googleBox.contains(vx, vy)) return BTN_MENU_GOOGLE;

        HitBox appleBox = _getButtonHitBox("downloadApple_001.png", midX + 18.0f * guiScale, bottomY, BTN_MENU_APPLE, guiScale, 1.0f);
        if (appleBox.contains(vx, vy)) return BTN_MENU_APPLE;

        float playY = 320.0f + (_menuPlayBtnY - 320.0f) * guiScale;
        HitBox playBox = _getButtonHitBox("GJ_playBtn_001.png", midX, playY, BTN_MENU_PLAY, guiScale, 1.0f);
        if (playBox.contains(vx, vy)) return BTN_MENU_PLAY;

        return BTN_COUNT;
    }

    // 4. Pause Menu
    if (_paused && !_showEndLayerUI) {
        float boxW = (1136.0f - 40.0f) * guiScale;
        float fsX = (midX - boxW * 0.5f) + 40.0f * guiScale;
        float fsY = 320.0f - (300.0f - 60.0f) * guiScale;

        std::string fsTexture = _isFullscreen ? "toggleFullscreenOff_001.png" : "toggleFullscreenOn_001.png";
        HitBox fsBox = _getButtonHitBox(fsTexture, fsX, fsY, BTN_PAUSE_FS, guiScale, 2.5f);
        if (fsBox.contains(vx, vy)) return BTN_PAUSE_FS;

        struct PauseBtnDef { const char* name; ButtonId id; };
        PauseBtnDef pBtns[3] = {
            {"GJ_replayBtn_001.png", BTN_PAUSE_REPLAY},
            {"GJ_playBtn2_001.png",  BTN_PAUSE_PLAY},
            {"GJ_menuBtn_001.png",   BTN_PAUSE_MENU}
        };

        float pW[3];
        float totalW = 0.0f;
        for (int i = 0; i < 3; ++i) {
            const AtlasFrame* af = findAtlasFrame(pBtns[i].name);
            pW[i] = ((af && af->w > 0.0f) ? af->w : 85.0f) * guiScale;
            totalW += pW[i];
        }
        float spacing = 40.0f * guiScale;
        totalW += spacing * 2.0f;

        float btnY = 320.0f + (330.0f - 320.0f) * guiScale;
        float pStartX = midX - totalW * 0.5f;
        for (int i = 0; i < 3; ++i) {
            float bx = pStartX + pW[i] * 0.5f;
            HitBox bBox = _getButtonHitBox(pBtns[i].name, bx, btnY, pBtns[i].id, guiScale, 1.0f);
            if (bBox.contains(vx, vy)) return pBtns[i].id;
            pStartX += pW[i] + spacing;
        }

        return BTN_COUNT;
    }

    // 5. End Layer
    if (_showEndLayerUI && !_endLayerHiding) {
        float dropOffsetY = 0.0f;
        if (_endSequencePhase >= 3) {
            float p = std::clamp((_endSequenceTimer - 3.45f) / 1.0f, 0.0f, 1.0f);
            dropOffsetY = (650.0f * easeBounceOut(p) - 640.0f) - 10.0f;
        }

        float navY = 320.0f + (545.0f - 320.0f) * guiScale + dropOffsetY;
        HitBox replayBox = _getButtonHitBox("GJ_replayBtn_001.png", midX - 160.0f * guiScale, navY, BTN_END_REPLAY, guiScale, 1.0f);
        if (replayBox.contains(vx, vy)) return BTN_END_REPLAY;

        HitBox menuBox = _getButtonHitBox("GJ_menuBtn_001.png", midX + 160.0f * guiScale, navY, BTN_END_MENU, guiScale, 1.0f);
        if (menuBox.contains(vx, vy)) return BTN_END_MENU;

        float storeY = 320.0f + (440.0f - 320.0f) * guiScale + dropOffsetY;
        HitBox appleBox = _getButtonHitBox("downloadApple_001.png", midX - 225.0f * guiScale, storeY, BTN_END_APPLE, guiScale, 1.0f);
        if (appleBox.contains(vx, vy)) return BTN_END_APPLE;

        HitBox googleBox = _getButtonHitBox("downloadGoogle_001.png", midX, storeY, BTN_END_GOOGLE, guiScale, 1.0f);
        if (googleBox.contains(vx, vy)) return BTN_END_GOOGLE;

        HitBox steamBox = _getButtonHitBox("downloadSteam_001.png", midX + 225.0f * guiScale, storeY, BTN_END_STEAM, guiScale, 1.0f);
        if (steamBox.contains(vx, vy)) return BTN_END_STEAM;

        return BTN_COUNT;
    }

    return BTN_COUNT;
}

void GameScene::handleEvent(const SDL_Event& event, int windowW, int windowH, SDL_Window* window) {
    if (window) {
        Uint32 flags = SDL_GetWindowFlags(window);
        _isFullscreen = (flags & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
    }

    if (_fadeState != 0) return;

    if (event.type == SDL_KEYDOWN) {
        if (event.key.keysym.sym == SDLK_SPACE || event.key.keysym.sym == SDLK_UP) {
            if (_menuActive) {
                _audio.playEffect("playSound_01");
                startGame();
            } else {
                pushButton();
            }
        } else if (event.key.keysym.sym == SDLK_ESCAPE) {
            if (_showSettingsPopup) {
                _showSettingsPopup = false;
            } else if (_showInfoPopup) {
                _showInfoPopup = false;
            } else if (_paused && !_showEndLayerUI) {
                resumeGame();
            } else if (!_showEndLayerUI && !_menuActive && !_slideIn && !_state.isDead && !_levelWon) {
                pauseGame();
            }
        }
    } else if (event.type == SDL_KEYUP) {
        if (event.key.keysym.sym == SDLK_SPACE || event.key.keysym.sym == SDLK_UP) {
            releaseButton();
        }
    } else if (event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEMOTION || event.type == SDL_MOUSEBUTTONUP) {
        float targetAspect = (float)screenWidth / (float)screenHeight;
        float windowAspect = (float)windowW / (float)windowH;
        int vpX = 0, vpY = 0, vpW = windowW, vpH = windowH;

        if (windowAspect > targetAspect) {
            vpW = (int)(windowH * targetAspect);
            vpX = (windowW - vpW) / 2;
        } else {
            vpH = (int)(windowW / targetAspect);
            vpY = (windowH - vpH) / 2;
        }

        int mouseX = (event.type == SDL_MOUSEMOTION) ? event.motion.x : event.button.x;
        int mouseY = (event.type == SDL_MOUSEMOTION) ? event.motion.y : event.button.y;

        float virtX = (float)(mouseX - vpX) * ((float)screenWidth / (float)vpW);
        float virtY = (float)(mouseY - vpY) * ((float)screenHeight / (float)vpH);
        float midX = screenWidth * 0.5f;
        float guiScale = getGuiScale();

        const float grooveScale = 0.7f * guiScale;
        const AtlasFrame* grooveAf = findAtlasFrame("slidergroove.png");
        const float origGrooveW = (grooveAf && grooveAf->w > 0.0f) ? grooveAf->w : 420.0f;
        const float trackWidth = (origGrooveW - 8.0f) * grooveScale;
        const float halfGrooveW = (origGrooveW * grooveScale) * 0.5f;

        if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
            if (_showSettingsPopup) {
                float fpsStartX = midX - halfGrooveW + 2.8f * guiScale;
                #if defined(_WIN32)
                const float basePopupH = 340.0f;
                #else
                const float basePopupH = 260.0f;
                #endif
                float fpsY = 320.0f - (basePopupH * guiScale * 0.5f) + 114.0f * guiScale;
                if (virtX >= fpsStartX - 25.0f * guiScale && virtX <= fpsStartX + trackWidth + 25.0f * guiScale &&
                    virtY >= fpsY - 25.0f * guiScale && virtY <= fpsY + 25.0f * guiScale) {
                    _draggingFpsSlider = true;
                    float rawVal = std::clamp((virtX - fpsStartX) / trackWidth, 0.0f, 1.0f);
                    int stepIdx = (int)std::round(rawVal * (Settings::get().fpsOptions.size() - 1));
                    Settings::get().setFpsIndex(stepIdx);
                    return;
                }
            }

            if (_paused && !_showSettingsPopup && !_showEndLayerUI) {
                float sliderY = 320.0f + (470.0f - 320.0f) * guiScale;
                float musicStartX = (midX - 220.0f * guiScale) - halfGrooveW + 2.8f * guiScale;
                if (virtX >= musicStartX - 25.0f * guiScale && virtX <= musicStartX + trackWidth + 25.0f * guiScale &&
                    virtY >= sliderY - 25.0f * guiScale && virtY <= sliderY + 25.0f * guiScale) {
                    _draggingMusicSlider = true;
                    float val = std::clamp((virtX - musicStartX) / trackWidth, 0.0f, 1.0f);
                    if (val < 0.03f) val = 0.0f;
                    _audio.setUserMusicVolume(val);
                    return;
                }

                float sfxStartX = (midX + 220.0f * guiScale) - halfGrooveW + 2.8f * guiScale;
                if (virtX >= sfxStartX - 25.0f * guiScale && virtX <= sfxStartX + trackWidth + 25.0f * guiScale &&
                    virtY >= sliderY - 25.0f * guiScale && virtY <= sliderY + 25.0f * guiScale) {
                    _draggingSfxSlider = true;
                    float val = std::clamp((virtX - sfxStartX) / trackWidth, 0.0f, 1.0f);
                    if (val < 0.03f) val = 0.0f;
                    _sfxVolume = val;
                    _audio.setSfxVolume(_sfxVolume);
                    return;
                }
            }

            ButtonId hit = _checkButtonHit(virtX, virtY);
            if (hit != BTN_COUNT) {
                _heldBtn = hit;
                _isButtonPressed = true;
                _btnAnims[hit].press(_getBaseScale(hit));
                return;
            }

            if (_showSettingsPopup || _showInfoPopup) return;

            if (!_menuActive && !_paused && !_showEndLayerUI) {
                if (_pauseBtnVisible && _pauseBtnAlpha > 0.05f) {
                    const AtlasFrame* pbAf = findAtlasFrame("GJ_pauseBtn_clean_001.png");
                    float pbW = ((pbAf && pbAf->w > 0.0f) ? pbAf->w : 40.0f) * guiScale;
                    float pbH = ((pbAf && pbAf->h > 0.0f) ? pbAf->h : 40.0f) * guiScale;
                    float pbCenterX = screenWidth - 30.0f * guiScale;
                    float pbCenterY = 30.0f * guiScale;

                    if (std::abs(virtX - pbCenterX) <= pbW && std::abs(virtY - pbCenterY) <= pbH) {
                        pauseGame();
                        return;
                    }
                }
                pushButton();
            }
        } else if (event.type == SDL_MOUSEMOTION) {
            if (_showSettingsPopup && _draggingFpsSlider) {
                float fpsStartX = midX - halfGrooveW + 2.8f * guiScale;
                float rawVal = std::clamp((virtX - fpsStartX) / trackWidth, 0.0f, 1.0f);
                int stepIdx = (int)std::round(rawVal * (Settings::get().fpsOptions.size() - 1));
                Settings::get().setFpsIndex(stepIdx);
                return;
            }

            if (_paused && !_showSettingsPopup && !_showEndLayerUI) {
                if (_draggingMusicSlider) {
                    float startX = (midX - 220.0f * guiScale) - halfGrooveW + 2.8f * guiScale;
                    float val = std::clamp((virtX - startX) / trackWidth, 0.0f, 1.0f);
                    if (val < 0.03f) val = 0.0f;
                    _audio.setUserMusicVolume(val);
                    return;
                } else if (_draggingSfxSlider) {
                    float startX = (midX + 220.0f * guiScale) - halfGrooveW + 2.8f * guiScale;
                    float val = std::clamp((virtX - startX) / trackWidth, 0.0f, 1.0f);
                    if (val < 0.03f) val = 0.0f;
                    _sfxVolume = val;
                    _audio.setSfxVolume(_sfxVolume);
                    return;
                }
            }

            if (_heldBtn != BTN_COUNT) {
                ButtonId cur = _checkButtonHit(virtX, virtY);
                if (cur != _heldBtn && _isButtonPressed) {
                    _isButtonPressed = false;
                    _btnAnims[_heldBtn].deselect();
                } else if (cur == _heldBtn && !_isButtonPressed) {
                    _isButtonPressed = true;
                    _btnAnims[_heldBtn].press(_getBaseScale(_heldBtn));
                }
            }
        } else if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_LEFT) {
            if (_draggingMusicSlider || _draggingSfxSlider || _draggingFpsSlider) {
                Settings::get().musicVolume = _audio.getUserMusicVolume();
                Settings::get().sfxVolume = _sfxVolume;
                Settings::get().save();
            }

            _draggingMusicSlider = false;
            _draggingSfxSlider = false;
            _draggingFpsSlider = false;

            if (_heldBtn != BTN_COUNT) {
                ButtonId releaseHit = _checkButtonHit(virtX, virtY);
                ButtonId active = _heldBtn;
                bool wasPressed = _isButtonPressed;

                _heldBtn = BTN_COUNT;
                _isButtonPressed = false;
                _btnAnims[active].release();

                if (wasPressed && releaseHit == active) {
                    switch (active) {
                        case BTN_INFO_CLOSE: _showInfoPopup = false; break;
                        case BTN_INFO_YT: openURL("https://www.youtube.com/watch?v=JhKyKEDxo8Q"); break;
                        case BTN_SETTINGS_CLOSE: _showSettingsPopup = false; break;
                        case BTN_MENU_SETTINGS: _showSettingsPopup = true; break;
                        case BTN_MENU_INFO: _showInfoPopup = true; break;
                        #if defined(_WIN32)
                        case BTN_SETTINGS_RENDER_PREV:
                            Settings::get().prevRenderer();
                            break;
                        case BTN_SETTINGS_RENDER_NEXT:
                        case BTN_SETTINGS_RENDER_BOX:
                            Settings::get().nextRenderer();
                            break;
                            #endif
                        case BTN_MENU_FS:
                        case BTN_PAUSE_FS:
                            toggleFullscreen(window);
                            _isFullscreen = !_isFullscreen;
                            Settings::get().fullscreen = _isFullscreen;
                            Settings::get().save();
                            break;
                        case BTN_MENU_STEAM: openURL("https://store.steampowered.com/app/322170/Geometry_Dash"); break;
                        case BTN_MENU_GOOGLE: openURL("https://play.google.com/store/apps/details?id=com.robtopx.geometryjump&hl=en"); break;
                        case BTN_MENU_APPLE: openURL("https://apps.apple.com/us/app/geometry-dash/id625334537"); break;
                        case BTN_MENU_PLAY: _audio.playEffect("playSound_01"); startGame(); break;
                        case BTN_PAUSE_PLAY: resumeGame(); break;
                        case BTN_PAUSE_REPLAY: resumeGame(); restartLevel(); break;
                        case BTN_PAUSE_MENU:
                            _audio.playEffect("quitSound_01");
                            _audio.stopMusic();
                            _fadeState = 1;
                            _fadeTimer = 0.0f;
                            break;
                        case BTN_END_REPLAY:
                            _hideEndLayer([this]() { restartLevel(); });
                            break;
                        case BTN_END_MENU:
                            _audio.playEffect("quitSound_01");
                            _audio.stopMusic();
                            _fadeState = 1;
                            _fadeTimer = 0.0f;
                            break;
                        default: break;
                    }
                }
            }
            releaseButton();
        }
    }
}

float GameScene::_quantizeDelta(float dt) {
    float dtSec = dt + _deltaBuffer;
    int steps = (int)std::round(dtSec / fixedTimeStep);
    steps = std::clamp(steps, 0, 60);
    float actualDt = steps * fixedTimeStep;
    _deltaBuffer = dtSec - actualDt;
    return 60.0f * actualDt;
}

void GameScene::_updateBackground(float dt) { (void)dt; }

void GameScene::_updateCameraY(float dt) {
    float targetY = _cameraY;
    if (_level->hasCeiling()) {
        targetY = _level->flyCameraTarget;
    } else {
        float pY = _state.y;
        float centerOffset = _cameraY - unusedConst180 + 320.0f;
        if (pY > centerOffset + 140.0f) {
            targetY = pY - 320.0f - 140.0f + unusedConst180;
        } else if (pY < centerOffset - 80.0f) {
            targetY = pY - 320.0f + 80.0f + unusedConst180;
        }
    }
    if (targetY < 0.0f) targetY = 0.0f;
    if (dt > 0.0f) {
        _cameraY += (targetY - _cameraY) / (10.0f / dt);
        if (_cameraY < 0.0f) _cameraY = 0.0f;
    }
}

void GameScene::_startCompleteLightRays() {
    _lightRays.clear();
    const int rayCount = 8;
    float baseAngle = -135.0f;
    float stepAngle = 90.0f / (float)rayCount;
    float targetLen = std::round(std::sqrt((float)(screenWidth * screenWidth) + 102400.0f)) + 65.0f;

    std::vector<float> angles(rayCount);
    for (int i = 0; i < rayCount; ++i) angles[i] = baseAngle + i * stepAngle;
    for (int i = rayCount - 1; i > 0; --i) std::swap(angles[i], angles[rand() % (i + 1)]);

    for (int i = 0; i < rayCount; ++i) {
        float rnd1 = (rand() % 1000) / 500.0f - 1.0f;
        float rnd2 = (rand() % 1000) / 500.0f - 1.0f;
        float rnd3 = (rand() % 1000) / 500.0f - 1.0f;
        float rnd4 = (rand() % 1000) / 500.0f - 1.0f;

        float delay = std::max(0.0f, (i * 0.195f + 0.04f + 0.04f * rnd1));
        CompleteLightRay ray;
        ray.angleDeg = angles[i] + stepAngle * ((rand() % 1000) / 1000.0f) + 180.0f;
        ray.targetW = 60.0f + 40.0f * rnd2;
        ray.targetH = targetLen;
        ray.currentW = 2.0f;
        ray.currentH = 1.0f;
        ray.maxAlpha = std::clamp((155.0f / 255.0f) + (100.0f / 255.0f) * rnd4, 0.0f, 1.0f);
        ray.currentAlpha = 0.0f;
        ray.delay = delay;
        ray.duration = 0.18f + 0.04f * rnd3;
        ray.elapsed = 0.0f;
        ray.fadeDelay = delay + ray.duration + 0.40f;
        ray.fadeDuration = 0.4f + 0.1f * rnd1;
        ray.fadeElapsed = 0.0f;
        ray.started = false;
        ray.fading = false;
        ray.done = false;
        _lightRays.push_back(ray);
    }
}

void GameScene::_updateCompleteLightRays(float dt) {
    for (auto& ray : _lightRays) {
        if (ray.done) continue;
        if (!ray.started) {
            ray.delay -= dt;
            if (ray.delay <= 0.0f) ray.started = true;
        } else if (!ray.fading) {
            ray.elapsed += dt;
            float t = std::min(ray.elapsed / ray.duration, 1.0f);
            float ease = 1.0f - (1.0f - t) * (1.0f - t);
            ray.currentH = 1.0f + (ray.targetH - 1.0f) * ease;
            ray.currentW = 2.0f + (ray.targetW - 2.0f) * ease;
            ray.currentAlpha = ray.maxAlpha;
            if (ray.elapsed >= ray.duration + 0.35f) ray.fading = true;
        } else {
            ray.fadeElapsed += dt;
            float t = std::min(ray.fadeElapsed / ray.fadeDuration, 1.0f);
            ray.currentAlpha = ray.maxAlpha * (1.0f - t);
            if (t >= 1.0f) ray.done = true;
        }
    }
}

void GameScene::_renderCompleteLightRays() {
    if (_lightRays.empty()) return;

    float originX = _level->endXPos - _cameraX + 60.0f;
    float originY = flipY(_endPortalGameY) + _cameraY;

    for (const auto& ray : _lightRays) {
        if (!ray.started || ray.done || ray.currentAlpha <= 0.0f) continue;

        float rad = ray.angleDeg * 0.0174532925f;
        float cosR = std::cos(rad);
        float sinR = std::sin(rad);

        float wBase = 2.0f + (ray.currentW - 2.0f) * 0.25f;
        float wEnd  = ray.currentW;
        float hEnd  = ray.currentH;

        auto rotP = [cosR, sinR, originX, originY](float px, float py, float& rx, float& ry) {
            rx = originX + (px * cosR - py * sinR);
            ry = originY + (px * sinR + py * cosR);
        };

        float x0, y0, x1, y1, x2, y2, x3, y3;
        rotP(-wBase * 0.5f, 0.0f, x0, y0);
        rotP( wBase * 0.5f, 0.0f, x1, y1);
        rotP( wEnd  * 0.5f, hEnd, x2, y2);
        rotP(-wEnd  * 0.5f, hEnd, x3, y3);

        RenderDevice::get().drawColorQuad(x0, y0, x1, y1, x2, y2, x3, y3,
                                          0.0f, 1.0f, 0.0f, ray.currentAlpha, BLEND_ADD);
    }
    applyBlendMode(BLEND_NORMAL);
}

struct FlightGlitter { float x, y, life, maxLife, scale; };
static std::vector<FlightGlitter> _flightGlitters;
static float _flightGlitterTimer = 0.0f;

void GameScene::update(float dt) {
    if (SHOW_FPS) {
        _fpsFrameCount++;
        Uint32 nowTick = SDL_GetTicks();
        if (nowTick - _lastFpsUpdateTick >= 250) {
            int fps = (int)std::round((_fpsFrameCount * 1000.0f) / (float)(nowTick - _lastFpsUpdateTick));
            _fpsText = std::to_string(fps) + " FPS";
            _fpsDisplayText = _fpsText + " - " + RenderDevice::get().getBackendName();
            _fpsFrameCount = 0;
            _lastFpsUpdateTick = nowTick;
            float fpsY = (_menuActive || _paused) ? 62.0f : 12.0f;
            _updateFpsGlyphs(fpsY);
        }
    }

    for (int i = 0; i < BTN_COUNT; ++i) _btnAnims[i].update(dt);

    if (_pauseBtnFading) {
        _pauseBtnFadeTimer += dt;
        float t = std::min(_pauseBtnFadeTimer / _pauseBtnFadeDuration, 1.0f);
        _pauseBtnAlpha = _pauseBtnFadeFrom + (_pauseBtnFadeTo - _pauseBtnFadeFrom) * t;
        if (t >= 1.0f) _pauseBtnFading = false;
    }

    if (_newBestActive) {
        _newBestTimer += dt;
        if (_newBestTimer <= 0.40f) {
            _newBestScale = easeElasticOut(_newBestTimer / 0.40f);
        } else if (_newBestTimer <= 1.10f) {
            _newBestScale = 1.0f;
        } else if (_newBestTimer <= 1.30f) {
            float t = (_newBestTimer - 1.10f) / 0.20f;
            _newBestScale = std::max(0.01f, 1.0f - t * t);
        } else {
            _newBestActive = false;
            _newBestScale = 0.01f;
        }
    }

    if (_endLayerHiding) {
        _endLayerHideTimer += dt;
        if (_endLayerHideTimer >= 0.5f) {
            _endLayerHiding = false;
            _showEndLayerUI = false;
            if (_endLayerHideCallback) {
                auto cb = _endLayerHideCallback;
                _endLayerHideCallback = nullptr;
                cb();
            }
        }
    }

    bool isFlightActive = _state.isFlying && !_state.isDead && !_levelWon && !_menuActive && !_paused;
    if (isFlightActive) {
        _flightGlitterTimer += dt;
        while (_flightGlitterTimer >= 0.06f) {
            _flightGlitterTimer -= 0.06f;
            if (_flightGlitters.size() < 60) {
                FlightGlitter fg;
                float centerX = _cameraX + screenWidth * 0.5f;
                float centerY = yFlipBase - _cameraY;
                fg.x = centerX + (((rand() % 1000) / 500.0f) - 1.0f) * (screenWidth / 1.8f);
                fg.y = centerY + 320.0f * (((rand() % 1000) / 500.0f) - 1.0f);
                fg.maxLife = (200.0f + (rand() % 1601)) / 1000.0f;
                fg.life = 0.0f;
                fg.scale = 0.375f;
                _flightGlitters.push_back(fg);
            }
        }
    } else {
        _flightGlitterTimer = 0.0f;
    }

    for (auto& fg : _flightGlitters) fg.life += dt;
    _flightGlitters.erase(
        std::remove_if(_flightGlitters.begin(), _flightGlitters.end(), [](const FlightGlitter& fg) {
            return fg.life >= fg.maxLife;
        }),
        _flightGlitters.end()
    );

    if (_fadeState == 1) {
        _fadeTimer += dt;
        float t = std::min(_fadeTimer / 0.4f, 1.0f);
        _blackFadeAlpha = t;
        if (t >= 1.0f) {
            _menuActive = true;
            _paused = false;
            _showEndLayerUI = false;
            _showSettingsPopup = false;
            _showInfoPopup = false;
            _state.reset();
            _player->reset();
            _player->setCubeVisible(false);
            _player->setShipVisible(false);
            _level->resetObjects();
            _level->resetGroundState();
            _level->resetColorTriggers();
            _level->resetEnterEffectTriggers();
            _level->resetVisibility();
            _colorManager.reset();
            float bgR, bgG, bgB, gR, gG, gB;
            _colorManager.getGLColor(ColorManager::COLOR_BG, bgR, bgG, bgB);
            _colorManager.getGLColor(ColorManager::COLOR_GROUND, gR, gG, gB);
            _level->setGroundColor(gR, gG, gB);
            WinEffects::reset();
            _flightGlitters.clear();
            _resetGameplayState();
            _attempts = 1;
            _showAttemptsLabel = false;
            _fadeState = 2;
            _fadeTimer = 0.0f;
        }
        return;
    } else if (_fadeState == 2) {
        _fadeTimer += dt;
        float t = std::min(_fadeTimer / 0.4f, 1.0f);
        _blackFadeAlpha = 1.0f - t;
        if (t >= 1.0f) {
            _fadeState = 0;
            _blackFadeAlpha = 0.0f;
        }
    }

    if (_shakeTimer > 0.0f) {
        _shakeTimer -= dt;
        if (_shakeTimer <= 0.0f) _shakeTimer = 0.0f;
    }

    if (_flashAlpha > 0.0f) {
        _flashAlpha = std::max(0.0f, _flashAlpha - dt * 2.5f);
    }

    if (_paused) {
        _deltaBuffer = 0.0f;
        return;
    }

    if (_isMenuAnimatingOut) {
        _menuAnimTimer += dt;
        if (_menuAnimTimer >= 0.3f) _isMenuAnimatingOut = false;
    }

    if (_menuActive) {
        _menuPlayTimer += dt;
        float cycle = std::fmod(_menuPlayTimer, 1.5f);
        float e = 0.0f;
        if (cycle < 0.75f) {
            float u = cycle / 0.75f;
            e = (u < 0.5f) ? (2.0f * u * u) : (1.0f - 2.0f * (1.0f - u) * (1.0f - u));
            _menuPlayBtnY = 320.0f + 4.0f * e;
        } else {
            float u = (cycle - 0.75f) / 0.75f;
            e = (u < 0.5f) ? (2.0f * u * u) : (1.0f - 2.0f * (1.0f - u) * (1.0f - u));
            _menuPlayBtnY = 324.0f - 4.0f * e;
        }

        float dx = dt * 60.0f * gravityConst * physicsConst09 * 0.25f;
        _menuCameraX += dx;
        _cameraX = _menuCameraX;
        _slideGroundX += dx;
        _bgScrollX += dx * 0.1f;
        _prevCameraX = _cameraX;

        _level->stepGroundAnimation(dt);
        _level->updateGroundTiles(_slideGroundX, _cameraY, dt);

        _menuGlitterTimer += dt;
        while (_menuGlitterTimer >= 0.035f) {
            _menuGlitterTimer -= 0.035f;
            MenuGlitter mg;
            mg.x = (screenWidth * 0.5f) + ((rand() % 260) - 130);
            mg.y = 320.0f + ((rand() % 200) - 100);
            mg.life = 0.0f;
            mg.maxLife = 1.0f + (rand() % 100) / 100.0f;
            mg.scale = 0.5f;
            _menuParticles.push_back(mg);
        }
        for (auto& mp : _menuParticles) mp.life += dt;
        _menuParticles.erase(
            std::remove_if(_menuParticles.begin(), _menuParticles.end(), [](const MenuGlitter& mp){
                return mp.life >= mp.maxLife;
            }),
            _menuParticles.end()
        );
        return;
    }

    if (_slideIn) {
        float qDt = _quantizeDelta(dt);
        float playerDx = qDt * gravityConst * physicsConst09;
        _playerWorldX += playerDx;
        float groundDx = playerDx * 0.25f;
        _slideGroundX += groundDx;
        _bgScrollX += groundDx * 0.1f;

        _player->updateGroundRotation(qDt * physicsConst09);
        _player->update(dt, _playerWorldX, _cameraY, _cameraX);
        _level->stepGroundAnimation(dt);
        _level->updateGroundTiles(_slideGroundX, _cameraY, dt);
        _level->applyEnterEffects(_cameraX);

        if (_playerWorldX >= 0.0f) {
            _slideIn = false;
            _playerWorldX = 0.0f;
            _cameraX = _playerWorldX - groundYOffset;
            _prevCameraX = _cameraX;
            _audio.startMusic();
            _firstPlay = false;
            _pauseBtnVisible = true;
            _pauseBtnAlpha = 0.0f;
            _pauseBtnFading = true;
            _pauseBtnFadeTimer = 0.0f;
            _pauseBtnFadeDuration = 0.5f;
            _pauseBtnFadeFrom = 0.0f;
            _pauseBtnFadeTo = 75.0f / 255.0f;
        }
        return;
    }

    if (_state.isDead) {
        if (!_deathSoundPlayed) {
            _audio.stopMusic();
            _audio.playEffect("explode_11", 0.65f);
            _deathSoundPlayed = true;
        }
        if (!_newBestShown) {
            _newBestShown = true;
            float endX = _level->endXPos > 0.0f ? _level->endXPos : 6000.0f;
            _lastPercent = std::clamp((int)std::floor((_playerWorldX / endX) * 100.0f), 0, 99);
            if (_lastPercent > _bestPercent) {
                _bestPercent = _lastPercent;
                _hadNewBest = true;
                _showNewBest();
            }
        }
        _player->update(dt, _playerWorldX, _cameraY, _cameraX);
        _deathTimer += dt * 1000.0f;
        if (_deathTimer > (_hadNewBest ? 1400.0f : 1000.0f)) restartLevel();
        return;
    }

    if (!_levelWon && _level->endXPos > 0.0f && _playerWorldX >= _level->endXPos - 600.0f) {
        _levelWon = true;
        _triggerEndPortal();
    }

    if (_levelWon) {
        if (_endCameraOverride && _endCamTweenActive) {
            _endCamTweenTime += dt;
            float t = std::min(_endCamTweenTime / 1.2f, 1.0f);
            float p = (t < 0.5f) ? (std::pow(2.0f * t, 1.8f) * 0.5f) : (1.0f - std::pow(2.0f * (1.0f - t), 1.8f) * 0.5f);
            _cameraX = _endCamFromX + (_endCamToX - _endCamFromX) * p;
            _cameraY = _endCamFromY + (_endCamToY - _endCamFromY) * p;
            if (t >= 1.0f) _endCamTweenActive = false;
        }

        float dx = _cameraX - _prevCameraX;
        _slideGroundX += dx;
        _bgScrollX += dx * 0.1f;
        _prevCameraX = _cameraX;

        _player->update(dt, _playerWorldX, _cameraY, _cameraX);
        _level->stepGroundAnimation(dt);
        _level->updateGroundTiles(_slideGroundX, _cameraY, dt);
        _level->applyEnterEffects(_cameraX);
        _audio.update(dt);
        _endSequenceTimer += dt;
        _updateCompleteLightRays(dt);

        if (_endSequencePhase == 1 && _endSequenceTimer >= 1.95f) {
            _endSequencePhase = 2;
            _completeBannerVisible = true;
            _completeBannerTimer = 0.0f;
            float vX = _level->endXPos - _cameraX + 60.0f;
            float vY = flipY(_endPortalGameY) + _cameraY;

            WinEffects::drawExpandingRing(vX, vY, 10.0f, (float)screenWidth, 800.0f, true, false, colorGreenTint);
            WinEffects::drawExpandingRing(screenWidth * 0.5f, 250.0f, 10.0f, 1000.0f, 800.0f, true, false, colorGreenTint);

            for (int i = 0; i < 5; ++i) {
                WinEffects::drawExpandingRing(vX, vY, 10.0f, (float)screenWidth, 500.0f, false, true, colorGreenTint, i * 50.0f);
            }
            for (int i = 0; i < 10; ++i) {
                float d = std::max(0.0f, 150.0f * i + (rand() % 160 - 80.0f));
                WinEffects::spawnFinishParticles(colorGreenTint, colorCyanTint, d);
            }
        }

        if (_completeBannerVisible) {
            _completeBannerTimer += dt;
            if (_completeBannerTimer <= 0.66f) {
                _completeBannerScale = 1.1f * easeElasticOut(_completeBannerTimer / 0.66f);
            } else if (_completeBannerTimer <= 1.54f) {
                _completeBannerScale = 1.1f;
            } else if (_completeBannerTimer <= 1.76f) {
                float t = (_completeBannerTimer - 1.54f) / 0.22f;
                _completeBannerScale = 1.1f * (1.0f - t * t);
            } else {
                _completeBannerVisible = false;
            }
        }

        if (_endSequencePhase == 2 && _endSequenceTimer >= 3.45f) {
            _endSequencePhase = 3;
            _showEndLayerUI = true;
            _pauseBtnFading = true;
            _pauseBtnFadeTimer = 0.0f;
            _pauseBtnFadeDuration = 0.3f;
            _pauseBtnFadeFrom = _pauseBtnAlpha;
            _pauseBtnFadeTo = 0.0f;
        }

        if (_showEndLayerUI && !_endLayerHiding) {
            float p = std::clamp((_endSequenceTimer - 3.45f) / 1.0f, 0.0f, 1.0f);
            if (p >= 1.0f && !_starAwardStarted) {
                _starAwardStarted = true;
                _starAwardTimer = 0.0f;
            }

            if (_starAwardStarted) {
                _starAwardTimer += dt;
                float sp = std::min(_starAwardTimer / 0.3f, 1.0f);
                float sBounce = easeBounceOut(sp);
                _starAwardScale = 3.0f + (0.8f - 3.0f) * sBounce;
                _starAwardAlpha = sp;

                if (_starAwardTimer >= 0.10f && !_starAwardSoundPlayed) {
                    _starAwardSoundPlayed = true;
                    _audio.playEffect("highscoreGet02");

                    float dropOffsetY = (650.0f * easeBounceOut(1.0f) - 640.0f) - 10.0f;
                    float starX = (screenWidth * 0.5f) + 225.0f;
                    float starY = 265.0f + dropOffsetY;
                    WinEffects::drawExpandingRing(starX, starY, 20.0f, 220.0f, 400.0f, true, false, 16776960);
                    WinEffects::spawnStarParticles(starX, starY, 30);
                }
            }
        }
        return;
    }

    _playTime += dt;
    _audio.update(dt);
    _level->updateAudioScale(_audio.getMeteringValue());

    float qDt = _quantizeDelta(dt);
    int subSteps = qDt > 0.0f ? std::clamp((int)std::round(4.0f * qDt), 1, 60) : 0;
    float subDt = subSteps > 0 ? (qDt / subSteps) * physicsConst09 : 0.0f;
    float subDx = subSteps > 0 ? (qDt / subSteps) : 0.0f;
    float preFrameY = _state.y;

    for (int i = 0; i < subSteps; ++i) {
        _state.lastY = _state.y;
        _player->updateJump(subDt);
        _state.y += _state.yVelocity * subDt;
        _player->checkCollisions(_playerWorldX - groundYOffset, _cameraY);
        _playerWorldX += subDx * gravityConst * physicsConst09;

        if (!_state.isFlying) {
            if (_state.onGround) _player->updateGroundRotation(subDt);
            else _player->updateRotateAction(fixedTimeStep);
        }
    }

    _state.lastY = preFrameY;

    if (!_endCameraOverride) {
        float targetX = _playerWorldX - groundYOffset;
        if (_level->endXPos > 0.0f) {
            float endLockX = _level->endXPos - (float)screenWidth;
            if (targetX >= endLockX - 200.0f) {
                _endCameraOverride = true;
                _endCamTweenActive = true;
                _endCamTweenTime = 0.0f;
                _endCamFromX = _cameraX;
                _endCamToX = endLockX;
                _endCamFromY = _cameraY;
                _endCamToY = -140.0f + _endPortalGameY;
            } else {
                _cameraX = targetX;
            }
        } else {
            _cameraX = targetX;
        }
    }

    if (_endCameraOverride && _endCamTweenActive) {
        _endCamTweenTime += dt;
        float t = std::min(_endCamTweenTime / 1.2f, 1.0f);
        float p = (t < 0.5f) ? (std::pow(2.0f * t, 1.8f) * 0.5f) : (1.0f - std::pow(2.0f * (1.0f - t), 1.8f) * 0.5f);
        _cameraX = _endCamFromX + (_endCamToX - _endCamFromX) * p;
        _cameraY = _endCamFromY + (_endCamToY - _endCamFromY) * p;
        if (t >= 1.0f) _endCamTweenActive = false;
    } else if (!_endCameraOverride) {
        _updateCameraY(qDt);
    }

    float dx = _cameraX - _prevCameraX;
    _slideGroundX += dx;
    _bgScrollX += dx * 0.1f;
    _prevCameraX = _cameraX;

    if (_state.isFlying) _player->updateShipRotation(qDt);

    for (const auto& ct : _level->checkColorTriggers(_playerWorldX)) {
        _colorManager.triggerColor(ct.index, { (int)(ct.r * 255), (int)(ct.g * 255), (int)(ct.b * 255) }, ct.duration);
    }
    _colorManager.step(dt);

    float bgR, bgG, bgB, gR, gG, gB;
    _colorManager.getGLColor(ColorManager::COLOR_BG, bgR, bgG, bgB);
    _colorManager.getGLColor(ColorManager::COLOR_GROUND, gR, gG, gB);
    _level->setGroundColor(gR, gG, gB);

    _level->checkEnterEffectTriggers(_playerWorldX);
    _level->applyEnterEffects(_cameraX);
    _level->stepGroundAnimation(dt);
    _level->updateGroundTiles(_slideGroundX, _cameraY, dt);
    _level->updatePortals(dt, _cameraX);
    _player->update(dt, _playerWorldX, _cameraY, _cameraX);
    _level->updateEndPortalY(_cameraY, _state.isFlying);
    _endPortalGameY = _level->getEndPortalGameY();
}

void GameScene::_triggerEndPortal() {
    _player->playEndAnimation(_level->endXPos, [this]() {
        _levelComplete();
    }, _endPortalGameY);
}

void GameScene::_levelComplete() {
    _audio.fadeOutMusic(1500.0f);
    _audio.playEffect("endStart_02", 0.8f);
    _endSequencePhase = 1;
    _endSequenceTimer = 0.0f;
    _shakeTimer = 1.95f;
    _shakeIntensity = 4.5f;
    _flashAlpha = 1.0f;
    _startCompleteLightRays();

    static const std::vector<std::string> quotes = {
        "Awesome!", "Good\nJob!", "Well\nDone!", "Impressive!",
        "Amazing!", "Incredible!", "Skillful!", "Brilliant!",
        "Not\nbad!", "Warp\nSpeed!", "Challenge\nBreaker!",
        "Reflex\nMaster!", "I am\nspeechless...", "You are...\nThe One!",
        "How is this\npossible!?", "You beat\nme..."
    };
    _completeMessage = quotes[rand() % quotes.size()];
}

void GameScene::_renderNewBest() {
    if (!_newBestActive || _newBestScale <= 0.01f) return;
    float guiScale = getGuiScale();
    float midX = screenWidth * 0.5f;
    float centerY = 320.0f + (300.0f - 320.0f) * guiScale;

    const AtlasFrame* nbAf = findAtlasFrame("GJ_newBest_001.png");
    float origW = nbAf ? nbAf->w : 170.0f;
    float origH = nbAf ? nbAf->h : 40.0f;
    float sc = _newBestScale * guiScale;
    drawAtlasFrame("GJ_newBest_001.png", midX, centerY - (origH * sc) * 0.5f, origW * sc, origH * sc);

    std::string pctStr = std::to_string(_lastPercent) + "%";
    drawBitmapText("bigFont", pctStr, midX, centerY + (2.0f + 25.0f) * sc,
                   1.1f * sc, 1.0f, 1.0f, 1.0f, 1.0f, true);
}

void GameScene::_updateFpsGlyphs(float fpsY) {
    _cachedFpsY = fpsY;
    _cachedFpsGlyphs.clear();
    const BitmapFont* font = getFont("bigFont");
    if (!font) return;
    _cachedFpsTexID = font->textureID;

    float curX = 14.0f;
    int prevChar = -1;
    const float scale = 0.35f;

    for (unsigned char ch : _fpsDisplayText) {
        const BitmapChar* c = font->charLookup[ch];
        if (!c) continue;

        if (prevChar != -1 && !c->kerning.empty()) {
            auto kIt = c->kerning.find(prevChar);
            if (kIt != c->kerning.end()) curX += kIt->second * scale;
        }

        if (c->width > 0 && c->height > 0) {
            FpsGlyphQuad q;
            q.gx = curX + c->xOffset * scale;
            q.gy = fpsY + c->yOffset * scale;
            q.gw = c->width * scale;
            q.gh = c->height * scale;
            q.u0 = c->u0; q.v0 = c->v0;
            q.u1 = c->u1; q.v1 = c->v1;
            _cachedFpsGlyphs.push_back(q);
        }

        curX += c->xAdvance * scale;
        prevChar = ch;
    }
}

void GameScene::render() {
    float bgR, bgG, bgB;
    _colorManager.getGLColor(ColorManager::COLOR_BG, bgR, bgG, bgB);

    if (_bgTexID == 0 || _shakeTimer > 0.0f) {
        RenderDevice::get().clear(bgR, bgG, bgB, 1.0f);
    }
    RenderDevice::get().pushMatrix();

    if (_shakeTimer > 0.0f) {
        float sx = (((rand() % 200) / 100.0f) - 1.0f) * _shakeIntensity;
        float sy = (((rand() % 200) / 100.0f) - 1.0f) * _shakeIntensity;
        RenderDevice::get().translate(sx, sy);
    }

    if (_bgTexID == 0) {
        auto itBg = BootScene::textures.find("game_bg_01");
        if (itBg != BootScene::textures.end() && itBg->second.id != 0) {
            _bgTexID = itBg->second.id;
        } else {
            for (const auto& pair : BootScene::textures) {
                if (pair.second.id != 0 && (pair.first.find("game_bg") != std::string::npos ||
                    pair.first.find("bg_01") != std::string::npos ||
                    pair.first.find("bg") != std::string::npos)) {
                    _bgTexID = pair.second.id;
                    BootScene::textures["game_bg_01"] = pair.second;
                    break;
                }
            }
        }
    }

    if (_bgTexID != 0) {
        RenderDevice::get().drawRepeatedBackground(_bgTexID, _bgScrollX, _cameraY, bgR, bgG, bgB);
    }

    if (!_menuActive) {
        RenderDevice::get().pushMatrix();
        RenderDevice::get().translate(-_cameraX, _cameraY);
        _level->renderLayer0(_cameraX, _cameraY);
        _level->renderLayer1(_cameraX, _cameraY);
        RenderDevice::get().popMatrix();
    }

    if (!_flightGlitters.empty()) {
        for (const auto& fg : _flightGlitters) {
            float pt = fg.life / fg.maxLife;
            float sc = fg.scale * (1.0f - pt);
            float alpha = 1.0f - pt;
            float size = 20.0f * sc;
            drawAtlasFrame("square.png", fg.x - _cameraX, fg.y + _cameraY, size, size, 0.0f, 0.0f, 1.0f, 0.0f, alpha);
        }
    }

    if (!_menuActive) {
        _player->render(_cameraX, _cameraY);
        _level->renderLayer2(_cameraX, _cameraY);
    }

    _level->renderGround(_slideGroundX, _cameraY);

    _renderCompleteLightRays();
    WinEffects::render();

    if (_flashAlpha > 0.0f) {
        RenderDevice::get().drawRect(0.0f, 0.0f, screenWidth, screenHeight,
                                     0.3f, 1.0f, 0.5f, _flashAlpha * 0.9f, BLEND_ADD);
    }
    applyBlendMode(BLEND_NORMAL);

    RenderDevice::get().popMatrix();

    _renderNewBest();

    if (_completeBannerVisible && _completeBannerScale > 0.02f) {
        float guiScale = getGuiScale();
        applyBlendMode(BLEND_NORMAL);
        const AtlasFrame* lcAf = findAtlasFrame("GJ_levelComplete_001.png");
        float lw = (lcAf ? lcAf->w : 400.0f) * _completeBannerScale * guiScale;
        float lh = (lcAf ? lcAf->h : 80.0f) * _completeBannerScale * guiScale;
        drawAtlasFrame("GJ_levelComplete_001.png", screenWidth * 0.5f, 320.0f + (250.0f - 320.0f) * guiScale, lw, lh);
    }

    if (_menuActive || _isMenuAnimatingOut) {
        _renderMenu();
    } else {
        _renderHUD();
    }

    if (_paused && !_showEndLayerUI) {
        _renderPauseOverlay();
    }

    if (_showEndLayerUI) {
        _renderEndLayer();
    }

    // Popups / Overlays
    if (_showInfoPopup) {
        _renderInfoPopup();
    }

    if (_showSettingsPopup) {
        _renderSettingsPopup();
    }

    if (SHOW_FPS) {
        float fpsY = (_menuActive || _paused) ? (62.0f * getGuiScale()) : 12.0f;
        if (_cachedFpsGlyphs.empty() || _cachedFpsY != fpsY) {
            _updateFpsGlyphs(fpsY);
        }

        BlendMode curBlend = RenderDevice::get().getBlendMode();
        for (const auto& q : _cachedFpsGlyphs) {
            RenderDevice::get().batchQuad(
                _cachedFpsTexID,
                q.gx + 1.0f, q.gy + 1.0f, q.u0, q.v0,
                q.gx + q.gw + 1.0f, q.gy + 1.0f, q.u1, q.v0,
                q.gx + q.gw + 1.0f, q.gy + q.gh + 1.0f, q.u1, q.v1,
                q.gx + 1.0f, q.gy + q.gh + 1.0f, q.u0, q.v1,
                0.0f, 0.0f, 0.0f, 0.6f, curBlend
            );
        }
        for (const auto& q : _cachedFpsGlyphs) {
            RenderDevice::get().batchQuad(
                _cachedFpsTexID,
                q.gx, q.gy, q.u0, q.v0,
                q.gx + q.gw, q.gy, q.u1, q.v0,
                q.gx + q.gw, q.gy + q.gh, q.u1, q.v1,
                q.gx, q.gy + q.gh, q.u0, q.v1,
                0.35f, 1.0f, 0.35f, 0.9f, curBlend
            );
        }
    }

    if (_blackFadeAlpha > 0.001f) {
        RenderDevice::get().drawRect(0.0f, 0.0f, screenWidth, screenHeight, 0.0f, 0.0f, 0.0f, _blackFadeAlpha);
    }

    RenderDevice::get().flushBatch();
}

void GameScene::_renderHUD() {
    float guiScale = getGuiScale();
    if (_showAttemptsLabel) {
        float posX = _attemptsLabelX - _cameraX;
        float posY = _attemptsLabelY + _cameraY;
        if (posX >= -350.0f && posX <= screenWidth + 350.0f) {
            drawBitmapText("bigFont", "Attempt " + std::to_string(_attempts),
                           posX, posY, 0.85f * guiScale, 1.0f, 1.0f, 1.0f, 1.0f, true);
        }
    }

    if (_pauseBtnVisible && _pauseBtnAlpha > 0.005f) {
        const AtlasFrame* pbAf = findAtlasFrame("GJ_pauseBtn_clean_001.png");
        float pbW = ((pbAf && pbAf->w > 0.0f) ? pbAf->w : 40.0f) * guiScale;
        float pbH = ((pbAf && pbAf->h > 0.0f) ? pbAf->h : 40.0f) * guiScale;
        float pbCenterX = screenWidth - 30.0f * guiScale;
        float pbCenterY = 30.0f * guiScale;
        drawAtlasFrame("GJ_pauseBtn_clean_001.png", pbCenterX, pbCenterY, pbW, pbH, 0.0f, 1.0f, 1.0f, 1.0f, _pauseBtnAlpha);
    }
}

void GameScene::_renderMenu() {
    float t = std::min(_menuAnimTimer / 0.3f, 1.0f);
    float tPlay = std::min(_menuAnimTimer / 0.2f, 1.0f);
    float ease = t * t;
    float playExitScale = 1.0f - (tPlay * tPlay);
    float midX = screenWidth * 0.5f;
    float midY = screenHeight * 0.5f;
    float guiScale = getGuiScale();

    applyBlendMode(BLEND_ADD);
    for (const auto& mp : _menuParticles) {
        float alpha = 0.6f * (1.0f - mp.life / mp.maxLife) * (1.0f - t);
        float sc = mp.scale * (1.0f - mp.life / mp.maxLife) * guiScale;
        drawAtlasFrame("square.png", mp.x, mp.y, 20.0f * sc, 20.0f * sc, 0.0f, 0.0f, 0.314f, 0.745f, alpha);
    }
    applyBlendMode(BLEND_NORMAL);

    const AtlasFrame* logoAf = findAtlasFrame("GJ_logo_001.png");
    float origLogoW = logoAf ? logoAf->w : 430.0f;
    float origLogoH = logoAf ? logoAf->h : 80.0f;
    float logoW = origLogoW * guiScale;
    float logoH = origLogoH * guiScale;
    float logoY = midY + (100.0f - 320.0f) * guiScale - ease * (100.0f + origLogoH) * guiScale;
    drawAtlasFrame("GJ_logo_001.png", midX, logoY, logoW, logoH);

    const AtlasFrame* tryAf = findAtlasFrame("tryMe_001.png");
    float origTryW = tryAf ? tryAf->w : 140.0f;
    float origTryH = tryAf ? tryAf->h : 50.0f;
    float tryW = origTryW * guiScale;
    float tryH = origTryH * guiScale;
    float tryX = midX + 175.0f * guiScale;
    float tryY = midY + (182.5f - 320.0f) * guiScale - ease * (182.5f + origTryH) * guiScale;
    drawAtlasFrame("tryMe_001.png", tryX, tryY, tryW, tryH);

    if (playExitScale > 0.01f) {
        const AtlasFrame* playFrame = findAtlasFrame("GJ_playBtn_001.png");
        float baseScale = _btnAnims[BTN_MENU_PLAY].scale;
        float origPlayW = playFrame ? playFrame->w : 126.0f;
        float origPlayH = playFrame ? playFrame->h : 126.0f;
        float pw = origPlayW * playExitScale * baseScale * guiScale;
        float ph = origPlayH * playExitScale * baseScale * guiScale;
        float playY = midY + (_menuPlayBtnY - 320.0f) * guiScale;
        drawAtlasFrame("GJ_playBtn_001.png", midX, playY, pw, ph);
    }

    float bottomY = midY + (555.0f - 320.0f) * guiScale + ease * (screenHeight + 50.0f - 555.0f) * guiScale;

    const AtlasFrame* robFrame = findAtlasFrame("RobTopLogoBig_001.png");
    float rw = (robFrame ? robFrame->w : 150.0f) * 0.9f * guiScale;
    float rh = (robFrame ? robFrame->h : 50.0f) * 0.9f * guiScale;
    float rx = midX - 408.0f * guiScale;
    drawAtlasFrame("RobTopLogoBig_001.png", rx, bottomY, rw, rh);

    auto drawStoreBtn = [&](const std::string& name, ButtonId id, float px) {
        const AtlasFrame* af = findAtlasFrame(name);
        float curScale = _btnAnims[id].scale;
        float bw = (af ? af->w : 140.0f) * curScale * guiScale;
        float bh = (af ? af->h : 45.0f) * curScale * guiScale;
        drawAtlasFrame(name, px, bottomY, bw, bh);
    };

    drawStoreBtn("downloadSteam_001.png",  BTN_MENU_STEAM,  midX + 438.0f * guiScale);
    drawStoreBtn("downloadGoogle_001.png", BTN_MENU_GOOGLE, midX + 228.0f * guiScale);
    drawStoreBtn("downloadApple_001.png",  BTN_MENU_APPLE,  midX + 18.0f * guiScale);

    auto drawCornerIcon = [&](const std::string& name, ButtonId id, float px, float py, float r, float g, float b, float a) {
        const AtlasFrame* af = findAtlasFrame(name);
        float curScale = _btnAnims[id].scale;
        float iw = (af ? af->w : 40.0f) * curScale * guiScale;
        float ih = (af ? af->h : 40.0f) * curScale * guiScale;
        drawAtlasFrame(name, px, py - ease * (py + ih), iw, ih, 0.0f, r, g, b, a);
    };

    std::string fsTexture = _isFullscreen ? "toggleFullscreenOff_001.png" : "toggleFullscreenOn_001.png";
    float cornerAlpha = 0.8f * (1.0f - ease);
    float cornerOffset = 33.0f * guiScale;

    // Top-left corner: Fullscreen
    drawCornerIcon(fsTexture, BTN_MENU_FS, cornerOffset, cornerOffset, 0.0f, 102.0f / 255.0f, 1.0f, cornerAlpha);

    // Top-right corner: Settings button (pause menu icon)
    drawCornerIcon("GJ_menuBtn_001.png", BTN_MENU_SETTINGS, screenWidth - cornerOffset, cornerOffset, 1.0f, 1.0f, 1.0f, cornerAlpha);

    // Stacked below Settings: Info button
    drawCornerIcon("GJ_infoIcon_001.png", BTN_MENU_INFO, screenWidth - cornerOffset, 85.0f * guiScale, 0.0f, 102.0f / 255.0f, 1.0f, cornerAlpha);

    drawGenericText("© 2026 RobTop Games · geometrydash.com", screenWidth - 20.0f * guiScale, midY + (625.0f - 320.0f) * guiScale + ease * (680.0f - 625.0f) * guiScale, 14.0f * guiScale, 1.0f, 1.0f, 1.0f, 0.30f, 2);
}

void GameScene::_renderSlider(float centerX, float centerY, float progress, bool isDragging,
                              const std::string& iconName, const std::string& textLabel,
                              const std::string& valueText)
{
    float guiScale = getGuiScale();
    const float grooveScale = 0.7f * guiScale;
    const AtlasFrame* grooveAf = findAtlasFrame("slidergroove.png");
    const float origGrooveW = (grooveAf && grooveAf->w > 0.0f) ? grooveAf->w : 420.0f;
    const float origGrooveH = (grooveAf && grooveAf->h > 0.0f) ? grooveAf->h : 24.0f;
    const float grooveW = origGrooveW * grooveScale;
    const float grooveH = origGrooveH * grooveScale;
    const float trackWidth = (origGrooveW - 8.0f) * grooveScale;
    const float barH = 16.0f * grooveScale; // Exactly 11.2f * guiScale to preserve aspect ratio with slidergroove

    const float trackStartX = centerX - (origGrooveW * grooveScale) * 0.5f + 2.8f * guiScale;
    const float clampedProg = std::clamp(progress, 0.0f, 1.0f);
    const float fillW = (clampedProg < 0.01f) ? 0.0f : (clampedProg * trackWidth);

    if (!iconName.empty()) {
        const AtlasFrame* iconAf = findAtlasFrame(iconName);
        float iconW = (iconAf && iconAf->w > 0.0f) ? iconAf->w * 1.2f * guiScale : 43.2f * guiScale;
        float iconH = (iconAf && iconAf->h > 0.0f) ? iconAf->h * 1.2f * guiScale : 43.2f * guiScale;
        drawAtlasFrame(iconName, centerX - 185.0f * guiScale, centerY, iconW, iconH);
    } else if (!textLabel.empty()) {
        drawBitmapText("bigFont", textLabel, centerX - 185.0f * guiScale, centerY - 2.0f * guiScale, 0.50f * guiScale, 1.0f, 1.0f, 1.0f, 1.0f, true);
    }

    if (!valueText.empty()) {
        drawBitmapText("bigFont", valueText, centerX, centerY - 22.0f * guiScale, 0.45f * guiScale, 1.0f, 1.0f, 1.0f, 1.0f, true);
    }

    if (fillW > 0.0f) {
        float yTop = centerY - (barH * 0.5f);
        float yBot = centerY + (barH * 0.5f);

        uint32_t texID = 0;
        float texW = 0.0f;
        float texH = 0.0f;
        float u0_base = 0.0f, v0_base = 0.0f, u1_base = 1.0f, v1_base = 1.0f;

        auto it = BootScene::textures.find("sliderBar");
        if (it != BootScene::textures.end() && it->second.id != 0) {
            texID = it->second.id;
            texW = (float)it->second.width;
            texH = (float)it->second.height;
        } else {
            const AtlasFrame* barAf = findAtlasFrame("sliderBar.png");
            if (!barAf) barAf = findAtlasFrame("sliderBar_001.png");
            if (barAf && BootScene::textures.find("GJ_WebSheet") != BootScene::textures.end()) {
                texID = BootScene::textures["GJ_WebSheet"].id;
                texW = barAf->w;
                texH = barAf->h;
                u0_base = barAf->u0; v0_base = barAf->v0;
                u1_base = barAf->u1; v1_base = barAf->v1;
            }
        }

        if (texID != 0 && texW > 0.0f && texH > 0.0f) {
            // Natural aspect ratio of sliderBar (64x16 -> 4.0f)
            float aspect = texW / texH;
            // To preserve exact 1:1 proportion without stretching or distortion,
            // screen repeat cycle width is barH * aspect.
            // For 64x16 with grooveScale 0.7: barH = 11.2, tileW = 44.8px (= 64 * 0.7).
            // For HD textures (256x64, 512x128, etc.): aspect = 4.0, tileW = 44.8px.
            // Compatible with textures of any resolution and aspect ratio!
            float tileW = barH * aspect;
            if (tileW <= 0.0f) tileW = barH * 4.0f;

            float uSpan = u1_base - u0_base;
            float drawn = 0.0f;
            while (drawn < fillW) {
                float curW = std::min(tileW, fillW - drawn);
                float frac = curW / tileW;
                float segU0 = u0_base;
                float segU1 = u0_base + uSpan * frac;

                RenderDevice::get().batchQuad(
                    texID,
                    trackStartX + drawn,        yTop, segU0, v0_base,
                    trackStartX + drawn + curW, yTop, segU1, v0_base,
                    trackStartX + drawn + curW, yBot, segU1, v1_base,
                    trackStartX + drawn,        yBot, segU0, v1_base,
                    1.0f, 1.0f, 1.0f, 1.0f, BLEND_NORMAL
                );
                drawn += curW;
            }
            RenderDevice::get().flushBatch();
        } else {
            RenderDevice::get().drawRect(trackStartX, yTop, fillW, barH, 0.25f, 0.85f, 0.15f, 1.0f);
        }
    }

    drawAtlasFrame("slidergroove.png", centerX, centerY, grooveW, grooveH);

    std::string thumbName = isDragging ? "sliderthumbsel.png" : "sliderthumb.png";
    const AtlasFrame* thAf = findAtlasFrame(thumbName);
    float thW = (thAf && thAf->w > 0.0f) ? thAf->w * grooveScale : 28.0f * guiScale;
    float thH = (thAf && thAf->h > 0.0f) ? thAf->h * grooveScale : thW;
    float thumbX = trackStartX + (clampedProg * trackWidth);
    drawAtlasFrame(thumbName, thumbX, centerY, thW, thH);
}

void GameScene::_renderPauseOverlay() {
    float guiScale = getGuiScale();
    float midX = screenWidth * 0.5f;
    float midY = screenHeight * 0.5f;

    RenderDevice::get().drawRect(0.0f, 0.0f, screenWidth, screenHeight, 0.0f, 0.0f, 0.0f, 75.0f / 255.0f);

    float boxW = (1136.0f - 40.0f) * guiScale;
    float boxH = 600.0f * guiScale;
    drawScale9("square04_001", midX, midY, boxW, boxH, 52.0f * guiScale, 0.0f, 0.0f, 0.0f, 150.0f / 255.0f);

    std::string fsTexture = _isFullscreen ? "toggleFullscreenOff_001.png" : "toggleFullscreenOn_001.png";
    const AtlasFrame* fsFrame = findAtlasFrame(fsTexture);
    float fsScale = _btnAnims[BTN_PAUSE_FS].scale;
    float fsw = (fsFrame ? fsFrame->w : 40.0f) * fsScale * guiScale;
    float fsh = (fsFrame ? fsFrame->h : 40.0f) * fsScale * guiScale;
    float fsX = (midX - boxW * 0.5f) + 40.0f * guiScale;
    float fsY = midY - (300.0f - 60.0f) * guiScale;
    drawAtlasFrame(fsTexture, fsX, fsY, fsw, fsh);

    drawBitmapText("bigFont", "Stereo Madness", midX, midY - (300.0f - 65.0f) * guiScale, 0.70f * guiScale, 1.0f, 1.0f, 1.0f, 1.0f, true);
    drawBitmapText("bigFont", "Normal Mode", midX, midY - (300.0f - 130.0f) * guiScale, 0.55f * guiScale, 1.0f, 1.0f, 1.0f, 1.0f, true);

    const AtlasFrame* barFrame = findAtlasFrame("GJ_progressBar_001.png");
    float origW = (barFrame && barFrame->w > 0.0f) ? barFrame->w : 680.0f;
    float origH = (barFrame && barFrame->h > 0.0f) ? barFrame->h : 40.0f;
    float barY = midY - (300.0f - 170.0f) * guiScale;
    drawAtlasFrame("GJ_progressBar_001.png", midX, barY, origW * guiScale, origH * guiScale, 0.0f, 0.0f, 0.0f, 0.0f, 125.0f / 255.0f);

    int percent = std::clamp(_bestPercent, 0, 100);
    if (percent > 0 && barFrame && BootScene::textures.find("GJ_WebSheet") != BootScene::textures.end()) {
        uint32_t texID = BootScene::textures["GJ_WebSheet"].id;

        float scaleX = 0.992f;
        float scaleY = 0.86f;
        float totalW = origW * scaleX * guiScale;
        float totalH = origH * scaleY * guiScale;

        float startX = midX - (totalW * 0.5f);
        float startY = barY - (totalH * 0.5f);

        float cropW = std::max(1.0f, std::floor(origW * (percent / 100.0f)));
        float drawW = cropW * scaleX * guiScale;

        float u0_bar = barFrame->u0;
        float v0_bar = barFrame->v0;
        float u1_bar = u0_bar + (barFrame->u1 - u0_bar) * (cropW / origW);
        float v1_bar = barFrame->v1;

        RenderDevice::get().batchQuad(
            texID,
            startX,         startY,          u0_bar, v0_bar,
            startX + drawW, startY,          u1_bar, v0_bar,
            startX + drawW, startY + totalH, u1_bar, v1_bar,
            startX,         startY + totalH, u0_bar, v1_bar,
            0.0f, 1.0f, 0.0f, 1.0f, BLEND_NORMAL
        );
        RenderDevice::get().flushBatch();
    }

    drawBitmapText("bigFont", std::to_string(percent) + "%", midX, barY, 0.50f * guiScale, 1.0f, 1.0f, 1.0f, 1.0f, true);

    struct PauseBtnDef { std::string frame; ButtonId id; };
    PauseBtnDef pauseBtns[3] = {
        {"GJ_replayBtn_001.png", BTN_PAUSE_REPLAY},
        {"GJ_playBtn2_001.png",  BTN_PAUSE_PLAY},
        {"GJ_menuBtn_001.png",   BTN_PAUSE_MENU}
    };

    float btnWidths[3];
    float totalBtnW = 0.0f;
    for (int i = 0; i < 3; ++i) {
        const AtlasFrame* af = findAtlasFrame(pauseBtns[i].frame);
        btnWidths[i] = ((af && af->w > 0.0f) ? af->w : 85.0f) * guiScale;
        totalBtnW += btnWidths[i];
    }
    float spacing = 40.0f * guiScale;
    totalBtnW += spacing * (3 - 1);

    float btnY = midY + (330.0f - 320.0f) * guiScale;
    float btnStartX = midX - totalBtnW * 0.5f;
    for (int i = 0; i < 3; ++i) {
        float posX = btnStartX + btnWidths[i] * 0.5f;
        const AtlasFrame* af = findAtlasFrame(pauseBtns[i].frame);
        float sc = _btnAnims[pauseBtns[i].id].scale * guiScale;
        float bw = (af ? af->w : 85.0f) * sc;
        float bh = (af ? af->h : 85.0f) * sc;
        drawAtlasFrame(pauseBtns[i].frame, posX, btnY, bw, bh);
        btnStartX += btnWidths[i] + spacing;
    }

    float sliderY = midY + (470.0f - 320.0f) * guiScale;
    _renderSlider(midX - 220.0f * guiScale, sliderY, _audio.getUserMusicVolume(), _draggingMusicSlider,
                  "gj_songIcon_001.png", "", "");
    _renderSlider(midX + 220.0f * guiScale, sliderY, _sfxVolume, _draggingSfxSlider,
                  "GJ_sfxIcon_001.png", "", "");
}

// -------------------------------------------------------------
// SETTINGS WINDOW
// -------------------------------------------------------------
void GameScene::_renderSettingsPopup() {
    float guiScale = getGuiScale();
    float midX = screenWidth * 0.5f;
    RenderDevice::get().drawRect(0.0f, 0.0f, screenWidth, screenHeight, 0.0f, 0.0f, 0.0f, 100.0f / 255.0f);

    #if defined(_WIN32)
    const float basePopupH = 340.0f;
    #else
    const float basePopupH = 260.0f;
    #endif
    const float popupW = 480.0f * guiScale;
    const float popupH = basePopupH * guiScale;

    drawScale9("GJ_square02", midX, 320.0f, popupW, popupH, 52.0f * guiScale, 1.0f, 1.0f, 1.0f, 1.0f);

    const AtlasFrame* closeAf = findAtlasFrame("GJ_closeBtn_001.png");
    float closeSc = _btnAnims[BTN_SETTINGS_CLOSE].scale * guiScale;
    float cw = (closeAf ? closeAf->w : 40.0f) * closeSc;
    float ch = (closeAf ? closeAf->h : 40.0f) * closeSc;
    float closeY = 320.0f - (popupH * 0.5f) + 24.0f * guiScale;
    drawAtlasFrame("GJ_closeBtn_001.png", midX - 220.0f * guiScale, closeY, cw, ch);

    // "Settings" Title
    float titleY = 320.0f - (popupH * 0.5f) + 38.0f * guiScale;
    drawBitmapText("bigFont", "Settings", midX, titleY, 0.70f * guiScale, 1.0f, 1.0f, 1.0f, 1.0f, true);

    // 1. FPS Limiter
    float fpsProgress = (float)Settings::get().fpsIndex / (float)(Settings::get().fpsOptions.size() - 1);
    float fpsY = 320.0f - (popupH * 0.5f) + 114.0f * guiScale;
    _renderSlider(midX, fpsY, fpsProgress, _draggingFpsSlider,
                  "", "FPS", Settings::get().currentFps().label);

    // 2. Graphics Renderer Selector (Windows only)
    #if defined(_WIN32)
    if (!Settings::get().rendererOptions.empty()) {
        float rendY = 320.0f - (popupH * 0.5f) + 185.0f * guiScale;
        drawBitmapText("goldFont", "Renderer", midX, rendY - 26.0f * guiScale, 0.50f * guiScale, 1.0f, 0.85f, 0.2f, 1.0f, true);

        float prevScale = _btnAnims[BTN_SETTINGS_RENDER_PREV].scale * guiScale;
        float nextScale = _btnAnims[BTN_SETTINGS_RENDER_NEXT].scale * guiScale;
        drawBitmapText("bigFont", "<", midX - 115.0f * guiScale, rendY, 0.60f * prevScale, 1.0f, 0.9f, 0.2f, 1.0f, true);
        drawBitmapText("bigFont", ">", midX + 115.0f * guiScale, rendY, 0.60f * nextScale, 1.0f, 0.9f, 0.2f, 1.0f, true);

        std::string rendLabel = Settings::get().currentRendererLabel();
        float boxScale = _btnAnims[BTN_SETTINGS_RENDER_BOX].scale * guiScale;
        drawBitmapText("bigFont", rendLabel, midX, rendY + 3.0f * guiScale, 0.50f * boxScale, 1.0f, 1.0f, 1.0f, 1.0f, true);

        if (Settings::get().currentBackend() != RenderDevice::get().getBackend()) {
            drawBitmapText("goldFont", "(Restart game to apply)", midX, rendY + 28.0f * guiScale, 0.38f * guiScale, 1.0f, 0.45f, 0.35f, 1.0f, true);
        }
    }
    #endif
}

// -------------------------------------------------------------
// INFO WINDOW (Credits and links)
// -------------------------------------------------------------
void GameScene::_renderInfoPopup() {
    float guiScale = getGuiScale();
    float midX = screenWidth * 0.5f;
    RenderDevice::get().drawRect(0.0f, 0.0f, screenWidth, screenHeight, 0.0f, 0.0f, 0.0f, 100.0f / 255.0f);

    const float popupW = 480.0f * guiScale;
    const float popupH = 336.0f * guiScale;

    drawScale9("GJ_square02", midX, 320.0f, popupW, popupH, 52.0f * guiScale, 1.0f, 1.0f, 1.0f, 1.0f);

    const AtlasFrame* closeAf = findAtlasFrame("GJ_closeBtn_001.png");
    float closeSc = _btnAnims[BTN_INFO_CLOSE].scale * guiScale;
    float cw = (closeAf ? closeAf->w : 40.0f) * closeSc;
    float ch = (closeAf ? closeAf->h : 40.0f) * closeSc;
    drawAtlasFrame("GJ_closeBtn_001.png", midX - 220.0f * guiScale, 320.0f - (popupH * 0.5f) + 24.0f * guiScale, cw, ch);

    drawBitmapText("bigFont", "Credits", midX, 320.0f - 100.0f * guiScale, 0.70f * guiScale, 1.0f, 1.0f, 1.0f, 1.0f, true);
    drawBitmapText("goldFont", "Made by RobTop Games", midX, 320.0f - 56.0f * guiScale, 0.55f * guiScale, 1.0f, 0.8f, 0.2f, 1.0f, true);
    drawBitmapText("goldFont", "Song: Stereo Madness", midX, 320.0f - 20.0f * guiScale, 0.55f * guiScale, 1.0f, 0.8f, 0.2f, 1.0f, true);

    float textW = 145.0f;
    const BitmapFont* gf = getFont("goldFont");
    if (gf) {
        float tw = 0.0f;
        for (char ch : std::string("by ForeverBound")) {
            const BitmapChar* c = gf->charLookup[(unsigned char)ch];
            if (c) tw += c->xAdvance * 0.55f;
        }
        if (tw > 0.0f) textW = tw;
    }

    const AtlasFrame* ytAf = findAtlasFrame("gj_ytIcon_001.png");
    float ytSc = _btnAnims[BTN_INFO_YT].scale * guiScale;
    float baseW = (ytAf && ytAf->w > 0.0f) ? ytAf->w : 64.0f;
    float baseH = (ytAf && ytAf->h > 0.0f) ? ytAf->h : 44.0f;
    float ytw = baseW * ytSc;
    float yth = baseH * ytSc;

    const float spacing = 12.0f * guiScale;
    const float ytDrawW = baseW * 0.50f * guiScale;
    float totalGroupW = textW * guiScale + spacing + ytDrawW;
    float groupStartX = midX - (totalGroupW * 0.5f);
    float textX = groupStartX + (textW * 0.5f * guiScale);
    float ytX = groupStartX + textW * guiScale + spacing + (ytDrawW * 0.5f);
    float creditLine3Y = 320.0f + 14.0f * guiScale;

    drawBitmapText("goldFont", "by ForeverBound", textX, creditLine3Y, 0.55f * guiScale, 1.0f, 0.8f, 0.2f, 1.0f, true);
    drawAtlasFrame("gj_ytIcon_001.png", ytX, creditLine3Y, ytw, yth);

    drawGenericText("© 2026 RobTop Games. All rights reserved.", midX, 320.0f + 100.0f * guiScale, 12.0f * guiScale, 0.0f, 0.0f, 0.0f, 0.7f, 1);
    drawGenericText("Unauthorized copying, distribution, or hosting of this demo is prohibited.", midX, 320.0f + 118.0f * guiScale, 12.0f * guiScale, 0.0f, 0.0f, 0.0f, 0.7f, 1);
}

void GameScene::_renderEndLayer() {
    applyBlendMode(BLEND_NORMAL);
    float guiScale = getGuiScale();
    float midX = screenWidth * 0.5f;
    float dropOffsetY = 0.0f;
    float overlayAlpha = 0.0f;

    if (_endLayerHiding) {
        float t = std::min(_endLayerHideTimer / 0.5f, 1.0f);
        float ease = (t < 0.5f) ? (2.0f * t * t) : (1.0f - 2.0f * (1.0f - t) * (1.0f - t));
        dropOffsetY = -640.0f * ease * guiScale;
        overlayAlpha = (100.0f / 255.0f) * (1.0f - t);
    } else {
        float p = 1.0f;
        if (_endSequencePhase >= 3) {
            p = std::clamp((_endSequenceTimer - 3.45f) / 1.0f, 0.0f, 1.0f);
        }
        float bp = easeBounceOut(p);
        dropOffsetY = ((650.0f * bp - 640.0f) - 10.0f) * guiScale;
        overlayAlpha = (100.0f / 255.0f) * p;
    }

    RenderDevice::get().drawRect(0.0f, 0.0f, screenWidth, screenHeight, 0.0f, 0.0f, 0.0f, overlayAlpha);

    RenderDevice::get().pushMatrix();
    RenderDevice::get().translate(0.0f, dropOffsetY);

    const AtlasFrame* topAf = findAtlasFrame("GJ_table_top_001.png");
    float topW = (topAf && topAf->w > 0.0f) ? topAf->w * guiScale : 712.0f * guiScale;
    float sideOffset = (topW * 0.5f) - 31.0f * guiScale;
    float boxW = (sideOffset * 2.0f) + 12.0f * guiScale;
    float boxH = 465.0f * guiScale;
    float boxCenterY = 320.0f - 5.0f * guiScale;

    const AtlasFrame* chainAf = findAtlasFrame("chain_01_001.png");
    float chw = (chainAf ? chainAf->w : 22.0f) * guiScale;
    float chh = 90.0f * guiScale;
    float chainBottomY = 320.0f + (28.0f - 320.0f) * guiScale;
    drawAtlasFrame("chain_01_001.png", midX - 312.0f * guiScale, chainBottomY - chh * 0.5f, chw, chh);
    drawAtlasFrame("chain_01_001.png", midX + 312.0f * guiScale, chainBottomY - chh * 0.5f, chw, chh);

    RenderDevice::get().drawRect(midX - boxW * 0.5f, boxCenterY - boxH * 0.5f, boxW, boxH, 0.0f, 0.0f, 0.0f, 180.0f / 255.0f);

    const AtlasFrame* sideAf = findAtlasFrame("GJ_table_side_001.png");
    float sw = (sideAf ? sideAf->w : 40.0f) * guiScale;
    drawAtlasFrame("GJ_table_side_001.png", midX - sideOffset, boxCenterY, sw, boxH);
    drawAtlasFrame("GJ_table_side_001.png", midX + sideOffset, boxCenterY, sw, boxH, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, true, false);

    const AtlasFrame* botAf = findAtlasFrame("GJ_table_bottom_001.png");
    float topH = (topAf ? topAf->h : 80.0f) * guiScale;
    float botH = (botAf ? botAf->h : 80.0f) * guiScale;
    drawAtlasFrame("GJ_table_top_001.png", midX, 320.0f + (75.0f - 320.0f) * guiScale, topW, topH);
    drawAtlasFrame("GJ_table_bottom_001.png", midX, 320.0f + (555.0f - 320.0f) * guiScale, topW, botH);

    const AtlasFrame* titleAf = findAtlasFrame("GJ_levelComplete_001.png");
    float tlw = (titleAf ? titleAf->w : 400.0f) * 0.8f * guiScale;
    float tlh = (titleAf ? titleAf->h : 80.0f) * 0.8f * guiScale;
    drawAtlasFrame("GJ_levelComplete_001.png", midX, 320.0f + (160.0f - 320.0f) * guiScale, tlw, tlh);

    float statsScale = 0.55f * guiScale;
    drawBitmapText("goldFont", "Attempts: " + std::to_string(_attempts), midX, 320.0f + (245.0f - 320.0f) * guiScale, statsScale, 1.0f, 0.8f, 0.2f, 1.0f, true);
    drawBitmapText("goldFont", "Jumps: " + std::to_string(_totalJumps), midX, 320.0f + (290.0f - 320.0f) * guiScale, statsScale, 1.0f, 0.8f, 0.2f, 1.0f, true);
    drawBitmapText("goldFont", "Time: " + formatPlayTime(_playTime), midX, 320.0f + (335.0f - 320.0f) * guiScale, statsScale, 1.0f, 0.8f, 0.2f, 1.0f, true);
    drawBitmapText("bigFont", _completeMessage, midX + 225.0f * guiScale, 320.0f + (335.0f - 320.0f) * guiScale, 0.65f * guiScale, 1.0f, 1.0f, 1.0f, 1.0f, true);

    if (_starAwardStarted && _starAwardAlpha > 0.01f) {
        const AtlasFrame* starAf = findAtlasFrame("GJ_bigStar_001.png");
        float stw = (starAf ? starAf->w : 64.0f) * _starAwardScale * guiScale;
        float sth = (starAf ? starAf->h : 64.0f) * _starAwardScale * guiScale;
        drawAtlasFrame("GJ_bigStar_001.png", midX + 225.0f * guiScale, 320.0f + (265.0f - 320.0f) * guiScale, stw, sth, 0.0f, 1.0f, 1.0f, 1.0f, _starAwardAlpha);
    }

    const AtlasFrame* getItAf = findAtlasFrame("getIt_001.png");
    float giw = ((getItAf ? getItAf->w : 120.0f) / 1.5f) * guiScale;
    float gih = ((getItAf ? getItAf->h : 60.0f) / 1.5f) * guiScale;
    drawAtlasFrame("getIt_001.png", midX - 225.0f * guiScale, 320.0f + (340.0f - 320.0f) * guiScale, giw, gih);

    auto drawEndStoreBtn = [&](const std::string& name, ButtonId id, float px) {
        const AtlasFrame* af = findAtlasFrame(name);
        float sc = _btnAnims[id].scale * guiScale;
        float bw = (af ? af->w : 140.0f) * sc;
        float bh = (af ? af->h : 45.0f) * sc;
        drawAtlasFrame(name, px, 320.0f + (440.0f - 320.0f) * guiScale, bw, bh);
    };

    drawEndStoreBtn("downloadApple_001.png",  BTN_END_APPLE,  midX - 225.0f * guiScale);
    drawEndStoreBtn("downloadGoogle_001.png", BTN_END_GOOGLE, midX);
    drawEndStoreBtn("downloadSteam_001.png",  BTN_END_STEAM,  midX + 225.0f * guiScale);

    auto drawEndNavBtn = [&](const std::string& name, ButtonId id, float px, float py) {
        const AtlasFrame* af = findAtlasFrame(name);
        float sc = _btnAnims[id].scale * guiScale;
        float bw = (af ? af->w : 85.0f) * sc;
        float bh = (af ? af->h : 85.0f) * sc;
        drawAtlasFrame(name, px, py, bw, bh);
    };

    float navY = 320.0f + (545.0f - 320.0f) * guiScale;
    drawEndNavBtn("GJ_replayBtn_001.png", BTN_END_REPLAY, midX - 160.0f * guiScale, navY);
    drawEndNavBtn("GJ_menuBtn_001.png",   BTN_END_MENU,   midX + 160.0f * guiScale, navY);

    RenderDevice::get().popMatrix();
}
