#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export ANDROID_HOME="${ANDROID_HOME:-/home/varzonelp/Android/Sdk}"

# Opciones por defecto
MIN_SDK=21
ABI_FILTERS="arm64-v8a,armeabi-v7a,x86_64"
NDK_VERSION="26.3.11579264"
LEGACY_MODE=false

# Procesar argumentos / flags
for arg in "$@"; do
    case "$arg" in
        --legacy|--android4|--jellybean)
            LEGACY_MODE=true
            MIN_SDK=16
            ABI_FILTERS="armeabi-v7a"
            # NDK 25 es compatible con llamadas de abort en API < 21
            # (NDK 26 requiere android_set_abort_message introducido en API 21)
            NDK_VERSION="25.1.8937393"
            ;;
        --min-sdk=*)
            MIN_SDK="${arg#*=}"
            ;;
        --abis=*)
            ABI_FILTERS="${arg#*=}"
            ;;
        --ndk=*)
            NDK_VERSION="${arg#*=}"
            ;;
        --help|-h)
            echo "Uso: $0 [opciones]"
            echo "Opciones:"
            echo "  --legacy, --android4   Compila para dispositivos antiguos (Android 4.1 Jelly Bean / API 16, NDK 25, armeabi-v7a)"
            echo "  --min-sdk=<version>    Especifica manualmente la versión mínima de Android (ej: 16, 21)"
            echo "  --abis=<abis>          Especifica arquitecturas separadas por comas (ej: armeabi-v7a)"
            echo "  --ndk=<version>        Especifica la versión del NDK (ej: 25.1.8937393)"
            echo "  --help, -h             Muestra esta ayuda"
            exit 0
            ;;
        *)
            echo "Argumento desconocido: $arg (usa --help para ver opciones)"
            exit 1
            ;;
    esac
done

export ANDROID_NDK_HOME="$ANDROID_HOME/ndk/$NDK_VERSION"

echo "============================================="
echo "   Building Geometry Dash for Android"
echo "============================================="
echo "SDK: $ANDROID_HOME"
echo "NDK: $ANDROID_NDK_HOME ($NDK_VERSION)"
echo "Min SDK: $MIN_SDK $([ "$LEGACY_MODE" = true ] && echo "(Modo Legacy / Android 4.1 activado)" || echo "")"
echo "ABIs: $ABI_FILTERS"

if [ ! -d "$SCRIPT_DIR/third_party/SDL2-2.30.8" ]; then
    echo "Downloading SDL2 source (2.30.8)..."
    mkdir -p "$SCRIPT_DIR/third_party"
    curl -sL https://github.com/libsdl-org/SDL/releases/download/release-2.30.8/SDL2-2.30.8.tar.gz | tar -xz -C "$SCRIPT_DIR/third_party"
fi

if [ ! -e "$SCRIPT_DIR/android/app/jni/SDL" ]; then
    ln -s ../../../third_party/SDL2-2.30.8 "$SCRIPT_DIR/android/app/jni/SDL"
fi

cd "$SCRIPT_DIR/android"
./gradlew assembleDebug -PminSdkVersion="$MIN_SDK" -PabiFilters="$ABI_FILTERS" -PndkVersion="$NDK_VERSION"

APK_PATH="$SCRIPT_DIR/android/app/build/outputs/apk/debug/app-debug.apk"
if [ -f "$APK_PATH" ]; then
    echo "============================================="
    echo " Build SUCCESSFUL!"
    echo " APK generated at:"
    echo " $APK_PATH"
    echo " Size: $(du -h "$APK_PATH" | cut -f1)"
    echo " Target: minSdk $MIN_SDK ($ABI_FILTERS) [NDK $NDK_VERSION]"
    echo "============================================="
    echo "To install on connected device:"
    echo "  adb install -r \"$APK_PATH\""
    echo "============================================="
fi
