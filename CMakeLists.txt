cmake_minimum_required(VERSION 3.16)
project(GeometryDash LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Optimization flags across compilers
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-O3 -flto)
    add_link_options(-flto -O3)
    if(NOT MINGW AND NOT EMSCRIPTEN AND NOT ANDROID)
        add_compile_options(-march=native -ffast-math -fomit-frame-pointer)
    endif()
endif()

# Source files
set(GAME_SOURCES
    src/audio/audio-manager.cpp
    src/scenes/boot-scene.cpp
    src/utils/color-manager.cpp
    src/utils/font-helpers.cpp
    src/scenes/game-scene.cpp
    src/scenes/game-scene-ui.cpp
    src/level/level-data-helpers.cpp
    src/level/level-renderer.cpp
    src/main.cpp
    src/utils/pako-compression.cpp
    src/player/player.cpp
    src/render/render-d3d8.cpp
    src/render/render-d3d9.cpp
    src/render/render-device.cpp
    src/render/render-gl1.cpp
    src/render/render-webgl.cpp
    src/sprite/sprite-layer-helper.cpp
    src/trail/trail-renderer.cpp
    src/utils/win-effects.cpp
)

# Include directories for all subfolders under src
set(GAME_INCLUDE_DIRS
    src
    src/assets
    src/vendor
    src/audio
    src/scenes
    src/utils
    src/player
    src/level
    src/sprite
    src/trail
    src/render
    src/game
    src/game/scenes
    src/game/level
    src/game/player
    src/game/sprite
    src/game/trail
    src/game/utils
)

# -----------------------------------------------------------------------------
# Web (Emscripten)
# -----------------------------------------------------------------------------
if(EMSCRIPTEN)
    add_executable(GeometryDash ${GAME_SOURCES})

    set(EMSCRIPTEN_LINK_FLAGS
        "-sUSE_SDL=2"
        "-sUSE_ZLIB=1"
        "-sMAX_WEBGL_VERSION=2"
        "-sMIN_WEBGL_VERSION=1"
        "-sALLOW_MEMORY_GROWTH=1"
        "-sINITIAL_MEMORY=67108864"
        "--preload-file ${CMAKE_CURRENT_SOURCE_DIR}/assets@assets"
    )

    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/shell_web.html")
        list(APPEND EMSCRIPTEN_LINK_FLAGS "--shell-file ${CMAKE_CURRENT_SOURCE_DIR}/shell_web.html")
    endif()

    string(JOIN " " EMSCRIPTEN_LINK_FLAGS_STR ${EMSCRIPTEN_LINK_FLAGS})
    target_include_directories(GeometryDash PRIVATE ${GAME_INCLUDE_DIRS})
    set_target_properties(GeometryDash PROPERTIES
        OUTPUT_NAME "index"
        SUFFIX ".html"
        LINK_FLAGS "${EMSCRIPTEN_LINK_FLAGS_STR}"
    )

# -----------------------------------------------------------------------------
# Windows (MSVC / MinGW / Cross-compilation)
# -----------------------------------------------------------------------------
elseif(WIN32)
    # Include resource icon if present
    set(EXTRA_SOURCES "")
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/resource.rc")
        set(EXTRA_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/resource.rc")
    endif()

    add_executable(GeometryDash WIN32 ${GAME_SOURCES} ${EXTRA_SOURCES})

    find_package(SDL2 REQUIRED)
    find_package(ZLIB REQUIRED)

    target_include_directories(GeometryDash PRIVATE ${SDL2_INCLUDE_DIRS} ${ZLIB_INCLUDE_DIRS} ${GAME_INCLUDE_DIRS})
    target_link_libraries(GeometryDash PRIVATE
        ${SDL2_LIBRARIES}
        ${ZLIB_LIBRARIES}
        opengl32
        dinput8
        dxguid
        user32
        gdi32
        winmm
        imm32
        ole32
        oleaut32
        shell32
        setupapi
        version
        uuid
    )

    if(MINGW)
        target_link_options(GeometryDash PRIVATE -static -static-libgcc -static-libstdc++ -Wl,--stack,8388608)
    endif()

# -----------------------------------------------------------------------------
# Linux / macOS Desktop
# -----------------------------------------------------------------------------
else()
    add_executable(GeometryDash ${GAME_SOURCES})

    find_package(SDL2 REQUIRED)
    find_package(OpenGL REQUIRED)
    find_package(ZLIB REQUIRED)
    find_package(Threads REQUIRED)

    target_include_directories(GeometryDash PRIVATE
        ${SDL2_INCLUDE_DIRS}
        ${OPENGL_INCLUDE_DIR}
        ${ZLIB_INCLUDE_DIRS}
        ${GAME_INCLUDE_DIRS}
    )

    target_link_libraries(GeometryDash PRIVATE
        ${SDL2_LIBRARIES}
        OpenGL::GL
        ${ZLIB_LIBRARIES}
        Threads::Threads
        ${CMAKE_DL_LIBS}
        m
    )
endif()

# -----------------------------------------------------------------------------
# Copy assets to build directory on Desktop
# -----------------------------------------------------------------------------
if(NOT EMSCRIPTEN AND NOT ANDROID)
    add_custom_command(TARGET GeometryDash POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/assets"
            "$<TARGET_FILE_DIR:GeometryDash>/assets"
        COMMENT "Copying assets directory to executable directory"
    )
endif()
