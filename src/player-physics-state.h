#pragma once

class PlayerPhysicsState {
public:
    float y;
    float lastY;
    float lastGroundPosY;
    float yVelocity;

    bool onGround;
    bool canJump;
    bool isJumping;
    bool gravityFlipped;
    bool isFlying;
    bool wasBoosted;

    float collideTop;
    float collideBottom;
    bool onCeiling;

    bool upKeyDown;
    bool upKeyPressed;
    bool isDead;

    PlayerPhysicsState() {
        reset();
    }

    void reset() {
        y = 30.0f;
        lastY = 30.0f;
        lastGroundPosY = 30.0f;
        yVelocity = 0.0f;

        onGround = true;
        canJump = true;
        isJumping = false;
        gravityFlipped = false;
        isFlying = false;
        wasBoosted = false;

        collideTop = 0.0f;
        collideBottom = 0.0f;
        onCeiling = false;

        upKeyDown = false;
        upKeyPressed = false;
        isDead = false;
    }
};
