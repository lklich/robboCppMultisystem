//
//  NativeMenu_win.cpp
//  Robbo (C++ / SDL3)
//
//  Pasek menu „Gra” w oknie Windows. Polecenia z menu przychodzą jako WM_COMMAND do procedury
//  okna, więc okno SDL jest „podklasowane” (własna procedura, potem oryginalna procedura SDL).
//

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "NativeMenu.h"

namespace {

HWND gHwnd = nullptr;
HMENU gBar = nullptr;
HMENU gGame = nullptr;
WNDPROC gOriginal = nullptr;
Uint32 gEventType = 0;
NativeMenu::State gState;

void setEnabled(UINT id, bool on) {
    EnableMenuItem(gGame, id, MF_BYCOMMAND | (on ? MF_ENABLED : MF_GRAYED));
}

LRESULT CALLBACK robboWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_COMMAND && HIWORD(wParam) == 0) {
        const int id = LOWORD(wParam);
        if (id >= NativeMenu::cmdSave && id <= NativeMenu::cmdQuit) {
            SDL_Event e;
            SDL_zero(e);
            e.type = gEventType;
            e.user.code = id;
            SDL_PushEvent(&e);
            return 0;
        }
    }
    if (msg == WM_INITMENUPOPUP && reinterpret_cast<HMENU>(wParam) == gGame) {
        setEnabled(NativeMenu::cmdSave, gState.canSave);
        setEnabled(NativeMenu::cmdSaves, gState.ready);
        setEnabled(NativeMenu::cmdLevels, gState.ready);
        setEnabled(NativeMenu::cmdForget, gState.ready);
        ModifyMenuW(gGame, NativeMenu::cmdMute, MF_BYCOMMAND | MF_STRING, NativeMenu::cmdMute,
                    gState.muted ? L"Włącz dźwięk\tCtrl+D" : L"Wyłącz dźwięk\tCtrl+D");
    }
    return CallWindowProcW(gOriginal, hwnd, msg, wParam, lParam);
}

}  // namespace

namespace NativeMenu {

void install(SDL_Window* window, Uint32 eventType) {
    gEventType = eventType;
    gHwnd = static_cast<HWND>(
        SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
    if (!gHwnd) return;

    gGame = CreatePopupMenu();
    AppendMenuW(gGame, MF_STRING, cmdSave, L"Zapisz stan gry\tCtrl+S");
    AppendMenuW(gGame, MF_STRING, cmdSaves, L"Lista zapisów…\tCtrl+L");
    AppendMenuW(gGame, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(gGame, MF_STRING, cmdLevels, L"Wybierz planszę…\tCtrl+P");
    AppendMenuW(gGame, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(gGame, MF_STRING, cmdGameMenu, L"Menu w grze\tF1");
    AppendMenuW(gGame, MF_STRING, cmdFullscreen, L"Pełny ekran\tF11");
    AppendMenuW(gGame, MF_STRING, cmdMute, L"Wyłącz dźwięk\tCtrl+D");
    AppendMenuW(gGame, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(gGame, MF_STRING, cmdLoadTape, L"Wczytaj dane z kasety…\tCtrl+O");
    AppendMenuW(gGame, MF_STRING, cmdForget, L"Usuń dane oryginału z tego komputera");
    AppendMenuW(gGame, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(gGame, MF_STRING, cmdQuit, L"Zakończ\tCtrl+Q");

    gBar = CreateMenu();
    AppendMenuW(gBar, MF_POPUP, reinterpret_cast<UINT_PTR>(gGame), L"&Gra");

    gOriginal = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(gHwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(robboWndProc)));

    // Pasek menu zabiera część obszaru okna – przywróć rozmiar obszaru gry sprzed dodania menu.
    int w = 0, h = 0;
    SDL_GetWindowSize(window, &w, &h);
    SetMenu(gHwnd, gBar);
    SDL_SetWindowSize(window, w, h);
}

void update(const State& state) { gState = state; }

void setFullscreen(bool fullscreen) {
    if (!gHwnd || !gBar) return;
    SetMenu(gHwnd, fullscreen ? nullptr : gBar);
}

bool available() { return gHwnd != nullptr; }

}  // namespace NativeMenu
