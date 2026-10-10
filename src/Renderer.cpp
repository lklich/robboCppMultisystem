//
//  Renderer.cpp
//  Robbo (C++ / SDL3)
//

#include "Renderer.h"

#include "AtariData.h"
#include "Game.h"

#include <algorithm>

Renderer::Renderer(std::vector<uint32_t> palette)
    : pixels_(width * height, 0xFF000000u), palette_(std::move(palette)) {}

void Renderer::fillRow(int row, uint8_t c) {
    if (row < 0 || row >= height) return;
    std::fill_n(pixels_.begin() + row * width, width, color(c));
}

void Renderer::fillAll(uint8_t c) {
    std::fill(pixels_.begin(), pixels_.end(), color(c));
}

void Renderer::mode4Row(int row, const std::vector<uint8_t>& chars, int offset, const std::vector<uint8_t>& charset,
                        int line, uint8_t bg, const uint8_t pf[4]) {
    if (row < 0 || row >= height) return;
    const uint32_t normal[4] = {color(bg), color(pf[0]), color(pf[1]), color(pf[2])};
    const uint32_t inverse[4] = {normal[0], normal[1], normal[2], color(pf[3])};
    uint32_t* buf = pixels_.data();
    int p = row * width + narrowX;
    const int mask = static_cast<int>(chars.size()) - 1;
    for (int i = 0; i < 32; ++i) {
        const uint8_t ch = chars[(offset + i) & mask];
        const uint32_t* cols = (ch & 0x80) ? inverse : normal;
        const uint8_t byte = charset[(ch & 0x7F) * 8 + line];
        for (int px = 0; px < 4; ++px) {
            const uint32_t c = cols[(byte >> (6 - px * 2)) & 3];
            buf[p] = c;
            buf[p + 1] = c;
            p += 2;
        }
    }
}

void Renderer::mode2Row(int row, int x, const uint8_t* chars, int count, const std::vector<uint8_t>& font, int line,
                        uint8_t pf1, uint8_t pf2) {
    if (row < 0 || row >= height) return;
    const uint32_t fg = color(static_cast<uint8_t>((pf2 & 0xF0) | (pf1 & 0x0F)));
    const uint32_t bg = color(pf2);
    uint32_t* buf = pixels_.data();
    int p = row * width + x;
    for (int i = 0; i < count; ++i) {
        const uint8_t c = chars[i];
        uint8_t byte = font[(c & 0x7F) * 8 + line];
        if (c & 0x80) byte = static_cast<uint8_t>(~byte);
        for (int bit = 0; bit < 8; ++bit) {
            buf[p] = (byte & (0x80 >> bit)) ? fg : bg;
            p += 1;
        }
    }
}

void Renderer::modeARow(int row, const uint8_t* bytes, int count, const uint8_t colors[4]) {
    if (row < 0 || row >= height) return;
    const uint32_t cols[4] = {color(colors[0]), color(colors[1]), color(colors[2]), color(colors[3])};
    uint32_t* buf = pixels_.data();
    int p = row * width + narrowX;
    for (int i = 0; i < count; ++i) {
        const uint8_t b = bytes[i];
        for (int px = 0; px < 4; ++px) {
            const uint32_t c = cols[(b >> (6 - px * 2)) & 3];
            for (int k = 0; k < 4; ++k) { buf[p] = c; p += 1; }
        }
    }
}

void Renderer::modeCRow(int row, const std::vector<uint8_t>& bytes, uint8_t bk, uint8_t pf0) {
    if (row < 0 || row >= height) return;
    const uint32_t on = color(pf0), off = color(bk);
    uint32_t* buf = pixels_.data();
    int p = row * width + narrowX;
    for (uint8_t b : bytes) {
        for (int bit = 0; bit < 8; ++bit) {
            const uint32_t c = (b & (0x80 >> bit)) ? on : off;
            buf[p] = c; buf[p + 1] = c;
            p += 2;
        }
    }
}

std::vector<uint32_t> Renderer::levelPreview(const Level& level, const AtariData& data) {
    std::vector<uint8_t> charset = data.charset();
    const int style = ((level.number - 1) >> 2) & 15;
    const auto wa = data.wallsA(), wb = data.wallsB();
    for (int i = 0; i < 16; ++i) {
        charset[i] = wa[style * 16 + i];
        charset[0x100 + i] = wb[style * 16 + i];
    }
    Game::animateCharset(charset, data.animBanks(), 0, 0, 0x0D);
    const auto table = data.codeToChar();
    const auto& pal = data.palette;
    const int w = previewWidth, h = previewHeight;
    const uint32_t bg = pal[level.meta(7)];
    const uint32_t normal[4] = {bg, pal[level.meta(2)], pal[level.meta(3)], pal[level.meta(4)]};
    const uint32_t inverse[4] = {bg, pal[level.meta(2)], pal[level.meta(3)], pal[level.meta(5)]};
    std::vector<uint32_t> out(w * h, bg);
    for (int wy = 0; wy < h; ++wy) {
        const int tileRow = wy / 16, half = (wy % 16) / 8, line = wy % 8;
        int p = wy * w;
        for (int tx = 0; tx < 16; ++tx) {
            int code = level.bytes[tileRow * 16 + tx];
            code = code == 0xA0 ? 0 : (code & 0x7F);
            const int first = table[code];
            for (int k = 0; k < 2; ++k) {
                const int ch = (first + k + half * 0x20) & 0xFF;
                const uint32_t* cols = (ch & 0x80) ? inverse : normal;
                const uint8_t byte = charset[(ch & 0x7F) * 8 + line];
                for (int px = 0; px < 4; ++px) {
                    const uint32_t c = cols[(byte >> (6 - px * 2)) & 3];
                    out[p] = c; out[p + 1] = c;
                    p += 2;
                }
            }
        }
    }
    return out;
}
