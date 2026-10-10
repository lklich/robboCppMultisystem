//
//  AtariData.h
//  Robbo (C++ / SDL3)
//
//  Dane oryginalnej gry Robbo (LK Avalon 1989, autor: Janusz Pelc) odczytane z kasety:
//  obraz pamięci Atari $0000-$BFFF po wczytaniu obu stron taśmy.
//
//  Program NIE zawiera tych danych. Odtwarza je OriginalTape z pliku .cas wskazanego przez
//  gracza i zapisuje w katalogu danych użytkownika (robbo_mem.bin).
//

#pragma once

#include <cstdint>
#include <string>
#include <vector>

/// Jedna plansza w oryginalnym formacie (512 bajtów: mapa 16×31 + 16 bajtów meta).
struct Level {
    static constexpr int width = 16;
    static constexpr int height = 31;

    std::vector<uint8_t> bytes;
    int number = 0;
    /// meta[1] = śrubki (BCD), meta[2...6] = COLOR0-COLOR4, meta[7] = tło planszy (PCOLR3)
    uint8_t meta(int i) const { return bytes[496 + i]; }
};

class AtariData {
public:
    static constexpr int levelBase = 0x3800;
    static constexpr int levelCount = 56;

    /// Pamięć Atari $0000-$BFFF zaraz po wczytaniu gry.
    std::vector<uint8_t> mem;
    std::vector<uint32_t> palette;   // 256 kolorów Atari PAL jako 0xAARRGGBB
    std::vector<Level> levels;       // 56 planet ($3800-$A7FF)

    /// Buduje dane gry z obrazu pamięci $0000-$BFFF.
    explicit AtariData(std::vector<uint8_t> memory);

    uint8_t byte(int addr) const { return mem[addr]; }
    std::vector<uint8_t> bytes(int addr, int count) const {
        return std::vector<uint8_t>(mem.begin() + addr, mem.begin() + addr + count);
    }

    // Zestawy znaków
    std::vector<uint8_t> charset() const { return bytes(0x1C00, 1024); }    // znaki planszy (CHBASE $1C)
    std::vector<uint8_t> animBanks() const { return bytes(0x1800, 1024); }  // banki animacji $1800 i $1A00
    std::vector<uint8_t> font() const { return bytes(0x2000, 1024); }       // napisy i pasek statusu (CHBASE $20)
    std::vector<uint8_t> wallsA() const { return bytes(0x0E00, 256); }      // górne znaki ścian (16 wzorów)
    std::vector<uint8_t> wallsB() const { return bytes(0x0F00, 256); }      // dolne znaki ścian
    std::vector<uint8_t> codeToChar() const { return bytes(0x34DB, 128); }

    /// Paleta Atari PAL jako 0xAARRGGBB (dostępna także bez danych gry).
    static std::vector<uint32_t> atariPalette();

    /// Kod znaku w oryginalnej czcionce gry ($2000) dla znaku Unicode; -1 = brak znaku w czcionce.
    static int textCode(char32_t ch);
    /// Tekst UTF-8 -> wiersz kodów znaków (wyśrodkowany w `width` znakach).
    static std::vector<uint8_t> encode(const std::string& utf8, int width = 32, bool centered = true);
};

/// Dekodowanie UTF-8 do znaków Unicode.
std::u32string utf8Decode(const std::string& s);
