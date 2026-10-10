//
//  Renderer.h
//  Robbo (C++ / SDL3)
//
//  Programowy odpowiednik ANTIC i GTIA: bufor 336×240 (wiersz 0 = linia 8 ekranu PAL),
//  rysowany linia po linii w trybach używanych przez grę (2, 4, A, C).
//  Piksele w formacie 0xAARRGGBB (SDL_PIXELFORMAT_ARGB8888).
//

#pragma once

#include <cstdint>
#include <vector>

class AtariData;
struct Level;

class Renderer {
public:
    static constexpr int width = 336;
    static constexpr int height = 240;
    static constexpr int narrowX = 40;    // wąskie pole gry: 256 pikseli
    static constexpr int normalX = 8;     // zwykłe pole gry: 320 pikseli

    explicit Renderer(std::vector<uint32_t> palette);

    const std::vector<uint32_t>& pixels() const { return pixels_; }
    uint32_t color(uint8_t c) const { return palette_[c]; }

    void fillRow(int row, uint8_t c);
    void fillAll(uint8_t c);

    /// Tryb 4 (znaki 4-kolorowe, 32 znaki w wąskim polu). `line` = linia znaku 0-7.
    void mode4Row(int row, const std::vector<uint8_t>& chars, int offset, const std::vector<uint8_t>& charset,
                  int line, uint8_t bg, const uint8_t pf[4]);
    /// Tryb 2 (znaki 1-bitowe). Kolor liter: odcień COLPF2 z jasnością COLPF1, tło COLPF2.
    void mode2Row(int row, int x, const uint8_t* chars, int count, const std::vector<uint8_t>& font, int line,
                  uint8_t pf1, uint8_t pf2);
    /// Tryb A (grafika 4-kolorowa, 16 bajtów w wąskim polu, piksel = 8 punktów).
    void modeARow(int row, const uint8_t* bytes, int count, const uint8_t colors[4]);
    /// Tryb C (grafika 2-kolorowa, 16 bajtów w wąskim polu).
    void modeCRow(int row, const std::vector<uint8_t>& bytes, uint8_t bk, uint8_t pf0);

    /// Cała plansza 16×31 kafli (256×496 px) w kolorach planszy, ze ścianami właściwymi dla numeru.
    static constexpr int previewWidth = 256;
    static constexpr int previewHeight = 31 * 16;
    static std::vector<uint32_t> levelPreview(const Level& level, const AtariData& data);

private:
    std::vector<uint32_t> pixels_;
    std::vector<uint32_t> palette_;
};
