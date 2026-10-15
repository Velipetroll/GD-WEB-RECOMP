#include "game-scene.h"
#include "boot-scene.h"
#include "font-helpers.h"
#include "render-device.h"
#include <algorithm>
#include <cmath>

SettingsLayout GameScene::_getSettingsLayout() const {
    SettingsLayout l;
    float guiScale = getGuiScale();
    float midX = screenWidth * 0.5f;

    #if defined(_WIN32)
    const float basePopupH = 390.0f;
    #else
    const float basePopupH = 320.0f;
    #endif

    l.popupW = 480.0f * guiScale;
    l.popupH = basePopupH * guiScale;
    l.topY = 320.0f - (l.popupH * 0.5f);
    l.closeY = l.topY + 24.0f * guiScale;
    l.titleY = l.topY + 36.0f * guiScale;
    l.fpsY = l.topY + 98.0f * guiScale;

    #if defined(_WIN32)
    l.rendY = l.topY + 168.0f * guiScale;
    l.qualY = l.topY + 240.0f * guiScale;
    l.showFpsY = l.topY + 312.0f * guiScale;
    #else
    l.qualY = l.topY + 176.0f * guiScale;
    l.showFpsY = l.topY + 252.0f * guiScale;
    #endif

    l.sliderScale = 0.58f * guiScale;
    const AtlasFrame* grooveAf = findAtlasFrame("slidergroove.png");
    const float origGrooveW = (grooveAf && grooveAf->w > 0.0f) ? grooveAf->w : 420.0f;
    l.sliderTrackWidth = (origGrooveW - 8.0f) * l.sliderScale;
    l.sliderHalfGrooveW = (origGrooveW * l.sliderScale) * 0.5f;
    l.sliderStartX = midX - l.sliderHalfGrooveW + 2.8f * (l.sliderScale / 0.7f);

    return l;
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
        SettingsLayout l = _getSettingsLayout();
        HitBox closeBox = _getButtonHitBox("GJ_closeBtn_001.png", midX - 220.0f * guiScale, l.closeY, BTN_SETTINGS_CLOSE, guiScale, 2.0f);
        if (closeBox.contains(vx, vy)) return BTN_SETTINGS_CLOSE;

        #if defined(_WIN32)
        if (!Settings::get().rendererOptions.empty()) {
            HitBox prevBox = { midX - 115.0f * guiScale, l.rendY, 24.0f * guiScale, 22.0f * guiScale };
            if (prevBox.contains(vx, vy)) return BTN_SETTINGS_RENDER_PREV;

            HitBox nextBox = { midX + 115.0f * guiScale, l.rendY, 24.0f * guiScale, 22.0f * guiScale };
            if (nextBox.contains(vx, vy)) return BTN_SETTINGS_RENDER_NEXT;

            HitBox boxHit = { midX, l.rendY, 85.0f * guiScale, 18.0f * guiScale };
            if (boxHit.contains(vx, vy)) return BTN_SETTINGS_RENDER_BOX;
        }
        #endif

        HitBox prevQualBox = { midX - 95.0f * guiScale, l.qualY, 26.0f * guiScale, 22.0f * guiScale };
        if (prevQualBox.contains(vx, vy)) return BTN_SETTINGS_QUALITY_PREV;

        HitBox nextQualBox = { midX + 95.0f * guiScale, l.qualY, 26.0f * guiScale, 22.0f * guiScale };
        if (nextQualBox.contains(vx, vy)) return BTN_SETTINGS_QUALITY_NEXT;

        HitBox boxQualHit = { midX, l.qualY, 90.0f * guiScale, 20.0f * guiScale };
        if (boxQualHit.contains(vx, vy)) return BTN_SETTINGS_QUALITY_BOX;

        HitBox prevFpsBox = { midX - 85.0f * guiScale, l.showFpsY, 26.0f * guiScale, 24.0f * guiScale };
        if (prevFpsBox.contains(vx, vy)) return BTN_SETTINGS_SHOW_FPS_PREV;

        HitBox nextFpsBox = { midX + 85.0f * guiScale, l.showFpsY, 26.0f * guiScale, 24.0f * guiScale };
        if (nextFpsBox.contains(vx, vy)) return BTN_SETTINGS_SHOW_FPS_NEXT;

        HitBox boxFpsHit = { midX, l.showFpsY, 120.0f * guiScale, 24.0f * guiScale };
        if (boxFpsHit.contains(vx, vy)) return BTN_SETTINGS_SHOW_FPS_BOX;

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

        if (_levelList.size() > 1) {
            float selY = playY + 130.0f * guiScale;
            HitBox prevBox = { midX - 170.0f * guiScale, selY, 30.0f * guiScale, 28.0f * guiScale };
            if (prevBox.contains(vx, vy)) return BTN_MENU_LEVEL_PREV;
            HitBox nextBox = { midX + 170.0f * guiScale, selY, 30.0f * guiScale, 28.0f * guiScale };
            if (nextBox.contains(vx, vy)) return BTN_MENU_LEVEL_NEXT;
            HitBox nameBox = { midX, selY, 140.0f * guiScale, 28.0f * guiScale };
            if (nameBox.contains(vx, vy)) return BTN_MENU_LEVEL_NEXT;
        }

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
    float playY = midY + (_menuPlayBtnY - 320.0f) * guiScale;

    for (const auto& mp : _menuParticles) {
        float pt = std::min(mp.life / mp.maxLife, 1.0f);
        float alpha = (0.6f + (0.2f - 0.6f) * pt) * (1.0f - t);
        float sc = mp.scale * (1.0f - pt) * guiScale;
        float px = midX + mp.rx * guiScale;
        float py = playY + mp.ry * guiScale;
        drawAtlasFrame("square.png", px, py, 20.0f * sc, 20.0f * sc, 0.0f, 0.0f, 0.3137f, 0.7451f, alpha, false, false, BLEND_ADD);
    }

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

    if (_levelList.size() > 1 && playExitScale > 0.01f) {
        float selY = playY + 130.0f * guiScale;
        const LevelEntry& le = _levelList[_selectedLevel];
        float a = playExitScale;
        RenderDevice::get().drawRect(midX - 140.0f * guiScale, selY - 28.0f * guiScale, 280.0f * guiScale, 56.0f * guiScale, 0.0f, 0.0f, 0.0f, 0.45f * a);
        float prevS = _btnAnims[BTN_MENU_LEVEL_PREV].scale;
        float nextS = _btnAnims[BTN_MENU_LEVEL_NEXT].scale;
        drawBitmapText("bigFont", "<", midX - 170.0f * guiScale, selY, 0.8f * prevS * guiScale, 1.0f, 1.0f, 1.0f, a, true);
        drawBitmapText("bigFont", ">", midX + 170.0f * guiScale, selY, 0.8f * nextS * guiScale, 1.0f, 1.0f, 1.0f, a, true);
        float nameY = le.custom ? selY - 8.0f * guiScale : selY;
        drawBitmapText("goldFont", le.name, midX, nameY, 0.6f * guiScale, 1.0f, 1.0f, 1.0f, a, true);
        if (le.custom) {
            drawBitmapText("bigFont", "Custom", midX, selY + 14.0f * guiScale, 0.3f * guiScale, 0.4f, 0.8f, 1.0f, a, true);
        }
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
                              const std::string& valueText, float customGrooveScale)
{
    float guiScale = getGuiScale();
    const float grooveScale = (customGrooveScale > 0.0f) ? customGrooveScale : (0.7f * guiScale);
    const AtlasFrame* grooveAf = findAtlasFrame("slidergroove.png");
    const float origGrooveW = (grooveAf && grooveAf->w > 0.0f) ? grooveAf->w : 420.0f;
    const float origGrooveH = (grooveAf && grooveAf->h > 0.0f) ? grooveAf->h : 24.0f;
    const float grooveW = origGrooveW * grooveScale;
    const float grooveH = origGrooveH * grooveScale;
    const float trackWidth = (origGrooveW - 8.0f) * grooveScale;
    const float barH = 16.0f * grooveScale;

    const float startOffset = 2.8f * (customGrooveScale > 0.0f ? (customGrooveScale / 0.7f) : guiScale);
    const float trackStartX = centerX - (origGrooveW * grooveScale) * 0.5f + startOffset;
    const float clampedProg = std::clamp(progress, 0.0f, 1.0f);
    const float fillW = (clampedProg < 0.01f) ? 0.0f : (clampedProg * trackWidth);

    if (!iconName.empty()) {
        const AtlasFrame* iconAf = findAtlasFrame(iconName);
        float iconW = (iconAf && iconAf->w > 0.0f) ? iconAf->w * 1.2f * guiScale : 43.2f * guiScale;
        float iconH = (iconAf && iconAf->h > 0.0f) ? iconAf->h * 1.2f * guiScale : 43.2f * guiScale;
        drawAtlasFrame(iconName, centerX - 185.0f * guiScale, centerY, iconW, iconH);
    } else if (!textLabel.empty()) {
        float labelX = (customGrooveScale > 0.0f) ? (centerX - (grooveW * 0.5f) - 30.0f * guiScale) : (centerX - 185.0f * guiScale);
        float labelScale = (customGrooveScale > 0.0f) ? 0.44f * guiScale : 0.50f * guiScale;
        drawBitmapText("bigFont", textLabel, labelX, centerY - 2.0f * guiScale, labelScale, 1.0f, 1.0f, 1.0f, 1.0f, true);
    }

    if (!valueText.empty()) {
        float valY = (customGrooveScale > 0.0f) ? (centerY - 18.0f * guiScale) : (centerY - 22.0f * guiScale);
        float valScale = (customGrooveScale > 0.0f) ? 0.40f * guiScale : 0.45f * guiScale;
        drawBitmapText("bigFont", valueText, centerX, valY, valScale, 1.0f, 1.0f, 1.0f, 1.0f, true);
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
            float aspect = texW / texH;
            float tileW = barH * aspect;
            if (tileW <= 0.0f) tileW = barH * 4.0f;

            float uSpan = u1_base - u0_base;
            float drawn = 0.0f;
            while (drawn < fillW) {
                float curW = std::min(tileW, fillW - drawn);
                float frac = curW / tileW;
                float segU0 = u0_base;
                float segU1 = u0_base + uSpan * frac;

                RenderDevice::get().batchAxisAlignedQuad(
                    texID,
                    trackStartX + drawn,        yTop,
                    trackStartX + drawn + curW, yBot,
                    segU0, v0_base,
                    segU1, v1_base,
                    1.0f, 1.0f, 1.0f, 1.0f, BLEND_NORMAL
                );
                drawn += curW;
            }
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

        RenderDevice::get().batchAxisAlignedQuad(
            texID,
            startX,         startY,
            startX + drawW, startY + totalH,
            u0_bar, v0_bar,
            u1_bar, v1_bar,
            0.0f, 1.0f, 0.0f, 1.0f, BLEND_NORMAL
        );
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

void GameScene::_renderSettingsPopup() {
    float guiScale = getGuiScale();
    float midX = screenWidth * 0.5f;
    RenderDevice::get().drawRect(0.0f, 0.0f, screenWidth, screenHeight, 0.0f, 0.0f, 0.0f, 100.0f / 255.0f);

    SettingsLayout l = _getSettingsLayout();

    drawScale9("GJ_square02", midX, 320.0f, l.popupW, l.popupH, 52.0f * guiScale, 1.0f, 1.0f, 1.0f, 1.0f);

    const AtlasFrame* closeAf = findAtlasFrame("GJ_closeBtn_001.png");
    float closeSc = _btnAnims[BTN_SETTINGS_CLOSE].scale * guiScale;
    float cw = (closeAf ? closeAf->w : 40.0f) * closeSc;
    float ch = (closeAf ? closeAf->h : 40.0f) * closeSc;
    drawAtlasFrame("GJ_closeBtn_001.png", midX - 220.0f * guiScale, l.closeY, cw, ch);

    // "Settings" Title
    drawBitmapText("bigFont", "Settings", midX, l.titleY, 0.70f * guiScale, 1.0f, 1.0f, 1.0f, 1.0f, true);

    // 1. FPS Limiter
    float fpsProgress = (float)Settings::get().fpsIndex / (float)(Settings::get().fpsOptions.size() - 1);
    _renderSlider(midX, l.fpsY, fpsProgress, _draggingFpsSlider,
                  "", "FPS", Settings::get().currentFps().label, l.sliderScale);

    // 2. Graphics Renderer Selector (Windows only)
    #if defined(_WIN32)
    if (!Settings::get().rendererOptions.empty()) {
        drawBitmapText("goldFont", "Renderer", midX, l.rendY - 26.0f * guiScale, 0.50f * guiScale, 1.0f, 0.85f, 0.2f, 1.0f, true);

        float prevScale = _btnAnims[BTN_SETTINGS_RENDER_PREV].scale * guiScale;
        float nextScale = _btnAnims[BTN_SETTINGS_RENDER_NEXT].scale * guiScale;
        drawBitmapText("bigFont", "<", midX - 115.0f * guiScale, l.rendY, 0.60f * prevScale, 1.0f, 0.9f, 0.2f, 1.0f, true);
        drawBitmapText("bigFont", ">", midX + 115.0f * guiScale, l.rendY, 0.60f * nextScale, 1.0f, 0.9f, 0.2f, 1.0f, true);

        std::string rendLabel = Settings::get().currentRendererLabel();
        float boxScale = _btnAnims[BTN_SETTINGS_RENDER_BOX].scale * guiScale;
        drawBitmapText("bigFont", rendLabel, midX, l.rendY + 3.0f * guiScale, 0.50f * boxScale, 1.0f, 1.0f, 1.0f, 1.0f, true);

        if (Settings::get().currentBackend() != RenderDevice::get().getBackend()) {
            drawBitmapText("goldFont", "(Restart game to apply)", midX, l.rendY + 28.0f * guiScale, 0.38f * guiScale, 1.0f, 0.45f, 0.35f, 1.0f, true);
        }
    }
    #endif

    // 3. Graphics Quality Selector
    drawBitmapText("goldFont", "Quality", midX, l.qualY - 26.0f * guiScale, 0.50f * guiScale, 1.0f, 0.85f, 0.2f, 1.0f, true);

    float prevQualScale = _btnAnims[BTN_SETTINGS_QUALITY_PREV].scale * guiScale;
    float nextQualScale = _btnAnims[BTN_SETTINGS_QUALITY_NEXT].scale * guiScale;
    drawBitmapText("bigFont", "<", midX - 95.0f * guiScale, l.qualY, 0.60f * prevQualScale, 1.0f, 0.9f, 0.2f, 1.0f, true);
    drawBitmapText("bigFont", ">", midX + 95.0f * guiScale, l.qualY, 0.60f * nextQualScale, 1.0f, 0.9f, 0.2f, 1.0f, true);

    std::string qualLabel = Settings::get().currentQualityLabel();
    if (Settings::get().qualityIndex == QUALITY_AUTO) {
        qualLabel += " (" + std::string(gpu::presetName(gpu::resolvedPreset())) + ")";
    }
    float qualBoxScale = _btnAnims[BTN_SETTINGS_QUALITY_BOX].scale * guiScale;
    drawBitmapText("bigFont", qualLabel, midX, l.qualY + 3.0f * guiScale, 0.46f * qualBoxScale, 1.0f, 1.0f, 1.0f, 1.0f, true);

    // 4. Show FPS Toggle
    drawBitmapText("goldFont", "Show FPS", midX, l.showFpsY - 24.0f * guiScale, 0.50f * guiScale, 1.0f, 0.85f, 0.2f, 1.0f, true);

    float prevFpsScale = _btnAnims[BTN_SETTINGS_SHOW_FPS_PREV].scale * guiScale;
    float nextFpsScale = _btnAnims[BTN_SETTINGS_SHOW_FPS_NEXT].scale * guiScale;
    drawBitmapText("bigFont", "<", midX - 85.0f * guiScale, l.showFpsY, 0.60f * prevFpsScale, 1.0f, 0.9f, 0.2f, 1.0f, true);
    drawBitmapText("bigFont", ">", midX + 85.0f * guiScale, l.showFpsY, 0.60f * nextFpsScale, 1.0f, 0.9f, 0.2f, 1.0f, true);

    bool isFpsOn = Settings::get().showFps;
    std::string fpsToggleLabel = isFpsOn ? "Enabled" : "Disabled";
    float fr = isFpsOn ? 0.35f : 0.85f;
    float fg = isFpsOn ? 1.0f : 0.40f;
    float fb = isFpsOn ? 0.35f : 0.40f;

    float fpsBoxScale = _btnAnims[BTN_SETTINGS_SHOW_FPS_BOX].scale * guiScale;
    drawBitmapText("bigFont", fpsToggleLabel, midX, l.showFpsY + 3.0f * guiScale, 0.50f * fpsBoxScale, fr, fg, fb, 1.0f, true);
}

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
