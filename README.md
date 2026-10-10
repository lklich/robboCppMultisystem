# Robbo – wersja C++ / SDL3 (macOS, Windows, Linux)

Konwersja klasycznej gry logicznej **Robbo** (Atari XL/XE, LK Avalon 1989, autor: Janusz Pelc).
To przeniesienie wersji `robbboMac` (Swift/SwiftUI) do **C++17 i SDL3**: ten sam kod kompiluje się
na macOS, Windows i Linuksie.

Silnik odwzorowuje oryginalny kod 6502 klatka po klatce (plansze, ruch, dźwięk POKEY, ekran
tytułowy, demo i zakończenie) – jest przeniesiony 1:1 z wersji Swift.

> **Program nie zawiera żadnych danych oryginalnej gry** – grafiki, plansz, dźwięków ani kodu.
> Przy pierwszym uruchomieniu trzeba wskazać obraz kasety oryginalnego Robbo (`.cas`, obie strony).
> Dane są sprawdzane sumą SHA-256 i zapisywane w katalogu użytkownika; potem plik kasety nie jest potrzebny.

---

## Co jest potrzebne do kompilacji

| System | Narzędzia |
|---|---|
| macOS 11+ | Xcode lub Command Line Tools (`xcode-select --install`), **CMake 3.20+** (`brew install cmake` albo CMake.app z cmake.org), git |
| Windows 10/11 | **Visual Studio 2022** z zestawem „Programowanie aplikacji klasycznych w języku C++” (ma CMake i git), albo MSYS2/MinGW-w64 |
| Linux | `g++` lub `clang`, CMake, git, nagłówki X11/Wayland i ALSA/PulseAudio (lista niżej) |

**SDL3 nie trzeba instalować.** Przy pierwszej konfiguracji CMake sam pobiera SDL3 z GitHuba
(wersja `release-3.4.18`) i linkuje go statycznie, więc powstaje jeden plik programu bez dodatkowych
bibliotek. Pierwsze budowanie trwa przez to 1–3 minuty i wymaga internetu. Później wszystko działa
już bez sieci.

---

## macOS

```bash
cd robboSDL
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
open build/Robbo.app
```

Wynik: **`build/Robbo.app`** – zwykła aplikacja z ikoną, którą można skopiować do `/Applications`.

Wersja uniwersalna (Apple Silicon i Intel w jednym pliku):

```bash
cmake -S . -B build-universal -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build build-universal -j
```

Projekt Xcode (jeśli wolisz pracować w Xcode):

```bash
cmake -S . -B build-xcode -G Xcode
open build-xcode/Robbo.xcodeproj
```

Aplikacja nie jest podpisana. Jeśli przeniesiesz ją na inny Mac, przy pierwszym uruchomieniu kliknij ją
prawym przyciskiem → **Otwórz** → **Otwórz**.

---

## Windows (Visual Studio 2022)

W **Developer PowerShell for VS 2022** (albo zwykłym wierszu poleceń, jeśli CMake jest w PATH):

```powershell
cd robboSDL
cmake -S . -B build
cmake --build build --config Release
.\build\Release\Robbo.exe
```

Wynik: **`build\Release\Robbo.exe`** – jeden plik z ikoną. Działa bez instalowania SDL i bez
Visual C++ Redistributable (biblioteka C jest linkowana statycznie), więc można go po prostu skopiować
na inny komputer.

Można też w Visual Studio wybrać **Plik → Otwórz → Folder…**, wskazać `robboSDL`, poczekać na
konfigurację CMake, wybrać cel **Robbo.exe** i nacisnąć **F5**.

### Windows – MinGW-w64 (MSYS2)

```bash
pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja git
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/Robbo.exe
```

---

## Linux (np. Ubuntu/Debian)

```bash
sudo apt install build-essential cmake git ninja-build \
     libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxss-dev libxtst-dev libxfixes-dev \
     libwayland-dev libxkbcommon-dev libegl-dev libgl-dev libasound2-dev libpulse-dev
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/Robbo
```

---

## SDL3 zainstalowane w systemie (opcjonalnie)

Jeśli nie chcesz, żeby CMake pobierał SDL3 (np. budowanie bez internetu), zainstaluj SDL3 w wersji
3.4 lub nowszej i włącz opcję `ROBBO_SYSTEM_SDL`:

```bash
# macOS
brew install sdl3
cmake -S . -B build -DROBBO_SYSTEM_SDL=ON

# Windows (vcpkg)
vcpkg install sdl3
cmake -S . -B build -DROBBO_SYSTEM_SDL=ON -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake
```

Inną wersję SDL z GitHuba można wybrać opcją `-DROBBO_SDL_TAG=release-3.x.y`.

---

## Pierwsze uruchomienie

1. Pojawi się ekran „Potrzebne są dane z oryginalnej kasety”.
2. Przeciągnij na okno **oba pliki `.cas`** (strona A i B) albo naciśnij **Enter** i zaznacz oba pliki naraz.
   Działa też jeden plik `.cas` zawierający obie strony. Kolejność nie ma znaczenia.
   W Windows można też przeciągnąć oba pliki na ikonę `Robbo.exe`.
3. Po sprawdzeniu sumy SHA-256 od razu pojawia się ekran tytułowy.

Komunikaty błędów są takie same jak w wersji macOS (np. „Odczytano tylko stronę A…”).

### Gdzie są dane

| System | Katalog |
|---|---|
| macOS | `~/Library/Application Support/Robbo/` |
| Windows | `%APPDATA%\Robbo\` |
| Linux | `~/.local/share/Robbo/` |

W katalogu są: `robbo_mem.bin` (dane odczytane z kasety), `saves.bin` (zapisy gry)
oraz opcjonalnie `selftest_orig.bin` (nagrania do testu zgodności).

---

## Sterowanie

| Klawisz | Działanie |
|---|---|
| Strzałki | ruch (joystick) |
| Spacja lub Shift | strzał |
| Enter | START |
| Tab | OPTION – demo |
| Esc | poddanie planszy (utrata życia) |
| Backspace | RESET – powrót do ekranu tytułowego |
| litery na ekranie tytułowym | sekretne hasło autora, jak w oryginale |
| T na ekranie tytułowym | test zgodności silnika z oryginałem |

**Gamepad** (Xbox, PlayStation, Switch Pro i inne obsługiwane przez SDL): krzyżak lub lewa gałka – ruch,
przyciski A/B/X/Y – strzał, Start – START, Back/Select – demo, Guide/PS – menu.

### Menu „Gra” – zapis stanu gry i wybór planszy

Jak w wersji Swift, program ma menu **„Gra”**:
- **macOS** – na pasku menu u góry ekranu (obok „Robbo”),
- **Windows** – pasek menu w oknie (w trybie pełnoekranowym jest chowany),
- **Linux** – tylko menu w grze (F1).

Dodatkowo **F1** (albo F10) otwiera menu rysowane w grze; gra jest wtedy wstrzymana. Skróty działają
zawsze (na macOS z klawiszem **Cmd**, w Windows i Linuksie z **Ctrl**):

| Skrót | Działanie |
|---|---|
| Cmd/Ctrl+S | zapisz stan gry (tylko w trakcie gry, nie w demo) |
| Cmd/Ctrl+L | lista zapisów: wczytywanie i usuwanie, z miniaturą ekranu |
| Cmd/Ctrl+P | wybór planety (miniatury wszystkich 56 plansz), gra od wybranej planety |
| Cmd/Ctrl+O | wczytaj dane z kasety ponownie |
| Cmd/Ctrl+D | wyłącz/włącz dźwięk (na macOS ⌘M to systemowe „Minimalizuj”) |
| F11 lub Alt+Enter | pełny ekran |
| Cmd/Ctrl+Q | zakończ |

Zapis jest możliwy tylko w trakcie gry (nie na ekranie tytułowym i nie w demo) – wtedy pozycja
„Zapisz stan gry” jest wyszarzona. Wybór planszy zaczyna grę od wybranej planety z wynikiem 0 i 8 życiami.
Menu w grze obsługuje się strzałkami, Enter i Esc, myszą albo gamepadem. Pozycja
„Usuń dane oryginału” wymaga potwierdzenia.

---

## Budowa projektu

```
robboSDL/
├── CMakeLists.txt          budowanie (SDL3 pobierane automatycznie)
├── res/                    ikony (robbo.icns, robbo.ico), zasoby Windows, Info.plist dla macOS
└── src/
    ├── Engine.*            silnik – procedury 6502 oryginału (port 1:1 z Engine.swift)
    ├── Game.*              przebieg gry: tytuł, plansze, śmierć, demo, zakończenie, przerwanie VBI
    ├── Renderer.*          programowy ANTIC/GTIA – bufor 336×240, tryby 2, 4, A, C
    ├── Pokey.*             synteza dźwięku POKEY i „klikanie” głośnika
    ├── AtariData.*         dane z pamięci Atari, paleta PAL, czcionka z polskimi literami
    ├── Palette.inc         paleta Atari PAL (z atari_pal.act)
    ├── OriginalTape.*      odczyt kasety .cas (rekordy, klucz XOR, segmenty) i weryfikacja
    ├── Sha256.*            SHA-256 (zamiast CryptoKit)
    ├── SaveStore.*         zapisy gry z miniaturą
    ├── SelfTest.*          test zgodności z nagraniami z oryginału
    ├── Storage.*           pliki i katalog danych (przez SDL, ścieżki UTF-8)
    ├── App.*               okno, dźwięk, klawiatura/gamepad, menu w grze, import kasety
    ├── NativeMenu*         systemowe menu „Gra”: _mac.mm (Cocoa), _win.cpp (Win32), _none.cpp (Linux)
    └── main.cpp            start programu i tryby testowe
```

Wszystko poza `App.*`, `Storage.*` i `main.cpp` to czysty C++ bez zależności od SDL.

### Co się zmieniło względem wersji Swift

- Menu „Gra” jest jak w wersji Swift (systemowe na macOS i Windows). Okno importu oraz listy zapisów
  i wyboru planszy (w SwiftUI były to arkusze) rysowane są w grze czcionką oryginału – tak samo na każdym systemie.
- Dźwięk: `AVAudioEngine` → `SDL_AudioStream`. Synteza POKEY jest bez zmian.
- Doszły: gamepad, pełny ekran, wyciszanie, przeciąganie plików na ikonę programu.
- Zapisy gry mają nowy format (`saves.bin` zamiast `saves.json`). Zapisów z wersji macOS nie da się
  wczytać. Dane z kasety trzeba wskazać raz, bo wersja Swift działa w piaskownicy (sandbox) i trzyma je w innym katalogu.
- Poprawka: w zakończeniu gry wersja Swift liczyła `Int.max + 16`, co w Swifcie kończy program błędem
  przepełnienia. W C++ użyta jest bezpieczna wartość.
- Poprawka: Backspace (RESET) naciśnięty na ekranie tytułowym był zapamiętywany i wczytany zapis gry
  (albo gra od wybranej planszy) od razu wracał do ekranu tytułowego. Ten sam błąd jest w wersji Swift
  (`releaseAllKeys` nie czyści `breakPressed`).

---

## Tryby testowe (wiersz poleceń)

```bash
# test zgodności silnika z nagraniami z oryginału (wynik na konsoli, kod wyjścia 0 = wszystko zgodne)
Robbo --selftest selftest_orig.bin [--tape A.cas B.cas]

# N ramek bez okna i zapis ekranu do PNG; skrypt naciśnięć: "ramka:klawisz:1|0,..."
# klawisze: up down left right fire shift esc start option break
Robbo --headless 300 plansza1.png "5:start:1,8:start:0,150:right:1,190:right:0" --tape A.cas B.cas

# automatyczne zrzuty ekranów interfejsu (tytuł, menu, plansze, zapisy, demo) i wyjście
Robbo --shots katalog
```

Na macOS program w paczce uruchamia się z wiersza poleceń jako `build/Robbo.app/Contents/MacOS/Robbo`.
