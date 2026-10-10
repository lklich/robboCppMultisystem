//
//  NativeMenu.h
//  Robbo (C++ / SDL3)
//
//  Systemowe menu „Gra” – jak w wersji Swift:
//    macOS:   menu na pasku u góry ekranu (NativeMenu_mac.mm)
//    Windows: pasek menu w oknie (NativeMenu_win.cpp)
//    Linux:   brak – zostaje menu w grze pod F1 (NativeMenu_none.cpp)
//  Wybranie pozycji wysyła zdarzenie SDL (typ podany w install, user.code = Command).
//

#pragma once

#include <SDL3/SDL.h>

namespace NativeMenu {

enum Command {
    cmdSave = 1,        // Zapisz stan gry
    cmdSaves,           // Lista zapisów…
    cmdLevels,          // Wybierz planszę…
    cmdGameMenu,        // Menu w grze (F1)
    cmdFullscreen,      // Pełny ekran
    cmdMute,            // Wyłącz/włącz dźwięk
    cmdLoadTape,        // Wczytaj dane z kasety…
    cmdForget,          // Usuń dane oryginału
    cmdQuit,            // Zakończ (Windows)
};

/// Stan potrzebny do włączania/wyszarzania pozycji.
struct State {
    bool ready = false;     // są dane gry
    bool canSave = false;   // trwa zwykła rozgrywka
    bool muted = false;
};

void install(SDL_Window* window, Uint32 eventType);
void update(const State& state);
/// Windows: w trybie pełnoekranowym pasek menu jest chowany.
void setFullscreen(bool fullscreen);
/// Czy system ma własne menu (wtedy skróty obsługuje menu).
bool available();

}  // namespace NativeMenu
