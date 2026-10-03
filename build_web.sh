#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# Locate emsdk / emcmake
POSSIBLE_EMSDK=(
    "$EMSDK"
    "$HOME/.local/share/emsdk"
    "$HOME/.local/emsdk"
    "$HOME/emsdk"
    "/opt/emsdk"
)

EMCMAKE=""
for dir in "${POSSIBLE_EMSDK[@]}"; do
    if [ -n "$dir" ] && [ -f "$dir/upstream/emscripten/emcmake" ]; then
        EMCMAKE="$dir/upstream/emscripten/emcmake"
        export EMSDK="$dir"
        export PATH="$dir/upstream/emscripten:$dir/node/current/bin:$PATH"
        break
    fi
done

if [ -z "$EMCMAKE" ] && command -v emcmake &> /dev/null; then
    EMCMAKE="$(command -v emcmake)"
fi

if [ -z "$EMCMAKE" ]; then
    echo "Error: Emscripten (emcmake) not found."
    echo "Please install or activate emsdk."
    exit 1
fi

echo "==> Configuring and compiling Web target with CMake..."
"$EMCMAKE" cmake -B build/web -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build/web -j$(nproc)

echo "==> Web build completed successfully!"
echo "    Output files in: build/web/"
ls -lh build/web/index.*
