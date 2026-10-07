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
    CXX="$TC/bin/i686-w64-mingw32-g++"
    [ ! -f "$CXX" ] && CXX="$TC/bin/i686-w64-mingw32-clang++"
    WINDRES="$TC/bin/i686-w64-mingw32-windres"
    if [ -z "$SDL_DIR" ]; then
        if [ -d "$TC/SDL2-2.30.12/i686-w64-mingw32" ]; then
            SDL_DIR="$TC/SDL2-2.30.12/i686-w64-mingw32"
        elif [ -d "$TC/i686-w64-mingw32" ]; then
            SDL_DIR="$TC/i686-w64-mingw32"
        fi
    fi
elif command -v i686-w64-mingw32-g++ &> /dev/null; then
    CXX="i686-w64-mingw32-g++"
    WINDRES="i686-w64-mingw32-windres"
    SDL_DIR="${SDL_DIR:-/usr/i686-w64-mingw32}"
elif command -v i686-w64-mingw32-clang++ &> /dev/null; then
    CXX="i686-w64-mingw32-clang++"
    WINDRES="i686-w64-mingw32-windres"
    SDL_DIR="${SDL_DIR:-/usr/i686-w64-mingw32}"
else
    echo "Error: MinGW 32-bit (i686) compiler not found."
    echo "Please install mingw-w64 on your system (e.g. 'sudo pacman -S mingw-w64-gcc' or 'sudo apt install g++-mingw-w64-i686')."
    exit 1
fi

[ -z "$SDL_DIR" ] && SDL_DIR="/usr/i686-w64-mingw32"

echo "==> Using compiler: $CXX"
mkdir -p build/obj32

# Compile Windows resources (icon)
if [ -f "resource.rc" ]; then
    echo "==> Compiling resource.rc..."
    $WINDRES resource.rc -O coff -o build/obj32/resource.o
fi

# Compile C++ sources
INCLUDES="-Isrc -Isrc/assets -Isrc/vendor -Isrc/audio -Isrc/scenes -Isrc/utils -Isrc/player -Isrc/level -Isrc/sprite -Isrc/trail -Isrc/render -I$SDL_DIR/include/SDL2 -I$SDL_DIR/include"
CXXFLAGS="-O3 -flto -std=c++17 -march=pentium4 -msse2 -mfpmath=sse -fno-math-errno -fomit-frame-pointer -DNDEBUG $INCLUDES"
for src in $(find src -name "*.cpp"); do
    obj="build/obj32/$(basename "$src" .cpp).o"
    echo "  [CXX] $src -> $obj"
    $CXX $CXXFLAGS -c "$src" -o "$obj"
done

# Link final executable (100% static, no SDL2.dll dependency)
echo "==> Linking build/GeometryDash.exe..."
$CXX -O3 -flto -std=c++17 -march=pentium4 -msse2 -mfpmath=sse -fno-math-errno -fomit-frame-pointer -DNDEBUG \
    build/obj32/*.o \
    -L$SDL_DIR/lib \
    -static -lmingw32 -lSDL2main -lSDL2 -lz -lopengl32 -ldinput8 -ldxguid -luser32 -lgdi32 -lwinmm -limm32 -lole32 -loleaut32 -lshell32 -lsetupapi -lversion -luuid \
    -mwindows -static-libgcc -static-libstdc++ \
    -Wl,--stack,8388608 \
    -o build/GeometryDash.exe

# Copy assets
mkdir -p build/assets
cp -rf assets/. build/assets/
if [ ! -f "build/settings.cfg" ]; then
    echo "render_api=directx8" > build/settings.cfg
fi

echo "==> Windows 32-bit build completed successfully!"
echo "    Executable: build/GeometryDash.exe"
