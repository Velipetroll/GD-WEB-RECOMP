#include "pako-compression.h"
#include <zlib.h>
#include <sstream>
#include <iostream>

static std::unordered_map<int, ObjectDefinition> options8;
static bool catalogInitialized = false;

std::string PakoCompression::inflate(const uint8_t* data, size_t size) {
    if (!data || size == 0) return "";

    z_stream strm;
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    strm.avail_in = size;
    strm.next_in = const_cast<Bytef*>(data);

    if (inflateInit2(&strm, 15 + 32) != Z_OK) return "";

    std::string out;
    char buffer[16384];
    int ret;

    do {
        strm.avail_out = sizeof(buffer);
        strm.next_out = reinterpret_cast<Bytef*>(buffer);
        ret = ::inflate(&strm, Z_NO_FLUSH);
        if (ret == Z_NEED_DICT || ret == Z_DATA_ERROR || ret == Z_MEM_ERROR) {
            inflateEnd(&strm);
            return "";
        }
        out.append(buffer, sizeof(buffer) - strm.avail_out);
    } while (strm.avail_out == 0);

    inflateEnd(&strm);
    return out;
}

std::vector<uint8_t> PakoCompression::base64Decode(const std::string& input) {
    const std::string chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string s = input;
    for (char& c : s) {
        if (c == '-') c = '+';
        else if (c == '_') c = '/';
    }
    while (s.length() % 4 != 0) s += '=';

    std::vector<uint8_t> out;
    int val = 0, valb = -8;
    for (unsigned char c : s) {
        if (c == '=') break;
        size_t p = chars.find(c);
        if (p == std::string::npos) continue;
        val = (val << 6) + p;
        valb += 6;
        if (valb >= 0) {
            out.push_back((val >> valb) & 0xFF);
            valb -= 8;
        }
    }
    return out;
}

std::string PakoCompression::extractLevelString(const std::string& gmd) {
    size_t start = gmd.find_first_not_of(" \t\r\n");
    if (start == std::string::npos || gmd.compare(start, 2, "<d") != 0) return gmd;

    size_t k = gmd.find("<k>k4</k>");
    if (k == std::string::npos) return gmd;
    size_t s1 = gmd.find("<s>", k);
    if (s1 == std::string::npos) return gmd;
    s1 += 3;
    size_t s2 = gmd.find("</s>", s1);
    if (s2 == std::string::npos) return gmd;
    return gmd.substr(s1, s2 - s1);
}

std::string PakoCompression::extractLevelName(const std::string& gmd) {
    size_t k = gmd.find("<k>k2</k>");
    if (k == std::string::npos) return "";
    size_t s1 = gmd.find("<s>", k);
    if (s1 == std::string::npos) return "";
    s1 += 3;
    size_t s2 = gmd.find("</s>", s1);
    if (s2 == std::string::npos) return "";
    return gmd.substr(s1, s2 - s1);
}

void PakoCompression::initCatalog() {
    if (catalogInitialized) return;

    const std::string solid2 = "solid", hazard2 = "hazard", $i = "deco",
    portal = "portal", pad = "pad", ring = "ring",
    trigger = "trigger", speed = "speed", fly = "fly", cube = "cube";

    options8[1]  = {solid2, "square_01_001.png", 1, 1};
    options8[2]  = {solid2, "square_02_001.png", 1, 1};
    options8[3]  = {solid2, "square_03_001.png", 1, 1};
    options8[4]  = {solid2, "square_04_001.png", 1, 1};
    options8[5]  = {$i,     "square_05_001.png", 1, 1};
    options8[6]  = {solid2, "square_06_001.png", 1, 1};
    options8[7]  = {solid2, "square_07_001.png", 1, 1};
    options8[83] = {solid2, "square_08_001.png", 1, 1};

    ObjectDefinition d40 = {solid2, "plank_01_001.png", 1, 0.5f};
    d40.children.push_back({"plank_01_color_001.png", 0, 0, "normal", 0, 0, false});
    options8[40] = d40;

    options8[8]   = {hazard2, "spike_01_001.png", 1, 1, 30, 30, 0.2f, 0.4f};
    options8[39]  = {hazard2, "spike_02_001.png", 1, 1, 30, 14, 0.2f, 0.4f};
    options8[103] = {hazard2, "spike_03_001.png", 0.5f, 0.5f, 20, 19, 0.2f, 0.4f};
    options8[392] = {hazard2, "spike_04_001.png", 0.5f, 0.5f, 13, 12, 0.2f, 0.4f};

    ObjectDefinition d9 = {hazard2, "pit_01_001.png", 0, 0, 30, 27, 0.3f, 0.4f};
    d9.black = true;
    d9.randomFrames = {"pit_01_001.png", "pit_02_001.png", "pit_03_001.png"};
    options8[9] = d9;

    ObjectDefinition d61 = {hazard2, "pit_04_001.png", 0, 0, 30, 18, 0.3f, 0.4f};
    d61.black = true;
    options8[61] = d61;

    ObjectDefinition d10 = {portal, "portal_01_front_001.png", 1, 3}; d10.sub = "gravity_flip"; options8[10] = d10;
    ObjectDefinition d11 = {portal, "portal_02_front_001.png", 1, 3}; d11.sub = "gravity_normal"; options8[11] = d11;
    ObjectDefinition d12 = {portal, "portal_03_front_001.png", 1, 3}; d12.sub = cube; d12.portalParticle = true; d12.portalParticleColor = 5111552; options8[12] = d12;
    ObjectDefinition d13 = {portal, "portal_04_front_001.png", 1, 3}; d13.sub = fly; d13.portalParticle = true; d13.portalParticleColor = 16711935; options8[13] = d13;
    ObjectDefinition d45 = {portal, "portal_05_front_001.png", 1, 3}; d45.sub = fly; options8[45] = d45;
    ObjectDefinition d46 = {portal, "portal_06_front_001.png", 1, 3}; d46.sub = cube; options8[46] = d46;
    ObjectDefinition d47 = {portal, "portal_07_front_001.png", 1, 3}; d47.sub = fly; options8[47] = d47;

    ObjectDefinition d99 = {portal, "portal_03_front_001.png", 1, 3}; d99.sub = "normal_size"; d99.portalParticle = true; d99.portalParticleColor = 5111552; options8[99] = d99;
    ObjectDefinition d101 = {portal, "portal_04_front_001.png", 1, 3}; d101.sub = "mini"; d101.portalParticle = true; d101.portalParticleColor = 16711935; options8[101] = d101;

    ObjectDefinition d200 = {speed, "portal_09_front_001.png", 1, 3}; d200.sub = "slow"; options8[200] = d200;
    ObjectDefinition d201 = {speed, "portal_10_front_001.png", 1, 3}; d201.sub = "normal"; options8[201] = d201;
    ObjectDefinition d202 = {speed, "portal_08_front_001.png", 1, 3}; d202.sub = "fast"; options8[202] = d202;
    ObjectDefinition d203 = {speed, "portal_11_front_001.png", 1, 3}; d203.sub = "very_fast"; options8[203] = d203;

    // Jump pads: using authentic bump textures from GJ_GameSheet
    // ID 35: Yellow Pad (Medium jump / standard)
    ObjectDefinition dPadYellow = {pad, "bump_01_001.png", 1, 1};
    dPadYellow.glow = true;
    dPadYellow.blend = "additive";
    dPadYellow.tint = 16776960; // Yellow (RGB: 255, 255, 0)
    options8[35] = dPadYellow;
    // glow enabled below

    // ID 140: Pink Pad (Low jump / salto bajo)
    ObjectDefinition dPadPink = {pad, "bump_03_001.png", 1, 1};
    dPadPink.glow = true;
    dPadPink.blend = "additive";
    dPadPink.tint = 16737996; // Pink (RGB: 255, 105, 204)
    options8[140] = dPadPink;
    // glow enabled below

    // ID 1332: Red Pad (High jump / salto muy alto)
    ObjectDefinition dPadRed = {pad, "bump_01_001.png", 1, 1};
    dPadRed.glow = true;
    dPadRed.blend = "additive";
    dPadRed.tint = 16711680; // Red (RGB: 255, 0, 0)
    options8[1332] = dPadRed;
    // glow enabled below

    // ID 67: Gravity Pad (Cyan / inverts gravity)
    ObjectDefinition dPadGravity = {pad, "gravbump_01_001.png", 1, 1};
    dPadGravity.glow = true;
    dPadGravity.blend = "additive";
    dPadGravity.tint = 65535; // Cyan (RGB: 0, 255, 255)
    options8[67] = dPadGravity;
    // glow enabled below

    // Jump rings / Orbes (reacting to music / audioScale)
    ObjectDefinition dRingYellow = {ring, "ring_01_001.png", 1, 1};
    dRingYellow.glow = true;
    dRingYellow.blend = "additive";
    dRingYellow.tint = 16776960; // Yellow (RGB: 255, 255, 0)
    options8[36]  = dRingYellow;
    // glow enabled below

    // ID 84: Gravity Jump Ring (Cyan)
    ObjectDefinition dRingGravity = {ring, "gravring_01_001.png", 1, 1};
    dRingGravity.glow = true;
    dRingGravity.blend = "additive";
    dRingGravity.tint = 65535; // Cyan (RGB: 0, 255, 255)
    options8[84]  = dRingGravity;
    // glow enabled below

    // ID 141: Pink Jump Ring
    ObjectDefinition dRingPink = {ring, "ring_03_001.png", 1, 1};
    dRingPink.glow = true;
    dRingPink.blend = "additive";
    dRingPink.tint = 16737996; // Pink (RGB: 255, 105, 204)
    options8[141] = dRingPink;
    // glow enabled below

    options8[62]  = {solid2, "square_b_01_001.png", 1, 1};
    options8[63]  = {solid2, "square_b_02_001.png", 1, 1};
    options8[64]  = {solid2, "square_b_03_001.png", 1, 1};
    options8[65]  = {solid2, "square_b_04_001.png", 1, 1};
    options8[66]  = {solid2, "square_b_05_001.png", 1, 1};
    options8[68]  = {solid2, "square_b_06_001.png", 1, 1};
    options8[195] = {solid2, "square_01_001.png", 0.5f, 0.5f};
    options8[196] = {solid2, "plank_01_001.png", 0.5f, 0.25f};

    options8[88] = {hazard2, "sawblade_01_001.png", 1, 1};
    options8[89] = {hazard2, "sawblade_02_001.png", 2, 2};
    options8[98] = {hazard2, "sawblade_03_001.png", 3, 3};

    options8[48]  = {$i, "d_cloud_01_001.png", 0, 0};
    options8[49]  = {$i, "d_cloud_02_001.png", 0, 0};
    options8[129] = {$i, "d_cloud_03_001.png", 0, 0};
    options8[130] = {$i, "d_cloud_04_001.png", 0, 0};
    options8[131] = {$i, "d_cloud_05_001.png", 0, 0};

    options8[50] = {$i, "d_ball_01_001.png", 0, 0};
    options8[51] = {$i, "d_ball_02_001.png", 0, 0};
    options8[52] = {$i, "d_ball_03_001.png", 0, 0};
    options8[53] = {$i, "d_ball_04_001.png", 0, 0};
    options8[54] = {$i, "d_ball_05_001.png", 0, 0};
    options8[55] = {$i, "d_ball_06_001.png", 0, 0};
    options8[56] = {$i, "d_ball_07_001.png", 0, 0};
    options8[57] = {$i, "d_ball_08_001.png", 0, 0};
    options8[58] = {$i, "d_ball_09_001.png", 0, 0};
    options8[60] = {$i, "d_ball_06_001.png", 0, 0};

    options8[125] = {$i, "d_smallBall_01_001.png", 0, 0};
    options8[126] = {$i, "d_smallBall_02_001.png", 0, 0};
    options8[127] = {$i, "d_smallBall_03_001.png", 0, 0};
    options8[128] = {$i, "d_smallBall_04_001.png", 0, 0};
    options8[145] = {$i, "d_smallBall_05_001.png", 0, 0};

    ObjectDefinition d41 = {$i, "chain_01_001.png", 0, 0};
    d41.blend = "additive"; d41.tint = colorGreenTint;
    options8[41] = d41;

    options8[123] = {$i, "d_thorn_01_001.png", 0, 0};
    options8[124] = {$i, "d_thorn_02_001.png", 0, 0};

    ObjectDefinition d15 = {$i, "rod_01_001.png", 0, 0};
    d15.z = -6;
    d15.children.push_back({"rod_ball_01_001.png", 0.0f, -62.0f, "additive", colorGreenTint, 1, true});
    options8[15] = d15;

    ObjectDefinition d16 = {$i, "rod_02_001.png", 0, 0};
    d16.z = -6;
    d16.children.push_back({"rod_ball_02_001.png", 0.0f, -46.5f, "additive", colorGreenTint, 1, true});
    options8[16] = d16;

    ObjectDefinition d17 = {$i, "rod_03_001.png", 0, 0};
    d17.z = -6;
    d17.children.push_back({"rod_ball_03_001.png", 0.0f, -32.5f, "additive", colorGreenTint, 1, true});
    options8[17] = d17;

    options8[132] = {$i, "d_arrow_01_001.png", 0, 0};
    options8[133] = {$i, "d_exmark_01_001.png", 0, 0};
    options8[136] = {$i, "d_qmark_01_001.png", 0, 0};

    auto makeSpikeArt = [&](const std::string& fr) {
        ObjectDefinition d = {$i, fr, 0, 0};
        d.blend = "additive"; d.tint = colorGreenTint;
        return d;
    };
    options8[151] = makeSpikeArt("d_spikeart_01_001.png");
    options8[152] = makeSpikeArt("d_spikeart_02_001.png");
    options8[153] = makeSpikeArt("d_spikeart_03_001.png");

    options8[18] = makeSpikeArt("d_spikes_01_001.png");
    options8[19] = makeSpikeArt("d_spikes_02_001.png");
    options8[20] = makeSpikeArt("d_spikes_03_001.png");
    options8[21] = makeSpikeArt("d_spikes_04_001.png");

    auto makeFakeSpike = [&](const std::string& fr) {
        ObjectDefinition d = {$i, fr, 0, 0};
        d.black = true;
        return d;
    };
    options8[135]  = makeFakeSpike("fakeSpike_01_001.png");
    options8[1889] = makeFakeSpike("fakeSpike_01_001.png");
    options8[1890] = makeFakeSpike("fakeSpike_02_001.png");
    options8[1891] = makeFakeSpike("fakeSpike_03_001.png");
    options8[1892] = makeFakeSpike("fakeSpike_04_001.png");

    options8[150] = {$i, "d_cross_01_001.png", 0, 0};
    options8[134] = {$i, "d_largeSquare_01_001.png", 0, 0};
    options8[146] = {$i, "d_largeSquare_02_001.png", 0, 0};
    options8[138] = {$i, "d_art_01_001.png", 0, 0};
    options8[137] = {$i, "brick_02_001.png", 0, 0};
    options8[139] = {$i, "d_brick_01_001.png", 0, 0};
    options8[157] = {$i, "d_wave_01_001.png", 0, 0};
    options8[158] = {$i, "d_wave_02_001.png", 0, 0};
    options8[159] = {$i, "d_wave_03_001.png", 0, 0};
    options8[143] = {$i, "d_circle_01_001.png", 0, 0};
    options8[144] = {$i, "d_circle_02_001.png", 0, 0};

    options8[142]  = {$i, "secretCoin_01_001.png", 1, 1};
    options8[1329] = {$i, "secretCoin_2_01_001.png", 1, 1};

    for (int tId = 22; tId <= 28; ++tId) {
        ObjectDefinition dt = {trigger, "", 0, 0};
        dt.enterEffect = tId - 22;
        options8[tId] = dt;
    }

    ObjectDefinition d29 = {trigger, "", 0, 0}; d29.colorIdx = 1000; options8[29] = d29;
    ObjectDefinition d30 = {trigger, "", 0, 0}; d30.colorIdx = 1001; options8[30] = d30;

    options8[104]  = {trigger, "", 0, 0};
    options8[105]  = {trigger, "", 0, 0};
    options8[221]  = {trigger, "", 0, 0};
    options8[899]  = {trigger, "", 0, 0};
    options8[901]  = {trigger, "", 0, 0};
    options8[1006] = {trigger, "", 0, 0};
    options8[44]   = {$i, "", 0, 0};

    const std::vector<int> table3 = {
        1, 2, 3, 4, 6, 7, 83, 8, 39, 103, 392, 35, 36, 40, 140, 141, 62, 65, 66, 68, 195, 196
    };
    for (int it : table3) {
        if (options8.find(it) != options8.end()) {
            options8[it].glow = true;
        }
    }

    catalogInitialized = true;
}

const ObjectDefinition* PakoCompression::helperFn18(int id) {
    if (!catalogInitialized) initCatalog();
    auto it = options8.find(id);
    return (it != options8.end()) ? &it->second : nullptr;
}

LevelObjectRaw PakoCompression::helperFn16(const std::string& objectStr) {
    LevelObjectRaw obj;
    std::stringstream ss(objectStr);
    std::string token;
    std::vector<std::string> parts;

    while (std::getline(ss, token, ',')) parts.push_back(token);

    for (size_t i = 0; i + 1 < parts.size(); i += 2) {
        int k = std::stoi(parts[i]);
        obj.rawMap[k] = parts[i + 1];
    }

    if (obj.rawMap.find(1) == obj.rawMap.end()) return obj;
    obj.id = std::stoi(obj.rawMap[1]);
    if (obj.id == 0) return obj;

    if (obj.rawMap.find(2) != obj.rawMap.end())  obj.x = std::stof(obj.rawMap[2]);
    if (obj.rawMap.find(3) != obj.rawMap.end())  obj.y = std::stof(obj.rawMap[3]);
    if (obj.rawMap.find(4) != obj.rawMap.end())  obj.flipX = (obj.rawMap[4] == "1");
    if (obj.rawMap.find(5) != obj.rawMap.end())  obj.flipY = (obj.rawMap[5] == "1");
    if (obj.rawMap.find(6) != obj.rawMap.end())  obj.rot = std::stof(obj.rawMap[6]);
    if (obj.rawMap.find(32) != obj.rawMap.end()) obj.scale = std::stof(obj.rawMap[32]);
    if (obj.rawMap.find(24) != obj.rawMap.end()) obj.zLayer = std::stoi(obj.rawMap[24]);
    if (obj.rawMap.find(25) != obj.rawMap.end()) obj.zOrder = std::stoi(obj.rawMap[25]);
    if (obj.rawMap.find(57) != obj.rawMap.end()) obj.groups = obj.rawMap[57];
    if (obj.rawMap.find(21) != obj.rawMap.end()) obj.color1 = std::stoi(obj.rawMap[21]);
    if (obj.rawMap.find(22) != obj.rawMap.end()) obj.color2 = std::stoi(obj.rawMap[22]);

    return obj;
}

ParsedLevel PakoCompression::helperFn17(const std::string& rawLevelString) {
    std::string uncompressed;

    if (rawLevelString.find(',') != std::string::npos && rawLevelString.find(';') != std::string::npos) {
        uncompressed = rawLevelString;
    } else {
        std::vector<uint8_t> decoded = base64Decode(rawLevelString);
        uncompressed = inflate(decoded.data(), decoded.size());
    }

    ParsedLevel level;
    std::stringstream ss(uncompressed);
    std::string line;

    if (std::getline(ss, line, ';')) {
        level.settings = line;

        // Header is key,value pairs; kA13 = song offset in seconds, kA4 = start speed
        std::stringstream hs(line);
        std::string key, val;
        while (std::getline(hs, key, ',') && std::getline(hs, val, ',')) {
            if (key == "kA13") {
                try { level.songOffset = std::stof(val); } catch (...) {}
            } else if (key == "kA4") {
                try { level.startSpeed = std::stoi(val); } catch (...) {}
            } else if (key == "kA11") {
                level.startMini = (val == "1");
            }
        }
    }

    while (std::getline(ss, line, ';')) {
        if (line.empty()) continue;
        LevelObjectRaw rawObj = helperFn16(line);
        if (rawObj.id != 0) {
            level.objects.push_back(rawObj);
        }
    }

    std::cout << "[pako-compression] Level parsed: " << level.objects.size() << " objects loaded." << std::endl;
    return level;
}
