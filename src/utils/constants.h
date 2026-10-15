#pragma once
#include <cmath>
#include <cstdint>
#include <SDL2/SDL_opengl.h>

inline int screenWidth = (int)std::round(10240.0 / 9.0);
inline const int screenHeight = 640;
inline const int baseUnit = 60;
inline const int unusedConst180 = 180;
inline float groundYOffset = (float)screenWidth / 2.0f - 150.0f;

inline void setScreenWidth(int newWidth) {
    screenWidth = newWidth;
    groundYOffset = (float)newWidth / 2.0f - 150.0f;
}

inline const float fixedTimeStep = 1.0f / 240.0f;
inline const float gravityConst = 11.540004f;
inline const float physicsConst09 = 0.9f;
inline const float physicsConst1916 = 1.916398f;
inline const float physicsConst600 = 600.0f;
inline const int baseUnitAlias = baseUnit;

inline const unsigned int colorGreenTint = 65280;
inline const unsigned int colorCyanTint = 65535;

inline const char* const solid = "solid";
inline const char* const hazard = "hazard";
inline const char* const portalFly = "portal_fly";
inline const char* const portalCube = "portal_cube";
inline const char* const portalMini = "portal_mini";
inline const char* const portalNormal = "portal_normal";

inline const float yFlipBase = 460.0f;
inline float flipY(float yValue) {
    return yFlipBase - yValue;
}

enum BlendMode {
    BLEND_NORMAL,
    BLEND_ADD
};

inline BlendMode blendNormal = BLEND_NORMAL;
inline BlendMode blendAdd = BLEND_ADD;

// Forward declaration to render-device.cpp to synchronize Direct3D 8
void setEngineBlendMode(BlendMode mode);

inline void applyBlendMode(BlendMode mode) {
    // Communicate blend mode to active renderer (PMA handles normal and additive without changing GPU blend state)
    setEngineBlendMode(mode);
}

namespace GDLogic {
namespace MiniScale {

    // Escalas de jugador (PlayerObject::m_vehicleSize / offset 504)
    constexpr float NORMAL_SCALE = 1.0f;
    constexpr float MINI_SCALE   = 0.6f;

    // Factores de física (PlayerObject::updateJump / PlayerObject::update)
    constexpr float MINI_GRAVITY_DIVISOR   = 0.8f;      // Divisor v20 en updateJump: 1.0f / 0.8f = 1.25x gravedad
    constexpr float MINI_SHIP_GRAVITY_MOD  = 0.5875f;   // Modificador de gravedad en nave mini
    constexpr float MINI_UFO_GRAVITY_MOD   = 0.725f;    // Modificador de gravedad en UFO mini
    constexpr float MINI_WAVE_GRAVITY_MOD  = 0.61538f;  // Modificador de gravedad en Wave mini
    constexpr float MINI_JUMP_MULTIPLIER   = 0.8f;      // Multiplicador de salto para Cubo/Robot/UFO

    // IDs de Objeto en Nivel (m_objectID / offset 221)
    constexpr int OBJECT_ID_NORMAL_PORTAL  = 99;   // Portal de tamaño normal (verde)
    constexpr int OBJECT_ID_MINI_PORTAL    = 101;  // Portal de tamaño mini (magenta/rosa)

    // GameObjectType (offset 194)
    constexpr int GAMEOBJECT_TYPE_NORMAL_PORTAL = 17; // 0x11: RegularSizePortal
    constexpr int GAMEOBJECT_TYPE_MINI_PORTAL   = 18; // 0x12: MiniSizePortal

    // Códigos de Eventos de Nivel (GJGameEvent)
    constexpr int GAME_EVENT_PORTAL_NORMAL_SCALE = 55; // "Portal: Normal Scale"
    constexpr int GAME_EVENT_PORTAL_MINI_SCALE   = 56; // "Portal: Mini Scale"

    // Tag de acción interna para escala elástica
    constexpr int ACTION_TAG_PLAYER_SCALE = 6;

    // Dimensiones canónicas de los portales
    constexpr float PORTAL_SPRITE_WIDTH  = 31.0f;
    constexpr float PORTAL_SPRITE_HEIGHT = 90.0f;

    // Texturas y spritesheets
    inline const char* getMiniPortalBackFrame()      { return "portal_09_back_001.png"; }
    inline const char* getMiniPortalFrontFrame()     { return "portalshine_04_front_001.png"; }
    inline const char* getMiniPortalShineBackFrame() { return "portalshine_04_back_001.png"; }
    inline const char* getMiniPortalParticlePlist()  { return "portalEffect09.plist"; }

    inline const char* getNormalPortalBackFrame()      { return "portal_08_back_001.png"; }
    inline const char* getNormalPortalFrontFrame()     { return "portalshine_04_front_001.png"; }
    inline const char* getNormalPortalShineBackFrame() { return "portalshine_04_back_001.png"; }
    inline const char* getNormalPortalParticlePlist()  { return "portalEffect08.plist"; }

    struct PortalColor {
        uint8_t r, g, b;
    };

    // Mini: Magenta brillante (255, 0, 150)
    inline PortalColor getMiniPortalColor() {
        return {255, 0, 150};
    }

    // Normal: Verde brillante (0, 255, 150)
    inline PortalColor getNormalPortalColor() {
        return {0, 255, 150};
    }

    struct ScaleCircleParams {
        float startRadius;
        float endRadius;
        float duration;
        PortalColor color;
    };

    inline ScaleCircleParams getScaleCircleParameters(float vehicleSize) {
        ScaleCircleParams params{};
        if (vehicleSize == NORMAL_SCALE) {
            params.startRadius = 10.0f;
            params.endRadius   = 40.0f;
            params.duration    = 0.30f;
            params.color       = {0, 255, 150};
        } else {
            params.startRadius = 50.0f;
            params.endRadius   = 2.0f;
            params.duration    = 0.25f;
            params.color       = {255, 0, 150};
        }
        return params;
    }

    class MiniPhysicsEngine {
    public:
        static inline float getGravityDivisor(bool isMini) {
            return isMini ? MINI_GRAVITY_DIVISOR : 1.0f;
        }

        static inline float getEffectiveGravityMultiplier(bool isMini) {
            return isMini ? (1.0f / MINI_GRAVITY_DIVISOR) : 1.0f;
        }

        static inline float getModeGravityScale(bool isMini, bool isShip, bool isUFO, bool isWave) {
            if (!isMini) {
                if (isShip) return 0.47f;
                if (isUFO)  return 0.58f;
                if (isWave) return 0.40f;
                return 1.0f;
            }
            if (isShip) return MINI_SHIP_GRAVITY_MOD;
            if (isUFO)  return MINI_UFO_GRAVITY_MOD;
            if (isWave) return MINI_WAVE_GRAVITY_MOD;
            return 1.0f;
        }

        static inline float calculateCubeJumpVelocity(float baseJump, bool isMini) {
            return isMini ? (baseJump * MINI_JUMP_MULTIPLIER) : baseJump;
        }

        static inline float getShipMaxVelocityY(bool isMini) {
            return isMini ? (8.0f / 0.85f) : 8.0f;
        }

        static inline float getShipMinVelocityY(bool isMini) {
            return isMini ? (-6.4f / 0.85f) : -6.4f;
        }

        static inline float getUFOJumpImpulse(bool isMini) {
            return isMini ? (8.0f * MINI_JUMP_MULTIPLIER) : 7.0f;
        }

        static inline float getRotationStepDuration(bool isMini) {
            return isMini ? 0.33333f : 0.43333f;
        }

        static inline float getRobotAnimationSpeedFactor(bool isMini) {
            return isMini ? 0.85f : 1.0f;
        }

        static inline float getSlopeCollisionTolerance(float vehicleSize) {
            return vehicleSize * 20.0f;
        }

        static inline float getSpiderCheckOffset(float vehicleSize) {
            return vehicleSize * 8.0f;
        }

        static inline float getPadBounceModifier(int padType, bool isMini, bool isShip, bool isBall) {
            if (padType == 34) {
                if (isShip) return isMini ? 0.95f : 0.63f;
                if (isBall) return isMini ? 0.98f : 0.60f;
                return 1.25f;
            }
            return 1.0f;
        }

        static inline float getRingJumpVelocityModifier(bool isMini) {
            return isMini ? 0.8f : 1.0f;
        }

        static inline float getHitGroundParticleYOffset(float vehicleSize, float flipMod) {
            return (-15.0f * flipMod) * vehicleSize;
        }

        static inline float getCollisionMargin(float vehicleSize) {
            return vehicleSize * 5.0f;
        }

        static inline float getJumpStreakXOffset(float vehicleSize, float reverseMod) {
            return (vehicleSize * -2.0f) * reverseMod;
        }
    };

} // namespace MiniScale
} // namespace GDLogic
