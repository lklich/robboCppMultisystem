//
//  Storage.cpp
//  Robbo (C++ / SDL3)
//

#include "Storage.h"

#include <SDL3/SDL.h>

namespace Storage {

std::string dataDir() {
    static std::string dir;
    if (dir.empty()) {
        // Pusta nazwa organizacji: ten sam katalog co w wersji macOS (Application Support/Robbo).
        char* p = SDL_GetPrefPath("", "Robbo");
        if (p) {
            dir = p;
            SDL_free(p);
        } else {
            dir = "./";
        }
    }
    return dir;
}

std::string dataFile(const std::string& name) { return dataDir() + name; }

bool readFile(const std::string& path, std::vector<uint8_t>& out) {
    size_t size = 0;
    void* data = SDL_LoadFile(path.c_str(), &size);
    if (!data) return false;
    const uint8_t* b = static_cast<const uint8_t*>(data);
    out.assign(b, b + size);
    SDL_free(data);
    return true;
}

bool writeFile(const std::string& path, const std::vector<uint8_t>& bytes) {
    const std::string tmp = path + ".tmp";
    if (!SDL_SaveFile(tmp.c_str(), bytes.data(), bytes.size())) return false;
    SDL_RemovePath(path.c_str());
    if (!SDL_RenamePath(tmp.c_str(), path.c_str())) {
        // ostatnia deska ratunku: zwykły zapis
        SDL_RemovePath(tmp.c_str());
        return SDL_SaveFile(path.c_str(), bytes.data(), bytes.size());
    }
    return true;
}

bool removeFile(const std::string& path) { return SDL_RemovePath(path.c_str()); }

bool exists(const std::string& path) { return SDL_GetPathInfo(path.c_str(), nullptr); }

std::string baseName(const std::string& path) {
    const size_t p = path.find_last_of("/\\");
    return p == std::string::npos ? path : path.substr(p + 1);
}

}  // namespace Storage
