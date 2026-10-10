//
//  Game.cpp
//  Robbo (C++ / SDL3)
//
//  Port 1:1 z Game.swift.
//

#include "Game.h"

#include <algorithm>
#include <cctype>
#include <climits>
#include <cstring>

namespace {
const char kSecret[] = "JANUSZ PELC";   // $ACC4
}

Game::Game(const AtariData& d, Pokey& pokey)
    : data(d), renderer(d.palette), tables(d), engine(tables), pokey_(pokey) {
    charset_ = data.charset();
    font_ = data.font();
    credit_ = AtariData::encode("Konwersja: Leszek Klich");
    engine.sound = [this](int n) { playSound(n); };
    engine.input = [this] { return stick_ & 0x0F; };
    engine.firePressed = [this] { return fire(); };
    engine.isScrolling = [this](bool afterFrame) {
        if (scroll99_ != 0) return true;
        return afterFrame && scrollWouldStart();
    };
    enterTitle();
}

// MARK: - Klawiatura

void Game::setKey(GameKey key, bool pressed) {
    switch (key) {
    case GameKey::up: setStickBit(0, pressed); break;
    case GameKey::down: setStickBit(1, pressed); break;
    case GameKey::left: setStickBit(2, pressed); break;
    case GameKey::right: setStickBit(3, pressed); break;
    case GameKey::fire: spaceFire_ = pressed; break;
    case GameKey::shift: shiftFire_ = pressed; break;
    case GameKey::escape: escHeld_ = pressed; break;
    case GameKey::start: startHeld_ = pressed; break;
    case GameKey::option: optionHeld_ = pressed; break;
    case GameKey::breakKey: if (pressed) breakPressed_ = true; break;
    }
}

void Game::setStickBit(int bit, bool pressed) {
    if (pressed) stick_ &= ~(1 << bit); else stick_ |= (1 << bit);
}

bool Game::titleTyped(char ch) {
    if (!isOnTitle()) return false;
    const uint8_t a = static_cast<uint8_t>(std::toupper(static_cast<unsigned char>(ch)));
    const bool middle = std::memcmp(typed_.data() + 5, "JANUSZ", 6) == 0;
    if (a != typed_[10]) {
        typed_.erase(typed_.begin());
        typed_.push_back(a);
    }
    return ch == ' ' && middle;
}

void Game::releaseAllKeys() {
    stick_ = 0x0F;
    spaceFire_ = false;
    shiftFire_ = false;
    escHeld_ = false;
    startHeld_ = false;
    optionHeld_ = false;
    breakPressed_ = false;   // RESET wciśnięty wcześniej (np. na ekranie tytułowym) nie może przerwać wczytanej gry
}

// MARK: - Ramka 50 Hz

void Game::frame() {
    vbi();
    switch (phase_.kind) {
    case Phase::title: titleFrame(); break;
    case Phase::loadLevel: loadFrame(phase_.a, phase_.b); break;
    case Phase::play: playFrame(); break;
    case Phase::dying: dyingFrame(phase_.a); break;
    case Phase::levelDone: levelDoneFrame(phase_.a, phase_.b); break;
    case Phase::leaveDemo:
        if (phase_.a > 0) phase_.a -= 1; else enterTitle();
        break;
    case Phase::ending: endingFrame(); break;
    }
    render();
}

/// Przerwanie pionowe: zegar i liczniki systemu, potem procedura gry $3386.
void Game::vbi() {
    rtclok_ = (rtclok_ + 1) & 0xFF;
    if (cdtmv3_ > 0) cdtmv3_ -= 1;
    if (cdtmv4_ > 0) cdtmv4_ -= 1;
    if (flashFrames_ > 0) flashFrames_ -= 1;
    updateSound();                                         // $B803
    const uint8_t last = font_[0x307];                     // $2300-$2307: znak $60
    for (int i = 6; i >= 0; --i) font_[0x300 + i + 1] = font_[0x300 + i];
    font_[0x300] = last;
    scrollVBI();
}

// MARK: - Przewijanie ($33FB-$34C0)

bool Game::scrollWouldStart() const {
    const int robbo = engine.robboScreenAddr;
    if (screenTop_ + 0x80 >= robbo) return screenTop_ >= 0x001;
    return screenTop_ + 0x200 < robbo && screenTop_ < 0x540;
}

void Game::scrollVBI() {
    if (scroll99_ == 0) {
        const int robbo = engine.robboScreenAddr;
        if (screenTop_ + 0x80 >= robbo) {
            if (screenTop_ < 0x001) return;
            scroll9A_ = 0x0E;
            scroll99_ = 0x82;
        } else {
            if (screenTop_ + 0x200 >= robbo) return;
            if (screenTop_ >= 0x540) return;
            scroll9A_ = 0x02;
            scroll99_ = 0x42;
        }
    }
    if (scroll99_ & 0x80) {
        if (scroll9A_ == 0) {
            vscrol_ = 0;
            scroll9A_ = 0x0E;
            const int old = scroll99_;
            scroll99_ = (scroll99_ - 1) & 0xFF;
            if ((old & 0x0F) == 0) scroll99_ = 0;
            return;
        }
        const int a = scroll9A_;
        scroll9A_ -= 2;
        vscrol_ = a & 7;
        if ((a & 7) != 6) return;
        screenTop_ -= 32;
        vscrol_ = 6;
        if (scroll9A_ != 0) return;
    }
    const int a = scroll9A_;
    scroll9A_ += 2;
    vscrol_ = a & 7;
    if (vscrol_ != 0) return;
    screenTop_ += 32;
    if (scroll9A_ < 0x10) return;
    const int old = scroll99_;
    scroll99_ = (scroll99_ - 1) & 0xFF;
    if ((old & 0x0F) == 0) scroll99_ = 0;
    scroll9A_ = 2;
}

// MARK: - Dźwięk ($B800, $B84B)

void Game::playSound(int n) {
    if (n & 0x80) {
        for (int i = 0; i < 4; ++i) soundCount_[i] = 0;
        pokey_.silence();
        return;
    }
    const int ch = std::min(n, 3);
    soundCount_[ch] = 0x10;
    soundPtr_[ch] = 0xB896 + n * 32;
}

void Game::updateSound() {
    if ((rtclok_ & 3) != 0) return;
    for (int ch = 3; ch >= 0; --ch) {
        if (soundCount_[ch] == 0) {
            pokey_.write(ch, 0, 0);
            continue;
        }
        soundCount_[ch] -= 1;
        const int i = soundCount_[ch] * 2;
        const int audc = data.byte(soundPtr_[ch] + i);
        const int audf = data.byte(soundPtr_[ch] + i + 1);
        pokey_.write(ch, audf, audc);
    }
}

// MARK: - Zestaw znaków ($26F0, $275E)

void Game::setWalls() {
    const int style = (engine.level >> 2) & 15;
    const auto a = data.wallsA(), b = data.wallsB();
    for (int i = 0; i < 16; ++i) {
        charset_[i] = a[style * 16 + i];
        charset_[0x100 + i] = b[style * 16 + i];
    }
}

void Game::animateCharset(std::vector<uint8_t>& cs, const std::vector<uint8_t>& banks, int ticks, int robboAnim,
                          int dir) {
    const int bank = (ticks & 2) * 256;
    for (int y = 0xE0; y <= 0xFF; ++y) {
        cs[y] = banks[bank + y];
        cs[0x100 + y] = banks[bank + 0x100 + y];
    }
    int yy = 0xDF;
    for (int xx = 0xAF; xx >= 0x50; --xx) {
        cs[xx] = banks[bank + yy];
        cs[0x100 + xx] = banks[bank + 0x100 + yy];
        yy -= 1;
    }
    const int rb = (robboAnim & 1) * 0x200;
    int d = dir & 0xFF, yIdx = 4;
    for (int k = 0; k < 9; ++k) {
        yIdx -= 1;
        const int c = d & 1;
        d >>= 1;
        if (c == 0) break;
    }
    const int base = (((yIdx & 0xFF) << 4) & 0xFF) + 0x4F;
    for (int xx = 0; xx <= 15; ++xx) {
        const int src = (base - 15 + xx) & 0xFF;
        cs[0x2F0 + xx] = banks[rb + src];
        cs[0x3F0 + xx] = banks[rb + 0x100 + src];
    }
}

void Game::animateCharset() {
    animateCharset(charset_, data.animBanks(), engine.ticks, engine.robboAnim, engine.dir);
}

// MARK: - Ekran tytułowy ($A87B)

void Game::enterTitle() {
    int a = engine.level;
    if (engine.demo) {
        engine.demo = false;
        a = demoLevel_;
    }
    checkpoint_ = a;
    engine.level = a;
    // $A807
    screenOn_ = false;
    cdtmv3_ &= 0xFF;
    if (firstRun_) {
        firstRun_ = false;
        engine.level = 0;
        demoLevel_ = 0;
        engine.demo = false;
        checkpoint_ = 0;
    }
    maskHi_ = 0xFA; maskLo_ = 0x5A;
    const auto colors = data.bytes(0xACAD, 6);
    for (int i = 0; i < 6; ++i) shadow_[i] = colors[i];
    textPtr_ = 0xAE13;
    cdtmv3_ = 0xFF;
    typed_ = data.bytes(0xACB9, 11);
    typed_[0] = 0xFF;
    cursor_ = 0;
    std::fill_n(engine.scr.begin(), 0x400, 0);
    screenOn_ = true;
    escHeld_ = false;
    breakPressed_ = false;
    phase_ = {Phase::title, 0, 0};
}

void Game::titleFrame() {
    drawLogo();                                            // $ABD3
    pokey_.write(0, 0x10, 0);                              // STA AUDC1 = 0
    engine.scr[(0x160 + cursor_) & 0x7FF] = static_cast<uint8_t>((rtclok_ << 4) & 0x80);
    if (cdtmv3_ == 0) typewriter();
    // $A967: OPTION albo upływ czasu po końcu instrukcji -> demo
    if ((cdtmv3_ >> 8) == 1 || optionHeld_) {
        demoLevel_ = engine.level;
        startGame(true);
        return;
    }
    // $AC94: strzał, SHIFT lub START -> gra
    if (fire() || startHeld_) {
        startGame(false);
    }
}

/// $A8DE-$A941: instrukcja wypisywana znak po znaku w dolnej części ekranu.
void Game::typewriter() {
    if (cursor_ >= 0x20) {
        textPtr_ += cursor_;
        newline();
        return;
    }
    int c = data.byte(textPtr_ + cursor_);
    cdtmv3_ = (c & 3) + 2;
    pokey_.write(0, 0x10, 0x82);
    if (c == 0xFF) {
        cdtmv3_ |= 0x400;
        textPtr_ = 0xAE10;
        c = 0x40;
    }
    if (c == 0x40) {
        engine.scr[0x160 + cursor_] = 0;
        textPtr_ += cursor_ + 1;
        newline();
        return;
    }
    engine.scr[0x160 + cursor_] = static_cast<uint8_t>(c);
    cursor_ += 1;
}

void Game::newline() {
    cdtmv3_ = (cdtmv3_ & 0xFF00) | 0x0F;
    pokey_.write(0, 0x02, 0xC4);
    for (int y = 0; y < 256; ++y) engine.scr[0x80 + y] = engine.scr[0xA0 + y];
    cursor_ = 0;
}

/// $ABD3: kolory logo i litery „Robbo” z czcionki, z przesuwającym się wzorem pasków.
void Game::drawLogo() {
    for (int i = 3; i >= 0; --i) {
        if ((engine.random() & 0x1F) == 0) {
            shadow_[1 + i] = static_cast<uint8_t>((shadow_[1 + i] + 0x10) & 0xFF);
        }
    }
    const auto letters = data.bytes(0xAC7E, 5);
    for (int letter = 4; letter >= 0; --letter) {
        int dst = letter * 3 + 1;
        int src = letters[letter] * 8;
        for (int k = 0; k < 8; ++k) {
            const int g = font_[0x3E8 + (src & 0xFF)];
            int w = 0;
            for (int bit = 0; bit < 8; ++bit) {
                const int b = (g >> (7 - bit)) & 1;
                w = (w << 2) | (b << 1) | b;
            }
            engine.scr[dst & 0x7FF] = static_cast<uint8_t>((w >> 8) & maskHi_);
            engine.scr[(dst + 1) & 0x7FF] = static_cast<uint8_t>(w & 0xFF & maskLo_);
            rotateMaskLeft();
            dst += 16;
            src += 1;
        }
    }
    if ((rtclok_ & 3) != 0) return;
    if ((engine.random() & 0x1F) == 0) maskDir_ = (maskDir_ + 1) & 0xFF;
    if (maskDir_ & 1) rotateMaskLeft(); else rotateMaskRight();
}

void Game::rotateMaskLeft() {
    for (int k = 0; k < 2; ++k) {
        const int v = (maskHi_ << 8) | maskLo_;
        const int r = ((v << 1) | (v >> 15)) & 0xFFFF;
        maskHi_ = r >> 8; maskLo_ = r & 0xFF;
    }
}

void Game::rotateMaskRight() {
    for (int k = 0; k < 2; ++k) {
        const int v = (maskHi_ << 8) | maskLo_;
        const int r = ((v >> 1) | ((v & 1) << 15)) & 0xFFFF;
        maskHi_ = r >> 8; maskLo_ = r & 0xFF;
    }
}

// MARK: - Początek gry ($A988, $2400)

void Game::startGame(bool demo) {
    engine.demo = demo;
    engine.level = checkpoint_ & 0xFC;
    engine.lives = std::memcmp(typed_.data(), kSecret, 11) == 0 ? 0x80 : 8;
    cdtmv3_ = 0;
    initGame();
}

/// $2400: wynik od zera, plansza zasłonięta, potem start planszy.
void Game::initGame() {
    screenOn_ = false;
    scroll99_ = 0;
    engine.score = {0, 0, 0};
    animateCharset();
    for (int col = 0x0F; col >= 0; --col) engine.fillColumn(col, 0, 0x1F);
    engine.lifeLevel = engine.regX;
    screenTop_ = 0;
    vscrol_ = 0;
    engine.robboScreenAddr = 0;
    startLevel();
}

/// $2466: start (lub ponowny start) planszy.
void Game::startLevel() {
    playSound(0xFF);
    if (engine.demo) cdtmv4_ = 0x500 | (cdtmv4_ & 0xFF);
    const Level& level = data.levels[std::min<int>(engine.level, static_cast<int>(data.levels.size()) - 1)];
    std::copy_n(level.bytes.begin(), 512, engine.m.begin());
    engine.screws = engine.m[0x1F1];
    for (int i = 0; i < 5; ++i) shadow_[1 + i] = engine.m[0x1F2 + i];
    shadow_[0] = engine.m[0x1F7];
    engine.v9b = 1;
    engine.keys = 0;
    engine.ammo = 0;
    engine.moving = 0;
    engine.dir = 0x0D;
    escHeld_ = false;
    screenOn_ = true;
    phase_ = {Phase::loadLevel, 17, 1};
}

// MARK: - Odsłanianie planszy kolumnami ($26F0)

void Game::loadFrame(int column, int wait) {
    if (wait > 0) {
        phase_ = {Phase::loadLevel, column, wait - 1};
        return;
    }
    int col = column;
    if (col < 0) {
        phase_ = {Phase::play, 0, 0};
        return;
    }
    if (col == 17) {
        setWalls();
        for (int x = 0x0F; x >= 0; --x) {
            engine.x = x; engine.y = 0x1F;
            engine.put(0x13);
        }
        col = 16;
    }
    for (int row = 0; row < 0x1F; ++row) {
        engine.x = col; engine.y = row;
        int a = engine.get();
        if (a == 0x2A) {
            engine.stay();
            a = 0x1B;
        }
        if (a == 0x2B && engine.level == engine.lifeLevel) a = 0xE1;
        engine.x = col; engine.y = row;
        engine.put(a);
    }
    animateCharset();
    if (col & 1) engine.ticks = (engine.ticks + 1) & 0xFF;
    phase_ = {Phase::loadLevel, col - 1, 2};
}

// MARK: - Pętla główna ($2505)

void Game::runTick() {
    engine.tick();
    if (engine.flash > 0) { flashFrames_ = engine.flash; engine.flash = 0; }
    animateCharset();
    if (engine.stepClick) {
        engine.stepClick = false;
        pokey_.click();
    }
}

void Game::playFrame() {
    if (breakPressed_) {
        breakPressed_ = false;
        enterTitle();
        return;
    }
    if (cdtmv3_ != 0) return;
    cdtmv3_ = 7;
    engine.alive = 0;
    runTick();
    if (engine.v9b != 0) engine.alive = engine.v9b;
    if (engine.demo) {
        if (engine.alive == 0) { beginDeath(); return; }
        if (fire()) {
            phase_ = {Phase::leaveDemo, 17, 0};
            return;
        }
        if ((cdtmv4_ >> 8) != 0) return;
        beginLevelDone();
        return;
    }
    if (escHeld_) { beginDeath(); return; }
    if (engine.alive & 0x80) { beginLevelDone(); return; }
    if (engine.alive != 0) return;
    beginDeath();
}

// MARK: - Śmierć ($2542, $25A8)

void Game::beginDeath() {
    engine.moving = 0;
    cdtmv4_ = 0x30;
    phase_ = {Phase::dying, 0, 0};
}

void Game::dyingFrame(int stage) {
    if (breakPressed_) {
        breakPressed_ = false;
        enterTitle();
        return;
    }
    if (cdtmv3_ != 0) return;
    cdtmv3_ = 7;
    runTick();
    if ((cdtmv4_ & 0xFF) != 0) return;
    if (stage == 0) {
        playSound(0);
        engine.explodeAll();
        cdtmv4_ = (cdtmv4_ & 0xFF00) | 0x30;
        phase_ = {Phase::dying, 1, 0};
        return;
    }
    if (engine.demo) { beginLevelDone(); return; }
    const auto [lives, c] = Engine::bcdSub(engine.lives, 1);
    engine.lives = lives;
    if (c == 0) {
        engine.lives = (lives + 1) & 0xFF;
        enterTitle();
        return;
    }
    startLevel();
}

// MARK: - Koniec planszy ($2563)

void Game::beginLevelDone() {
    phase_ = {Phase::levelDone, 0x0F, 0};
}

void Game::levelDoneFrame(int column, int wait) {
    if (wait > 0) {
        phase_ = {Phase::levelDone, column, wait - 1};
        return;
    }
    if (column < 0) {
        if (engine.level >= 0x37) {
            if (engine.demo) enterTitle(); else startEnding();
            return;
        }
        engine.level += 1;
        startLevel();
        return;
    }
    engine.moving = 0;
    engine.fillColumn(column, 0, 0x1F);
    animateCharset();
    if (column & 1) engine.ticks = (engine.ticks + 1) & 0xFF;
    phase_ = {Phase::levelDone, column - 1, 2};
}

// MARK: - Zakończenie ($A9AE)

void Game::startEnding() {
    endOps_.clear();
    endIndex_ = 0;
    endWait_ = 0;
    endMode_ = 0;
    std::vector<EndOp> ops;
    auto act = [&ops](std::function<void()> f) { ops.push_back({-1, std::move(f)}); };
    auto wait = [&ops](int n) { ops.push_back({n, nullptr}); };
    auto draw = [this] { drawEndingScene(); };
    auto step = [&] { wait(7); act(draw); };            // $AB10

    act([this] {
        const auto c = data.bytes(0xACB3, 6);
        for (int i = 0; i < 6; ++i) shadow_[i] = c[i];
        e93_ = 0xFF; e96_ = 0xFF;
    });
    act(draw);
    wait(51);
    act([this] { e96_ = 0x1E; e97_ = 0x0C; eA8_ = 5; playSound(5); });
    for (int k = 0; k < 9; ++k) {
        act([this] { eA8_ ^= 0x10; });
        step();
        act([this] { e96_ = (e96_ - 1) & 0xFF; });
    }
    act([this] { eA8_ = 6; });
    step();
    wait(11);
    act([this] { e92_ = 0x0C; e93_ = 0; playSound(0x0E); });
    for (int k = 0; k < 12; ++k) {
        step();
        act([this] { eA8_ ^= 0x10; e93_ += 1; });
    }
    act([this] { playSound(0x0D); });
    for (int k = 0; k < 14; ++k) {
        act([this] { eA8_ = 0x13; });
        step();
        act([this] { eA8_ = 0x12; });
        step();
    }
    act([this] { eA8_ = 5; });
    for (int k = 0; k < 9; ++k) {
        act([this] { eA8_ ^= 0x10; });
        step();
        act([this] { e96_ = (e96_ - 1) & 0xFF; });
    }
    act([this] { playSound(0x0B); });
    for (int k = 0; k < 14; ++k) {
        step();
        act([this] { e97_ = (e97_ - 1) & 0xFF; e93_ = (e93_ - 1) & 0xFF; });
    }
    act([this] { playSound(5); });
    wait(51);
    act([this] { std::fill(shadow_.begin(), shadow_.end(), 0); });
    wait(1);
    act([this] {
        endMode_ = 1;
        std::fill_n(engine.scr.begin(), 0x300, 0x60);
    });
    for (int lum = 0; lum < 15; ++lum) {
        act([this, lum] { shadow_[2] = static_cast<uint8_t>(lum); });
        wait(3);
    }
    act([this] {
        shadow_[3] = 2;
        playSound(0);
        endMode_ = 2;
    });
    endOps_ = std::move(ops);
    phase_ = {Phase::ending, 0, 0};
    endingFrame();
}

void Game::endingFrame() {
    switch (endMode_) {
    case 2:
        // $AAB7: tekst pojawia się w przypadkowych miejscach, aż do naciśnięcia strzału
        for (int k = 0; k < 10; ++k) {
            for (int page = 0; page < 3; ++page) {
                const int y = engine.random();
                engine.scr[page * 0x100 + y] = data.byte(0xB2D5 + page * 0x100 + y);
            }
        }
        if (fire() || startHeld_) {
            playSound(9);
            flashX_ = 0x74;
            endMode_ = 3;
        }
        return;
    case 3:
        flashX_ -= 1;
        if (flashX_ < 0) {
            playSound(0x0D);
            engine.level = 0;
            enterTitle();
        }
        return;
    default:
        break;
    }
    while (endIndex_ < endOps_.size()) {
        if (endWait_ > 0) {
            endWait_ -= 1;
            return;
        }
        // kopia – akcja może zmienić wektor (np. powrót do menu)
        const EndOp op = endOps_[endIndex_];
        endIndex_ += 1;
        if (op.wait >= 0) endWait_ = op.wait; else op.act();
    }
}

/// $AB15: scena końcowa – podłoga, kamienie, Robbo i rakieta.
void Game::drawEndingScene() {
    std::fill_n(engine.scr.begin(), 0x400, 0);
    for (int x = 0x1E; x >= 0; x -= 2) endTile(x, 0x0E, 0x90);
    const auto stones = data.bytes(0xB555, 34);
    for (int i = 0x20; i >= 0; i -= 2) endTile(stones[i], stones[i + 1], 0);
    endTile(e96_, e97_, eA8_);
    const int blink = (rtclok_ << 3) & 0x80;
    endTile(e92_, e93_, blink | 2);
    endTile(e92_ + 2, e93_, blink | 3);
}

/// $AB80: kafel `a` w kolumnie znaków x i wierszu znaków y.
void Game::endTile(int x, int y, int a) {
    if ((x & 0xFF) >= 0x1F || (y & 0xFF) >= 0x0F) return;
    const int off = y * 32 + x;
    int c = (a << 1) & 0xFF;
    if (c >= 0x20) c += 0x20;
    c = (c & 0xFF) | (a & 0x80);
    engine.scr[off] = static_cast<uint8_t>(c & 0xFF);
    engine.scr[off + 1] = static_cast<uint8_t>((c + 1) & 0xFF);
    engine.scr[off + 0x20] = static_cast<uint8_t>((c + 0x20) & 0xFF);
    engine.scr[off + 0x21] = static_cast<uint8_t>((c + 0x21) & 0xFF);
}

// MARK: - Obraz

void Game::render() {
    const uint8_t bk = shadow_[5];
    renderer.fillAll(bk);
    if (!screenOn_) return;
    switch (phase_.kind) {
    case Phase::title: renderTitle(); break;
    case Phase::ending: renderEnding(); break;
    default: renderGame(); break;
    }
}

/// Display lista $36B7: pole gry w trybie 4 z przewijaniem, pasek statusu w trybie 2.
void Game::renderGame() {
    const uint8_t bk = shadow_[5];
    const uint8_t pf[4] = {shadow_[1], shadow_[2], shadow_[3], shadow_[4]};
    const uint8_t bg = flashFrames_ > 0 ? 0x0F : shadow_[0];
    for (int s = 0; s < 161; ++s) {
        const int p = s + vscrol_;
        renderer.mode4Row(21 + s, engine.scr, screenTop_ + (p >> 3) * 32, charset_, p & 7, bg, pf);
    }
    std::vector<uint8_t> top, bottom;
    statusRows(top, bottom);
    for (int line = 0; line < 8; ++line) {
        renderer.mode2Row(194 + line, Renderer::normalX, top.data(), 40, font_, line, 0x0A, bk);
        renderer.mode2Row(202 + line, Renderer::normalX, bottom.data(), 40, font_, line, 0x0A, bk);
    }
    if (engine.demo) {
        const auto text = data.bytes(0x372B, 40);
        const uint8_t pf1 = static_cast<uint8_t>((rtclok_ >> 2) & 0xFF);
        for (int line = 0; line < 8; ++line) {
            renderer.mode2Row(210 + line, Renderer::normalX, text.data(), 40, font_, line, pf1, bk);
        }
    }
}

/// Pasek statusu jak w przerwaniu $339C-$33E2.
void Game::statusRows(std::vector<uint8_t>& top, std::vector<uint8_t>& bottom) const {
    top = data.bytes(0x36DB, 40);
    bottom = data.bytes(0x3703, 40);
    auto put = [&](int v, int y) {
        top[y] = static_cast<uint8_t>(0x0B + (v & 0x0F));
        bottom[y] = static_cast<uint8_t>(0x01 + (v & 0x0F));
        top[y - 1] = static_cast<uint8_t>(0x0B + ((v >> 4) & 0x0F));
        bottom[y - 1] = static_cast<uint8_t>(0x01 + ((v >> 4) & 0x0F));
    };
    const int n = (engine.level + 1) % 100;
    put(((n / 10) << 4) | (n % 10), 0x25);
    put(engine.ammo, 0x1F);
    put(engine.keys, 0x19);
    put(engine.lives, 0x13);
    put(engine.screws, 0x0D);
    put(engine.score[2], 7);
    put(engine.score[1], 5);
    put(engine.score[0], 3);
}

/// Display lista $ACCF: logo w trybie A, napisy w trybie 2, kreski w trybie C.
void Game::renderTitle() {
    const uint8_t bk = shadow_[5];
    const uint8_t logoColors[4] = {bk, shadow_[1], shadow_[2], shadow_[3]};
    for (int k = 0; k < 8; ++k) {
        for (int sub = 0; sub < 4; ++sub) {
            renderer.modeARow(21 + k * 5 + sub, engine.scr.data() + k * 16, 16, logoColors);
        }
    }
    // kolory zmieniane przez program w trakcie rysowania ekranu (VCOUNT $24, $30, $4A)
    auto pf1 = [&](int row) -> uint8_t {
        return row < 64 ? shadow_[2] : row < 88 ? 0x0E : row < 140 ? 0x04 : 0x06;
    };
    auto pf2 = [&](int row) -> uint8_t { return row < 64 ? shadow_[3] : 0; };
    auto text = [&](int row0, const uint8_t* chars) {
        for (int line = 0; line < 8; ++line) {
            const int r = row0 + line;
            renderer.mode2Row(r, Renderer::narrowX, chars, 32, font_, line, pf1(r), pf2(r));
        }
    };
    static const int rows[6][2] = {{67, 0xAD50}, {79, 0xAD70}, {98, 0xAD90},
                                   {106, 0xADB0}, {118, 0xADD0}, {126, 0xADF0}};
    for (const auto& ra : rows) text(ra[0], data.mem.data() + ra[1]);
    const auto rule = data.bytes(0xAD40, 16);
    renderer.modeCRow(145, rule, bk, shadow_[1]);
    for (int k = 0; k < 8; ++k) text(147 + k * 8, engine.scr.data() + 0x80 + k * 32);
    renderer.modeCRow(212, rule, bk, shadow_[1]);
    for (int line = 0; line < 8; ++line) {
        renderer.mode2Row(222 + line, Renderer::narrowX, credit_.data(), 32, font_, line, 0x06, 0);
    }
}

/// Display listy $AD07 (scena w trybie 4, znaki $1800) i $AD22 (napis w trybie 2).
void Game::renderEnding() {
    if (endMode_ == 0) {
        const auto banks = data.animBanks();
        const uint8_t bk = shadow_[5];
        const uint8_t pf[4] = {shadow_[1], shadow_[2], shadow_[3], shadow_[4]};
        for (int s = 0; s < 19 * 8; ++s) {
            renderer.mode4Row(48 + s, engine.scr, (s >> 3) * 32, banks, s & 7, bk, pf);
        }
        return;
    }
    // INT_MAX / 2 – w wersji Swift Int.max + 16 przepełniało się
    const int band = endMode_ == 3 ? flashX_ * 2 - 8 : INT_MAX / 2;
    for (int s = 0; s < 24 * 8; ++s) {
        const int row = 40 + s;
        if (row >= band + 16) continue;          // poniżej paska wszystko czarne
        uint8_t pf2 = shadow_[3];
        if (row >= band) pf2 = static_cast<uint8_t>(15 - (row - band));
        const int off = (s >> 3) * 32;
        renderer.mode2Row(row, Renderer::narrowX, engine.scr.data() + off, 32, font_, s & 7, shadow_[2], pf2);
    }
}

// MARK: - Zapis stanu gry

bool Game::makeSnapshot(GameSnapshot& s) const {
    if (!canSave()) return false;
    const Engine& e = engine;
    s.m = e.m; s.scr = e.scr;
    s.x = e.x; s.y = e.y; s.sx = e.sx; s.sy = e.sy; s.rx = e.rx; s.ry = e.ry;
    s.dir = e.dir; s.v9b = e.v9b; s.alive = e.alive; s.level = e.level; s.keys = e.keys; s.lives = e.lives;
    s.screws = e.screws; s.lifeLevel = e.lifeLevel; s.ammo = e.ammo; s.score = e.score;
    s.moving = e.moving; s.ticks = e.ticks; s.robboAnim = e.robboAnim; s.cooldown = e.cooldown;
    s.robboScreenAddr = e.robboScreenAddr;
    s.cdtmv3 = cdtmv3_; s.cdtmv4 = cdtmv4_; s.screenTop = screenTop_; s.vscrol = vscrol_;
    s.scroll99 = scroll99_; s.scroll9A = scroll9A_; s.shadow = shadow_; s.checkpoint = checkpoint_;
    return true;
}

void Game::restore(const GameSnapshot& s) {
    Engine& e = engine;
    e.m = s.m; e.scr = s.scr; e.x = s.x; e.y = s.y; e.sx = s.sx; e.sy = s.sy; e.rx = s.rx; e.ry = s.ry;
    e.dir = s.dir; e.v9b = s.v9b; e.alive = s.alive; e.level = s.level; e.keys = s.keys;
    e.lives = s.lives; e.screws = s.screws; e.lifeLevel = s.lifeLevel; e.ammo = s.ammo;
    e.score = s.score; e.moving = s.moving; e.ticks = s.ticks; e.robboAnim = s.robboAnim;
    e.cooldown = s.cooldown; e.robboScreenAddr = s.robboScreenAddr;
    e.demo = false;
    cdtmv3_ = s.cdtmv3; cdtmv4_ = s.cdtmv4; screenTop_ = s.screenTop; vscrol_ = s.vscrol;
    scroll99_ = s.scroll99; scroll9A_ = s.scroll9A; shadow_ = s.shadow; checkpoint_ = s.checkpoint;
    charset_ = data.charset();
    setWalls();
    animateCharset();
    flashFrames_ = 0;
    screenOn_ = true;
    releaseAllKeys();
    playSound(0xFF);
    phase_ = {Phase::play, 0, 0};
}

void Game::startAt(int index) {
    if (index < 0 || index >= static_cast<int>(data.levels.size())) return;
    releaseAllKeys();
    engine.demo = false;
    engine.level = index;
    engine.lives = 8;
    cdtmv3_ = 0;
    initGame();
}
