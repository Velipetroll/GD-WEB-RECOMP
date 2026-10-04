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

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>
#endif

static constexpr bool DEBUG_SPAWN_AT_END = 0;

static void openURL(const std::string& url) {
#ifdef __EMSCRIPTEN__
    EM_ASM({
        window.open(UTF8ToString($0), '_blank');
    }, url.c_str());
#else
    SDL_OpenURL(url.c_str());
#endif
}

static void toggleFullscreen(SDL_Window* window) {
    if (!window) return;
#ifdef __EMSCRIPTEN__
    EmscriptenFullscreenChangeEvent fsStatus;
    if (emscripten_get_fullscreen_status(&fsStatus) == EMSCRIPTEN_RESULT_SUCCESS && fsStatus.isFullscreen) {
        emscripten_exit_fullscreen();
    } else {
        emscripten_request_fullscreen("#canvas", 1);
    }
#else
    Uint32 flags = SDL_GetWindowFlags(window);
    if (flags & SDL_WINDOW_FULLSCREEN_DESKTOP) {
        SDL_SetWindowFullscreen(window, 0);
    } else {
        SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
    }
#endif
}

static float easeElasticOut(float t) {
    if (t <= 0.0f) return 0.01f;
    if (t >= 1.0f) return 1.0f;
    float p = 0.6f;
    return std::pow(2.0f, -10.0f * t) * std::sin((t - p / 4.0f) * (2.0f * 3.14159265f) / p) + 1.0f;
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
    _btnAnims[BTN_SETTINGS_QUALITY_PREV].init(1.0f);
    _btnAnims[BTN_SETTINGS_QUALITY_NEXT].init(1.0f);
    _btnAnims[BTN_SETTINGS_QUALITY_BOX].init(1.0f);
    _btnAnims[BTN_SETTINGS_SHOW_FPS_PREV].init(1.0f);
    _btnAnims[BTN_SETTINGS_SHOW_FPS_NEXT].init(1.0f);
    _btnAnims[BTN_SETTINGS_SHOW_FPS_BOX].init(1.0f);

    _pauseBtnVisible = false;
    _pauseBtnAlpha = 0.0f;
    _pauseBtnFading = false;

    if (Settings::get().showFps) {
        _lastFpsUpdateTick = SDL_GetTicks();
        _fpsFrameCount = 0;
        _fpsText = "60 FPS";
        _fpsDisplayText = std::string("60 FPS - ") + RenderDevice::get().getBackendName();
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
    _menuParticles.clear();

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

void GameScene::onAppPause() {
    if (_appSuspended) return;
    _appSuspended = true;

    // Record whether music was playing
    _musicWasPlayingBeforeSuspend = _audio.isMusicPlaying();

    // If in gameplay, pause the game so player doesn't crash while away
    if (!_paused && !_menuActive && !_slideIn && !_state.isDead && !_levelWon) {
        pauseGame();
    } else {
        _audio.pauseMusic();
    }

    _audio.suspendAudio();
}

void GameScene::onAppResume() {
    if (!_appSuspended) return;
    _appSuspended = false;

    _audio.resumeAudio();

    // If the game is still paused, don't resume music (pause menu doesn't play level music in GD).
    // Only resume music if the game is NOT paused and music was playing before suspend.
    if (!_paused && _musicWasPlayingBeforeSuspend) {
        _audio.resumeMusic();
    }
}


void GameScene::handleEvent(const SDL_Event& event, int windowW, int windowH, SDL_Window* window) {
    #ifdef __EMSCRIPTEN__
    _isFullscreen = EM_ASM_INT({
        return (document.fullscreenElement || document.webkitFullscreenElement) ? 1 : 0;
    }) != 0;
    #else
    if (window) {
        Uint32 flags = SDL_GetWindowFlags(window);
        _isFullscreen = (flags & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
    }
    #endif

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
    } else if (event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEMOTION || event.type == SDL_MOUSEBUTTONUP ||
        event.type == SDL_FINGERDOWN || event.type == SDL_FINGERMOTION || event.type == SDL_FINGERUP) {

        int mouseX = 0, mouseY = 0;
    bool isDown = false, isMotion = false, isUp = false;

    if (event.type == SDL_FINGERDOWN || event.type == SDL_FINGERMOTION || event.type == SDL_FINGERUP) {
        mouseX = (int)(event.tfinger.x * (float)windowW);
        mouseY = (int)(event.tfinger.y * (float)windowH);
        isDown = (event.type == SDL_FINGERDOWN);
        isMotion = (event.type == SDL_FINGERMOTION);
        isUp = (event.type == SDL_FINGERUP);
    } else {
        mouseX = (event.type == SDL_MOUSEMOTION) ? event.motion.x : event.button.x;
        mouseY = (event.type == SDL_MOUSEMOTION) ? event.motion.y : event.button.y;
        isDown = (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT);
        isMotion = (event.type == SDL_MOUSEMOTION);
        isUp = (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_LEFT);
    }

    float virtX = 0.0f;
    float virtY = 0.0f;

    #ifdef __EMSCRIPTEN__
    // Obtiene las coordenadas proporcionales al tamaño real del canvas en pantalla
    virtX = (float)EM_ASM_DOUBLE({
        var c = Module['canvas'] || document.getElementById('canvas') || document.querySelector('canvas');
        if (!c) return $0;
        var rect = c.getBoundingClientRect();
        if (rect.width <= 0) return $0;
        var clientX = (window.lastClientX !== undefined && window.lastClientX !== null) ? window.lastClientX : (event && event.clientX !== undefined ? event.clientX : $0);
        return ((clientX - rect.left) / rect.width) * $1;
    }, mouseX, (double)screenWidth);

    virtY = (float)EM_ASM_DOUBLE({
        var c = Module['canvas'] || document.getElementById('canvas') || document.querySelector('canvas');
        if (!c) return $0;
        var rect = c.getBoundingClientRect();
        if (rect.height <= 0) return $0;
        var clientY = (window.lastClientY !== undefined && window.lastClientY !== null) ? window.lastClientY : (event && event.clientY !== undefined ? event.clientY : $0);
        return ((clientY - rect.top) / rect.height) * 640.0;
    }, mouseY);
    #else
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

    virtX = (float)(mouseX - vpX) * ((float)screenWidth / (float)vpW);
    virtY = (float)(mouseY - vpY) * ((float)screenHeight / (float)vpH);
    #endif

    float midX = screenWidth * 0.5f;
    float guiScale = getGuiScale();

    const float grooveScale = 0.7f * guiScale;
    const AtlasFrame* grooveAf = findAtlasFrame("slidergroove.png");
    const float origGrooveW = (grooveAf && grooveAf->w > 0.0f) ? grooveAf->w : 420.0f;
    const float trackWidth = (origGrooveW - 8.0f) * grooveScale;
    const float halfGrooveW = (origGrooveW * grooveScale) * 0.5f;

    if (isDown) {
        ButtonId hit = _checkButtonHit(virtX, virtY);
        if (hit != BTN_COUNT) {
            _heldBtn = hit;
            _isButtonPressed = true;
            _btnAnims[hit].press(_getBaseScale(hit));
            return;
        }

        if (_showSettingsPopup) {
            SettingsLayout l = _getSettingsLayout();
            // Generous touch hit box: covers label, value text above track, and entire slider track
            float minX = l.sliderStartX - 60.0f * guiScale;
            float maxX = l.sliderStartX + l.sliderTrackWidth + 30.0f * guiScale;
            float minY = l.fpsY - 30.0f * guiScale;
            float maxY = l.fpsY + 30.0f * guiScale;

            if (virtX >= minX && virtX <= maxX && virtY >= minY && virtY <= maxY) {
                _draggingFpsSlider = true;
                if (Settings::get().fpsOptions.size() == 2) {
                    // For 2 options (VSync / Unlimited):
                    // Clicking left side -> VSync, right side -> Unlimited.
                    // Clicking on the "FPS" label or text value toggles the setting.
                    float midTrackX = l.sliderStartX + l.sliderTrackWidth * 0.5f;
                    if (virtX < l.sliderStartX) {
                        // Tapped on the "FPS" label or far left: toggle between VSync and Unlimited
                        int nextIdx = (Settings::get().fpsIndex == 0) ? 1 : 0;
                        Settings::get().setFpsIndex(nextIdx);
                    } else if (virtX < midTrackX) {
                        Settings::get().setFpsIndex(0); // VSync
                    } else {
                        Settings::get().setFpsIndex(1); // Unlimited
                    }
                } else {
                    float rawVal = std::clamp((virtX - l.sliderStartX) / l.sliderTrackWidth, 0.0f, 1.0f);
                    int stepIdx = (int)std::round(rawVal * (Settings::get().fpsOptions.size() - 1));
                    Settings::get().setFpsIndex(stepIdx);
                }
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

        if (_showSettingsPopup || _showInfoPopup) return;

        if (!_menuActive && !_paused && !_showEndLayerUI) {
            if (_pauseBtnVisible && _pauseBtnAlpha > 0.005f) {
                const AtlasFrame* pbAf = findAtlasFrame("GJ_pauseBtn_clean_001.png");
                float pbW = ((pbAf && pbAf->w > 0.0f) ? pbAf->w : 40.0f) * guiScale;
                float pbH = ((pbAf && pbAf->h > 0.0f) ? pbAf->h : 40.0f) * guiScale;
                float pbCenterX = screenWidth - 30.0f * guiScale;
                float pbCenterY = 30.0f * guiScale;
                float hitW = std::max(pbW * 1.5f, 28.0f * guiScale);
                float hitH = std::max(pbH * 1.5f, 28.0f * guiScale);

                if (std::abs(virtX - pbCenterX) <= hitW && std::abs(virtY - pbCenterY) <= hitH) {
                    pauseGame();
                    return;
                }
            }
            pushButton();
        }
    } else if (isMotion) {
        if (_showSettingsPopup && _draggingFpsSlider) {
            SettingsLayout l = _getSettingsLayout();
            float rawVal = std::clamp((virtX - l.sliderStartX) / l.sliderTrackWidth, 0.0f, 1.0f);
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
    } else if (isUp) {
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

            if (wasPressed && (releaseHit == active || releaseHit == BTN_COUNT)) {
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
                    case BTN_SETTINGS_QUALITY_PREV:
                        Settings::get().prevQuality();
                        break;
                    case BTN_SETTINGS_QUALITY_NEXT:
                    case BTN_SETTINGS_QUALITY_BOX:
                        Settings::get().nextQuality();
                        break;
                    case BTN_SETTINGS_SHOW_FPS_PREV:
                    case BTN_SETTINGS_SHOW_FPS_NEXT:
                    case BTN_SETTINGS_SHOW_FPS_BOX:
                        Settings::get().showFps = !Settings::get().showFps;
                        Settings::get().save();
                        if (Settings::get().showFps) {
                            _lastFpsUpdateTick = SDL_GetTicks();
                            _fpsFrameCount = 0;
                            _fpsText = "60 FPS";
                            _fpsDisplayText = std::string("60 FPS - ") + RenderDevice::get().getBackendName();
                            _cachedFpsGlyphs.clear();
                        }
                        break;
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
        ray.maxAlpha = 1.0f;
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

    float originX = _level->endXPos + 60.0f;
    float originY = flipY(_endPortalGameY);

    const AtlasFrame* sqAf = AtlasManager::squareFrame ? AtlasManager::squareFrame : AtlasManager::findAtlasFrame("square.png");
    uint32_t sheetId = 0;
    auto itWs = BootScene::textures.find("GJ_WebSheet");
    if (itWs != BootScene::textures.end()) sheetId = itWs->second.id;

    for (size_t ri = 0; ri < _lightRays.size(); ++ri) {
        if (!gpu::keepParticle(ri)) continue;
        const auto& ray = _lightRays[ri];
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

        if (sqAf && sheetId != 0) {
            RenderDevice::get().batchQuad(sheetId,
                                          x0, y0, sqAf->u0, sqAf->v0,
                                          x1, y1, sqAf->u1, sqAf->v0,
                                          x2, y2, sqAf->u1, sqAf->v1,
                                          x3, y3, sqAf->u0, sqAf->v1,
                                          0.0f, 1.0f, 0.0f, ray.currentAlpha, BLEND_ADD);
        } else {
            RenderDevice::get().drawColorQuad(x0, y0, x1, y1, x2, y2, x3, y3,
                                              0.0f, 1.0f, 0.0f, ray.currentAlpha, BLEND_ADD);
        }
    }
}

struct FlightGlitter { float x, y, life, maxLife, scale; };
static std::vector<FlightGlitter> _flightGlitters;
static float _flightGlitterTimer = 0.0f;

void GameScene::update(float dt) {
    if (Settings::get().showFps) {
        _fpsFrameCount++;
        Uint32 nowTick = SDL_GetTicks();
        if (_lastFpsUpdateTick == 0) _lastFpsUpdateTick = nowTick;
        if (nowTick - _lastFpsUpdateTick >= 250) {
            int fps = (int)std::round((_fpsFrameCount * 1000.0f) / (float)(nowTick - _lastFpsUpdateTick));
            _fpsText = std::to_string(fps) + " FPS";
            _fpsDisplayText = _fpsText + " - " + RenderDevice::get().getBackendName();
            _fpsFrameCount = 0;
            _lastFpsUpdateTick = nowTick;
            float fpsY = (_menuActive || _paused) ? (62.0f * getGuiScale()) : 12.0f;
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
            float rndX = (float)rand() / (float)RAND_MAX;
            float rndY = (float)rand() / (float)RAND_MAX;
            mg.rx = rndX * 260.0f - 130.0f;
            mg.ry = rndY * 200.0f - 100.0f;
            mg.life = 0.0f;
            mg.maxLife = 1.0f + ((float)rand() / (float)RAND_MAX);
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
        _level->updatePortals(dt, _cameraX);
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
        _renderCompleteLightRays();
        _level->renderLayer1(_cameraX, _cameraY);
        RenderDevice::get().popMatrix();

        if (!_flightGlitters.empty()) {
            for (size_t gi = 0; gi < _flightGlitters.size(); ++gi) {
                if (!gpu::keepParticle(gi)) continue;
                const auto& fg = _flightGlitters[gi];
                float pt = fg.life / fg.maxLife;
                float sc = fg.scale * (1.0f - pt);
                float alpha = 1.0f - pt;
                float size = 20.0f * sc;
                drawAtlasFrame("square.png", fg.x - _cameraX, fg.y + _cameraY, size, size, 0.0f, 0.0f, 1.0f, 0.0f, alpha, false, false, BLEND_ADD);
            }
        }
    }

    if (!_menuActive) {
        _player->render(_cameraX, _cameraY);
        _level->renderLayer2(_cameraX, _cameraY);
    }

    _level->renderGround(_slideGroundX, _cameraY);

    WinEffects::render();

    if (_flashAlpha > 0.0f && gpu::knobs().fullscreenFlash) {
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

    if (Settings::get().showFps) {
        float fpsY = (_menuActive || _paused) ? (62.0f * getGuiScale()) : 12.0f;
        if (_cachedFpsGlyphs.empty() || _cachedFpsY != fpsY) {
            _updateFpsGlyphs(fpsY);
        }

        BlendMode curBlend = RenderDevice::get().getBlendMode();
        for (const auto& q : _cachedFpsGlyphs) {
            RenderDevice::get().batchAxisAlignedQuad(
                _cachedFpsTexID,
                q.gx + 1.0f, q.gy + 1.0f,
                q.gx + q.gw + 1.0f, q.gy + q.gh + 1.0f,
                q.u0, q.v0, q.u1, q.v1,
                0.0f, 0.0f, 0.0f, 0.6f, curBlend
            );
        }
        for (const auto& q : _cachedFpsGlyphs) {
            RenderDevice::get().batchAxisAlignedQuad(
                _cachedFpsTexID,
                q.gx, q.gy,
                q.gx + q.gw, q.gy + q.gh,
                q.u0, q.v0, q.u1, q.v1,
                0.35f, 1.0f, 0.35f, 0.9f, curBlend
            );
        }
    }

    if (_blackFadeAlpha > 0.001f) {
        RenderDevice::get().drawRect(0.0f, 0.0f, screenWidth, screenHeight, 0.0f, 0.0f, 0.0f, _blackFadeAlpha);
    }

    RenderDevice::get().flushBatch();
}

