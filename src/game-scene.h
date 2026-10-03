#pragma once
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <cmath>
#include <algorithm>
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>
#include "constants.h"
#include "player-physics-state.h"
#include "level-renderer.h"
#include "player.h"
#include "color-manager.h"
#include "audio-manager.h"
#include "settings.h"

struct MenuGlitter {
    float rx = 0.0f, ry = 0.0f;
    float life = 0.0f, maxLife = 1.0f;
    float scale = 0.5f;
};

struct CompleteLightRay {
    float angleDeg = 0.0f;
    float targetW = 60.0f;
    float targetH = 1250.0f;
    float currentW = 2.0f;
    float currentH = 1.0f;
    float maxAlpha = 0.8f;
    float currentAlpha = 0.0f;
    float delay = 0.0f;
    float duration = 0.18f;
    float elapsed = 0.0f;
    float fadeDelay = 0.0f;
    float fadeDuration = 0.4f;
    float fadeElapsed = 0.0f;
    bool started = false;
    bool fading = false;
    bool done = false;
};

enum ButtonId {
    BTN_MENU_PLAY = 0,
    BTN_MENU_FS,
    BTN_MENU_SETTINGS,
    BTN_MENU_INFO,
    BTN_MENU_STEAM,
    BTN_MENU_GOOGLE,
    BTN_MENU_APPLE,
    BTN_PAUSE_FS,
    BTN_PAUSE_REPLAY,
    BTN_PAUSE_PLAY,
    BTN_PAUSE_MENU,
    BTN_END_REPLAY,
    BTN_END_MENU,
    BTN_END_APPLE,
    BTN_END_GOOGLE,
    BTN_END_STEAM,
    BTN_INFO_CLOSE,
    BTN_INFO_YT,
    BTN_SETTINGS_CLOSE,
    BTN_SETTINGS_RENDER_PREV,
    BTN_SETTINGS_RENDER_NEXT,
    BTN_SETTINGS_RENDER_BOX,
    BTN_SETTINGS_QUALITY_PREV,
    BTN_SETTINGS_QUALITY_NEXT,
    BTN_SETTINGS_QUALITY_BOX,
    BTN_SETTINGS_SHOW_FPS_PREV,
    BTN_SETTINGS_SHOW_FPS_NEXT,
    BTN_SETTINGS_SHOW_FPS_BOX,
    BTN_COUNT
};

struct ButtonAnim {
    float scale = 1.0f;
    float baseScale = 1.0f;
    float fromScale = 1.0f;
    float targetScale = 1.0f;
    float timer = 0.0f;
    float duration = 0.30f;
    bool animating = false;

    void init(float base) {
        baseScale = base;
        scale = base;
        fromScale = base;
        targetScale = base;
        animating = false;
    }

    void press(float base) {
        baseScale = base;
        fromScale = scale;
        targetScale = 1.26f * baseScale;
        timer = 0.0f;
        duration = 0.30f;
        animating = true;
    }

    void deselect() {
        fromScale = scale;
        targetScale = baseScale;
        timer = 0.0f;
        duration = 0.40f;
        animating = true;
    }

    void release() {
        scale = baseScale;
        targetScale = baseScale;
        animating = false;
    }

    void update(float dt) {
        if (animating) {
            timer += dt;
            float t = std::min(timer / duration, 1.0f);
            float bounce;
            if (t < (1.0f / 2.75f)) {
                bounce = 7.5625f * t * t;
            } else if (t < (2.0f / 2.75f)) {
                float p = t - (1.5f / 2.75f);
                bounce = 7.5625f * p * p + 0.75f;
            } else if (t < (2.5f / 2.75f)) {
                float p = t - (2.25f / 2.75f);
                bounce = 7.5625f * p * p + 0.9375f;
            } else {
                float p = t - (2.625f / 2.75f);
                bounce = 7.5625f * p * p + 0.984375f;
            }
            scale = fromScale + (targetScale - fromScale) * bounce;
            if (t >= 1.0f) {
                scale = targetScale;
                animating = false;
            }
        }
    }
};

struct HitBox {
    float x = 0.0f;
    float y = 0.0f;
    float halfW = 0.0f;
    float halfH = 0.0f;

    bool contains(float px, float py) const {
        return std::abs(px - x) <= halfW && std::abs(py - y) <= halfH;
    }
};

struct SettingsLayout {
    float popupW = 0.0f;
    float popupH = 0.0f;
    float topY = 0.0f;
    float titleY = 0.0f;
    float closeY = 0.0f;
    float fpsY = 0.0f;
    #if defined(_WIN32)
    float rendY = 0.0f;
    #endif
    float qualY = 0.0f;
    float showFpsY = 0.0f;
    float sliderScale = 0.0f;
    float sliderTrackWidth = 0.0f;
    float sliderHalfGrooveW = 0.0f;
    float sliderStartX = 0.0f;
};

class GameScene {
public:
    GameScene();
    ~GameScene();

    void init();
    void handleEvent(const SDL_Event& event, int windowW, int windowH, SDL_Window* window = nullptr);
    void update(float dt);
    void render();

    void startGame();
    void restartLevel();
    void pauseGame();
    void resumeGame();

    void pushButton();
    void releaseButton();

    bool isMenuActive() const { return _menuActive; }
    bool isPaused() const { return _paused; }

    float getGuiScale() const {
        float scaleW = (float)screenWidth / 1136.0f;
        float scaleH = (float)screenHeight / 640.0f;
        return std::min(1.0f, std::min(scaleW, scaleH));
    }

private:
    void _updateBackground(float dt);
    void _updateCameraY(float dt);
    float _quantizeDelta(float dt);
    void _resetGameplayState();
    void _triggerEndPortal();
    void _levelComplete();
    void _showNewBest();
    void _renderNewBest();
    void _hideEndLayer(std::function<void()> onComplete);

    SettingsLayout _getSettingsLayout() const;

    HitBox _getButtonHitBox(const std::string& frameName, float cx, float cy, ButtonId id, float baseScale, float expandFactor);
    ButtonId _checkButtonHit(float vx, float vy);
    float _getBaseScale(ButtonId id) const;

    void _startCompleteLightRays();
    void _updateCompleteLightRays(float dt);
    void _renderCompleteLightRays();

    void _renderHUD();
    void _renderMenu();
    void _renderPauseOverlay();
    void _renderEndLayer();
    void _renderInfoPopup();
    void _renderSettingsPopup();

    void _renderSlider(float centerX, float centerY, float progress, bool isDragging,
                       const std::string& iconName, const std::string& textLabel,
                       const std::string& valueText, float customGrooveScale = -1.0f);

    PlayerPhysicsState _state;
    std::unique_ptr<LevelRenderer> _level;
    std::unique_ptr<Player> _player;
    ColorManager _colorManager;
    AudioManager _audio;
    uint32_t _bgTexID = 0;

    float _cameraX;
    float _cameraY;
    float _prevCameraX;
    float _menuCameraX;
    float _playerWorldX;
    float _slideGroundX;
    float _bgScrollX;

    int _fadeState = 0;
    float _fadeTimer = 0.0f;
    float _blackFadeAlpha = 0.0f;

    bool _menuActive = true;
    bool _slideIn = false;
    bool _paused = false;
    bool _levelWon = false;
    bool _firstPlay = true;
    bool _isFullscreen = false;

    bool _endCameraOverride = false;
    bool _endCamTweenActive = false;
    float _endCamTweenTime = 0.0f;
    float _endCamFromX = 0.0f, _endCamToX = 0.0f;
    float _endCamFromY = 0.0f, _endCamToY = 0.0f;

    int _endSequencePhase = 0;
    float _endSequenceTimer = 0.0f;
    float _shakeTimer = 0.0f;
    float _shakeIntensity = 0.0f;
    float _flashAlpha = 0.0f;
    std::vector<CompleteLightRay> _lightRays;

    bool _completeBannerVisible = false;
    float _completeBannerTimer = 0.0f;
    float _completeBannerScale = 0.01f;

    bool _starAwardStarted = false;
    bool _starAwardSoundPlayed = false;
    float _starAwardTimer = 0.0f;
    float _starAwardScale = 3.0f;
    float _starAwardAlpha = 0.0f;

    bool _showInfoPopup = false;
    bool _showSettingsPopup = false;

    bool _draggingMusicSlider = false;
    bool _draggingSfxSlider = false;
    bool _draggingFpsSlider = false;
    float _sfxVolume = 1.0f;

    bool _isMenuAnimatingOut = false;
    float _menuAnimTimer = 0.0f;

    float _deltaBuffer = 0.0f;

    int _attempts = 1;
    int _bestPercent = 0;
    int _lastPercent = 0;
    int _totalJumps = 0;
    float _playTime = 0.0f;
    float _deathTimer = 0.0f;
    bool _deathSoundPlayed = false;
    bool _newBestShown = false;
    bool _hadNewBest = false;

    bool _newBestActive = false;
    float _newBestTimer = 0.0f;
    float _newBestScale = 0.01f;

    bool _endLayerHiding = false;
    float _endLayerHideTimer = 0.0f;
    std::function<void()> _endLayerHideCallback;

    ButtonAnim _btnAnims[BTN_COUNT];
    ButtonId _heldBtn = BTN_COUNT;
    bool _isButtonPressed = false;

    float _menuPlayBtnY = 320.0f;
    float _menuPlayTimer = 0.0f;
    float _menuGlitterTimer = 0.0f;
    std::vector<MenuGlitter> _menuParticles;

    float _endPortalGameY = 240.0f;
    bool _showEndLayerUI = false;
    std::string _completeMessage = "Awesome!";

    bool _showAttemptsLabel = false;
    float _attemptsLabelX = 0.0f;
    float _attemptsLabelY = 150.0f;

    bool _pauseBtnVisible = false;
    float _pauseBtnAlpha = 0.0f;
    bool _pauseBtnFading = false;
    float _pauseBtnFadeTimer = 0.0f;
    float _pauseBtnFadeDuration = 0.5f;
    float _pauseBtnFadeFrom = 0.0f;
    float _pauseBtnFadeTo = 75.0f / 255.0f;

    // --- FPS COUNTER ---
    struct FpsGlyphQuad {
        float gx, gy, gw, gh;
        float u0, v0, u1, v1;
    };
    std::vector<FpsGlyphQuad> _cachedFpsGlyphs;
    float _cachedFpsY = -1.0f;
    uint32_t _cachedFpsTexID = 0;
    void _updateFpsGlyphs(float fpsY);

    Uint32 _lastFpsUpdateTick = 0;
    int _fpsFrameCount = 0;
    std::string _fpsText = "60 FPS";
    std::string _fpsDisplayText = "60 FPS";
};
