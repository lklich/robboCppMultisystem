//
//  main.cpp
//  Robbo (C++ / SDL3)
//
//  Konwersja klasycznej gry logicznej Robbo (Atari XL/XE, LK Avalon 1989, autor: Janusz Pelc).
//
//  Uruchomienie bez parametrów otwiera okno gry. Parametry dla testów:
//    --tape A.cas B.cas            dane z plików kasety zamiast z pamięci podręcznej
//    --selftest nagrania.bin       test zgodności silnika z oryginałem (wynik na konsoli)
//    --headless N plik.png [skrypt] N ramek bez okna i zapis ekranu, skrypt: "ramka:klawisz:0|1,..."
//    --shots katalog               zrzuty kolejnych ekranów interfejsu (z oknem) i wyjście
//

#include "App.h"
#include "OriginalTape.h"
#include "SelfTest.h"
#include "Storage.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <cstdio>
#include <cstdlib>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace {

bool loadMemory(const std::vector<std::string>& tapes, std::vector<uint8_t>& mem) {
    if (tapes.empty()) {
        return Storage::readFile(Storage::dataFile("robbo_mem.bin"), mem) && OriginalTape::isValidImage(mem);
    }
    std::vector<TapeFile> files;
    for (const auto& t : tapes) {
        TapeFile f;
        f.name = Storage::baseName(t);
        if (!Storage::readFile(t, f.bytes)) {
            std::fprintf(stderr, "Nie można odczytać %s\n", t.c_str());
            return false;
        }
        files.push_back(std::move(f));
    }
    try {
        mem = OriginalTape::extract(files);
        return true;
    } catch (const ImportError& e) {
        std::fprintf(stderr, "%s\n", e.what());
        return false;
    }
}

bool parseKey(const std::string& name, GameKey& out) {
    static const std::map<std::string, GameKey> keys = {
        {"up", GameKey::up}, {"down", GameKey::down}, {"left", GameKey::left}, {"right", GameKey::right},
        {"fire", GameKey::fire}, {"shift", GameKey::shift}, {"esc", GameKey::escape}, {"start", GameKey::start},
        {"option", GameKey::option}, {"break", GameKey::breakKey}};
    auto it = keys.find(name);
    if (it == keys.end()) return false;
    out = it->second;
    return true;
}

int runHeadless(const std::vector<uint8_t>& mem, int frames, const std::string& out, const std::string& script) {
    AtariData data(mem);
    Pokey pokey(data.bytes(0x2400, 0x51), 44100);
    Game game(data, pokey);
    std::multimap<int, std::pair<GameKey, bool>> events;
    std::stringstream ss(script);
    std::string item;
    while (std::getline(ss, item, ',')) {
        int f = 0, down = 0;
        char name[32] = {};
        if (std::sscanf(item.c_str(), "%d:%31[a-z]:%d", &f, name, &down) == 3) {
            GameKey k;
            if (parseKey(name, k)) events.insert({f, {k, down != 0}});
        }
    }
    std::vector<float> audio(882);
    for (int i = 0; i < frames; ++i) {
        auto range = events.equal_range(i);
        for (auto it = range.first; it != range.second; ++it) game.setKey(it->second.first, it->second.second);
        game.frame();
        pokey.render(audio.data(), static_cast<int>(audio.size()));   // 1/50 s dźwięku – sprawdza syntezę
    }
    auto px = game.renderer.pixels();
    SDL_Surface* s = SDL_CreateSurfaceFrom(Renderer::width, Renderer::height, SDL_PIXELFORMAT_ARGB8888, px.data(),
                                           Renderer::width * 4);
    if (!s || !SDL_SavePNG(s, out.c_str())) {
        std::fprintf(stderr, "Zapis %s: %s\n", out.c_str(), SDL_GetError());
        return 1;
    }
    SDL_DestroySurface(s);
    std::printf("level=%d lives=%02X score=%02X%02X%02X\n", game.engine.level + 1, game.engine.lives,
                game.engine.score[0], game.engine.score[1], game.engine.score[2]);
    return 0;
}

}  // namespace

int main(int argc, char* argv[]) {
    std::vector<std::string> tapes;
    std::string selftest, headlessOut, script;
    int headlessFrames = -1;
    App::Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--tape") {
            while (i + 1 < argc && argv[i + 1][0] != '-') tapes.emplace_back(argv[++i]);
        } else if (a == "--selftest" && i + 1 < argc) {
            selftest = argv[++i];
        } else if (a == "--headless" && i + 2 < argc) {
            headlessFrames = std::atoi(argv[++i]);
            headlessOut = argv[++i];
            if (i + 1 < argc && argv[i + 1][0] != '-') script = argv[++i];
        } else if (a == "--shots" && i + 1 < argc) {
            options.shotsDir = argv[++i];
        } else if (!a.empty() && a[0] != '-') {
            tapes.push_back(a);   // pliki przeciągnięte na ikonę programu
        }
    }

    if (!selftest.empty() || headlessFrames >= 0) {
        std::vector<uint8_t> mem;
        if (!loadMemory(tapes, mem)) {
            std::fprintf(stderr, "Brak danych gry – podaj --tape strona_A.cas strona_B.cas\n");
            return 2;
        }
        if (headlessFrames >= 0) return runHeadless(mem, headlessFrames, headlessOut, script);
        std::vector<uint8_t> rec;
        if (!Storage::readFile(selftest, rec)) {
            std::fprintf(stderr, "Nie można odczytać %s\n", selftest.c_str());
            return 2;
        }
        AtariData data(mem);
        EngineTables tables(data);
        const auto res = SelfTest::run(tables, rec);
        std::printf("%s\n", res.message.c_str());
        return res.ok() ? 0 : 1;
    }

    if (!tapes.empty()) {
        // np. pliki .cas przeciągnięte na ikonę programu – zapis do pamięci podręcznej przed startem
        std::vector<uint8_t> mem;
        if (loadMemory(tapes, mem)) Storage::writeFile(Storage::dataFile("robbo_mem.bin"), mem);
    }
    App app;
    if (!app.init(options)) return 1;
    app.run();
    return 0;
}
