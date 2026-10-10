//
//  AtariData.cpp
//  Robbo (C++ / SDL3)
//

#include "AtariData.h"

namespace {
const uint8_t kPaletteRGB[768] = {
#include "Palette.inc"
};
}

AtariData::AtariData(std::vector<uint8_t> memory) : mem(std::move(memory)) {
    palette = atariPalette();
    levels.reserve(levelCount);
    for (int i = 0; i < levelCount; ++i) {
        const int a = levelBase + i * 512;
        Level lv;
        lv.bytes.assign(mem.begin() + a, mem.begin() + a + 512);
        lv.number = i + 1;
        levels.push_back(std::move(lv));
    }
}

std::vector<uint32_t> AtariData::atariPalette() {
    std::vector<uint32_t> pal(256);
    for (int i = 0; i < 256; ++i) {
        const uint32_t r = kPaletteRGB[i * 3], g = kPaletteRGB[i * 3 + 1], b = kPaletteRGB[i * 3 + 2];
        pal[i] = 0xFF000000u | (r << 16) | (g << 8) | b;
    }
    return pal;
}

int AtariData::textCode(char32_t ch) {
    switch (ch) {
    case U'ą': case U'Ą': return 0x20;
    case U'ć': case U'Ć': return 0x3B;
    case U'ę': case U'Ę': return 0x3C;
    case U'ł': case U'Ł': return 0x3D;
    case U'ń': case U'Ń': return 0x3E;
    case U'ó': case U'Ó': return 0x3F;
    case U'ś': case U'Ś': return 0x7B;
    case U'ż': case U'Ż': case U'ź': case U'Ź': return 0x7C;
    case U'.': return 0x42;
    case U',': return 0x41;
    case U':': return 0x43;
    case U'!': return 0x44;
    case U'-': return 0x1F;
    case U' ': return 0x00;
    default: break;
    }
    if (ch >= U'0' && ch <= U'9') return static_cast<int>(ch - U'0') + 0x15;   // cyfry
    if (ch >= U'A' && ch <= U'Z') return static_cast<int>(ch) - 0x20;          // wielkie litery
    if (ch >= U'a' && ch <= U'z') return static_cast<int>(ch);                 // małe litery
    return -1;
}

std::vector<uint8_t> AtariData::encode(const std::string& utf8, int width, bool centered) {
    std::vector<uint8_t> row;
    for (char32_t ch : utf8Decode(utf8)) {
        const int c = textCode(ch);
        row.push_back(static_cast<uint8_t>(c < 0 ? 0 : c));
    }
    if (static_cast<int>(row.size()) > width) row.resize(width);
    const int pad = centered ? (width - static_cast<int>(row.size())) / 2 : 0;
    std::vector<uint8_t> out(width, 0);
    for (size_t i = 0; i < row.size(); ++i) out[pad + i] = row[i];
    return out;
}

std::u32string utf8Decode(const std::string& s) {
    std::u32string out;
    size_t i = 0;
    while (i < s.size()) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        char32_t cp;
        int extra;
        if (c < 0x80) { cp = c; extra = 0; }
        else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; extra = 1; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; extra = 2; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; extra = 3; }
        else { ++i; continue; }
        if (i + extra >= s.size() + (extra > 0 ? 0 : 1)) break;   // urwana sekwencja na końcu
        ++i;
        for (int k = 0; k < extra && i < s.size(); ++k, ++i) {
            cp = (cp << 6) | (static_cast<unsigned char>(s[i]) & 0x3F);
        }
        out.push_back(cp);
    }
    return out;
}
