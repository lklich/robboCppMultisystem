//
//  Engine.h
//  Robbo (C++ / SDL3)
//
//  Wierne odwzorowanie silnika oryginalnego Robbo (LK Avalon 1989, Janusz Pelc).
//  Każda funkcja odpowiada procedurze 6502 (adres w komentarzu). Mapa planszy leży
//  pod $0500-$06FF (16 × 32, wiersz 31 to meta), pamięć ekranu pod $1000-$17FF
//  (kafel = 2 × 2 znaki, wiersz kafli = 64 bajty).
//
//  Port 1:1 z wersji Swift (Engine.swift).
//

#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

class AtariData;

struct EngineTables {
    std::vector<uint8_t> props;     // $3637 – właściwości kodów (bit 0: można zniszczyć, bit 1: bomba niszczy)
    std::vector<uint8_t> anim;      // $355B – następny kod animacji dla $61-$7A
    std::vector<uint8_t> chars;     // $34DB – kod obiektu -> pierwszy znak kafla
    std::vector<uint8_t> qmark;     // $2C5B – zawartość znaku zapytania
    std::vector<uint8_t> dirs;      // $2AE9 – tablica kierunków
    std::vector<uint8_t> bombOff;   // $2D17 – przesunięcia pól wybuchu
    std::vector<uint8_t> bombVal;   // $2D20 – kody wybuchu
    std::vector<uint8_t> box;       // $2BD5 – skrzynia pchnięta w kierunku 0-3

    explicit EngineTables(const AtariData& data);
};

class Engine {
public:
    /// Wynik ruchu: (zablokowany, A).
    using Move = std::pair<bool, int>;
    using MoveFn = Move (Engine::*)();
    using ShootFn = void (Engine::*)();

    const EngineTables& t;

    // Połączenia ze „sprzętem”
    std::function<int()> random;                 // RANDOM ($D20A)
    std::function<void(int)> sound;              // JSR $B800
    std::function<int()> input;                  // $2941 & STICK0 & STICK1
    std::function<bool()> firePressed;           // $2891 == 0
    std::function<bool(bool)> isScrolling;       // $99 != 0 (true = po odczekaniu ramki)

    // Pamięć
    std::vector<uint8_t> m = std::vector<uint8_t>(0x400, 0);    // $0500-$08FF
    std::vector<uint8_t> scr = std::vector<uint8_t>(0x800, 0);  // $1000-$17FF
    int ptr = 0;                    // $80/$81 względem $0500
    int scrAddr = 0;                // $82/$83 względem $1000
    int regX = 0;                   // rejestr X po procedurze rysowania

    // Zmienne strony zerowej i komórki w kodzie
    int x = 0, y = 0;               // $92 $93
    int sx = 0, sy = 0;             // $94 $95
    int rx = 0, ry = 0;             // $96 $97
    int dir = 0x0D;                 // $98
    int v9b = 1;                    // $9B
    int alive = 0;                  // $9C (0 brak Robbo, 1 żyje, $80 odlot)
    int level = 0;                  // $9D
    int keys = 0;                   // $9E (BCD)
    int lives = 8;                  // $9F (BCD)
    int screws = 0;                 // $A0 (BCD)
    int lifeLevel = 0xFF;           // $A1
    int ammo = 0;                   // $A2 (BCD)
    bool demo = false;              // $A3
    std::array<int, 3> score{0, 0, 0};   // $A4 $A5 $A6 (BCD)
    int moving = 0;                 // $A8
    int ticks = 0;                  // $266B
    int robboAnim = 0;              // $275D
    int cooldown = 0;               // $2940
    int robboScreenAddr = 0;        // $86/$87 względem $1000

    // Zdarzenia dla warstwy gry
    bool stepClick = false;
    int flash = 0;

    explicit Engine(const EngineTables& tables);

    // BCD
    static std::pair<int, int> bcdAdd(int a, int b, int c = 0);
    /// SED SEC SBC: (wynik, C) – C = 0 oznacza pożyczkę.
    static std::pair<int, int> bcdSub(int a, int b);
    static int firstZero(int v);

    // Pamięć
    int get();
    int mem(int offset) const { return m[(ptr + offset) & 0x3FF]; }
    void draw(int a, int yOff = 0);
    void put(int a);
    int take();

    Move mvRight();
    Move mvLeft();
    Move mvDown();
    Move mvUp();
    Move mvDir();
    std::pair<int, int> look(int dx, int dy);
    void addScore(int a, int xx);

    void tick();
    void endTick();
    void stay();
    void afterMove();

    void fillColumn(int col, int rowFrom, int rowTo);
    void explodeAll();

private:
    void handle(int c);
    void robbo();
    void walk();
    void back() { x = sx; y = sy; }
    void interact(int a);
    void takeMove();

    void hit(int p, int code);
    void questionHit();
    bool gunFires();
    void shootLeft();
    void shootRight();
    void shootUp();
    void shootDown();
    void shot(MoveFn mv, ShootFn shoot);
    void laser(int dx, int dy, int val);
    void laserMove(MoveFn mv, int dx, int dy, int head, int beam, int val);
    bool blastCheck(int xx);
    void blastLook(int dx, int dy, int val);
    void blasterGun(int dx, int dy, int val);
    void blasterMove(MoveFn mv, int dx, int dy, int val);
    void roll(MoveFn mv, ShootFn shoot);
    void rotatingGun();
    void movingGun(bool right);
    void magnet();
    void pulled(MoveFn mv);
    void check();
    void bear(MoveFn m1, int a, MoveFn m2, int b);
    void bird(MoveFn mv, int turn);
    void shootingBird(MoveFn mv, int turn);
    void eyes();
    void barrier();
    void capsule();
    void capsuleOn();
    void questionMark();
    void bomb();
    void robboAppear();
    void teleport();
};
