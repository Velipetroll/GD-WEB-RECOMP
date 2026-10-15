#pragma once
#include <string>
#include <vector>
#include <filesystem>
#include <SDL2/SDL.h>

inline std::vector<uint8_t> loadAssetBinary(const std::string& path) {
    SDL_RWops* rw = SDL_RWFromFile(path.c_str(), "rb");
    #ifdef __ANDROID__
    if (!rw && path.rfind("assets/", 0) == 0) {
        // In Android APKs, assets might be stored directly at root of asset manager
        rw = SDL_RWFromFile(path.substr(7).c_str(), "rb");
    }
    #endif

    if (!rw) return {};

    Sint64 size = SDL_RWsize(rw);
    if (size <= 0) {
        SDL_RWclose(rw);
        return {};
    }

    std::vector<uint8_t> data(static_cast<size_t>(size));
    size_t read = SDL_RWread(rw, data.data(), 1, static_cast<size_t>(size));
    SDL_RWclose(rw);

    if (read != static_cast<size_t>(size)) {
        data.resize(read);
    }
    return data;
}

inline std::string loadAssetText(const std::string& path) {
    auto bin = loadAssetBinary(path);
    if (bin.empty()) return "";
    return std::string(bin.begin(), bin.end());
}

inline std::string getAssetFilePath(const std::string& relativePath) {
#ifdef __ANDROID__
    const char* internalDir = SDL_AndroidGetInternalStoragePath();
    if (!internalDir) return relativePath;

    std::string destPath = std::string(internalDir) + "/" + relativePath;

    // Check if destination file already exists and is non-empty
    SDL_RWops* check = SDL_RWFromFile(destPath.c_str(), "rb");
    if (check) {
        Sint64 sz = SDL_RWsize(check);
        SDL_RWclose(check);
        if (sz > 0) return destPath;
    }

    // Extract asset binary to internal directory
    std::vector<uint8_t> data = loadAssetBinary(relativePath);
    if (data.empty()) return relativePath;

    std::filesystem::path p(destPath);
    std::filesystem::create_directories(p.parent_path());

    SDL_RWops* out = SDL_RWFromFile(destPath.c_str(), "wb");
    if (out) {
        SDL_RWwrite(out, data.data(), 1, data.size());
        SDL_RWclose(out);
        return destPath;
    }
#endif
    return relativePath;
}
