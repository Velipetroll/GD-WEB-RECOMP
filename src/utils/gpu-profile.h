#pragma once
// GPU hardware profile + graphics quality presets.
// Header-only so every build script (make / build_win*.sh / build_web.sh) picks it up without changes.
//
// The detection is aimed at the Intel GMA family, the main low-end target of this port:
//   tier 0 (LOW)    : GMA 900/950, GMA 3100 (G31/G33/Q33/Q35), GMA 3150 (Pineview / Atom N4xx-N5xx)
//                     -> no hardware T&L, low fill rate, frequently no NPOT textures in GL
//   tier 1 (MEDIUM) : GMA X3000/X3100/X3500 (965 family), GMA 4500 / X4500 (G41/G43/G45/GM45)
//   tier 2 (HIGH)   : anything else
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cctype>
#include <string>
#include <initializer_list>

enum QualityPreset : int { QUALITY_AUTO = 0, QUALITY_LOW = 1, QUALITY_MEDIUM = 2, QUALITY_HIGH = 3 };

struct GpuCaps {
    bool detected   = false;
    bool npot       = true;   // non-power-of-two textures usable (clamp, no mipmaps)
    bool isIntelGMA = false;
    int  tier       = 2;      // 0 = LOW, 1 = MEDIUM, 2 = HIGH
    std::string name;
};

struct QualityKnobs {
    float particleDensity;  // fraction of decorative particles that are drawn
    bool  fullscreenFlash;  // full-screen additive flash (one extra full-screen blended fill)
    bool  texture16bit;     // RGBA4444 for textures (half the texture bandwidth)
    bool  downscaleNpot;    // NPOT texture on a POT-only GPU: downscale to the lower POT instead of padding
    int   circleSegments;   // tessellation of drawCircle
    int   bgResolution;     // 1024 for High, 512 for Medium/Low
};

namespace gpu {

inline GpuCaps& caps() { static GpuCaps c; return c; }
inline int& presetRef() { static int p = QUALITY_AUTO; return p; }

inline const char* presetName(int p) {
    switch (p) {
        case QUALITY_LOW:    return "Low";
        case QUALITY_MEDIUM: return "Medium";
        case QUALITY_HIGH:   return "High";
        default:             return "Auto";
    }
}

// Effective preset after resolving AUTO through the detected hardware tier.
inline int resolvedPreset() {
    int p = presetRef();
    if (p != QUALITY_AUTO) return p;
    return caps().tier == 0 ? QUALITY_LOW : (caps().tier == 1 ? QUALITY_MEDIUM : QUALITY_HIGH);
}

inline const QualityKnobs& knobs() {
    static const QualityKnobs kLow    = { 0.40f, false, true,  true,  10, 512 };
    static const QualityKnobs kMedium = { 0.70f, true,  false, false, 12, 512 };
    static const QualityKnobs kHigh   = { 1.00f, true,  false, false, 16, 1024 };
    int p = resolvedPreset();
    return p == QUALITY_LOW ? kLow : (p == QUALITY_MEDIUM ? kMedium : kHigh);
}

// Deterministic particle thinning: keeps a well-spread subset of indices (golden-ratio stride),
// so the same particles stay visible from frame to frame (no flicker).
inline bool keepParticle(size_t index) {
    float d = knobs().particleDensity;
    if (d >= 0.999f) return true;
    uint32_t h = (uint32_t)(index * 2654435761u);
    return (float)(h >> 8) * (1.0f / 16777216.0f) < d;
}

inline std::string lower(const char* s) {
    std::string r = s ? s : "";
    for (auto& c : r) c = (char)std::tolower((unsigned char)c);
    return r;
}

inline bool containsAny(const std::string& s, std::initializer_list<const char*> keys) {
    for (const char* k : keys) if (s.find(k) != std::string::npos) return true;
    return false;
}

// Tier from a renderer / adapter description string (GL_RENDERER or D3DADAPTER_IDENTIFIER::Description).
inline int tierFromName(const std::string& rawName, bool* isGMA) {
    std::string lname = lower(rawName.c_str());
    bool intel = containsAny(lname, { "intel", "gma", "mesa dri intel" });
    if (intel && containsAny(lname, { "x3000", "x3100", "x3500", "965", "4500", "x4500", "4 series", "series 4", "g41", "g43", "g45",
                                      "gm45", "q45", "eaglelake", "cantiga", "broadwater", "crestline", "bearlake" })) {
        if (isGMA) *isGMA = true;
        return 1;
    }
    if (intel && containsAny(lname, { "915", "945", "950", "gma 900", "3100", "3150", "3 series", "series 3", "g31", "g33", "q33", "q35",
                                      "pineview", "igd", "i915", "atom" })) {
        if (isGMA) *isGMA = true;
        return 0;
    }
    if (isGMA && containsAny(lname, { "gma" })) *isGMA = true;
    return 2;
}

// Tier from a PCI device id (Direct3D adapter identifier, vendor 0x8086).
inline int tierFromPciId(uint32_t vendor, uint32_t device, bool* isGMA) {
    if (vendor != 0x8086) return -1;
    static const uint16_t low[] = { 0x2582, 0x2592, 0x2772, 0x27A2, 0x27AE, 0x29B2, 0x29C2, 0x29D2,
                                    0xA001, 0xA011 };
    static const uint16_t mid[] = { 0x2972, 0x2982, 0x2992, 0x29A2, 0x2A02, 0x2A12, 0x2A42,
                                    0x2E02, 0x2E12, 0x2E22, 0x2E32, 0x2E42, 0x2E92 };
    for (uint16_t id : low) if (id == device)       { if (isGMA) *isGMA = true; return 0; }
    for (uint16_t id : mid) if (id == device)       { if (isGMA) *isGMA = true; return 1; }
    return -1;
}

inline bool isPow2(int v) { return v > 0 && (v & (v - 1)) == 0; }
inline int nextPow2(int v) { int p = 1; while (p < v) p <<= 1; return p; }

} // namespace gpu
