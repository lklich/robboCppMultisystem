//
//  Game.h
//  Robbo (C++ / SDL3)
//
//  Przebieg oryginalnej gry Robbo (LK Avalon 1989): ekran tytułowy z instrukcją
//  pisaną na maszynie ($A800), plansze ($2400-$25A8), śmierć, demo, zakończenie ($A9AE)
//  oraz przerwanie pionowe ($3386: dźwięk, pasek statusu, przewijanie). Wszystko liczone
//  w ramkach 50 Hz, jak na Atari PAL.
//

#pragma once

#include "AtariData.h"
#include "Engine.h"
#include "Pokey.h"
#include "Renderer.h"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

enum class GameKey { up, down, left, right, fire, shift, escape, start, option, breakKey };

/// Pełny stan rozgrywki (silnik + przebieg gry) – do zapisów.
struct GameSnapshot {
    std::vector<uint8_t> m;
    std::vector<uint8_t> scr;
    int x = 0, y = 0, sx = 0, sy = 0, rx = 0, ry = 0;
    int dir = 0, v9b = 0, alive = 0, level = 0, keys = 0, lives = 0, screws = 0, lifeLevel = 0, ammo = 0;
    std::array<int, 3> score{0, 0, 0};
    int moving = 0, ticks = 0, robboAnim = 0, cooldown = 0, robboScreenAddr = 0;
    int cdtmv3 = 0, cdtmv4 = 0, screenTop = 0, vscrol = 0, scroll99 = 0, scroll9A = 0;
    std::vector<uint8_t> shadow;
    int checkpoint = 0;
};

class Game {
public:
    const AtariData& data;
    Renderer renderer;
    EngineTables tables;
    Engine engine;

    Game(const AtariData& data, Pokey& pokey);

    bool isOnTitle() const { return phase_.kind == Phase::title; }

    // Klawiatura
    void setKey(GameKey key, bool pressed);
    /// Litery wpisywane na ekranie tytułowym (sekretne hasło autora). Zwraca true,
    /// gdy znak został „zjedzony” (spacja w środku hasła nie uruchamia gry).
    bool titleTyped(char ch);
    /// Puszcza wszystkie klawisze (np. gdy okno traci fokus), żeby Robbo nie szedł sam.
    void releaseAllKeys();

    /// Jedna ramka 50 Hz (logika + obraz w renderer).
    void frame();

    // Zapisy i wybór planszy
    bool canSave() const { return phase_.kind == Phase::play && !engine.demo; }
    int currentLevelNumber() const { return engine.level + 1; }
    bool makeSnapshot(GameSnapshot& out) const;
    void restore(const GameSnapshot& s);
    /// Nowa gra od wybranej planszy (0 = pierwsza): wynik od zera, 8 żyć.
    void startAt(int levelIndex);

    /// $275E: animowane znaki z banków $1800/$1A00 i klatka Robbo zależna od kierunku.
    static void animateCharset(std::vector<uint8_t>& cs, const std::vector<uint8_t>& banks, int ticks,
                               int robboAnim, int dir);

private:
    struct Phase {
        enum Kind { title, loadLevel, play, dying, levelDone, leaveDemo, ending } kind = title;
        int a = 0;   // column / stage / frames
        int b = 0;   // wait
    };

    struct EndOp {
        int wait = -1;                 // >= 0: czekanie, -1: akcja
        std::function<void()> act;
    };

    Pokey& pokey_;

    // Sprzęt i system
    int rtclok_ = 0;              // $14
    int cdtmv3_ = 0;              // $21C/$21D – rytm tur gry, maszyna do pisania w menu
    int cdtmv4_ = 0;              // $21E/$21F – czekanie po śmierci, czas demo
    int screenTop_ = 0;           // $36BB/$36BC – LMS pola gry (względem $1000)
    int vscrol_ = 0;              // VSCROL
    int scroll99_ = 0;            // $99
    int scroll9A_ = 0;            // $9A
    std::vector<uint8_t> shadow_ = std::vector<uint8_t>(7, 0);   // $2C3-$2C9: tło planszy, COLOR0-4
    int flashFrames_ = 0;         // błysk tła przy aktywacji kapsuły
    bool screenOn_ = false;       // SDMCTL
    std::vector<uint8_t> charset_;   // $1C00-$1FFF
    std::vector<uint8_t> font_;      // $2000-$23FF (znak $60 obraca się w przerwaniu)

    // dźwięk ($F0-$F7)
    int soundCount_[4] = {0, 0, 0, 0};
    int soundPtr_[4] = {0, 0, 0, 0};

    // Ekran tytułowy
    bool firstRun_ = true;        // $A806
    int checkpoint_ = 0;          // $A989 – plansza startowa (zaokrąglana do 4)
    int demoLevel_ = 0;           // $AB
    int cursor_ = 0;              // $AA
    int textPtr_ = 0xAE13;        // $84/$85
    int maskHi_ = 0xFA, maskLo_ = 0x5A;   // $AC83/$AC84 – wzór pasków logo
    int maskDir_ = 0;             // $AC52
    std::vector<uint8_t> typed_ = std::vector<uint8_t>(11, 0);   // $ACB9-$ACC3
    std::vector<uint8_t> credit_;

    // Zakończenie
    std::vector<EndOp> endOps_;
    size_t endIndex_ = 0;
    int endWait_ = 0;
    int endMode_ = 0;             // 0 scena z rakietą, 1 napis, 2 odsłanianie, 3 wygaszanie
    int e92_ = 0, e93_ = 0xFF, e96_ = 0xFF, e97_ = 0, eA8_ = 0;
    int flashX_ = 0;

    Phase phase_;

    // Wejście
    int stick_ = 0x0F;            // bity: 0 góra, 1 dół, 2 lewo, 3 prawo (0 = wciśnięty)
    bool spaceFire_ = false;
    bool shiftFire_ = false;
    bool fire() const { return spaceFire_ || shiftFire_; }
    bool escHeld_ = false;
    bool startHeld_ = false;
    bool optionHeld_ = false;
    bool breakPressed_ = false;

    void setStickBit(int bit, bool pressed);
    void vbi();
    bool scrollWouldStart() const;
    void scrollVBI();
    void playSound(int n);
    void updateSound();
    void setWalls();
    void animateCharset();

    void enterTitle();
    void titleFrame();
    void typewriter();
    void newline();
    void drawLogo();
    void rotateMaskLeft();
    void rotateMaskRight();

    void startGame(bool demo);
    void initGame();
    void startLevel();
    void loadFrame(int column, int wait);
    void runTick();
    void playFrame();
    void beginDeath();
    void dyingFrame(int stage);
    void beginLevelDone();
    void levelDoneFrame(int column, int wait);

    void startEnding();
    void endingFrame();
    void drawEndingScene();
    void endTile(int x, int y, int a);

    void render();
    void renderGame();
    void statusRows(std::vector<uint8_t>& top, std::vector<uint8_t>& bottom) const;
    void renderTitle();
    void renderEnding();
};
