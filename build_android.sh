#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export ANDROID_HOME="${ANDROID_HOME:-/home/varzonelp/Android/Sdk}"
export ANDROID_NDK_HOME="${ANDROID_NDK_HOME:-$ANDROID_HOME/ndk/26.3.11579264}"

echo "============================================="
echo "   Building Geometry Dash for Android"
echo "============================================="
echo "SDK: $ANDROID_HOME"
echo "NDK: $ANDROID_NDK_HOME"

if [ ! -d "$SCRIPT_DIR/third_party/SDL2-2.30.8" ]; then
    echo "Downloading SDL2 source (2.30.8)..."
    mkdir -p "$SCRIPT_DIR/third_party"
    curl -sL https://github.com/libsdl-org/SDL/releases/download/release-2.30.8/SDL2-2.30.8.tar.gz | tar -xz -C "$SCRIPT_DIR/third_party"
fi

if [ ! -e "$SCRIPT_DIR/android/app/jni/SDL" ]; then
    ln -s ../../../third_party/SDL2-2.30.8 "$SCRIPT_DIR/android/app/jni/SDL"
fi

cd "$SCRIPT_DIR/android"
./gradlew assembleDebug

APK_PATH="$SCRIPT_DIR/android/app/build/outputs/apk/debug/app-debug.apk"
if [ -f "$APK_PATH" ]; then
    echo "============================================="
    echo " Build SUCCESSFUL!"
    echo " APK generated at:"
    echo " $APK_PATH"
    echo " Size: $(du -h "$APK_PATH" | cut -f1)"
    echo "============================================="
    echo "To install on connected device:"
    echo "  adb install -r \"$APK_PATH\""
    echo "============================================="
fi
