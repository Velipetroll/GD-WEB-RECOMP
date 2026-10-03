#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# Locate em++ / emsdk
POSSIBLE_EMSDK=(
    "$EMSDK"
    "$HOME/.local/share/emsdk"
    "$HOME/.local/emsdk"
    "$HOME/emsdk"
    "/opt/emsdk"
)

EMXX=""
for dir in "${POSSIBLE_EMSDK[@]}"; do
    if [ -n "$dir" ] && [ -f "$dir/upstream/emscripten/em++" ]; then
        EMXX="$dir/upstream/emscripten/em++"
        export EMSDK="$dir"
        export PATH="$dir/upstream/emscripten:$dir/node/current/bin:$PATH"
        break
    fi
done

if [ -z "$EMXX" ] && command -v em++ &> /dev/null; then
    EMXX="$(command -v em++)"
fi

if [ -z "$EMXX" ]; then
    echo "Error: Emscripten (em++) compiler not found."
    echo "Please install or activate emsdk (e.g. 'git clone https://github.com/emscripten-core/emsdk.git ~/.local/share/emsdk && cd ~/.local/share/emsdk && ./emsdk install latest && ./emsdk activate latest')."
    exit 1
fi

echo "==> Using Emscripten compiler: $EMXX"
mkdir -p build/web

SRCS=(
    src/audio-manager.cpp
    src/boot-scene.cpp
    src/color-manager.cpp
    src/font-helpers.cpp
    src/game-scene.cpp
    src/level-data-helpers.cpp
    src/level-renderer.cpp
    src/main.cpp
    src/pako-compression.cpp
    src/player.cpp
    src/render-d3d8.cpp
    src/render-d3d9.cpp
    src/render-device.cpp
    src/render-gl1.cpp
    src/render-webgl.cpp
    src/sprite-layer-helper.cpp
    src/trail-renderer.cpp
    src/win-effects.cpp
)

# Template shell if available
SHELL_ARG=""
if [ -f "shell_web.html" ]; then
    SHELL_ARG="--shell-file shell_web.html"
fi

echo "==> Compiling WebAssembly & WebGL build..."
"$EMXX" -std=c++17 -O3 -flto \
    -sUSE_SDL=2 \
    -sUSE_ZLIB=1 \
    -sMAX_WEBGL_VERSION=2 \
    -sMIN_WEBGL_VERSION=1 \
    -sALLOW_MEMORY_GROWTH=1 \
    -sINITIAL_MEMORY=67108864 \
    --preload-file assets \
    $SHELL_ARG \
    "${SRCS[@]}" \
    -o build/web/index.html

echo "==> Web build completed successfully!"
echo "    Output files in: build/web/"
ls -lh build/web/
