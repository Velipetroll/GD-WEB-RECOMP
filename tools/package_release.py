#!/usr/bin/env python3
import os
import shutil
import zipfile
import tarfile
import subprocess

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_DIR = os.path.dirname(SCRIPT_DIR)
DIST_DIR = os.path.join(PROJECT_DIR, "dist")
BUILD_DIR = os.path.join(PROJECT_DIR, "build")
ASSETS_DIR = os.path.join(PROJECT_DIR, "assets")

# Check for strip tools
LLVM_STRIP_CANDIDATES = [
    os.path.expanduser("~/.local/share/toolchain/bin/llvm-strip"),
    os.path.expanduser("~/.local/toolchain/bin/llvm-strip"),
    "/opt/llvm-mingw/bin/llvm-strip",
    "llvm-strip",
    "i686-w64-mingw32-strip",
    "x86_64-w64-mingw32-strip"
]

llvm_strip = None
for cand in LLVM_STRIP_CANDIDATES:
    if shutil.which(cand) or os.path.isfile(cand):
        llvm_strip = cand
        break

def strip_binary(bin_path, is_windows=False):
    if is_windows and llvm_strip:
        subprocess.run([llvm_strip, "-s", bin_path], check=False)
    else:
        if shutil.which("strip"):
            subprocess.run(["strip", "-s", bin_path], check=False)

def copy_game_assets(dst_assets):
    os.makedirs(dst_assets, exist_ok=True)
    for root, dirs, files in os.walk(ASSETS_DIR):
        rel_root = os.path.relpath(root, ASSETS_DIR)
        target_dir = dst_assets if rel_root == "." else os.path.join(dst_assets, rel_root)
        os.makedirs(target_dir, exist_ok=True)
        for f in files:
            if f in ("index-game.js", "banner.png") or "vendor" in root:
                continue
            shutil.copy2(os.path.join(root, f), os.path.join(target_dir, f))

import sys

def main():
    version = sys.argv[1] if len(sys.argv) > 1 else "v1.0.1"
    os.makedirs(DIST_DIR, exist_ok=True)
    print(f"Packaging release binaries ({version}) into {DIST_DIR}...")

    # 1. Windows 32-bit
    win32_bin = os.path.join(BUILD_DIR, "GeometryDash.exe")
    if os.path.isfile(win32_bin):
        stage = os.path.join(DIST_DIR, "stage_win32")
        if os.path.exists(stage): shutil.rmtree(stage)
        os.makedirs(stage)
        shutil.copy2(win32_bin, os.path.join(stage, "GeometryDash.exe"))
        strip_binary(os.path.join(stage, "GeometryDash.exe"), is_windows=True)
        copy_game_assets(os.path.join(stage, "assets"))
        with open(os.path.join(stage, "README.txt"), "w") as f:
            f.write(f"Geometry Dash Web Native Port - {version} (Windows 32-bit)\n"
                    "===================================================\n\n"
                    "Controls:\n- Space / Up Arrow / Left Mouse Button: Jump / Fly\n"
                    "- Escape: Pause menu / Back\n\n"
                    "Features:\n- Direct3D 8 (Intel GMA FastPath), Direct3D 9, OpenGL 1.1\n"
                    "- 240Hz physics simulation, zero external runtime DLL dependencies\n"
                    "- In-game settings popup: FPS Limiter, Renderer selection, Audio controls, Show FPS toggle\n"
                    "- Compatible with Windows XP, Vista, 7, 8, 10, 11 (32-bit & 64-bit)\n")
        out_zip = os.path.join(DIST_DIR, f"GeometryDash-{version}-win32.zip")
        with zipfile.ZipFile(out_zip, "w", zipfile.ZIP_DEFLATED) as zf:
            for root, dirs, files in os.walk(stage):
                for f in files:
                    full = os.path.join(root, f)
                    zf.write(full, os.path.relpath(full, stage))
        shutil.rmtree(stage)
        print(f"✓ Windows 32-bit: {out_zip} ({os.path.getsize(out_zip):,} bytes)")

    # 2. Windows 64-bit
    win64_bin = os.path.join(BUILD_DIR, "GeometryDash_x64.exe")
    if os.path.isfile(win64_bin):
        stage = os.path.join(DIST_DIR, "stage_win64")
        if os.path.exists(stage): shutil.rmtree(stage)
        os.makedirs(stage)
        shutil.copy2(win64_bin, os.path.join(stage, "GeometryDash.exe"))
        strip_binary(os.path.join(stage, "GeometryDash.exe"), is_windows=True)
        copy_game_assets(os.path.join(stage, "assets"))
        with open(os.path.join(stage, "README.txt"), "w") as f:
            f.write(f"Geometry Dash Web Native Port - {version} (Windows 64-bit)\n"
                    "===================================================\n\n"
                    "Controls:\n- Space / Up Arrow / Left Mouse Button: Jump / Fly\n"
                    "- Escape: Pause menu / Back\n\n"
                    "Features:\n- Direct3D 9, OpenGL 1.1\n"
                    "- 240Hz physics simulation, zero external runtime DLL dependencies\n"
                    "- In-game settings popup: FPS Limiter, Renderer selection, Audio controls, Show FPS toggle\n"
                    "- Compatible with Windows 7, 8, 10, 11 (64-bit)\n")
        out_zip = os.path.join(DIST_DIR, f"GeometryDash-{version}-win64.zip")
        with zipfile.ZipFile(out_zip, "w", zipfile.ZIP_DEFLATED) as zf:
            for root, dirs, files in os.walk(stage):
                for f in files:
                    full = os.path.join(root, f)
                    zf.write(full, os.path.relpath(full, stage))
        shutil.rmtree(stage)
        print(f"✓ Windows 64-bit: {out_zip} ({os.path.getsize(out_zip):,} bytes)")

    # 3. Linux 64-bit
    linux_bin = os.path.join(BUILD_DIR, "GeometryDash")
    if os.path.isfile(linux_bin):
        stage = os.path.join(DIST_DIR, "stage_linux")
        if os.path.exists(stage): shutil.rmtree(stage)
        os.makedirs(stage)
        shutil.copy2(linux_bin, os.path.join(stage, "GeometryDash"))
        strip_binary(os.path.join(stage, "GeometryDash"), is_windows=False)
        os.chmod(os.path.join(stage, "GeometryDash"), 0o755)
        copy_game_assets(os.path.join(stage, "assets"))
        with open(os.path.join(stage, "README.txt"), "w") as f:
            f.write(f"Geometry Dash Web Native Port - {version} (Linux x86_64)\n"
                    "===================================================\n\n"
                    "Run:\n  ./GeometryDash\n\n"
                    "Controls:\n- Space / Up Arrow / Left Mouse Button: Jump / Fly\n"
                    "- Escape: Pause menu / Back\n\n"
                    "Requirements:\n- SDL2 (libsdl2-2.0), OpenGL (libGL), zlib\n")
        out_tar = os.path.join(DIST_DIR, f"GeometryDash-{version}-linux-x86_64.tar.gz")
        with tarfile.open(out_tar, "w:gz") as tf:
            for root, dirs, files in os.walk(stage):
                for f in files:
                    full = os.path.join(root, f)
                    tf.add(full, arcname=os.path.relpath(full, stage))
        shutil.rmtree(stage)
        print(f"✓ Linux x86_64: {out_tar} ({os.path.getsize(out_tar):,} bytes)")

    # 4. WebAssembly & WebGL
    web_dir = os.path.join(BUILD_DIR, "web")
    if os.path.isfile(os.path.join(web_dir, "index.html")):
        out_zip = os.path.join(DIST_DIR, f"GeometryDash-{version}-web.zip")
        with zipfile.ZipFile(out_zip, "w", zipfile.ZIP_DEFLATED) as zf:
            for root, dirs, files in os.walk(web_dir):
                for f in files:
                    full = os.path.join(root, f)
                    zf.write(full, os.path.relpath(full, web_dir))
        print(f"✓ WebAssembly / WebGL: {out_zip} ({os.path.getsize(out_zip):,} bytes)")

    print(f"Release packaging for {version} completed successfully!")

if __name__ == "__main__":
    main()
