#pragma once
#include <cmath>
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
    if (mode == BLEND_ADD) {
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    } else {
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }
    // Communicate blend mode to active renderer (DirectX 8 / OpenGL)
    setEngineBlendMode(mode);
}
