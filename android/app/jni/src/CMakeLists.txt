cmake_minimum_required(VERSION 3.6)

project(GDWebNative)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../../src)

set(GAME_SOURCES
    ${SRC_DIR}/audio/audio-manager.cpp
    ${SRC_DIR}/scenes/boot-scene.cpp
    ${SRC_DIR}/utils/color-manager.cpp
    ${SRC_DIR}/utils/font-helpers.cpp
    ${SRC_DIR}/scenes/game-scene.cpp
    ${SRC_DIR}/scenes/game-scene-ui.cpp
    ${SRC_DIR}/level/level-data-helpers.cpp
    ${SRC_DIR}/level/level-renderer.cpp
    ${SRC_DIR}/main.cpp
    ${SRC_DIR}/utils/pako-compression.cpp
    ${SRC_DIR}/player/player.cpp
    ${SRC_DIR}/render/render-d3d8.cpp
    ${SRC_DIR}/render/render-d3d9.cpp
    ${SRC_DIR}/render/render-device.cpp
    ${SRC_DIR}/render/render-gl1.cpp
    ${SRC_DIR}/render/render-webgl.cpp
    ${SRC_DIR}/sprite/sprite-layer-helper.cpp
    ${SRC_DIR}/trail/trail-renderer.cpp
    ${SRC_DIR}/utils/win-effects.cpp
)

add_library(main SHARED ${GAME_SOURCES})

target_include_directories(main PRIVATE
    ${SRC_DIR}
    ${SRC_DIR}/assets
    ${SRC_DIR}/vendor
    ${SRC_DIR}/audio
    ${SRC_DIR}/scenes
    ${SRC_DIR}/utils
    ${SRC_DIR}/player
    ${SRC_DIR}/level
    ${SRC_DIR}/sprite
    ${SRC_DIR}/trail
    ${SRC_DIR}/render
    ${CMAKE_CURRENT_SOURCE_DIR}/../SDL/include
)

target_compile_definitions(main PRIVATE
    __ANDROID__
)

target_link_libraries(main PRIVATE
    SDL2
    GLESv2
    z
    log
    android
    OpenSLES
)
