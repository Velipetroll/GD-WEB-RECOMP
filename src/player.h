#pragma once
#include <vector>
#include <functional>
#include <cmath>
#include <SDL2/SDL_opengl.h>
#include "constants.h"
#include "player-physics-state.h"
#include "sprite-layer-helper.h"
#include "trail-renderer.h"

class LevelRenderer;

struct ExplosionLayer {
    float u0 = 0.0f, v0 = 0.0f, u1 = 0.0f, v1 = 0.0f;
    float r = 1.0f, g = 1.0f, b = 1.0f;
    bool valid = false;
};

struct ShardQuad {
    float vx0 = 0.0f, vy0 = 0.0f, vx1 = 0.0f, vy1 = 0.0f;
    float u0 = 0.0f, v0 = 0.0f, u1 = 0.0f, v1 = 0.0f;
    float r = 1.0f, g = 1.0f, b = 1.0f;
};

struct ExplosionPiece {
    float x = 0.0f, y = 0.0f;
    float w = 0.0f, h = 0.0f;
    float xVel = 0.0f, yVel = 0.0f;
    float angle = 0.0f, rotDelta = 0.0f;
    float timer = 1.4f;
    float fadeTime = 0.5f;
    float halfSize = 10.0f;
    bool hasTrail = false;
    float trailTimer = 0.0f;

    std::vector<ShardQuad> quads;
};

struct PieceTrailParticle {
    float x = 0.0f, y = 0.0f;
    float life = 0.0f, maxLife = 0.3f;
    float scale = 0.5f;
};

struct CubeDustParticle {
    float x = 0.0f, y = 0.0f;
    float vx = 0.0f, vy = 0.0f;
    float startScale = 0.5f, endScale = 0.0f;
    float life = 0.0f, maxLife = 0.25f;
    float gravityY = 600.0f;
    float r = 0.0f, g = 1.0f, b = 0.0f;
};

struct DeathParticle {
    float x = 0.0f, y = 0.0f;
    float vx = 0.0f, vy = 0.0f;
    float startScale = 0.5625f, endScale = 0.0f;
    float life = 0.0f, maxLife = 0.8f;
};

struct ShipFlameParticle {
    float x = 0.0f, y = 0.0f;
    float vx = 0.0f, vy = 0.0f;
    float startScale = 0.5f, endScale = 0.0f;
    float life = 0.0f, maxLife = 0.3f;
    float gravityY = 600.0f;
    float r1 = 1.0f, g1 = 0.627f, b1 = 0.0f;
    float r2 = 1.0f, g2 = 0.0f,   b2 = 0.0f;
};

struct ShipDragParticle {
    float x = 0.0f, y = 0.0f;
    float vx = 0.0f, vy = 0.0f;
    float startScale = 0.375f, endScale = 0.0f;
    float life = 0.0f, maxLife = 0.15f;
    float gravityX = -700.0f;
    float gravityY = 600.0f;
};

class Player {
public:
    Player(PlayerPhysicsState& state, LevelRenderer& levelRenderer);
    ~Player();

    void reset();
    void update(float dt, float playerWorldX, float cameraY, float cameraX);
    void render(float cameraX, float cameraY);

    void updateJump(float dt);
    void checkCollisions(float playerWorldXArg, float cameraY);
    void hitGround(float playerWorldX);
    void killPlayer(float cameraY);

    void enterShipMode(const LevelObject* portal = nullptr);
    void exitShipMode();
    void setCubeVisible(bool visible);
    void setShipVisible(bool visible);

    void runRotateAction();
    void stopRotation();
    void updateRotateAction(float dt);
    void updateGroundRotation(float dt);
    void updateShipRotation(float dt);

    void playEndAnimation(float targetX, std::function<void()> onComplete, float targetY = 240.0f);

    PlayerPhysicsState& p;

    float getRotation() const { return _rotation; }
    void setShowHitboxes(bool show) { _showHitboxes = show; }

private:
    LevelRenderer& _levelRenderer;

    float _rotation = 0.0f;
    bool rotateActionActive = false;
    float rotateActionTime = 0.0f;
    float rotateActionDuration = 0.0f;
    float rotateActionStart = 0.0f;
    float rotateActionTotal = 0.0f;

    bool _showHitboxes = false;
    const LevelObject* _lastLandObject = nullptr;
    float _lastXOffset = 0.0f;
    float _lastWorldX = 0.0f;
    float _prevWorldX = 0.0f;

    LayeredSprite _playerGlowLayer;
    LayeredSprite _playerSpriteLayer;
    LayeredSprite _playerOverlayLayer;
    LayeredSprite _playerExtraLayer;

    LayeredSprite _shipGlowLayer;
    LayeredSprite _shipSpriteLayer;
    LayeredSprite _shipOverlayLayer;
    LayeredSprite _shipExtraLayer;

    TrailRenderer _streak;

    std::vector<CubeDustParticle> _dustParticles;
    float _dustAccumulator = 0.0f;
    void _spawnContinuousDust(float playerWorldX, float worldY);
    void _spawnLandDust(float playerWorldX, float worldY);
    void _updateDust(float dt);
    void _renderDust(float cameraX, float cameraY);

    std::vector<ShipFlameParticle> _shipFlames;
    std::vector<ShipDragParticle> _shipDrags;
    float _flyParticleTimer = 0.0f;
    float _flyParticle2Timer = 0.0f;
    float _shipDragTimer = 0.0f;
    void _updateShipParticles(float dt, float playerWorldX);
    void _renderShipParticles(float cameraX, float cameraY);

    bool _isExploding = false;
    float _explosionTime = 0.0f;
    float _shockwaveRadius = 18.0f;
    float _shockwaveAlpha = 1.0f;

    float _deathScreenX = 0.0f;
    float _deathScreenY = 0.0f;
    float _deathCameraY = 0.0f;

    std::vector<ExplosionPiece> _explosionPieces;
    std::vector<DeathParticle> _deathParticles;
    std::vector<PieceTrailParticle> _pieceTrails;
    void _createExplosionPieces(float x, float y);
    void _updateExplosion(float dt);

    bool _endAnimating = false;
    bool _endAnimationFinished = false;
    float _endAnimTime = 0.0f;
    float _endStartX = 0.0f, _endStartY = 0.0f;
    float _endP2X = 0.0f, _endP2Y = 0.0f;
    float _endP3X = 0.0f, _endP3Y = 0.0f;
    float _endStartAngle = 0.0f;
    bool _endWasFlying = false;
    std::function<void()> _endAnimCallback;

    float flipMod() const { return p.gravityFlipped ? -1.0f : 1.0f; }
    bool playerIsFalling() const;
    bool _isFallingPastThreshold() const;
    float convertToClosestRotation() const;
    float slerp2D(float a, float b, float t) const;
    void _checkSnapJump(const LevelObject* obj, float playerWorldX);
};
