"""
Geometry Dash Spritesheet Patcher & Texture Uncompressor

Extracts original high-fidelity textures from Geometry Dash (version 2.2 or earlier)
via Cocos2d .plist files or loose HD/UHD sprites, downsamples them with Lanczos filtering,
and repacks them into their exact coordinates in the web atlas (GJ_WebSheet.png / GJ_WebSheet.json).

This eliminates the blurry over-compression found in the web release assets while
preserving exact sprite layout and bounding boxes.
"""

import os
import json
import plistlib
import re
import shutil
import glob
from PIL import Image

def parse_rect(rect_str):
    """Extract numeric coordinates from string formatted as '{{x,y},{w,h}}'."""
    return [int(x) for x in re.findall(r'-?\d+', rect_str)]

def main():
    out_dir = "Generated_Files"
    extract_dir = os.path.join(out_dir, "Extracted_Textures")
    os.makedirs(extract_dir, exist_ok=True)

    json_file = 'GJ_WebSheet.json'
    if not os.path.exists(json_file):
        print(f"Error: {json_file} not found.")
        return

    with open(json_file, 'r') as f:
        json_data = json.load(f)

    tex_data = json_data['textures'][0]
    sheet_name = tex_data['image']

    if not os.path.exists(sheet_name):
        print(f"Error: {sheet_name} not found.")
        return

    original_sheet = Image.open(sheet_name).convert("RGBA")
    json_frames = {fi['filename']: fi for fi in tex_data['frames']}

    # Load packed textures from Cocos2d .plist files
    plist_db = {}
    for plist_file in glob.glob("*.plist") + glob.glob("*.plist.txt"):
        png_name = plist_file.replace(".plist.txt", ".png").replace(".plist", ".png")
        if os.path.exists(png_name):
            print(f"-> Loading atlas from {plist_file}...")
            img = Image.open(png_name).convert("RGBA")
            with open(plist_file, 'rb') as f:
                p_data = plistlib.load(f)
            for name, info in p_data.get('frames', {}).items():
                plist_db[name] = (info, img)

    # Prioritize loose PNG replacement textures
    loose_db = {}
    for png_file in glob.glob("*.png"):
        lname = png_file.lower()
        if "sheet" in lname:
            continue

        base_name = png_file.replace('-uhd', '').replace('-UHD', '')\
                            .replace('-hd', '').replace('-HD', '')

        if base_name not in loose_db:
            loose_db[base_name] = png_file
        else:
            curr = loose_db[base_name].lower()
            if '-uhd' in lname:
                loose_db[base_name] = png_file
            elif '-hd' in lname and '-uhd' not in curr:
                loose_db[base_name] = png_file

    if loose_db:
        print(f"-> Detected {len(loose_db)} loose PNG textures.")

    try:
        resample_filter = Image.Resampling.LANCZOS
    except AttributeError:
        resample_filter = Image.LANCZOS

    forced_replacements = {
        "player_01_001.png": "player_04_001.png",
        "player_01_2_001.png": "player_04_2_001.png"
    }

    # Extract and adapt textures to target dimensions
    for name, j_info in json_frames.items():
        out_path = os.path.join(extract_dir, name)
        jx, jy = j_info['frame']['x'], j_info['frame']['y']
        jw, jh = j_info['frame']['w'], j_info['frame']['h']
        j_rot = j_info['rotated']
        target_name = forced_replacements.get(name, name)

        if (target_name in loose_db or target_name in plist_db) and not os.path.exists(out_path):
            if target_name in loose_db:
                full_p = Image.open(loose_db[target_name]).convert("RGBA")
            else:
                p_info, p_img = plist_db[target_name]
                if 'frame' not in p_info:
                    continue

                px, py, pw, ph = parse_rect(p_info['frame'])
                p_rot = p_info.get('rotated', False)
                psx, psy, psw, psh = parse_rect(p_info.get('sourceColorRect', f"{{0,0}},{{{pw},{ph}}}"))
                pow_w, poh_h = parse_rect(p_info.get('sourceSize', f"{{{pw},{ph}}}"))

                if p_rot:
                    crop_p = p_img.crop((px, py, px + ph, py + pw)).transpose(Image.Transpose.ROTATE_90)
                else:
                    crop_p = p_img.crop((px, py, px + pw, py + ph))

                full_p = Image.new("RGBA", (pow_w, poh_h), (0, 0, 0, 0))
                full_p.paste(crop_p, (psx, psy))

            jow_w, joh_h = j_info['sourceSize']['w'], j_info['sourceSize']['h']
            if full_p.size != (jow_w, joh_h):
                full_p = full_p.resize((jow_w, joh_h), resample_filter)

            jsx, jsy = j_info['spriteSourceSize']['x'], j_info['spriteSourceSize']['y']
            jsw, jsh = j_info['spriteSourceSize']['w'], j_info['spriteSourceSize']['h']
            final_extr = full_p.crop((jsx, jsy, jsx + jsw, jsy + jsh))
            final_extr.save(out_path)

        elif not os.path.exists(out_path):
            crop_j = original_sheet.crop((jx, jy, jx + jw, jy + jh))
            if j_rot:
                crop_j = crop_j.transpose(Image.Transpose.ROTATE_90)
            crop_j.save(out_path)

    # Reconstruct spritesheet with injected textures
    print(f"\n-> Building {sheet_name}...")
    new_sheet = original_sheet.copy()

    for name, j_info in json_frames.items():
        target_name = forced_replacements.get(name, name)
        if target_name in plist_db or target_name in loose_db:
            sprite_path = os.path.join(extract_dir, name)
            if os.path.exists(sprite_path):
                img = Image.open(sprite_path).convert("RGBA")
                jx, jy = j_info['frame']['x'], j_info['frame']['y']
                jw, jh = j_info['frame']['w'], j_info['frame']['h']
                j_rot = j_info['rotated']

                upright_w = jh if j_rot else jw
                upright_h = jw if j_rot else jh

                if img.size != (upright_w, upright_h):
                    img = img.resize((upright_w, upright_h), resample_filter)

                if j_rot:
                    img = img.transpose(Image.Transpose.ROTATE_270)

                new_sheet.paste(img, (jx, jy))

    out_png = os.path.join(out_dir, sheet_name)
    new_sheet.save(out_png)

    out_json = os.path.join(out_dir, json_file)
    shutil.copy(json_file, out_json)

    print(f"-> Done! Output saved to: {out_dir}/")

if __name__ == "__main__":
    main()
