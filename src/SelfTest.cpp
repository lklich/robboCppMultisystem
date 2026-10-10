//
//  SelfTest.cpp
//  Robbo (C++ / SDL3)
//

#include "SelfTest.h"

#include "Engine.h"

#include <cstdio>
#include <cstring>

namespace {

const char* kNames[16] = {"rx", "ry", "dir", "9B", "alive", "level", "keys", "lives", "screws", "A1",
                          "ammo", "demo", "moving", "ticks", "anim", "cooldown"};

uint32_t fnv(const std::vector<uint8_t>& bytes) {
    uint32_t h = 0x811C9DC5u;
    for (uint8_t x : bytes) h = (h ^ x) * 0x01000193u;
    return h;
}

void getVars(const Engine& e, int out[16]) {
    const int v[16] = {e.rx, e.ry, e.dir, e.v9b, e.alive, e.level, e.keys, e.lives, e.screws, e.lifeLevel,
                       e.ammo, e.demo ? 1 : 0, e.moving, e.ticks, e.robboAnim, e.cooldown};
    std::memcpy(out, v, sizeof(v));
}

void setVars(Engine& e, const std::vector<int>& v) {
    e.rx = v[0]; e.ry = v[1]; e.dir = v[2]; e.v9b = v[3]; e.alive = v[4]; e.level = v[5];
    e.keys = v[6]; e.lives = v[7]; e.screws = v[8]; e.lifeLevel = v[9]; e.ammo = v[10];
    e.demo = v[11] != 0; e.moving = v[12]; e.ticks = v[13]; e.robboAnim = v[14]; e.cooldown = v[15];
}

std::string hex(int v) {
    char b[16];
    std::snprintf(b, sizeof(b), "%x", v);
    return b;
}

}  // namespace

namespace SelfTest {

Result run(const EngineTables& tables, const std::vector<uint8_t>& b) {
    Result res;
    if (b.size() <= 8 || std::memcmp(b.data(), "RBO1", 4) != 0) {
        res.message = "Test: zły plik";
        return res;
    }
    const int count = int(b[4]) | int(b[5]) << 8 | int(b[6]) << 16 | int(b[7]) << 24;
    size_t p = 8;
    bool truncated = false;
    auto take = [&](size_t n) {
        if (p + n > b.size()) { truncated = true; p = b.size(); return std::vector<int>(n, 0); }
        std::vector<int> v(b.begin() + p, b.begin() + p + n);
        p += n;
        return v;
    };
    auto takeBytes = [&](size_t n) {
        if (p + n > b.size()) { truncated = true; p = b.size(); return std::vector<uint8_t>(n, 0); }
        std::vector<uint8_t> v(b.begin() + p, b.begin() + p + n);
        p += n;
        return v;
    };
    auto list = [&]() {
        if (p >= b.size()) { truncated = true; return std::vector<int>(); }
        const size_t n = b[p++];
        return take(n);
    };

    std::string firstFailure;
    for (int index = 0; index < count && !truncated; ++index) {
        const auto m = takeBytes(0x400);
        const auto scr = takeBytes(0x800);
        const auto vars = take(16);
        const auto score = take(3);
        if (p + 2 > b.size()) { truncated = true; break; }
        const size_t nRand = size_t(b[p]) | size_t(b[p + 1]) << 8;
        p += 2;
        const auto rand = take(nRand);
        const auto inp = list(), fire = list(), scroll = list(), snd = list();
        const auto postM = takeBytes(0x200);
        if (p + 4 > b.size()) { truncated = true; break; }
        const uint32_t postHash = uint32_t(b[p]) | uint32_t(b[p + 1]) << 8 | uint32_t(b[p + 2]) << 16 |
                                  uint32_t(b[p + 3]) << 24;
        p += 4;
        const auto postVars = take(16);
        const auto postScore = take(3);
        if (truncated) break;

        Engine e(tables);
        e.m = m; e.scr = scr;
        setVars(e, vars);
        e.score = {score[0], score[1], score[2]};
        size_t ri = 0, ii = 0, fi = 0, si = 0;
        std::vector<int> sounds;
        e.random = [&] { const int v = ri < rand.size() ? rand[ri] : 0; ++ri; return v; };
        e.input = [&] { const int v = ii < inp.size() ? inp[ii] : 0x0F; ++ii; return v; };
        e.firePressed = [&] { const bool v = fi < fire.size() ? fire[fi] != 0 : false; ++fi; return v; };
        e.isScrolling = [&](bool) { const bool v = si < scroll.size() ? scroll[si] != 0 : false; ++si; return v; };
        e.sound = [&](int n) { sounds.push_back(n & 0xFF); };
        e.tick();

        std::string problem;
        int mine[16];
        getVars(e, mine);
        for (int i = 0; i < 16; ++i) {
            if ((mine[i] & 0xFF) != postVars[i]) {
                problem = std::string(kNames[i]) + " " + hex(mine[i] & 0xFF) + " zamiast " + hex(postVars[i]);
                break;
            }
        }
        if (problem.empty() && (e.score[0] != postScore[0] || e.score[1] != postScore[1] || e.score[2] != postScore[2]))
            problem = "wynik";
        if (problem.empty()) {
            for (int i = 0; i < 0x200; ++i) {
                if (e.m[i] != postM[i]) {
                    problem = "pole (" + std::to_string(i % 16) + "," + std::to_string(i / 16) + ") " + hex(e.m[i]) +
                              " zamiast " + hex(postM[i]);
                    break;
                }
            }
        }
        if (problem.empty() && fnv(e.scr) != postHash) problem = "ekran";
        if (problem.empty() && sounds != snd) problem = "dźwięki";
        if (problem.empty() && ri != rand.size())
            problem = "zużyto " + std::to_string(ri) + "/" + std::to_string(rand.size()) + " liczb losowych";
        if (!problem.empty()) {
            if (firstFailure.empty()) firstFailure = "tura " + std::to_string(index) + ": " + problem;
        } else {
            res.passed += 1;
        }
    }
    res.total = count;
    res.message = "Test zgodności: " + std::to_string(res.passed) + "/" + std::to_string(count) +
                  " tur zgodnych z oryginałem";
    if (!firstFailure.empty()) res.message += " – pierwszy błąd: " + firstFailure;
    if (truncated) res.message += " (plik nagrań urwany)";
    return res;
}

}  // namespace SelfTest
