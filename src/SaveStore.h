//
//  SaveStore.h
//  Robbo (C++ / SDL3)
//
//  Zapisy stanu gry: lista zapisów z miniaturką (plik saves.bin w katalogu danych).
//

#pragma once

#include "Game.h"

#include <cstdint>
#include <string>
#include <vector>

struct SaveEntry {
    static constexpr int thumbWidth = 168;    // połowa ekranu 336×240
    static constexpr int thumbHeight = 120;

    uint64_t id = 0;
    int64_t date = 0;               // sekundy od 1970 (UTC)
    int level = 0;
    std::string score;              // 6 cyfr
    std::string lives;              // 2 cyfry
    std::vector<uint32_t> thumbnail;   // ARGB8888, thumbWidth × thumbHeight
    GameSnapshot snapshot;
};

class SaveStore {
public:
    static constexpr size_t maxEntries = 30;

    explicit SaveStore(std::string path);

    const std::vector<SaveEntry>& entries() const { return entries_; }
    void add(SaveEntry entry);
    void remove(uint64_t id);

private:
    std::string path_;
    std::vector<SaveEntry> entries_;

    void load();
    void persist() const;
};

/// Miniatura z bufora ekranu (zmniejszenie 2×).
std::vector<uint32_t> makeThumbnail(const std::vector<uint32_t>& pixels, int width, int height);
