#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# Search for MinGW toolchain in standard directories
POSSIBLE_TOOLCHAINS=(
    "$TOOLCHAIN_DIR"
    "$HOME/.local/share/toolchain"
    "$HOME/.local/toolchain"
    "$HOME/toolchain"
    "/opt/llvm-mingw"
    "/opt/toolchain"
)

TC=""
for dir in "${POSSIBLE_TOOLCHAINS[@]}"; do
    if [ -n "$dir" ] && [ -d "$dir/bin" ]; then
        TC="$dir"
        break
    fi
done

if [ -n "$TC" ]; then
    CXX="$TC/bin/x86_64-w64-mingw32-g++"
    [ ! -f "$CXX" ] && CXX="$TC/bin/x86_64-w64-mingw32-clang++"
    WINDRES="$TC/bin/x86_64-w64-mingw32-windres"
    if [ -z "$SDL_DIR" ]; then
        if [ -d "$TC/SDL2-2.30.12/x86_64-w64-mingw32" ]; then
            SDL_DIR="$TC/SDL2-2.30.12/x86_64-w64-mingw32"
        elif [ -d "$TC/x86_64-w64-mingw32" ]; then
            SDL_DIR="$TC/x86_64-w64-mingw32"
        fi
    fi
elif command -v x86_64-w64-mingw32-g++ &> /dev/null; then
    CXX="x86_64-w64-mingw32-g++"
    WINDRES="x86_64-w64-mingw32-windres"
    SDL_DIR="${SDL_DIR:-/usr/x86_64-w64-mingw32}"
elif command -v x86_64-w64-mingw32-clang++ &> /dev/null; then
    CXX="x86_64-w64-mingw32-clang++"
    WINDRES="x86_64-w64-mingw32-windres"
    SDL_DIR="${SDL_DIR:-/usr/x86_64-w64-mingw32}"
else
    echo "Error: MinGW 64-bit (x86_64) compiler not found."
    echo "Please install mingw-w64 on your system (e.g. 'sudo pacman -S mingw-w64-gcc' or 'sudo apt install g++-mingw-w64-x86-64')."
    exit 1
fi

[ -z "$SDL_DIR" ] && SDL_DIR="/usr/x86_64-w64-mingw32"

echo "==> Using 64-bit compiler: $CXX"
mkdir -p build/obj64

# Compile Windows resources (icon)
if [ -f "resource.rc" ]; then
    echo "==> Compiling resource.rc for x64..."
    $WINDRES resource.rc -O coff -o build/obj64/resource.o
fi

# Compile C++ sources
INCLUDES="-Isrc -Isrc/assets -Isrc/vendor -Isrc/audio -Isrc/scenes -Isrc/utils -Isrc/player -Isrc/level -Isrc/sprite -Isrc/trail -Isrc/render -I$SDL_DIR/include/SDL2 -I$SDL_DIR/include"
CXXFLAGS="-O3 -flto -std=c++17 -march=x86-64 -msse2 -fno-math-errno -fomit-frame-pointer -DNDEBUG $INCLUDES"
for src in $(find src -name "*.cpp"); do
    obj="build/obj64/$(basename "$src" .cpp).o"
    echo "  [CXX x64] $src -> $obj"
    $CXX $CXXFLAGS -c "$src" -o "$obj"
done

# Link final executable (100% static, no SDL2.dll dependencies)
echo "==> Linking build/GeometryDash_x64.exe..."
$CXX -O3 -flto -std=c++17 -march=x86-64 -msse2 -fno-math-errno -fomit-frame-pointer -DNDEBUG \
    build/obj64/*.o \
    -L$SDL_DIR/lib \
    -static -lmingw32 -lSDL2main -lSDL2 -lz -lopengl32 -ldinput8 -ldxguid -luser32 -lgdi32 -lwinmm -limm32 -lole32 -loleaut32 -lshell32 -lsetupapi -lversion -luuid \
    -mwindows -static-libgcc -static-libstdc++ \
    -Wl,--stack,8388608 \
    -o build/GeometryDash_x64.exe

# Copy assets
mkdir -p build/assets
cp -rf assets/. build/assets/
if [ ! -f "build/settings.cfg" ]; then
    echo "render_api=directx9" > build/settings.cfg
fi

echo "==> Windows 64-bit build completed successfully!"
echo "    Executable: build/GeometryDash_x64.exe"
