#include "player.h"
#include "level-renderer.h"
#include "boot-scene.h"
#include "render-device.h"
#include <algorithm>
#include <iostream>
#include <cmath>

Player::Player(PlayerPhysicsState& state, LevelRenderer& levelRenderer)
: p(state),
_levelRenderer(levelRenderer),
_streak(0.231f, 10.0f, 8.0f, 100.0f, colorCyanTint, 0.7f)
{
    float helperFn22 = flipY(p.y);
    float value71 = groundYOffset;

    _playerGlowLayer    = createLayeredSprite(value71, helperFn22, "player_01_glow_001.png", 9, false);
    _playerSpriteLayer  = createLayeredSprite(value71, helperFn22, "player_01_001.png", 10, true);
    _playerOverlayLayer = createLayeredSprite(value71, helperFn22, "player_01_2_001.png", 8, true);
    _playerExtraLayer   = createLayeredSprite(value71, helperFn22, "player_01_extra_001.png", 12, true);

    _playerSpriteLayer.setTint(0.0f, 1.0f, 0.0f);
    _playerOverlayLayer.setTint(0.0f, 1.0f, 1.0f);

    _shipGlowLayer    = createLayeredSprite(value71, helperFn22, "ship_01_glow_001.png", 9, false);
    _shipSpriteLayer  = createLayeredSprite(value71, helperFn22, "ship_01_001.png", 10, false);
    _shipOverlayLayer = createLayeredSprite(value71, helperFn22, "ship_01_2_001.png", 8, false);
    _shipExtraLayer   = createLayeredSprite(value71, helperFn22, "ship_01_extra_001.png", 12, false);

    _shipSpriteLayer.setTint(0.0f, 1.0f, 0.0f);
    _shipOverlayLayer.setTint(0.0f, 1.0f, 1.0f);

    setCubeVisible(true);
    setShipVisible(false);
}

Player::~Player() {}

void Player::setCubeVisible(bool visible) {
    _playerSpriteLayer.setVisible(visible);
    _playerOverlayLayer.setVisible(visible);
    _playerExtraLayer.setVisible(visible);
    _playerGlowLayer.setVisible(false);
}

void Player::setShipVisible(bool visible) {
    _shipSpriteLayer.setVisible(visible);
    _shipOverlayLayer.setVisible(visible);
    _shipExtraLayer.setVisible(visible);
    _shipGlowLayer.setVisible(false);
}

void Player::enterShipMode(const LevelObject* portal) {
    if (p.isFlying) return;
    p.isFlying = true;
    p.yVelocity *= 0.5f;
    p.onGround = false;
    p.canJump = false;
    p.isJumping = false;
    stopRotation();
    _rotation = 0.0f;

    _streak.reset();
    _streak.start();

    _shipFlames.clear();
    _shipDrags.clear();
    _flyParticleTimer = 0.0f;
    _flyParticle2Timer = 0.0f;
    _shipDragTimer = 0.0f;

    setShipVisible(true);
    setCubeVisible(true);
    _playerSpriteLayer.setScale(0.55f);
    _playerOverlayLayer.setScale(0.55f);
    _playerExtraLayer.setScale(0.55f);

    float portalY = portal ? portal->y : p.y;
    _levelRenderer.setFlyMode(true, portalY);
}

void Player::exitShipMode() {
    if (!p.isFlying) return;
    p.isFlying = false;
    p.yVelocity *= 0.5f;
    p.onGround = false;
    p.canJump = false;
    p.isJumping = false;
    stopRotation();
    _rotation = 0.0f;

    _streak.stop();
    _streak.reset();

    _shipFlames.clear();
    _shipDrags.clear();

    setShipVisible(false);
    setCubeVisible(true);
    _playerSpriteLayer.setScale(1.0f);
    _playerOverlayLayer.setScale(1.0f);
    _playerExtraLayer.setScale(1.0f);

    _levelRenderer.setFlyMode(false, 0.0f);
}

void Player::runRotateAction() {
    rotateActionActive = true;
    rotateActionTime = 0.0f;
    rotateActionDuration = 0.39f / physicsConst09;
    rotateActionStart = _rotation;
    rotateActionTotal = 3.14159265f * flipMod();
}

void Player::stopRotation() {
    rotateActionActive = false;
}

void Player::updateRotateAction(float dt) {
    if (!rotateActionActive) return;
    rotateActionTime += dt;
    if (rotateActionTime >= rotateActionDuration) {
        rotateActionActive = false;
    }
    float t = std::min(rotateActionTime / rotateActionDuration, 1.0f);
    _rotation = rotateActionStart + rotateActionTotal * t;
}

float Player::convertToClosestRotation() const {
    float halfPi = 3.14159265f / 2.0f;
    return std::round(_rotation / halfPi) * halfPi;
}

float Player::slerp2D(float a, float b, float t) const {
    float diff = b - a;
    while (diff > 3.14159265f) diff -= 2.0f * 3.14159265f;
    while (diff < -3.14159265f) diff += 2.0f * 3.14159265f;
    return a + diff * t;
}

void Player::updateGroundRotation(float dt) {
    float target = convertToClosestRotation();
    float step = std::min(1.0f * dt, (0.1575f * 3.0f) * dt);
    _rotation = slerp2D(_rotation, target, step);
}

void Player::updateShipRotation(float dt) {
    float dy = -(p.y - p.lastY);
    float dx = 10.3860036f * dt;
    if (dx * dx + dy * dy >= 0.6f * dt) {
        float target = std::atan2(dy, dx);
        float step = std::min(1.0f * dt, 0.15f * dt);
        _rotation = slerp2D(_rotation, target, step);
    }
}

bool Player::playerIsFalling() const {
    return p.gravityFlipped ? (p.yVelocity > 3.832796f) : (p.yVelocity < 3.832796f);
}

bool Player::_isFallingPastThreshold() const {
    return p.gravityFlipped ? (p.yVelocity > 0.25f) : (p.yVelocity < -0.25f);
}

void Player::hitGround(float playerWorldX) {
    bool wasAirborne = !p.onGround;

    p.yVelocity = 0.0f;
    p.onGround = true;
    p.canJump = true;
    p.isJumping = false;
    stopRotation();

    if (wasAirborne && !p.isFlying) {
        _spawnLandDust(playerWorldX, flipY(p.y) + 30.0f);
    }
}

void Player::updateJump(float dt) {
    if (p.isFlying) {
        float num32 = 0.8f;
        if (p.upKeyDown && !p.wasBoosted) num32 = -1.0f;
        if (!p.upKeyDown && !playerIsFalling()) num32 = 1.2f;

        float num33 = 0.4f;
        if (p.upKeyDown && playerIsFalling()) num33 = 0.5f;

        p.yVelocity -= physicsConst1916 * dt * flipMod() * num32 * num33;
        if (p.upKeyDown) p.onGround = false;

        if (!p.wasBoosted) {
            if (p.gravityFlipped) {
                p.yVelocity = std::max(p.yVelocity, -16.0f);
                p.yVelocity = std::min(p.yVelocity, 12.8f);
            } else {
                p.yVelocity = std::max(p.yVelocity, -12.8f);
                p.yVelocity = std::min(p.yVelocity, 16.0f);
            }
        }
    }
    else if (p.upKeyDown && p.canJump) {
        p.isJumping = true;
        p.onGround = false;
        p.canJump = false;
        p.upKeyPressed = false;
        p.yVelocity = 22.360064f * flipMod();
        runRotateAction();
    }
    else if (p.isJumping) {
        p.yVelocity -= physicsConst1916 * dt * flipMod();
        if (playerIsFalling()) {
            p.isJumping = false;
            p.onGround = false;
        }
    }
    else {
        if (playerIsFalling()) p.canJump = false;
        p.yVelocity -= physicsConst1916 * dt * flipMod();

        if (p.gravityFlipped) p.yVelocity = std::min(p.yVelocity, 30.0f);
        else p.yVelocity = std::max(p.yVelocity, -30.0f);

        if (_isFallingPastThreshold() && !rotateActionActive) {
            runRotateAction();
        }
        if (playerIsFalling()) {
            bool fallCondition = p.gravityFlipped ? (p.yVelocity > 4.0f) : (p.yVelocity < -4.0f);
            if (fallCondition) p.onGround = false;
        }
    }
}

void Player::_checkSnapJump(const LevelObject* obj, float playerWorldX) {
    if (!obj || obj->type != "solid") return;
    _lastLandObject = obj;
    _lastXOffset = playerWorldX - obj->x;
}

void Player::checkCollisions(float playerWorldXArg, float cameraY) {
    const float radius = 30.0f;
    float playerWorldX = playerWorldXArg + groundYOffset;
    float currentY = p.y;
    float lastY = p.lastY;
    float innerTolerance = p.isFlying ? 12.0f : 20.0f;

    p.collideTop = 0.0f;
    p.collideBottom = 0.0f;
    p.onCeiling = false;
    bool landedOnBlock = false;

    const auto& nearby = _levelRenderer.getNearbySectionObjects(playerWorldX);
    for (LevelObject* item : nearby) {
        float left   = item->x - item->w * 0.5f;
        float right  = item->x + item->w * 0.5f;
        float bottom = item->y - item->h * 0.5f;
        float top    = item->y + item->h * 0.5f;

        if (playerWorldX + radius <= left || playerWorldX - radius >= right ||
            currentY + radius <= bottom   || currentY - radius >= top)
        {
            continue;
        }

        switch (item->objType) {
            case OBJ_PORTAL_FLY:
                if (!item->activated) {
                    item->activated = true;
                    enterShipMode(item);
                }
                break;
            case OBJ_PORTAL_CUBE:
                if (!item->activated) {
                    item->activated = true;
                    exitShipMode();
                }
                break;
            case OBJ_HAZARD:
                killPlayer(cameraY);
                return;
            case OBJ_SOLID: {
                float playerFootNow  = currentY - radius + innerTolerance;
                float playerFootLast = lastY    - radius + innerTolerance;
                float playerHeadNow  = currentY + radius - innerTolerance;
                float playerHeadLast = lastY    + radius - innerTolerance;

                const float sideInset = 9.0f;
                bool sideHit = (playerWorldX + sideInset > left && playerWorldX - sideInset < right &&
                                currentY + sideInset > bottom   && currentY - sideInset < top);

                bool landingCondition = (p.yVelocity <= 0.0f || p.onGround) &&
                                        (playerFootNow >= top || playerFootLast >= top);

                if (sideHit && !landingCondition) {
                    killPlayer(cameraY);
                    return;
                }

                if (playerWorldX + radius - 5.0f > left && playerWorldX - radius + 5.0f < right) {
                    if (landingCondition) {
                        p.y = top + radius;
                        hitGround(playerWorldX);
                        landedOnBlock = true;
                        p.collideBottom = top;
                        if (!p.isFlying) _checkSnapJump(item, playerWorldX);
                        break;
                    }
                    if ((playerHeadNow <= bottom || playerHeadLast <= bottom) &&
                        (p.yVelocity >= 0.0f || p.onGround) && p.isFlying)
                    {
                        p.y = bottom - radius;
                        hitGround(playerWorldX);
                        p.onCeiling = true;
                        p.collideTop = bottom;
                        break;
                    }
                }
                break;
            }
            default:
                break;
        }
    }

    float floorY = _levelRenderer.getFloorY();
    if (!landedOnBlock && p.y <= floorY + radius) {
        p.y = floorY + radius;
        hitGround(playerWorldX);
    }

    float ceilY = _levelRenderer.getCeilingY();
    if (_levelRenderer.hasCeiling() && p.y >= ceilY - radius) {
        p.y = ceilY - radius;
        hitGround(playerWorldX);
        p.onCeiling = true;
    }
}

void Player::killPlayer(float cameraY) {
    if (p.isDead) return;
    p.isDead = true;
    _streak.stop();
    _streak.reset();
    _shipFlames.clear();
    _shipDrags.clear();

    _deathScreenX = groundYOffset;
    _deathCameraY = cameraY;
    _deathScreenY = flipY(p.y) + cameraY;

    _createExplosionPieces(_deathScreenX, _deathScreenY);
    setCubeVisible(false);
    setShipVisible(false);
}

void Player::_createExplosionPieces(float x, float y) {
    _isExploding = true;
    _explosionTime = 0.0f;
    _shockwaveRadius = 18.0f;
    _shockwaveAlpha = 1.0f;

    _explosionPieces.clear();
    _deathParticles.clear();
    _pieceTrails.clear();

    struct ActiveLayer {
        const AtlasFrame* af;
        float offsetX, offsetY;
        float r, g, b;
    };
    std::vector<ActiveLayer> activeLayers;

    auto collectLayer = [&](const LayeredSprite& ls) {
        if (!ls.visible || ls.frameName.empty()) return;
        const AtlasFrame* af = findAtlasFrame(ls.frameName);
        if (!af || af->w <= 0.0f || af->h <= 0.0f) return;
        activeLayers.push_back({af, ls.offsetX, ls.offsetY, ls.r, ls.g, ls.b});
    };

    if (p.isFlying) {
        collectLayer(_shipOverlayLayer);
        collectLayer(_shipSpriteLayer);
        collectLayer(_shipExtraLayer);
        collectLayer(_playerOverlayLayer);
        collectLayer(_playerSpriteLayer);
        collectLayer(_playerExtraLayer);
    } else {
        collectLayer(_playerOverlayLayer);
        collectLayer(_playerSpriteLayer);
        collectLayer(_playerExtraLayer);
    }

    const float extra = 0.9f;
    const float value72 = 40.0f * extra;
    const int totalSize = (int)std::round(2.0f * value72);

    int cols = 2 + (int)std::round(2.0f * ((float)rand() / (float)RAND_MAX));
    int rows = 2 + (int)std::round(2.0f * ((float)rand() / (float)RAND_MAX));
    float rVal = (float)rand() / (float)RAND_MAX;
    if (rVal > 0.95f) cols = 1;
    else if (rVal > 0.90f) rows = 1;

    const float value80 = 9.34740324f * 0.8f;
    const float value81 = 0.5f * value80;
    const float value82 = 1.0f * value80;
    const float num32 = 0.45f;
    const float avgW = (float)totalSize / (float)cols;
    const float avgH = (float)totalSize / (float)rows;

    std::vector<int> colWidths;
    std::vector<int> colOffsets = {0};
    int sumW = 0;
    for (int i = 0; i < cols - 1; ++i) {
        float rFrac = (float)rand() / (float)RAND_MAX;
        int w = (int)std::round(avgW * (0.55f + rFrac * num32 * 2.0f));
        colWidths.push_back(w);
        sumW += w;
        colOffsets.push_back(sumW);
    }
    colWidths.push_back(totalSize - sumW);

    std::vector<int> rowHeights;
    std::vector<int> rowOffsets = {0};
    int sumH = 0;
    for (int i = 0; i < rows - 1; ++i) {
        float rFrac = (float)rand() / (float)RAND_MAX;
        int h = (int)std::round(avgH * (0.55f + rFrac * num32 * 2.0f));
        rowHeights.push_back(h);
        sumH += h;
        rowOffsets.push_back(sumH);
    }
    rowHeights.push_back(totalSize - sumH);

    int pieceIndex = 0;
    for (int i = 0; i < cols; ++i) {
        int i2 = colWidths[i];
        int i3 = colOffsets[i];

        for (int i4 = 0; i4 < rows; ++i4) {
            int value85 = rowHeights[i4];
            int value86 = rowOffsets[i4];
            if (i2 <= 0 || value85 <= 0) continue;

            pieceIndex++;
            ExplosionPiece piece;
            piece.w = (float)i2;
            piece.h = (float)value85;

            float shardX0 = (float)i3 - totalSize * 0.5f;
            float shardX1 = (float)(i3 + i2) - totalSize * 0.5f;
            float shardY0 = (float)value86 - totalSize * 0.5f;
            float shardY1 = (float)(value86 + value85) - totalSize * 0.5f;

            float shardCX = (shardX0 + shardX1) * 0.5f;
            float shardCY = (shardY0 + shardY1) * 0.5f;

            piece.x = x + shardCX;
            piece.y = y + shardCY;

            for (const auto& layer : activeLayers) {
                float layerW = layer.af->w;
                float layerH = layer.af->h;
                float layerX0 = layer.offsetX - layerW * 0.5f;
                float layerX1 = layer.offsetX + layerW * 0.5f;
                float layerY0 = layer.offsetY - layerH * 0.5f;
                float layerY1 = layer.offsetY + layerH * 0.5f;

                float ix0 = std::max(shardX0, layerX0);
                float ix1 = std::min(shardX1, layerX1);
                float iy0 = std::max(shardY0, layerY0);
                float iy1 = std::min(shardY1, layerY1);

                if (ix1 > ix0 && iy1 > iy0) {
                    ShardQuad q;
                    q.vx0 = ix0 - shardCX;
                    q.vx1 = ix1 - shardCX;
                    q.vy0 = iy0 - shardCY;
                    q.vy1 = iy1 - shardCY;

                    float uFrac0 = (ix0 - layerX0) / layerW;
                    float uFrac1 = (ix1 - layerX0) / layerW;
                    float vFrac0 = (iy0 - layerY0) / layerH;
                    float vFrac1 = (iy1 - layerY0) / layerH;

                    float uSpan = layer.af->u1 - layer.af->u0;
                    float vSpan = layer.af->v1 - layer.af->v0;
                    q.u0 = layer.af->u0 + uSpan * uFrac0;
                    q.u1 = layer.af->u0 + uSpan * uFrac1;
                    q.v0 = layer.af->v0 + vSpan * vFrac0;
                    q.v1 = layer.af->v0 + vSpan * vFrac1;

                    q.r = layer.r;
                    q.g = layer.g;
                    q.b = layer.b;

                    piece.quads.push_back(q);
                }
            }

            float rX = 2.0f * ((float)rand() / (float)RAND_MAX) - 1.0f;
            float rY = 2.0f * ((float)rand() / (float)RAND_MAX) - 1.0f;
            float rRot = 2.0f * ((float)rand() / (float)RAND_MAX) - 1.0f;

            piece.xVel = value81 + rX * value82;
            piece.yVel = -(12.0f + 6.0f * rY);
            piece.timer = 1.4f;
            piece.fadeTime = 0.5f;
            piece.rotDelta = (360.0f * rRot) / 60.0f;
            piece.halfSize = (float)std::min(i2, value85) * 0.5f;

            piece.hasTrail = (pieceIndex % 2 == 0);
            piece.trailTimer = 0.0f;

            _explosionPieces.push_back(piece);
        }
    }

    for (int i = 0; i < 100; ++i) {
        DeathParticle dp;
        dp.x = x + ((rand() % 41) - 20.0f);
        dp.y = y + ((rand() % 41) - 20.0f);

        float angle = ((rand() % 360) * 3.14159265f) / 180.0f;
        float speed = 200.0f + (rand() % 601);
        dp.vx = std::cos(angle) * speed;
        dp.vy = std::sin(angle) * speed;

        dp.startScale = 18.0f / 32.0f;
        dp.endScale = 0.0f;
        dp.maxLife = (50.0f + (rand() % 751)) / 1000.0f;
        dp.life = 0.0f;
        _deathParticles.push_back(dp);
    }
}

void Player::_updateExplosion(float dt) {
    if (!_isExploding) return;
    _explosionTime += dt;

    float t = std::min(_explosionTime / 0.5f, 1.0f);
    float easeOut = 1.0f - (1.0f - t) * (1.0f - t);

    _shockwaveRadius = 18.0f + 144.0f * easeOut;
    _shockwaveAlpha = 1.0f - t;

    const float factor = std::min(60.0f * dt * 0.9f, 2.0f);
    const float gravity = factor;
    const float groundScreenY = flipY(0) + _deathCameraY;

    for (auto& piece : _explosionPieces) {
        piece.timer -= dt;
        if (piece.timer > 0.0f) {
            piece.yVel += gravity;
            piece.xVel *= (0.98f + 0.02f * (1.0f - factor));

            piece.x += piece.xVel * factor;
            piece.y += piece.yVel * factor;

            const float groundLimit = groundScreenY - piece.halfSize;
            if (piece.y > groundLimit && piece.yVel > 0.0f) {
                piece.y = groundLimit;
                piece.yVel *= -0.8f;
                if (std::abs(piece.yVel) < 3.0f) {
                    piece.yVel = -3.0f;
                }
            }

            piece.angle += piece.rotDelta * factor;

            if (piece.hasTrail) {
                piece.trailTimer += dt;
                while (piece.trailTimer >= 0.025f) {
                    piece.trailTimer -= 0.025f;
                    PieceTrailParticle tp;
                    tp.x = piece.x + 3.0f * (((rand() % 1000) / 500.0f) - 1.0f) * 2.0f;
                    tp.y = piece.y + 3.0f * (((rand() % 1000) / 500.0f) - 1.0f) * 2.0f;
                    tp.maxLife = (200.0f + (rand() % 201)) / 1000.0f;
                    tp.life = 0.0f;
                    tp.scale = 0.5f;
                    _pieceTrails.push_back(tp);
                }
            }
        }
    }

    for (auto& dp : _deathParticles) {
        if (dp.life < dp.maxLife) {
            dp.life += dt;
            dp.x += dp.vx * dt;
            dp.y += dp.vy * dt;
        }
    }

    for (auto& tp : _pieceTrails) {
        tp.life += dt;
    }
    _pieceTrails.erase(
        std::remove_if(_pieceTrails.begin(), _pieceTrails.end(), [](const PieceTrailParticle& tp) {
            return tp.life >= tp.maxLife;
        }),
        _pieceTrails.end()
    );

    if (_explosionTime > 1.5f) {
        _isExploding = false;
        _explosionPieces.clear();
        _deathParticles.clear();
        _pieceTrails.clear();
    }
}

void Player::_spawnContinuousDust(float playerWorldX, float worldY) {
    float angleDeg = 225.0f + (rand() % 91);
    float speed = 110.0f + (rand() % 81);
    float angleRad = angleDeg * 3.14159265f / 180.0f;

    CubeDustParticle pt;
    pt.x = playerWorldX - 20.0f;
    pt.y = worldY + 26.0f;
    pt.vx = std::cos(angleRad) * speed;
    pt.vy = std::sin(angleRad) * speed;
    pt.life = 0.0f;
    pt.maxLife = (150.0f + (rand() % 301)) / 1000.0f;
    pt.startScale = 0.5f;
    pt.endScale = 0.0f;
    pt.gravityY = 600.0f;
    pt.r = 0.0f; pt.g = 1.0f; pt.b = 0.0f;
    _dustParticles.push_back(pt);
}

void Player::_spawnLandDust(float playerWorldX, float worldY) {
    for (int i = 0; i < 10; ++i) {
        float angleDeg = 210.0f + (rand() % 121);
        float speed = 250.0f + (rand() % 101);
        float angleRad = angleDeg * 3.14159265f / 180.0f;

        CubeDustParticle pt;
        pt.x = playerWorldX;
        pt.y = worldY;
        pt.vx = std::cos(angleRad) * speed;
        pt.vy = std::sin(angleRad) * speed;
        pt.life = 0.0f;
        pt.maxLife = (50.0f + (rand() % 551)) / 1000.0f;
        pt.startScale = 0.625f;
        pt.endScale = 0.0f;
        pt.gravityY = 1000.0f;
        pt.r = 0.0f; pt.g = 1.0f; pt.b = 0.0f;
        _dustParticles.push_back(pt);
    }
}

void Player::_updateDust(float dt) {
    for (auto& pt : _dustParticles) {
        pt.life += dt;
        pt.vy += pt.gravityY * dt;
        pt.x += pt.vx * dt;
        pt.y += pt.vy * dt;
    }
    _dustParticles.erase(
        std::remove_if(_dustParticles.begin(), _dustParticles.end(), [](const CubeDustParticle& pt) {
            return pt.life >= pt.maxLife;
        }),
        _dustParticles.end()
    );
}

void Player::_renderDust(float cameraX, float cameraY) {
    if (_dustParticles.empty()) return;
    applyBlendMode(BLEND_ADD);
    for (const auto& dp : _dustParticles) {
        float t = std::min(dp.life / dp.maxLife, 1.0f);
        float scale = dp.startScale + (dp.endScale - dp.startScale) * t;
        float alpha = 1.0f - t;
        float size = 20.0f * scale;

        drawAtlasFrame("square.png", dp.x - cameraX, dp.y + cameraY, size, size, 0.0f, dp.r, dp.g, dp.b, alpha);
    }
    applyBlendMode(BLEND_NORMAL);
}

void Player::_updateShipParticles(float dt, float playerWorldX) {
    if (!p.isFlying || p.isDead || _endAnimating || _endAnimationFinished) {
        _shipFlames.clear();
        _shipDrags.clear();
        return;
    }

    float rad = _rotation;
    float cosR = std::cos(rad), sinR = std::sin(rad);

    float exhaustX = playerWorldX - 24.0f * cosR - 18.0f * sinR;
    float exhaustY = flipY(p.y) - 24.0f * sinR + 18.0f * cosR;

    _flyParticleTimer += dt;
    while (_flyParticleTimer >= (1.0f / 30.0f)) {
        _flyParticleTimer -= (1.0f / 30.0f);

        float jitter = 4.0f * (((rand() % 1000) / 500.0f) - 1.0f);
        float angleDeg = 225.0f + (rand() % 91);
        float speed = 22.0f + (rand() % 17);
        float angleRad = angleDeg * 0.0174532925f;

        ShipFlameParticle fp;
        fp.x = exhaustX;
        fp.y = exhaustY + jitter;
        fp.vx = std::cos(angleRad) * speed;
        fp.vy = std::sin(angleRad) * speed;
        fp.life = 0.0f;
        fp.maxLife = (150.0f + (rand() % 301)) / 1000.0f;
        fp.startScale = 0.5f;
        fp.endScale = 0.0f;
        fp.gravityY = 600.0f;
        fp.r1 = 1.0f; fp.g1 = 0.627f; fp.b1 = 0.0f;
        fp.r2 = 1.0f; fp.g2 = 0.0f;   fp.b2 = 0.0f;
        _shipFlames.push_back(fp);
    }

    if (p.upKeyDown) {
        _flyParticle2Timer += dt;
        while (_flyParticle2Timer >= (1.0f / 30.0f)) {
            _flyParticle2Timer -= (1.0f / 30.0f);

            float jitter = 4.0f * (((rand() % 1000) / 500.0f) - 1.0f);
            float angleDeg = 180.0f + (rand() % 181);
            float speed = 220.0f + (rand() % 161);
            float angleRad = angleDeg * 0.0174532925f;

            ShipFlameParticle fp;
            fp.x = exhaustX;
            fp.y = exhaustY + jitter;
            fp.vx = std::cos(angleRad) * speed;
            fp.vy = std::sin(angleRad) * speed;
            fp.life = 0.0f;
            fp.maxLife = (150.0f + (rand() % 301)) / 1000.0f;
            fp.startScale = 0.75f;
            fp.endScale = 0.0f;
            fp.gravityY = 600.0f;
            fp.r1 = 1.0f; fp.g1 = 0.753f; fp.b1 = 0.0f;
            fp.r2 = 1.0f; fp.g2 = 0.0f;   fp.b2 = 0.0f;
            _shipFlames.push_back(fp);
        }
    } else {
        _flyParticle2Timer = 0.0f;
    }

    if (p.onGround && !p.onCeiling) {
        _shipDragTimer += dt;
        while (_shipDragTimer >= 0.025f) {
            _shipDragTimer -= 0.025f;

            float xOff = ((rand() % 37) - 18.0f);
            float angleDeg = 205.0f + (rand() % 91);
            float speed = 223.8f + ((rand() % 100) / 100.0f) * 120.0f;
            float angleRad = angleDeg * 0.0174532925f;

            ShipDragParticle dp;
            dp.x = playerWorldX + xOff;
            dp.y = flipY(p.y) + 30.0f;
            dp.vx = std::cos(angleRad) * speed;
            dp.vy = std::sin(angleRad) * speed;
            dp.life = 0.0f;
            dp.maxLife = (80.0f + (rand() % 141)) / 1000.0f;
            dp.startScale = 0.375f;
            dp.endScale = 0.0f;
            dp.gravityX = -700.0f;
            dp.gravityY = 600.0f;
            _shipDrags.push_back(dp);
        }
    } else {
        _shipDragTimer = 0.0f;
    }

    for (auto& fp : _shipFlames) {
        fp.life += dt;
        fp.vy += fp.gravityY * dt;
        fp.x += fp.vx * dt;
        fp.y += fp.vy * dt;
    }
    _shipFlames.erase(
        std::remove_if(_shipFlames.begin(), _shipFlames.end(), [](const ShipFlameParticle& p) {
            return p.life >= p.maxLife;
        }),
        _shipFlames.end()
    );

    for (auto& dp : _shipDrags) {
        dp.life += dt;
        dp.vx += dp.gravityX * dt;
        dp.vy += dp.gravityY * dt;
        dp.x += dp.vx * dt;
        dp.y += dp.vy * dt;
    }
    _shipDrags.erase(
        std::remove_if(_shipDrags.begin(), _shipDrags.end(), [](const ShipDragParticle& p) {
            return p.life >= p.maxLife;
        }),
        _shipDrags.end()
    );
}

void Player::_renderShipParticles(float cameraX, float cameraY) {
    if (_shipFlames.empty() && _shipDrags.empty()) return;

    applyBlendMode(BLEND_ADD);

    for (const auto& fp : _shipFlames) {
        float t = std::min(fp.life / fp.maxLife, 1.0f);
        float sc = fp.startScale + (fp.endScale - fp.startScale) * t;
        float alpha = 1.0f - t;
        float r = fp.r1 + (fp.r2 - fp.r1) * t;
        float g = fp.g1 + (fp.g2 - fp.g1) * t;
        float b = fp.b1 + (fp.b2 - fp.b1) * t;
        float size = 20.0f * sc;

        drawAtlasFrame("square.png", fp.x - cameraX, fp.y + cameraY, size, size, 0.0f, r, g, b, alpha);
    }

    for (const auto& dp : _shipDrags) {
        float t = std::min(dp.life / dp.maxLife, 1.0f);
        float sc = dp.startScale + (dp.endScale - dp.startScale) * t;
        float alpha = 1.0f - t;
        float size = 20.0f * sc;

        drawAtlasFrame("square.png", dp.x - cameraX, dp.y + cameraY, size, size, 0.0f, 1.0f, 0.95f, 0.6f, alpha);
    }

    applyBlendMode(BLEND_NORMAL);
}

void Player::playEndAnimation(float targetX, std::function<void()> onComplete, float targetY) {
    _endAnimating = true;
    _endAnimationFinished = false;
    _endAnimTime = 0.0f;
    _endAnimCallback = onComplete;

    _endWasFlying = p.isFlying;
    _endStartX = _lastWorldX;
    _endStartY = p.y;
    _endP2X = _endStartX + 80.0f;
    _endP2Y = targetY + 300.0f;
    _endP3X = targetX + 100.0f;
    _endP3Y = targetY - 40.0f;
    _endStartAngle = _rotation * 57.2958f;

    _dustParticles.clear();
    _shipFlames.clear();
    _shipDrags.clear();
}

void Player::update(float dt, float playerWorldX, float cameraY, float cameraX) {
    if (_endAnimationFinished) {
        return;
    }

    float forwardDx = playerWorldX - _prevWorldX;
    if (std::abs(forwardDx) > 50.0f) forwardDx = 0.0f;
    _prevWorldX = playerWorldX;
    _lastWorldX = playerWorldX;

    if (p.isDead) {
        _updateExplosion(dt);
        _updateDust(dt);
        return;
    }

    if (_endAnimating) {
        _endAnimTime += dt;
        float rawT = std::min(_endAnimTime / 1.0f, 1.0f);
        float val = std::pow(rawT, 1.2f);

        float omt = 1.0f - val;
        float omt2 = omt * omt;
        float omt3 = omt2 * omt;
        float val2 = val * val;
        float val3 = val2 * val;

        float currX = omt3 * _endStartX +
        3.0f * omt2 * val * _endStartX +
        3.0f * omt * val2 * _endP2X +
        val3 * _endP3X;

        float currY = omt3 * _endStartY +
        3.0f * omt2 * val * _endStartY +
        3.0f * omt * val2 * _endP2Y +
        val3 * _endP3Y;

        float screenX = currX - cameraX;
        float screenY = flipY(currY) + cameraY;

        float alpha = std::max(0.0f, 1.0f - val * val);
        float angle = _endStartAngle + 360.0f * std::pow(rawT, 1.5f);
        float rad = angle * 0.0174532925f;
        float cosR = std::cos(rad);
        float sinR = std::sin(rad);

        if (_endWasFlying) {
            float shipLocalY = 10.0f;
            float cubeLocalY = -10.0f;

            float shipX = screenX - shipLocalY * sinR;
            float shipY = screenY + shipLocalY * cosR;
            _shipSpriteLayer.setPosition(shipX, shipY);
            _shipOverlayLayer.setPosition(shipX, shipY);
            _shipExtraLayer.setPosition(shipX, shipY);
            _shipSpriteLayer.setAngle(angle);
            _shipOverlayLayer.setAngle(angle);
            _shipExtraLayer.setAngle(angle);
            _shipSpriteLayer.setAlpha(alpha);
            _shipOverlayLayer.setAlpha(alpha);
            _shipExtraLayer.setAlpha(alpha);

            float cubeX = screenX - cubeLocalY * sinR;
            float cubeY = screenY + cubeLocalY * cosR;
            _playerSpriteLayer.setPosition(cubeX, cubeY);
            _playerOverlayLayer.setPosition(cubeX, cubeY);
            _playerExtraLayer.setPosition(cubeX, cubeY);
            _playerSpriteLayer.setAngle(angle);
            _playerOverlayLayer.setAngle(angle);
            _playerExtraLayer.setAngle(angle);
            _playerSpriteLayer.setAlpha(alpha);
            _playerOverlayLayer.setAlpha(alpha);
            _playerExtraLayer.setAlpha(alpha);
        } else {
            _playerSpriteLayer.setPosition(screenX, screenY);
            _playerOverlayLayer.setPosition(screenX, screenY);
            _playerExtraLayer.setPosition(screenX, screenY);
            _playerSpriteLayer.setAngle(angle);
            _playerOverlayLayer.setAngle(angle);
            _playerExtraLayer.setAngle(angle);
            _playerSpriteLayer.setAlpha(alpha);
            _playerOverlayLayer.setAlpha(alpha);
            _playerExtraLayer.setAlpha(alpha);
        }

        _streak.setPosition(currX, flipY(currY));
        _streak.update(dt);

        if (rawT >= 1.0f) {
            _endAnimating = false;
            _endAnimationFinished = true;
            p.isFlying = false;

            _shipFlames.clear();
            _shipDrags.clear();
            _dustParticles.clear();

            setCubeVisible(false);
            setShipVisible(false);
            _streak.stop();
            _streak.reset();
            if (_endAnimCallback) _endAnimCallback();
        }
        return;
    }

    float screenX = playerWorldX - cameraX;
    float screenY = flipY(p.y) + cameraY;

    if (p.isFlying) {
        float rad = _rotation;
        float cosR = std::cos(rad), sinR = std::sin(rad);
        float offset = 10.0f;

        _shipSpriteLayer.setPosition(screenX - offset * sinR, screenY + offset * cosR);
        _shipOverlayLayer.setPosition(screenX - offset * sinR, screenY + offset * cosR);
        _shipExtraLayer.setPosition(screenX - offset * sinR, screenY + offset * cosR);
        _shipSpriteLayer.setAngle(rad * 57.2958f);
        _shipOverlayLayer.setAngle(rad * 57.2958f);
        _shipExtraLayer.setAngle(rad * 57.2958f);

        _playerSpriteLayer.setPosition(screenX + offset * sinR, screenY - offset * cosR);
        _playerOverlayLayer.setPosition(screenX + offset * sinR, screenY - offset * cosR);
        _playerExtraLayer.setPosition(screenX + offset * sinR, screenY - offset * cosR);
        _playerSpriteLayer.setAngle(rad * 57.2958f);
        _playerOverlayLayer.setAngle(rad * 57.2958f);
        _playerExtraLayer.setAngle(rad * 57.2958f);

        float exhaustX = playerWorldX - 24.0f * cosR - 18.0f * sinR;
        float exhaustY = flipY(p.y) - 24.0f * sinR + 18.0f * cosR;

        _streak.setPosition(exhaustX + 8.0f, exhaustY);
        _streak.update(dt);

        _dustAccumulator = 0.0f;
        _updateShipParticles(dt, playerWorldX);
    } else {
        _playerSpriteLayer.setPosition(screenX, screenY);
        _playerOverlayLayer.setPosition(screenX, screenY);
        _playerExtraLayer.setPosition(screenX, screenY);
        float deg = _rotation * 57.2958f;
        _playerSpriteLayer.setAngle(deg);
        _playerOverlayLayer.setAngle(deg);
        _playerExtraLayer.setAngle(deg);

        _streak.stop();
        _streak.reset();
        _shipFlames.clear();
        _shipDrags.clear();

        if (p.onGround) {
            _dustAccumulator += dt;
            while (_dustAccumulator >= 0.03333f) {
                _spawnContinuousDust(playerWorldX, flipY(p.y));
                _dustAccumulator -= 0.03333f;
            }
        } else {
            _dustAccumulator = 0.0f;
        }
    }

    _updateDust(dt);
}

void Player::render(float cameraX, float cameraY) {
    if (_endAnimationFinished) {
        return;
    }

    _renderDust(cameraX, cameraY);

    if (p.isFlying || _endAnimating) {
        _renderShipParticles(cameraX, cameraY);
        _streak.render(cameraX, cameraY);
    }

    if (_isExploding) {
        if (_shockwaveAlpha > 0.0f) {
            RenderDevice::get().drawCircle(_deathScreenX, _deathScreenY, _shockwaveRadius, 0.0f, 1.0f, 0.0f, _shockwaveAlpha, 32, BLEND_ADD);
        }

        applyBlendMode(BLEND_ADD);

        for (const auto& tp : _pieceTrails) {
            float pt = tp.life / tp.maxLife;
            float scale = tp.scale * (1.0f - pt);
            float alpha = 1.0f - pt;
            float size = 20.0f * scale;
            drawAtlasFrame("square.png", tp.x, tp.y, size, size, 0.0f, 0.0f, 1.0f, 0.0f, alpha);
        }

        for (const auto& dp : _deathParticles) {
            if (dp.life < dp.maxLife) {
                float pt = dp.life / dp.maxLife;
                float scale = dp.startScale + (dp.endScale - dp.startScale) * pt;
                float alpha = 1.0f - pt;
                float size = 20.0f * scale;

                drawAtlasFrame("square.png", dp.x, dp.y, size, size, 0.0f, 0.0f, 1.0f, 0.0f, alpha);
            }
        }
        applyBlendMode(BLEND_NORMAL);

        uint32_t atlas = BootScene::textures["GJ_WebSheet"].id;

        // Native batched rendering and CPU rotation for maximum performance on Intel GMA
        for (const auto& piece : _explosionPieces) {
            float pAlpha = (piece.timer < piece.fadeTime) ? (piece.timer / piece.fadeTime) : 1.0f;
            float rad = piece.angle * 0.0174532925f;
            float cosR = std::cos(rad);
            float sinR = std::sin(rad);

            for (const auto& q : piece.quads) {
                float x0 = piece.x + (q.vx0 * cosR - q.vy0 * sinR);
                float y0 = piece.y + (q.vx0 * sinR + q.vy0 * cosR);

                float x1 = piece.x + (q.vx1 * cosR - q.vy0 * sinR);
                float y1 = piece.y + (q.vx1 * sinR + q.vy0 * cosR);

                float x2 = piece.x + (q.vx1 * cosR - q.vy1 * sinR);
                float y2 = piece.y + (q.vx1 * sinR + q.vy1 * cosR);

                float x3 = piece.x + (q.vx0 * cosR - q.vy1 * sinR);
                float y3 = piece.y + (q.vx0 * sinR + q.vy1 * cosR);

                RenderDevice::get().batchQuad(
                    atlas,
                    x0, y0, q.u0, q.v0,
                    x1, y1, q.u1, q.v0,
                    x2, y2, q.u1, q.v1,
                    x3, y3, q.u0, q.v1,
                    q.r, q.g, q.b, pAlpha, BLEND_NORMAL
                );
            }
        }
        RenderDevice::get().flushBatch();
        return;
    }

    if (p.isFlying || (_endAnimating && _endWasFlying)) {
        _shipSpriteLayer.render();
        _shipOverlayLayer.render();
        _shipExtraLayer.render();
    }
    _playerOverlayLayer.render();
    _playerSpriteLayer.render();
    _playerExtraLayer.render();
}

void Player::reset() {
    p.reset();
    _rotation = 0.0f;
    rotateActionActive = false;
    _endAnimating = false;
    _endAnimationFinished = false;
    _endWasFlying = false;
    _isExploding = false;
    _explosionPieces.clear();
    _deathParticles.clear();
    _pieceTrails.clear();
    _dustParticles.clear();
    _dustAccumulator = 0.0f;
    _shipFlames.clear();
    _shipDrags.clear();
    _flyParticleTimer = 0.0f;
    _flyParticle2Timer = 0.0f;
    _shipDragTimer = 0.0f;

    setCubeVisible(true);
    setShipVisible(false);

    _playerSpriteLayer.setAlpha(1.0f);
    _playerOverlayLayer.setAlpha(1.0f);
    _playerExtraLayer.setAlpha(1.0f);
    _playerGlowLayer.setAlpha(1.0f);

    _shipSpriteLayer.setAlpha(1.0f);
    _shipOverlayLayer.setAlpha(1.0f);
    _shipExtraLayer.setAlpha(1.0f);
    _shipGlowLayer.setAlpha(1.0f);

    _playerSpriteLayer.setScale(1.0f);
    _playerOverlayLayer.setScale(1.0f);
    _playerExtraLayer.setScale(1.0f);

    _shipSpriteLayer.setScale(1.0f);
    _shipOverlayLayer.setScale(1.0f);
    _shipExtraLayer.setScale(1.0f);

    _streak.stop();
    _streak.reset();
}
