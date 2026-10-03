cmake_minimum_required(VERSION 3.6)

project(GDWebNative)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../../src)

set(GAME_SOURCES
    ${SRC_DIR}/audio-manager.cpp
    ${SRC_DIR}/boot-scene.cpp
    ${SRC_DIR}/color-manager.cpp
    ${SRC_DIR}/font-helpers.cpp
    ${SRC_DIR}/game-scene.cpp
    ${SRC_DIR}/level-data-helpers.cpp
    ${SRC_DIR}/level-renderer.cpp
    ${SRC_DIR}/main.cpp
    ${SRC_DIR}/pako-compression.cpp
    ${SRC_DIR}/player.cpp
    ${SRC_DIR}/render-device.cpp
    ${SRC_DIR}/render-webgl.cpp
    ${SRC_DIR}/sprite-layer-helper.cpp
    ${SRC_DIR}/trail-renderer.cpp
    ${SRC_DIR}/win-effects.cpp
)

add_library(main SHARED ${GAME_SOURCES})

target_include_directories(main PRIVATE
    ${SRC_DIR}
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
