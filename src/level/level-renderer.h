#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <cmath>
#include "utils/constants.h"
#include "level-data-helpers.h"
#include "pako-compression.h"

// Members are ordered so that everything the render loop touches sits in the first 64 bytes
// (one cache line): iterating thousands of sprites per frame on an old CPU no longer drags
// the std::string and the enter-effect bookkeeping through the cache.
struct VisualSprite {
    // --- hot (render) data: 64 bytes ---
    const AtlasFrame* framePtr = nullptr;
    float x = 0.0f, y = 0.0f;
    float w = 0.0f, h = 0.0f;
    float scaleX = 1.0f, scaleY = 1.0f;
    float rotation = 0.0f;
    float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;
    GLuint textureID = 0;
    BlendMode blend = BLEND_NORMAL;
    bool flipX = false, flipY = false;
    bool visible = true;
    bool audioScale = false;

    // --- cold data ---
    bool eeActive = false;
    int layer = 1;
    float baseX = 0.0f, baseY = 0.0f;
    float worldX = 0.0f;
    float baseAlpha = 1.0f;
    std::string frame;
};

struct ColorTrigger {
    float x = 0.0f;
    int index = 1000;
    float r = 1.0f, g = 1.0f, b = 1.0f;
    float duration = 0.0f;
    bool tintGround = false;
};

struct EnterEffectTrigger {
    float x = 0.0f;
    int effect = 0;
};

struct PortalVortexParticle {
    float rx = 0.0f, ry = 0.0f;
    float vx = 0.0f, vy = 0.0f;
    float life = 0.0f, maxLife = 0.5f;
};

class LevelRenderer {
public:
    LevelRenderer();
    ~LevelRenderer();

    void loadLevel(const std::string& levelStr);
    void resetObjects();
    void resetVisibility();
    void resetGroundState();

    void updateGroundTiles(float cameraX, float cameraY, float dt);
    void updatePortals(float dt, float cameraX);

    void stepGroundAnimation(float dt);
    void updateVisibility(float cameraX);
    void applyEnterEffects(float cameraX);
    void setGroundColor(float r, float g, float b);
    void updateAudioScale(float scale);

    void setFlyMode(bool active, float playerY);
    float getFloorY() const;
    float getCeilingY() const;
    bool hasCeiling() const { return _flyGroundActive; }
    float flyCameraTarget = -1.0f;

    void updateEndPortalY(float cameraY, bool isFlying);
    float getEndPortalGameY() const { return _endPortalGameY; }

    const std::vector<ColorTrigger>& checkColorTriggers(float cameraX);
    void resetColorTriggers();
    void checkEnterEffectTriggers(float cameraX);
    void resetEnterEffectTriggers();

    const std::vector<LevelObject*>& getNearbySectionObjects(float cameraX);

    void renderLayer0(float cameraX, float cameraY);
    void renderLayer1(float cameraX, float cameraY);
    void renderLayer2(float cameraX, float cameraY);
    void renderGround(float cameraX, float cameraY);

    float endXPos = 0.0f;
    float songOffset = 0.0f; // seconds, from kA13 in the level header
    int startSpeed = 0;      // kA4: initial speed (0: 1x, 1: 0.7x, 2: 1.1x, 3: 1.3x, 4: 1.6x)
    bool startMini = false;  // kA11: mini mode start (true: mini, false: normal)
    std::vector<LevelObject> objects;

private:
    void _buildGround();
    void _spawnLevelObjects(const std::vector<LevelObjectRaw>& rawObjects);
    void _addGlowSprite(float x, float y, const std::string& frame, const LevelObjectRaw& raw, float worldX);
    void _addToSection(const VisualSprite& sprite);
    void _addCollisionToSection(size_t objIndex, float worldX);
    std::string _getGlowFrameName(const std::string& frame);

    void _updateEndPortalVortex(float dt, float cameraX);

    float _tileW = 1012.0f;
    std::vector<float> _groundWorldX;
    float _maxGroundWorldX = 0.0f;
    float _groundR = 0.07f, _groundG = 0.27f, _groundB = 0.68f;

    bool _flyGroundActive = false;
    float _groundTargetValue = 0.0f;
    float _groundAnimFrom = 0.0f;
    float _groundAnimTo = 0.0f;
    float _groundAnimTime = 0.0f;
    float _groundAnimDuration = 0.5f;
    bool _groundAnimating = false;
    float _flyFloorY = 0.0f;
    float _flyCeilingY = 0.0f;
    float _currentAudioScale = 1.0f;

    float _groundStartScreenY = 460.0f;
    float _ceilingStartScreenY = 0.0f;
    float _lastCameraY = 0.0f;
    float _lastCameraX = 0.0f;

    float _endPortalGameY = 240.0f;
    std::vector<PortalVortexParticle> _vortexParticles;
    float _vortexTimer = 0.0f;

    std::vector<ColorTrigger> _colorTriggers;
    std::vector<ColorTrigger> _activeColorTriggers;
    size_t _colorTriggerIdx = 0;
    std::vector<EnterEffectTrigger> _enterEffectTriggers;
    size_t _enterEffectTriggerIdx = 0;
    int _activeEnterEffect = 0;
    int _activeExitEffect = 0;

    struct SectionLayers {
        std::vector<VisualSprite> layer0;
        std::vector<VisualSprite> layer1;
        std::vector<VisualSprite> layer2;
    };
    std::vector<SectionLayers> _sections;
    std::vector<std::vector<size_t>> _collisionSections;
    std::vector<LevelObject*> _nearbyBuffer;
    int _cachedCollisionSec = -1;
    int _visMinSec = -1;
    int _visMaxSec = -1;

    const AtlasFrame* _gndAf = nullptr;
    const AtlasFrame* _sqAf = nullptr;
    const AtlasFrame* _frontSqAf = nullptr;
    const AtlasFrame* _fillSqAf = nullptr;
    const AtlasFrame* _gradAf = nullptr;
    const AtlasFrame* _floorLineAf = nullptr;
    const AtlasFrame* _shadowAf = nullptr;
    uint32_t _webSheetId = 0;
};
