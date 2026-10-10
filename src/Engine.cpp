//
//  Engine.cpp
//  Robbo (C++ / SDL3)
//
//  Port 1:1 z Engine.swift – adresy procedur 6502 w komentarzach.
//

#include "Engine.h"

#include "AtariData.h"

#include <random>
#include <tuple>

EngineTables::EngineTables(const AtariData& data)
    : props(data.bytes(0x3637, 128)),
      anim(data.bytes(0x355B, 26)),
      chars(data.bytes(0x34DB, 128)),
      qmark(data.bytes(0x2C5B, 32)),
      dirs(data.bytes(0x2AE9, 16)),
      bombOff(data.bytes(0x2D17, 9)),
      bombVal(data.bytes(0x2D20, 9)),
      box(data.bytes(0x2BD5, 4)) {}

Engine::Engine(const EngineTables& tables) : t(tables) {
    random = [] {
        static std::mt19937 rng{std::random_device{}()};
        return static_cast<int>(rng() & 0xFF);
    };
    sound = [](int) {};
    input = [] { return 0x0F; };
    firePressed = [] { return false; };
    isScrolling = [](bool) { return false; };
}

// MARK: - BCD

std::pair<int, int> Engine::bcdAdd(int a, int b, int c) {
    int lo = (a & 0x0F) + (b & 0x0F) + c;
    if (lo > 9) lo += 6;
    int hi = (a >> 4) + (b >> 4) + (lo > 0x0F ? 1 : 0);
    lo &= 0x0F;
    if (hi > 9) hi += 6;
    return {((hi << 4) | lo) & 0xFF, hi > 0x0F ? 1 : 0};
}

std::pair<int, int> Engine::bcdSub(int a, int b) {
    int lo = (a & 0x0F) - (b & 0x0F);
    int borrow = 0;
    if (lo < 0) { lo += 10; borrow = 1; }
    int hi = (a >> 4) - (b >> 4) - borrow;
    int c = 1;
    if (hi < 0) { hi += 10; c = 0; }
    return {((hi << 4) | (lo & 0x0F)) & 0xFF, c};
}

int Engine::firstZero(int v) {
    int i = 0;
    while ((v & 1) != 0 && i <= 8) { v >>= 1; i += 1; }
    return i;
}

// MARK: - Pamięć

/// $3311: A = surowy bajt pola (x, y), ustawia wskaźnik $80/$81.
int Engine::get() {
    const int xx = x & 0xFF, yy = y & 0xFF;
    ptr = (((yy >> 4) & 1) << 8) + (((yy << 4) & 0xFF) | xx);
    return m[ptr & 0x3FF];
}

/// $32CC: zapis do mapy (ze stałym przesunięciem Y) i narysowanie kafla na ekranie.
void Engine::draw(int a, int yOff) {
    m[(ptr + yOff) & 0x3FF] = static_cast<uint8_t>(a & 0xFF);
    const int xx = x & 0xFF, yy = y & 0xFF;
    const int hi = 0x10 + ((yy >> 2) & 7);
    const int x2 = (xx << 1) & 0xFF;
    const int lo = (((yy << 6) & 0xC0) + x2 + (xx >> 7)) & 0xFF;
    x = x2 >> 1;
    scrAddr = ((hi << 8) | lo) - 0x1000;
    int c = a & 0x7F;
    if ((a & 0xFF) == 0xA0) c = 0;
    const int ch = t.chars[c];
    const int s = scrAddr;
    scr[s & 0x7FF] = static_cast<uint8_t>(ch);
    scr[(s + 1) & 0x7FF] = static_cast<uint8_t>((ch + 1) & 0xFF);
    scr[(s + 0x20) & 0x7FF] = static_cast<uint8_t>((ch + 0x20) & 0xFF);
    scr[(s + 0x21) & 0x7FF] = static_cast<uint8_t>((ch + 0x21) & 0xFF);
    regX = (ch + 0x21) & 0xFF;
}

/// $32B7: zapis kodu na pole (x, y) z rysowaniem; poza planszą nic nie robi.
void Engine::put(int a) {
    if ((x & 0xFF) >= 0x10 || (y & 0xFF) >= 0x20) {
        regX = x & 0xFF;
        return;
    }
    get();
    draw(a);
}

/// $32AA: zabiera obiekt z pola (zostaje $20), zwraca jego kod.
int Engine::take() {
    const int o = get();
    draw(0x20);
    return o;
}

// Ruchy obiektu z (x, y): (zablokowany, A). Po udanym ruchu x/y wskazują nowe pole.
Engine::Move Engine::mvRight() {          // $27E0
    x = (x + 1) & 0xFF;
    const int v = get();
    x = (x - 1) & 0xFF;
    if (v != 0x20) return {true, v};
    const int o = take();
    x = (x + 1) & 0xFF;
    put(o | 0x80);
    return {false, o};
}

Engine::Move Engine::mvLeft() {           // $27F9
    x = (x - 1) & 0xFF;
    const int v = get();
    x = (x + 1) & 0xFF;
    if (v != 0x20) return {true, v};
    const int o = take();
    x = (x - 1) & 0xFF;
    put(o);
    return {false, o};
}

Engine::Move Engine::mvDown() {           // $2810
    y = (y + 1) & 0xFF;
    const int v = get();
    y = (y - 1) & 0xFF;
    if (v != 0x20) return {true, v};
    const int o = take();
    y = (y + 1) & 0xFF;
    put(o | 0x80);
    return {false, o};
}

Engine::Move Engine::mvUp() {             // $2829
    y = (y - 1) & 0xFF;
    const int v = get();
    y = (y + 1) & 0xFF;
    if (v != 0x20) return {true, v};
    const int o = take();
    y = (y - 1) & 0xFF;
    put(o);
    return {false, o};
}

Engine::Move Engine::mvDir() {            // $2840
    switch (firstZero(dir)) {
    case 0: return mvUp();
    case 1: return mvDown();
    case 2: return mvLeft();
    case 3: return mvRight();
    default: return {true, 0xA0};
    }
}

/// $285F/$2869/$2873/$287D: podgląd sąsiada -> (właściwości, kod).
std::pair<int, int> Engine::look(int dx, int dy) {
    x = (x + dx) & 0xFF; y = (y + dy) & 0xFF;
    int v = get();
    x = (x - dx) & 0xFF; y = (y - dy) & 0xFF;
    if (v == 0xA0) v = 0;
    v &= 0x7F;
    regX = v;
    return {t.props[v], v};
}

void Engine::addScore(int a, int xx) {    // $2C7B
    int c = 0;
    std::tie(score[2], c) = bcdAdd(score[2], a, 0);
    std::tie(score[1], c) = bcdAdd(score[1], xx, c);
    std::tie(score[0], c) = bcdAdd(score[0], 0, c);
}

// MARK: - Tura ($266C)

void Engine::tick() {
    ticks = (ticks + 1) & 0xFF;
    stepClick = false;
    for (int yy = 0; yy < 0x1F; ++yy) {
        for (int xx = 0; xx < 0x10; ++xx) {
            sx = xx; sy = yy;
            x = xx; y = yy;
            const int v = get();
            if (v & 0x80) {
                if (v != 0xA0) put(v & 0x7F);
                continue;
            }
            if (v >= 0x61) {
                if (v >= 0x7B) continue;
                if (v == 0x6D) { v9b = 0; alive = 1; }
                put(t.anim[v - 0x61]);
                continue;
            }
            handle(v);
        }
    }
    endTick();
}

/// $275E (część logiczna): krok Robbo – licznik animacji i kliknięcie głośnika.
void Engine::endTick() {
    if (moving != 0 && alive != 0) {
        robboAnim = (robboAnim + 1) & 0xFF;
        stepClick = true;
    }
}

void Engine::handle(int c) {              // tablica $3575
    switch (c) {
    case 0x01: blasterGun(1, 0, 0xD8); break;
    case 0x04: blasterGun(-1, 0, 0x57); break;
    case 0x07: roll(&Engine::mvLeft, &Engine::shootLeft); break;
    case 0x08: roll(&Engine::mvRight, &Engine::shootRight); break;
    case 0x09: roll(&Engine::mvUp, &Engine::shootUp); break;
    case 0x0A: roll(&Engine::mvDown, &Engine::shootDown); break;
    case 0x0B: pulled(&Engine::mvLeft); break;
    case 0x0C: pulled(&Engine::mvRight); break;
    case 0x0D: movingGun(true); break;
    case 0x0E: movingGun(false); break;
    case 0x11: barrier(); break;
    case 0x14: capsule(); break;
    case 0x15: case 0x16: capsuleOn(); break;
    case 0x17: blasterGun(0, 1, 0xDA); break;
    case 0x18: blasterGun(0, -1, 0x59); break;
    case 0x1B: robboAppear(); break;
    case 0x1C: if (gunFires()) { sound(1); shootUp(); } break;
    case 0x1D: if (gunFires()) { sound(1); shootDown(); } break;
    case 0x1E: if (gunFires()) { sound(1); shootLeft(); } break;
    case 0x1F: if (gunFires()) { sound(1); shootRight(); } break;
    case 0x26: eyes(); break;
    case 0x27: if (gunFires()) laser(0, 1, 0xD6); break;
    case 0x28: magnet(); break;
    case 0x2A: robbo(); break;
    case 0x2C: case 0x2D: case 0x2E: case 0x2F: rotatingGun(); break;
    case 0x30: case 0x31: case 0x32: case 0x33: case 0x34:
    case 0x35: case 0x36: case 0x37: case 0x38: case 0x39: teleport(); break;
    case 0x3A: questionMark(); break;
    case 0x3C: if (gunFires()) laser(-1, 0, 0x53); break;
    case 0x3E: if (gunFires()) laser(1, 0, 0xD4); break;
    case 0x41: bear(&Engine::mvLeft, 0x44, &Engine::mvUp, 0x43); break;
    case 0x42: bear(&Engine::mvRight, 0xC3, &Engine::mvDown, 0x44); break;
    case 0x43: bear(&Engine::mvUp, 0x41, &Engine::mvRight, 0x42); break;
    case 0x44: bear(&Engine::mvDown, 0xC2, &Engine::mvLeft, 0x41); break;
    case 0x45: bear(&Engine::mvLeft, 0x47, &Engine::mvDown, 0x48); break;
    case 0x46: bear(&Engine::mvRight, 0xC8, &Engine::mvUp, 0xC7); break;
    case 0x47: bear(&Engine::mvUp, 0x46, &Engine::mvLeft, 0x45); break;
    case 0x48: bear(&Engine::mvDown, 0xC5, &Engine::mvRight, 0x46); break;
    case 0x49: bird(&Engine::mvLeft, 0xCA); break;
    case 0x4A: bird(&Engine::mvRight, 0xC9); break;
    case 0x4B: bird(&Engine::mvUp, 0xCC); break;
    case 0x4C: bird(&Engine::mvDown, 0xCB); break;
    case 0x4D: shootingBird(&Engine::mvLeft, 0x4E); break;
    case 0x4E: shootingBird(&Engine::mvRight, 0x4D); break;
    case 0x4F: shot(&Engine::mvLeft, &Engine::shootLeft); break;
    case 0x50: shot(&Engine::mvRight, &Engine::shootRight); break;
    case 0x51: shot(&Engine::mvUp, &Engine::shootUp); break;
    case 0x52: shot(&Engine::mvDown, &Engine::shootDown); break;
    case 0x53: laserMove(&Engine::mvLeft, -1, 0, 0x50, 0x5D, 0x53); break;
    case 0x54: laserMove(&Engine::mvRight, 1, 0, 0x4F, 0x5D, 0xD4); break;
    case 0x55: laserMove(&Engine::mvUp, 0, -1, 0x52, 0x5B, 0x55); break;
    case 0x56: laserMove(&Engine::mvDown, 0, 1, 0x51, 0x5B, 0xD6); break;
    case 0x57: blasterMove(&Engine::mvLeft, -1, 0, 0x57); break;
    case 0x58: blasterMove(&Engine::mvRight, 1, 0, 0xD8); break;
    case 0x59: blasterMove(&Engine::mvUp, 0, -1, 0x59); break;
    case 0x5A: blasterMove(&Engine::mvDown, 0, 1, 0xDA); break;
    case 0x5E: if (gunFires()) laser(0, -1, 0x55); break;
    case 0x60: bomb(); break;
    default: break;
    }
}

// MARK: - Robbo ($28B5)

void Engine::robbo() {
    if (v9b == 1) v9b = 0;
    alive = 1;
    cooldown = (cooldown - 1) & 0xFF;
    if (cooldown & 0x80) cooldown = (cooldown + 1) & 0xFF;
    bool fire = false;
    if (demo && (random() & 0x0F) == 0) {
        fire = true;
    } else if (firePressed()) {
        fire = true;
    }
    if (!fire) { walk(); return; }
    moving = 0;
    if (cooldown != 0) return;
    int a;
    if (demo && (dir & 0x80) == 0) {
        a = dir;
    } else {
        a = input();
    }
    if (a == 0x0F) return;
    dir = a;
    if (ammo == 0) {
        robboAnim = (robboAnim + 1) & 0xFF;
        return;
    }
    ammo = bcdSub(ammo, 1).first;
    cooldown = 5;
    switch (firstZero(a)) {
    case 0: shootUp(); break;
    case 1: shootDown(); break;
    case 2: shootLeft(); break;
    case 3: shootRight(); break;
    default: return;
    }
    sound(0x01);
}

void Engine::walk() {                      // $296C
    int a;
    if (demo) {
        if (moving != 0 && (random() & 0x0F) != 0) {
            a = (dir & 0x80) == 0 ? dir : input();
        } else {
            a = t.dirs[random() & 3];
        }
    } else {
        a = input();
    }
    if (a != 0x0F) dir = a;
    moving = 1;
    Move r{false, 0};
    if ((a & 1) == 0) {
        r = mvUp();
        if (!r.first) { afterMove(); return; }
        y = (y - 1) & 0xFF;
    } else if ((a & 2) == 0) {
        r = mvDown();
        if (!r.first) { afterMove(); return; }
        y = (y + 1) & 0xFF;
    } else if ((a & 4) == 0) {
        r = mvLeft();
        if (!r.first) { afterMove(); return; }
        x = (x - 1) & 0xFF;
    } else if ((a & 8) == 0) {
        r = mvRight();
        if (!r.first) { afterMove(); return; }
        x = (x + 1) & 0xFF;
    } else {
        stay();
        return;
    }
    interact(r.second);
}

/// $29D0
void Engine::stay() {
    moving = 0;
    afterMove();
}

/// $29D4: Robbo stoi na (x, y) – zapamiętanie pozycji, sąsiedzi, magnes z prawej.
void Engine::afterMove() {
    const int v = get();
    put(v);
    robboScreenAddr = scrAddr;
    rx = x; ry = y;
    if (v9b == 1) {
        static const int d[4][2] = {{0, -1}, {0, 1}, {-1, 0}, {1, 0}};
        for (const auto& dd : d) {
            const int code = look(dd[0], dd[1]).second;
            if (code >= 0x41 && code < 0x4F) {
                sound(0x0A);
                put(0x61);
                return;
            }
        }
    }
    get();
    int yy = 0, a = 0;
    do {
        yy = (yy + 1) & 0xFF;
        a = mem(yy);
    } while (a == 0x20);
    if (a != 0x29) return;
    alive = 1;
    put(0x8C);
    sound(0x0E);
}

void Engine::interact(int a) {             // $2A3B
    if (a == 0x0E || a == 0x0D || a == 0x3F || a == 0x23 || a == 0x14 || a == 0x40) {
        const bool blocked = mvDir().first;
        int xx = 0;
        if (!blocked) { sound(0x08); xx = 1; }
        moving = xx;
        back();
        mvDir();
        afterMove();
        return;
    }
    if (a >= 0x30 && a < 0x3A) {
        v9b = a;
        put(a | 0x80);
        back();
        sound(0x03);
        moving = 0;
        put(0x64);
        return;
    }
    if (a == 0x3D) {
        keys = bcdAdd(keys, 1).first;
        addScore(0x75, 0);
        sound(0x09);
        takeMove();
        return;
    }
    if (keys != 0 && (a == 0x7C || a == 0x12)) {
        keys = bcdSub(keys, 1).first;
        addScore(0, 1);
        sound(0x06);
        put(0x7A);
        return;
    }
    if (a == 0x24) {
        if (screws == 0) {
            addScore(0x50, 2);
        } else {
            screws = bcdSub(screws, 1).first;
            addScore(0, 1);
        }
        sound(0x04);
        takeMove();
        return;
    }
    if (a == 0x15 || a == 0x16) {
        back();
        put(0x20);
        addScore(0, 0x10);
        sound(0x0C);
        alive = 0x80;
        return;
    }
    if (a == 0x21) {
        const auto [s, c] = bcdAdd(9, ammo);
        ammo = c != 0 ? 0x99 : s;
        addScore(0x50, 0);
        sound(0x07);
        takeMove();
        return;
    }
    if (a == 0x2B) {
        lifeLevel = level;
        lives = bcdAdd(1, lives).first;
        addScore(0, 2);
        sound(0x05);
        takeMove();
        return;
    }
    if (a == 0x06) {
        if (!mvDir().first) {
            put(t.box[firstZero(dir) & 3]);
            sound(0x08);
            back();
            mvDir();
            afterMove();
            return;
        }
    }
    x = rx; y = ry;
    stay();
}

void Engine::takeMove() {                  // $2B12
    (void)take();
    back();
    mvDir();
    afterMove();
}

// MARK: - Pociski ($2D93, $2DD9, $2DFF, $2E25)

void Engine::hit(int p, int code) {        // $2DA5 / $2E5F
    if ((p & 1) == 0) { sound(0x02); return; }
    if (code == 0x40) { put(0x60); return; }
    if (code == 0x3F) { questionHit(); return; }
    sound(0x0A);
    put(0x61);
}

void Engine::questionHit() {               // $2FC8
    sound(0x0A);
    put(0x6E);
}

bool Engine::gunFires() { return random() < 0x12; }   // $2CC4

void Engine::shootLeft() {
    const auto [p, code] = look(-1, 0);
    x = (x - 1) & 0xFF;
    if (code == 0x20 || code == 0x5D) { put(0x4F); return; }
    hit(p, code);
}

void Engine::shootRight() {
    const auto [p, code] = look(1, 0);
    x = (x + 1) & 0xFF;
    if (code == 0x20 || code == 0x5D) { put(0xD0); return; }
    hit(p, code);
}

void Engine::shootUp() {
    const auto [p, code] = look(0, -1);
    y = (y - 1) & 0xFF;
    if (code == 0x20 || code == 0x5B) { put(0x51); return; }
    hit(p, code);
}

void Engine::shootDown() {
    const auto [p, code] = look(0, 1);
    y = (y + 1) & 0xFF;
    if (code == 0x20 || code == 0x5B) { put(0xD2); return; }
    hit(p, code);
}

/// $2D8A/$2DD0/$2DF6/$2E1C: lecący pocisk
void Engine::shot(MoveFn mv, ShootFn shoot) {
    const auto [blocked, a] = (this->*mv)();
    if (!blocked) return;
    put(a == 0x5D || a == 0x5B ? 0x20 : 0x66);       // $2D29
    (this->*shoot)();
}

// MARK: - Lasery

void Engine::laser(int dx, int dy, int val) {   // $2E51/$2E96/$2EBB/$2EE0
    const auto [p, code] = look(dx, dy);
    x = (x + dx) & 0xFF; y = (y + dy) & 0xFF;
    if (code == 0x20) { put(val); return; }
    hit(p, code);
}

void Engine::laserMove(MoveFn mv, int dx, int dy, int head, int beam, int val) {
    if (!(this->*mv)().first) {
        x = (x - dx) & 0xFF; y = (y - dy) & 0xFF;
        put(beam);
        return;
    }
    put(head);
    laser(dx, dy, val);
}

// MARK: - Blastery

/// $2FBA: true = przerwij (bomba)
bool Engine::blastCheck(int xx) {
    if (xx == 0x3F) { questionHit(); return false; }
    if (xx == 0x40) { put(0x60); return true; }
    return false;
}

void Engine::blastLook(int dx, int dy, int val) {
    const auto [p, code] = look(dx, dy);
    x = (x + dx) & 0xFF; y = (y + dy) & 0xFF;
    if (code != 0x20 && (p & 1) == 0) return;
    if (blastCheck(code)) return;
    put(val);
}

void Engine::blasterGun(int dx, int dy, int val) {
    if (gunFires()) blastLook(dx, dy, val);
}

void Engine::blasterMove(MoveFn mv, int dx, int dy, int val) {
    if (!(this->*mv)().first) {
        if (blastCheck(regX)) return;
        x = (x - dx) & 0xFF; y = (y - dy) & 0xFF;
        put(0x62);
        return;
    }
    put(0x62);
    blastLook(dx, dy, val);
}

// MARK: - Skrzynie toczące się ($2C8E-$2CBF)

void Engine::roll(MoveFn mv, ShootFn shoot) {
    if (!(this->*mv)().first) { sound(0x08); return; }
    put(0x06);
    (this->*shoot)();
}

// MARK: - Działa

void Engine::rotatingGun() {               // $2FD2
    if ((ticks & 3) == 0 && (random() & 3) == 0) {
        int a = take();
        if ((random() & 1) == 0) {
            a = (a + 1) & 0xFF;
            if (a >= 0x30) a = 0x2C;
        } else {
            a = (a - 1) & 0xFF;
            if (a < 0x2C) a = 0x2F;
        }
        put(a);
        return;
    }
    if ((random() & 7) != 0) return;
    switch ((get() - 0x2C) & 3) {
    case 0: shootRight(); break;
    case 1: shootDown(); break;
    case 2: shootLeft(); break;
    default: shootUp(); break;
    }
}

void Engine::movingGun(bool right) {       // $3032 (0D) / $301F (0E)
    if (ticks & 1) {
        if (right) {
            if (mvRight().first) put(0x0E);
        } else {
            if (mvLeft().first) put(0x0D);
        }
    }
    if (gunFires()) { sound(1); shootUp(); }
}

// MARK: - Magnesy

void Engine::magnet() {                    // $2D39 (28)
    if (ry != y) return;
    int yy = 0, a = 0;
    do {
        yy = (yy + 1) & 0xFF;
        a = mem(yy);
    } while (a == 0x20);
    if ((a & 0x7F) != 0x2A) return;
    m[(ptr + yy) & 0x3FF] = 0x8B;
    alive = 1;
    sound(0x0E);
}

void Engine::pulled(MoveFn mv) {           // $2D5D / $2D75
    alive = 1;
    if (!(this->*mv)().first) return;
    put(0x76);
    alive = 0;
    sound(0x0A);
}

// MARK: - Potwory

/// $3227: potwór obok Robbo zabija go.
void Engine::check() {
    static const int d[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
    for (const auto& dd : d) {
        if (look(dd[0], dd[1]).second == 0x2A) {
            x = (x + dd[0]) & 0xFF; y = (y + dd[1]) & 0xFF;
            sound(0x0A);
            put(0x61);
            back();
            return;
        }
    }
}

void Engine::bear(MoveFn m1, int a, MoveFn m2, int b) {
    check();
    if (!(this->*m1)().first) {
        put(a);
        check();
        return;
    }
    if (!(this->*m2)().first) { check(); return; }
    put(b);
}

void Engine::bird(MoveFn mv, int turn) {
    check();
    if (!(this->*mv)().first) { check(); return; }
    put(turn);
}

void Engine::shootingBird(MoveFn mv, int turn) {   // $31B5 / $31C5
    check();
    if ((this->*mv)().first) put(turn);
    check();
    if ((random() & 7) == 0) shootDown();
}

void Engine::eyes() {                      // $31E0 (26)
    check();
    if ((random() & 1) == 0) {
        if (y != ry) {
            const bool blocked = y > ry ? mvUp().first : mvDown().first;
            if (!blocked) return;
        }
        if (x != rx) {
            if (x > rx) mvLeft(); else mvRight();
            return;
        }
    }
    if (random() & 0x20) return;
    const int i = random() & 3;
    const int d = dir;
    dir = t.dirs[i];
    mvDir();
    dir = d;
}

// MARK: - Różne

void Engine::barrier() {                   // $3045 (11)
    get();
    int hold = mem(1);
    if (hold != 0x0F) hold = 0x20;
    get();
    int yy = 0;
    while (true) {
        yy = (yy + 1) & 0xFF;
        x = (x + 1) & 0xFF;
        if (mem(yy) == 0x0F) {
            x = (x - 1) & 0xFF;
            yy = (yy - 1) & 0xFF;
            if (yy != 0) draw(0x0F, yy);
            yy = (yy + 1) & 0xFF;
            x = (x + 1) & 0xFF;
            draw(0x20, yy);
        }
        if (mem(yy) == 0x05) break;
    }
    yy = (yy - 1) & 0xFF;
    x = (x - 1) & 0xFF;
    if (hold == 0x0F) draw(0x0F, yy);
}

void Engine::capsule() {                   // $3268 (14)
    if (screws != 0) return;
    sound(0x0D);
    flash = 3;
    put(0x15);
}

void Engine::capsuleOn() {                 // $328D (15, 16)
    if (ticks & 3) return;
    put(get() ^ 0x03);
}

void Engine::questionMark() {              // $2C03 (3A)
    const int v = t.qmark[random() & 0x1F];
    if (v != 0) { put(v); return; }
    put(0x2B);
    for (int yy = 0x1E; yy >= 0; --yy) {
        for (int xx = 0x0F; xx >= 0; --xx) {
            x = xx; y = yy;
            const int a = get() & 0x7F;
            if (a == 0x28 || a == 0x29) {
                put(0xA0);
            } else if (a == 0x7C || a == 0x12 || a == 0x26 || (a >= 0x41 && a < 0x4D)) {
                put(0xE1);
            }
        }
    }
    x = 0xFF; y = 0xFF;
    addScore(0, 5);
    sound(0x0D);
}

void Engine::bomb() {                      // $2CCE (60)
    sound(0);
    x = (x - 1) & 0xFF; y = (y - 1) & 0xFF;
    get();
    const int base = ptr;
    for (int i = 8; i >= 0; --i) {
        const int off = t.bombOff[i];
        int a = m[(base + off) & 0x3FF];
        if (a == 0xA0) continue;
        a &= 0x7F;
        if (i != 4 && (t.props[a] & 2) == 0) continue;
        m[(base + off) & 0x3FF] = a == 0x40 ? 0xE0 : t.bombVal[i];
    }
    x = (x + 1) & 0xFF; y = (y + 1) & 0xFF;
    get();
}

void Engine::robboAppear() {               // $289E (1B)
    if (isScrolling(false)) return;
    if (isScrolling(true)) return;
    sound(0x0B);
    put(0x6A);
}

void Engine::teleport() {                  // $2A99 (30-39)
    if (get() != v9b) return;
    put(0x69);
    if (mvDir().first) {
        const int d0 = dir;
        int yy = (firstZero(d0) + 1) * 4 - 1;
        bool ok = false;
        while (yy >= 0) {
            dir = t.dirs[yy & 15];
            if (!mvDir().first) { ok = true; break; }
            yy -= 1;
        }
        dir = d0;
        if (!ok) { put(v9b); return; }
    }
    stay();
    back();
    const int a = v9b;
    v9b = 1;
    put(a);
}

// MARK: - Procedury przebiegu gry

/// $2563 / $2714: kolumna wypełniona kodem $13 (od dołu do góry).
void Engine::fillColumn(int col, int rowFrom, int rowTo) {
    for (int row = rowTo; row >= rowFrom; --row) {
        x = col; y = row;
        put(0x13);
    }
}

/// $25A8: wybuch wszystkiego (bezpośredni zapis do mapy, bez rysowania).
void Engine::explodeAll() {
    for (int yy = 0x1F; yy >= 0; --yy) {
        for (int xx = 0x0F; xx >= 0; --xx) {
            x = xx; y = yy;
            const int a = get() & 0x7F;
            if (a == 0x20 || a == 0x13 || a == 0x11 || a == 0x05) continue;
            const int carry = a > 0x05 ? 1 : 0;
            m[ptr & 0x3FF] = static_cast<uint8_t>(((random() & 3) + 0x61 + carry) & 0xFF);
        }
    }
}
