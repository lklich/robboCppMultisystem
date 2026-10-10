//
//  App.cpp
//  Robbo (C++ / SDL3)
//

#include "App.h"

#include "NativeMenu.h"
#include "OriginalTape.h"
#include "SelfTest.h"
#include "Storage.h"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <random>

namespace {

#ifdef __APPLE__
const char* kMod = "Cmd";
#else
const char* kMod = "Ctrl";
#endif

const char* kCacheName = "robbo_mem.bin";
const char* kSelfTestName = "selftest_orig.bin";
constexpr int kSampleRate = 44100;
constexpr Uint64 kFrameNS = 20000000;     // 50 Hz jak Atari PAL

const SDL_Color kWhite{255, 255, 255, 255};
const SDL_Color kGray{150, 150, 150, 255};
const SDL_Color kDim{100, 100, 100, 255};
const SDL_Color kOrange{255, 160, 40, 255};
const SDL_Color kRed{255, 90, 80, 255};
const SDL_Color kGreen{120, 220, 120, 255};
const SDL_Color kPanel{16, 16, 24, 235};
const SDL_Color kSelect{60, 70, 140, 255};

std::string shortcut(const char* key) { return std::string(kMod) + "+" + key; }

/// Znak ASCII do czcionki awaryjnej (SDL_RenderDebugText) – polskie litery bez ogonków.
char asciiFold(char32_t ch) {
    switch (ch) {
    case U'ą': return 'a'; case U'Ą': return 'A';
    case U'ć': return 'c'; case U'Ć': return 'C';
    case U'ę': return 'e'; case U'Ę': return 'E';
    case U'ł': return 'l'; case U'Ł': return 'L';
    case U'ń': return 'n'; case U'Ń': return 'N';
    case U'ó': return 'o'; case U'Ó': return 'O';
    case U'ś': return 's'; case U'Ś': return 'S';
    case U'ż': case U'ź': return 'z';
    case U'Ż': case U'Ź': return 'Z';
    case U'„': case U'”': return '"';
    case U'–': case U'—': return '-';
    default: break;
    }
    if (ch >= 0x20 && ch < 0x7F) return static_cast<char>(ch);
    return '?';
}

std::string formatDate(int64_t t) {
    std::time_t tt = static_cast<std::time_t>(t);
    std::tm tmv{};
#ifdef _WIN32
    localtime_s(&tmv, &tt);
#else
    localtime_r(&tt, &tmv);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%d.%m.%Y %H:%M", &tmv);
    return buf;
}

}  // namespace

App::App() = default;

App::~App() {
    if (audio_) SDL_DestroyAudioStream(audio_);   // zatrzymuje wywołania zwrotne przed usunięciem Pokey
    audio_ = nullptr;
    game_.reset();
    pokey_.reset();
    freeTextures();
    if (fontTex_) SDL_DestroyTexture(fontTex_);
    if (screenTex_) SDL_DestroyTexture(screenTex_);
    for (auto* p : pads_) SDL_CloseGamepad(p);
    if (renderer_) SDL_DestroyRenderer(renderer_);
    if (window_) SDL_DestroyWindow(window_);
    SDL_Quit();
}

// MARK: - Start

bool App::init(const Options& options) {
    options_ = options;
    SDL_SetAppMetadata("Robbo", "1.0", "pl.lk.robbo");
    SDL_SetHint(SDL_HINT_WINDOWS_INTRESOURCE_ICON, "1");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
        // bez dźwięku i gamepada też da się grać
        if (!SDL_Init(SDL_INIT_VIDEO)) {
            SDL_Log("SDL_Init: %s", SDL_GetError());
            return false;
        }
    }
    window_ = SDL_CreateWindow("Robbo", Renderer::width * 3, Renderer::height * 3,
                               SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!window_) {
        SDL_Log("SDL_CreateWindow: %s", SDL_GetError());
        return false;
    }
    SDL_SetWindowMinimumSize(window_, viewW, viewH);
    renderer_ = SDL_CreateRenderer(window_, nullptr);
    if (!renderer_) {
        SDL_Log("SDL_CreateRenderer: %s", SDL_GetError());
        return false;
    }
    SDL_SetRenderVSync(renderer_, 1);
    SDL_SetRenderLogicalPresentation(renderer_, viewW, viewH, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    screenTex_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                   Renderer::width, Renderer::height);
    SDL_SetTextureScaleMode(screenTex_, SDL_SCALEMODE_NEAREST);

    dialogEvent_ = SDL_RegisterEvents(2);
    menuEvent_ = dialogEvent_ + 1;
    NativeMenu::install(window_, menuEvent_);   // menu „Gra” jak w wersji Swift (macOS, Windows)

    const SDL_AudioSpec spec{SDL_AUDIO_F32, 1, kSampleRate};
    audio_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, audioCallback, this);
    if (audio_) {
        SDL_ResumeAudioStreamDevice(audio_);
    } else {
        SDL_Log("Brak dźwięku: %s", SDL_GetError());
    }

    store_ = std::make_unique<SaveStore>(Storage::dataFile("saves.bin"));
    loadCachedData();
    if (game_) show("F1: menu – zapis gry i wybór planszy");
    startTicks_ = SDL_GetTicks();
    return true;
}

// MARK: - Dane z oryginalnej kasety

void App::loadCachedData() {
    std::vector<uint8_t> mem;
    if (Storage::readFile(Storage::dataFile(kCacheName), mem) && OriginalTape::isValidImage(mem)) {
        setData(std::move(mem));
    }
}

void App::setData(std::vector<uint8_t> memory) {
    closeOverlay();
    game_.reset();
    auto data = std::make_unique<AtariData>(std::move(memory));
    auto pokey = std::make_unique<Pokey>(data->bytes(0x2400, 0x51), kSampleRate);
    pokey->setMuted(muted_);
    replacePokey(std::move(pokey));
    data_ = std::move(data);
    game_ = std::make_unique<Game>(*data_, *pokey_);
    freeTextures();
    buildFontTexture();
    releaseInput();
    importError_.clear();
}

void App::forgetOriginal() {
    closeOverlay();
    game_.reset();
    replacePokey(nullptr);
    data_.reset();
    freeTextures();
    if (fontTex_) { SDL_DestroyTexture(fontTex_); fontTex_ = nullptr; }
    Storage::removeFile(Storage::dataFile(kCacheName));
    importError_.clear();
    show("Dane oryginału usunięte");
}

void App::importTape(const std::vector<std::string>& paths) {
    if (paths.empty()) return;
    std::vector<TapeFile> files;
    for (const auto& p : paths) {
        TapeFile f;
        f.name = Storage::baseName(p);
        if (!Storage::readFile(p, f.bytes)) {
            importError_ = "Nie można odczytać pliku „" + f.name + "”.";
            if (game_) show(importError_);
            return;
        }
        files.push_back(std::move(f));
    }
    try {
        auto mem = OriginalTape::extract(files);
        if (!Storage::writeFile(Storage::dataFile(kCacheName), mem)) {
            SDL_Log("Nie udało się zapisać %s", Storage::dataFile(kCacheName).c_str());
        }
        setData(std::move(mem));
        show("Dane gry odczytane z kasety i zapisane");
    } catch (const ImportError& e) {
        importError_ = e.what();
        if (game_) show(importError_);
    }
}

void App::chooseTape() { openDialog(DialogPurpose::tape, true); }

void App::runSelfTest() {
    if (!data_) return;
    const std::string path = Storage::dataFile(kSelfTestName);
    std::vector<uint8_t> rec;
    if (!Storage::readFile(path, rec)) {
        // Nagrania z oryginału nie są dołączane (zawierają dane gry) – plik trzeba wskazać raz.
        openDialog(DialogPurpose::selfTest, false);
        return;
    }
    windowTitle_ = "Robbo – trwa test…";
    SDL_SetWindowTitle(window_, windowTitle_.c_str());
    const auto res = SelfTest::run(game_->tables, rec);
    SDL_Log("%s", res.message.c_str());
    windowTitle_ = "Robbo – " + res.message;
    SDL_SetWindowTitle(window_, windowTitle_.c_str());
    show(res.message);
}

void App::openDialog(DialogPurpose purpose, bool many) {
    {
        std::lock_guard<std::mutex> g(dialogLock_);
        if (dialogOpen_) return;
        dialogOpen_ = true;
        dialogPurpose_ = purpose;
    }
    releaseInput();
    static const SDL_DialogFileFilter tapeFilters[] = {
        {"Kaseta Atari (.cas)", "cas;bin"},
        {"Wszystkie pliki", "*"},
    };
    static const SDL_DialogFileFilter anyFilter[] = {{"Wszystkie pliki", "*"}};
    if (purpose == DialogPurpose::tape) {
        SDL_ShowOpenFileDialog(dialogCallback, this, window_, tapeFilters, 2, nullptr, many);
    } else {
        SDL_ShowOpenFileDialog(dialogCallback, this, window_, anyFilter, 1, nullptr, many);
    }
}

void SDLCALL App::dialogCallback(void* userdata, const char* const* filelist, int) {
    App* app = static_cast<App*>(userdata);
    {
        std::lock_guard<std::mutex> g(app->dialogLock_);
        app->dialogFiles_.clear();
        if (filelist) {
            for (const char* const* f = filelist; *f; ++f) app->dialogFiles_.emplace_back(*f);
        } else {
            SDL_Log("Okno wyboru plików: %s", SDL_GetError());
        }
    }
    SDL_Event ev{};
    ev.type = app->dialogEvent_;
    SDL_PushEvent(&ev);
}

void App::handleDialogResult() {
    std::vector<std::string> files;
    DialogPurpose purpose;
    {
        std::lock_guard<std::mutex> g(dialogLock_);
        files.swap(dialogFiles_);
        purpose = dialogPurpose_;
        dialogOpen_ = false;
    }
    if (files.empty()) return;
    if (purpose == DialogPurpose::tape) {
        importTape(files);
        return;
    }
    std::vector<uint8_t> rec;
    if (!Storage::readFile(files.front(), rec)) {
        show("Test: brak pliku selftest_orig.bin");
        return;
    }
    Storage::writeFile(Storage::dataFile(kSelfTestName), rec);
    runSelfTest();
}

// MARK: - Menu systemowe „Gra”

void App::handleMenuCommand(int command) {
    switch (command) {
    case NativeMenu::cmdSave: saveCurrent(); break;
    case NativeMenu::cmdSaves: openOverlay(Overlay::saves); break;
    case NativeMenu::cmdLevels: openOverlay(Overlay::levels); break;
    case NativeMenu::cmdGameMenu:
        if (overlay_ == Overlay::menu) closeOverlay(); else openOverlay(Overlay::menu);
        break;
    case NativeMenu::cmdFullscreen: toggleFullscreen(); break;
    case NativeMenu::cmdMute: toggleMute(); break;
    case NativeMenu::cmdLoadTape: closeOverlay(); chooseTape(); break;
    case NativeMenu::cmdForget: confirmForget(); break;
    case NativeMenu::cmdQuit: running_ = false; break;
    default: break;
    }
}

void App::confirmForget() {
    if (!game_) return;
    releaseInput();
    const SDL_MessageBoxButtonData buttons[] = {
        {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Anuluj"},
        {0, 1, "Usuń"},
    };
    const SDL_MessageBoxData box{SDL_MESSAGEBOX_WARNING, window_, "Robbo",
                                 "Usunąć dane oryginału odczytane z kasety? Przy następnym uruchomieniu "
                                 "trzeba będzie znowu wskazać pliki .cas. Zapisy gry zostaną.",
                                 2, buttons, nullptr};
    int choice = 0;
    if (SDL_ShowMessageBox(&box, &choice) && choice == 1) forgetOriginal();
}

// MARK: - Dźwięk

void SDLCALL App::audioCallback(void* userdata, SDL_AudioStream* stream, int additional, int) {
    // Wywoływane z wątku audio przy zablokowanym strumieniu (SDL_LockAudioStream chroni pokey_).
    App* app = static_cast<App*>(userdata);
    const int frames = additional / static_cast<int>(sizeof(float));
    if (frames <= 0) return;
    if (static_cast<int>(app->audioBuf_.size()) < frames) app->audioBuf_.resize(frames);
    float* buf = app->audioBuf_.data();
    if (app->pokey_) {
        app->pokey_->render(buf, frames);
    } else {
        std::fill_n(buf, frames, 0.0f);
    }
    SDL_PutAudioStreamData(stream, buf, frames * static_cast<int>(sizeof(float)));
}

void App::replacePokey(std::unique_ptr<Pokey> p) {
    if (audio_) SDL_LockAudioStream(audio_);
    pokey_ = std::move(p);
    if (audio_) SDL_UnlockAudioStream(audio_);
}

// MARK: - Pętla główna

void App::run() {
    Uint64 last = SDL_GetTicksNS();
    Uint64 acc = 0;
    while (running_) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) handleEvent(e);
        if (!running_) break;

        const Uint64 now = SDL_GetTicksNS();
        acc += now - last;
        last = now;
        int n = 0;
        while (acc >= kFrameNS && n < 5) {
            if (game_ && overlay_ == Overlay::none) game_->frame();
            acc -= kFrameNS;
            ++n;
        }
        if (acc > 5 * kFrameNS) acc = 0;   // po uśpieniu/przeciąganiu okna nie nadrabiamy

        draw();
        if (!options_.shotsDir.empty()) testShots();
        SDL_RenderPresent(renderer_);
        int vsync = 0;
        if (!SDL_GetRenderVSync(renderer_, &vsync) || vsync == 0) SDL_Delay(2);
    }
}

// MARK: - Zdarzenia

void App::handleEvent(const SDL_Event& ev) {
    SDL_Event e = ev;
    if (e.type == dialogEvent_) {
        handleDialogResult();
        return;
    }
    if (e.type == menuEvent_) {
        handleMenuCommand(e.user.code);
        return;
    }
    switch (e.type) {
    case SDL_EVENT_QUIT:
        running_ = false;
        break;
    case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
        NativeMenu::setFullscreen(true);
        break;
    case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
        NativeMenu::setFullscreen(false);
        break;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        // Okno straciło fokus – puść wszystkie klawisze, żeby Robbo nie szedł sam.
        releaseInput();
        break;
    case SDL_EVENT_KEY_DOWN:
        handleKeyDown(e.key);
        break;
    case SDL_EVENT_KEY_UP:
        handleKeyUp(e.key);
        break;
    case SDL_EVENT_MOUSE_MOTION:
        SDL_ConvertEventToRenderCoordinates(renderer_, &e);
        mouseX_ = e.motion.x;
        mouseY_ = e.motion.y;
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        SDL_ConvertEventToRenderCoordinates(renderer_, &e);
        mouseX_ = e.button.x;
        mouseY_ = e.button.y;
        if (e.button.button == SDL_BUTTON_LEFT) {
            std::function<void()> action;
            for (const auto& b : buttons_) {
                if (hovered(b.rect)) action = b.action;
            }
            if (action) action();
        }
        break;
    case SDL_EVENT_MOUSE_WHEEL:
        if (overlay_ == Overlay::saves) savesScroll_ -= static_cast<int>(e.wheel.y);
        if (overlay_ == Overlay::levels) levelScroll_ -= static_cast<int>(e.wheel.y);
        break;
    case SDL_EVENT_DROP_BEGIN:
        dropped_.clear();
        break;
    case SDL_EVENT_DROP_FILE:
        if (e.drop.data) dropped_.emplace_back(e.drop.data);
        break;
    case SDL_EVENT_DROP_COMPLETE:
        if (!dropped_.empty()) {
            const auto files = dropped_;
            dropped_.clear();
            importTape(files);
        }
        break;
    case SDL_EVENT_GAMEPAD_ADDED:
        if (SDL_Gamepad* p = SDL_OpenGamepad(e.gdevice.which)) pads_.push_back(p);
        break;
    case SDL_EVENT_GAMEPAD_REMOVED:
        for (auto it = pads_.begin(); it != pads_.end(); ++it) {
            if (SDL_GetGamepadID(*it) == e.gdevice.which) {
                SDL_CloseGamepad(*it);
                pads_.erase(it);
                break;
            }
        }
        std::fill(std::begin(pad_), std::end(pad_), false);
        std::fill(std::begin(axis_), std::end(axis_), false);
        syncInput();
        break;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP:
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
        handleGamepad(e);
        break;
    default:
        break;
    }
}

/// Indeks GameKey dla klawisza (-1 = klawisz nieużywany przez grę).
/// Strzałki = joystick, spacja lub Shift = strzał, Esc = poddanie planszy,
/// Enter = START, Tab = OPTION (demo), Backspace = RESET (powrót do menu).
int App::gameKeyFor(SDL_Keycode key) {
    switch (key) {
    case SDLK_UP: return static_cast<int>(GameKey::up);
    case SDLK_DOWN: return static_cast<int>(GameKey::down);
    case SDLK_LEFT: return static_cast<int>(GameKey::left);
    case SDLK_RIGHT: return static_cast<int>(GameKey::right);
    case SDLK_SPACE: return static_cast<int>(GameKey::fire);
    case SDLK_LSHIFT: case SDLK_RSHIFT: return static_cast<int>(GameKey::shift);
    case SDLK_ESCAPE: return static_cast<int>(GameKey::escape);
    case SDLK_RETURN: case SDLK_KP_ENTER: return static_cast<int>(GameKey::start);
    case SDLK_TAB: return static_cast<int>(GameKey::option);
    case SDLK_BACKSPACE: return static_cast<int>(GameKey::breakKey);
    default: return -1;
    }
}

bool App::handleShortcut(const SDL_KeyboardEvent& k) {
    const bool mod = (k.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0;
    if (k.key == SDLK_F11 || (k.key == SDLK_RETURN && (k.mod & SDL_KMOD_ALT)) ||
        (mod && (k.mod & SDL_KMOD_CTRL) && (k.mod & SDL_KMOD_GUI) && k.key == SDLK_F)) {
        if (!k.repeat) toggleFullscreen();
        return true;
    }
    if (k.key == SDLK_F1 || k.key == SDLK_F10) {
        if (k.repeat) return true;
        if (overlay_ == Overlay::menu) closeOverlay(); else openOverlay(Overlay::menu);
        return true;
    }
    if (!mod) return false;
    if (k.repeat) return true;
    switch (k.key) {
    case SDLK_Q: running_ = false; return true;
    case SDLK_O: chooseTape(); return true;
    case SDLK_D: toggleMute(); return true;
    case SDLK_S: if (game_) saveCurrent(); return true;
    case SDLK_L: if (game_) openOverlay(Overlay::saves); return true;
    case SDLK_P: if (game_) openOverlay(Overlay::levels); return true;
    default: return true;   // inne skróty z Ctrl/Cmd nie trafiają do gry
    }
}

void App::handleKeyDown(const SDL_KeyboardEvent& k) {
    if (handleShortcut(k)) return;
    if (overlay_ != Overlay::none) {
        handleOverlayKey(k);
        return;
    }
    if (!game_) {
        if (!k.repeat && (k.key == SDLK_RETURN || k.key == SDLK_KP_ENTER || k.key == SDLK_SPACE)) chooseTape();
        return;
    }
    if (k.key == SDLK_T && game_->isOnTitle() && !k.repeat) {    // T – test zgodności silnika
        runSelfTest();
        return;
    }
    if (game_->isOnTitle() && !k.repeat && ((k.key >= SDLK_A && k.key <= SDLK_Z) || k.key == SDLK_SPACE)) {
        const char ch = k.key == SDLK_SPACE ? ' ' : static_cast<char>('A' + (k.key - SDLK_A));
        if (game_->titleTyped(ch)) return;
    }
    const int gk = gameKeyFor(k.key);
    if (gk < 0 || k.repeat) return;
    if (gk == static_cast<int>(GameKey::breakKey)) {
        game_->setKey(GameKey::breakKey, true);
        return;
    }
    kb_[gk] = true;
    syncInput();
}

void App::handleKeyUp(const SDL_KeyboardEvent& k) {
    const int gk = gameKeyFor(k.key);
    if (gk < 0 || gk == static_cast<int>(GameKey::breakKey)) return;
    kb_[gk] = false;
    syncInput();
}

void App::handleGamepad(const SDL_Event& e) {
    if (e.type == SDL_EVENT_GAMEPAD_AXIS_MOTION) {
        const int v = e.gaxis.value;
        const int dead = 16000;
        if (e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTX) {
            axis_[static_cast<int>(GameKey::left)] = v < -dead;
            axis_[static_cast<int>(GameKey::right)] = v > dead;
        } else if (e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTY) {
            axis_[static_cast<int>(GameKey::up)] = v < -dead;
            axis_[static_cast<int>(GameKey::down)] = v > dead;
        } else {
            return;
        }
        if (overlay_ == Overlay::none) syncInput();
        return;
    }
    const bool down = e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN;
    const auto button = static_cast<SDL_GamepadButton>(e.gbutton.button);

    if (button == SDL_GAMEPAD_BUTTON_GUIDE) {
        if (down) { if (overlay_ == Overlay::menu) closeOverlay(); else openOverlay(Overlay::menu); }
        return;
    }
    if (overlay_ != Overlay::none || !game_) {
        // nawigacja po menu gamepadem
        if (!down) return;
        SDL_KeyboardEvent k{};
        switch (button) {
        case SDL_GAMEPAD_BUTTON_DPAD_UP: k.key = SDLK_UP; break;
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN: k.key = SDLK_DOWN; break;
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT: k.key = SDLK_LEFT; break;
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: k.key = SDLK_RIGHT; break;
        case SDL_GAMEPAD_BUTTON_SOUTH: k.key = SDLK_RETURN; break;
        case SDL_GAMEPAD_BUTTON_EAST: k.key = SDLK_ESCAPE; break;
        default: return;
        }
        if (game_) handleOverlayKey(k); else if (k.key == SDLK_RETURN) chooseTape();
        return;
    }
    int gk = -1;
    switch (button) {
    case SDL_GAMEPAD_BUTTON_DPAD_UP: gk = static_cast<int>(GameKey::up); break;
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: gk = static_cast<int>(GameKey::down); break;
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: gk = static_cast<int>(GameKey::left); break;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: gk = static_cast<int>(GameKey::right); break;
    case SDL_GAMEPAD_BUTTON_SOUTH:
    case SDL_GAMEPAD_BUTTON_EAST:
    case SDL_GAMEPAD_BUTTON_WEST:
    case SDL_GAMEPAD_BUTTON_NORTH: gk = static_cast<int>(GameKey::fire); break;
    case SDL_GAMEPAD_BUTTON_START: gk = static_cast<int>(GameKey::start); break;
    case SDL_GAMEPAD_BUTTON_BACK: gk = static_cast<int>(GameKey::option); break;
    default: return;
    }
    // kilka przycisków strzału naraz – strzał trwa, dopóki którykolwiek jest wciśnięty
    if (gk == static_cast<int>(GameKey::fire)) {
        bool any = false;
        for (auto* p : pads_) {
            any = any || SDL_GetGamepadButton(p, SDL_GAMEPAD_BUTTON_SOUTH) ||
                  SDL_GetGamepadButton(p, SDL_GAMEPAD_BUTTON_EAST) || SDL_GetGamepadButton(p, SDL_GAMEPAD_BUTTON_WEST) ||
                  SDL_GetGamepadButton(p, SDL_GAMEPAD_BUTTON_NORTH);
        }
        pad_[gk] = any || down;
    } else {
        pad_[gk] = down;
    }
    syncInput();
}

void App::syncInput() {
    if (!game_) return;
    for (int i = 0; i < keyCount; ++i) {
        if (i == static_cast<int>(GameKey::breakKey)) continue;
        const bool want = kb_[i] || pad_[i] || axis_[i];
        if (want != applied_[i]) {
            game_->setKey(static_cast<GameKey>(i), want);
            applied_[i] = want;
        }
    }
}

void App::releaseInput() {
    std::fill(std::begin(kb_), std::end(kb_), false);
    std::fill(std::begin(pad_), std::end(pad_), false);
    std::fill(std::begin(axis_), std::end(axis_), false);
    std::fill(std::begin(applied_), std::end(applied_), false);
    if (game_) game_->releaseAllKeys();
}

// MARK: - Menu w grze

void App::openOverlay(Overlay o) {
    if (!game_ && o != Overlay::menu) return;
    releaseInput();
    overlay_ = o;
    confirmForget_ = false;
    if (o == Overlay::menu) menuSel_ = 0;
    if (o == Overlay::saves) { savesSel_ = 0; savesScroll_ = 0; }
    if (o == Overlay::levels) {
        levelSel_ = game_ ? std::clamp(game_->engine.level, 0, AtariData::levelCount - 1) : 0;
        levelScroll_ = levelSel_ / 8;
        ensureLevelTextures();
    }
}

void App::closeOverlay() {
    overlay_ = Overlay::none;
    confirmForget_ = false;
    releaseInput();
}

void App::handleOverlayKey(const SDL_KeyboardEvent& k) {
    const SDL_Keycode key = k.key;
    if (overlay_ == Overlay::menu) {
        // menu rysuje i obsługuje te same pozycje – liczba pozycji ustalana w drawMenu
        if (key == SDLK_ESCAPE) { closeOverlay(); return; }
        if (key == SDLK_UP) { menuSel_ -= 1; confirmForget_ = false; }
        if (key == SDLK_DOWN) { menuSel_ += 1; confirmForget_ = false; }
        if (key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_SPACE) {
            if (menuSel_ >= 0 && menuSel_ < static_cast<int>(menuActions_.size())) {
                auto action = menuActions_[menuSel_];
                if (action) action();
            }
        }
        return;
    }
    if (overlay_ == Overlay::saves) {
        const auto& list = store_->entries();
        const int n = static_cast<int>(list.size());
        switch (key) {
        case SDLK_ESCAPE: closeOverlay(); break;
        case SDLK_UP: savesSel_ = std::max(0, savesSel_ - 1); break;
        case SDLK_DOWN: savesSel_ = std::min(n - 1, savesSel_ + 1); break;
        case SDLK_RETURN: case SDLK_KP_ENTER:
            if (savesSel_ >= 0 && savesSel_ < n) loadSave(list[savesSel_]);
            break;
        case SDLK_DELETE: case SDLK_BACKSPACE:
            if (savesSel_ >= 0 && savesSel_ < n) deleteSave(list[savesSel_].id);
            break;
        case SDLK_S: saveCurrent(); break;
        default: break;
        }
        return;
    }
    if (overlay_ == Overlay::levels) {
        const int n = AtariData::levelCount;
        switch (key) {
        case SDLK_ESCAPE: closeOverlay(); break;
        case SDLK_LEFT: levelSel_ = std::max(0, levelSel_ - 1); break;
        case SDLK_RIGHT: levelSel_ = std::min(n - 1, levelSel_ + 1); break;
        case SDLK_UP: levelSel_ = std::max(0, levelSel_ - 8); break;
        case SDLK_DOWN: levelSel_ = std::min(n - 1, levelSel_ + 8); break;
        case SDLK_PAGEUP: levelSel_ = std::max(0, levelSel_ - 16); break;
        case SDLK_PAGEDOWN: levelSel_ = std::min(n - 1, levelSel_ + 16); break;
        case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_SPACE: startLevel(levelSel_); break;
        default: break;
        }
        // przewiń, żeby wybrana plansza była widoczna (2 wiersze na ekranie)
        const int row = levelSel_ / 8;
        if (row < levelScroll_) levelScroll_ = row;
        if (row > levelScroll_ + 1) levelScroll_ = row - 1;
    }
}

void App::saveCurrent() {
    if (!game_) return;
    SaveEntry entry;
    if (!game_->makeSnapshot(entry.snapshot)) {
        show("Zapis możliwy tylko w trakcie gry");
        return;
    }
    static std::mt19937_64 rng{std::random_device{}()};
    const Engine& e = game_->engine;
    char score[16], lives[8];
    std::snprintf(score, sizeof(score), "%02X%02X%02X", e.score[0] & 0xFF, e.score[1] & 0xFF, e.score[2] & 0xFF);
    std::snprintf(lives, sizeof(lives), "%02X", e.lives & 0xFF);
    entry.id = rng();
    entry.date = static_cast<int64_t>(std::time(nullptr));
    entry.level = game_->currentLevelNumber();
    entry.score = score;
    entry.lives = lives;
    entry.thumbnail = makeThumbnail(game_->renderer.pixels(), Renderer::width, Renderer::height);
    const int level = entry.level;
    store_->add(std::move(entry));
    savesSel_ = 0;
    show("Zapisano stan gry (plansza " + std::to_string(level) + ")");
}

void App::loadSave(const SaveEntry& e) {
    if (!game_) return;
    const int level = e.level;
    game_->restore(e.snapshot);
    closeOverlay();
    show("Wczytano zapis – plansza " + std::to_string(level));
}

void App::deleteSave(uint64_t id) {
    auto it = thumbTex_.find(id);
    if (it != thumbTex_.end()) {
        SDL_DestroyTexture(it->second);
        thumbTex_.erase(it);
    }
    store_->remove(id);
    savesSel_ = std::min(savesSel_, static_cast<int>(store_->entries().size()) - 1);
    if (savesSel_ < 0) savesSel_ = 0;
}

void App::startLevel(int index) {
    if (!game_) return;
    closeOverlay();
    game_->startAt(index);
    show("Plansza " + std::to_string(index + 1));
}

void App::toggleFullscreen() {
    const bool fs = (SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN) != 0;
    SDL_SetWindowFullscreen(window_, !fs);
}

void App::toggleMute() {
    muted_ = !muted_;
    if (pokey_) pokey_->setMuted(muted_);
    show(muted_ ? "Dźwięk wyłączony" : "Dźwięk włączony");
}

void App::show(const std::string& text) {
    message_ = text;
    messageUntil_ = SDL_GetTicks() + 2500;
}

// MARK: - Rysowanie

void App::draw() {
    NativeMenu::State st;
    st.ready = game_ != nullptr;
    st.canSave = game_ && game_->canSave() && overlay_ == Overlay::none;
    st.muted = muted_;
    NativeMenu::update(st);

    buttons_.clear();
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
    SDL_RenderClear(renderer_);
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);

    if (!game_) {
        drawImportScreen();
    } else {
        const auto& px = game_->renderer.pixels();
        SDL_UpdateTexture(screenTex_, nullptr, px.data(), Renderer::width * 4);
        const SDL_FRect dst{0, 0, static_cast<float>(viewW), static_cast<float>(viewH)};
        SDL_RenderTexture(renderer_, screenTex_, nullptr, &dst);
        switch (overlay_) {
        case Overlay::menu: drawMenu(); break;
        case Overlay::saves: drawSaves(); break;
        case Overlay::levels: drawLevels(); break;
        case Overlay::none: break;
        }
    }
    if (!game_ && overlay_ == Overlay::menu) drawMenu();
    drawToast();
}

/// Pierwsze uruchomienie: program nie zawiera danych oryginału, więc prosi o kasetę.
void App::drawImportScreen() {
    const float cx = viewW / 2.0f;
    drawTextCentered(cx, 40, "ROBBO", 5, kOrange);
    drawTextCentered(cx, 110, "Potrzebne są dane z oryginalnej kasety", 2, kWhite);
    drawWrapped(cx, 145, 600,
                "Ze względu na prawa autorskie (LK Avalon, Janusz Pelc) program nie zawiera grafiki, plansz "
                "ani dźwięków oryginału. Wskaż obraz kasety oryginalnej gry Robbo w formacie .cas - oba pliki "
                "(strona A i strona B) albo jeden plik z obiema stronami. Dane zostaną odczytane i zapisane na "
                "tym komputerze; później plik kasety nie będzie już potrzebny.",
                1, kGray, 13);

    const SDL_FRect drop{cx - 190, 245, 380, 90};
    const bool hot = hovered(drop);
    frameRect(drop.x, drop.y, drop.w, drop.h, hot ? kOrange : kDim);
    drawTextCentered(cx, 270, "Upuść tutaj pliki .cas", 2, hot ? kOrange : kGray);
    drawTextCentered(cx, 300, "albo kliknij, żeby wybrać", 1, hot ? kOrange : kGray);
    buttons_.push_back({drop, [this] { chooseTape(); }});

    button(cx - textWidth("Wybierz pliki .cas (Enter)", 2) / 2 - 8, 355, "Wybierz pliki .cas (Enter)", 2, kWhite,
           [this] { chooseTape(); });

    if (!importError_.empty()) drawWrapped(cx, 405, 620, importError_, 1, kRed, 13);
    drawTextCentered(cx, viewH - 18, "Folder danych: " + Storage::dataDir(), 1, kDim);
}

void App::drawMenu() {
    fillRect(0, 0, viewW, viewH, {0, 0, 0, 150});
    const float w = 440, h = 378, x = (viewW - w) / 2, y = (viewH - h) / 2;
    fillRect(x, y, w, h, kPanel);
    frameRect(x, y, w, h, kDim);
    drawTextCentered(viewW / 2.0f, y + 14, "Robbo – menu", 2, kOrange);

    struct Item {
        std::string label;
        std::string key;
        std::function<void()> action;
        bool enabled;
    };
    const bool ready = game_ != nullptr;
    std::vector<Item> items = {
        {"Wróć do gry", "F1", [this] { closeOverlay(); }, true},
        {"Zapisz stan gry", shortcut("S"), [this] { saveCurrent(); closeOverlay(); },
         ready && game_->canSave()},
        {"Lista zapisów", shortcut("L"), [this] { openOverlay(Overlay::saves); }, ready},
        {"Wybierz planszę", shortcut("P"), [this] { openOverlay(Overlay::levels); }, ready},
        {"Pełny ekran", "F11", [this] { toggleFullscreen(); }, true},
        {muted_ ? "Włącz dźwięk" : "Wyłącz dźwięk", shortcut("D"), [this] { toggleMute(); }, true},
        {"Wczytaj dane z kasety", shortcut("O"), [this] { closeOverlay(); chooseTape(); }, true},
        {confirmForget_ ? "Na pewno usunąć? Enter: tak" : "Usuń dane oryginału", "",
         [this] {
             if (confirmForget_) forgetOriginal(); else confirmForget_ = true;
         },
         ready},
        {"Zakończ", shortcut("Q"), [this] { running_ = false; }, true},
    };
    const int n = static_cast<int>(items.size());
    menuActions_.clear();
    if (menuSel_ < 0) menuSel_ = n - 1;
    if (menuSel_ >= n) menuSel_ = 0;
    float iy = y + 48;
    for (int i = 0; i < n; ++i) {
        const SDL_FRect r{x + 12, iy - 4, w - 24, 26};
        const bool hot = hovered(r);
        if (i == menuSel_) fillRect(r.x, r.y, r.w, r.h, kSelect);
        else if (hot) fillRect(r.x, r.y, r.w, r.h, {40, 40, 60, 255});
        const SDL_Color c = items[i].enabled ? kWhite : kDim;
        drawText(x + 24, iy + 1, items[i].label, 2, items[i].label.rfind("Na pewno", 0) == 0 ? kRed : c);
        if (!items[i].key.empty()) {
            drawText(x + w - 24 - textWidth(items[i].key, 1), iy + 5, items[i].key, 1, kGray);
        }
        auto action = items[i].action;
        const bool enabled = items[i].enabled;
        auto activate = [this, i, action, enabled] {
            menuSel_ = i;
            if (enabled) action();
        };
        buttons_.push_back({r, activate});
        menuActions_.push_back(activate);
        iy += 30;
    }
    // pomoc – sterowanie
    float hy = iy + 6;
    const char* help[] = {
        "Strzałki: ruch  Spacja/Shift: strzał",
        "Enter: START  Tab: demo  Esc: poddanie planszy",
        "Backspace: powrót do ekranu tytułowego",
    };
    for (const char* h : help) {
        drawTextCentered(viewW / 2.0f, hy, h, 1, kGray);
        hy += 12;
    }
}

void App::drawSaves() {
    fillRect(0, 0, viewW, viewH, {0, 0, 0, 170});
    fillRect(16, 12, viewW - 32, viewH - 24, kPanel);
    frameRect(16, 12, viewW - 32, viewH - 24, kDim);
    drawText(32, 26, "Zapisy gry", 2, kOrange);
    const bool canSave = game_ && game_->canSave();
    button(viewW - 32 - textWidth("Zapisz bieżący stan (S)", 1) - 16, 24, "Zapisz bieżący stan (S)", 1,
           canSave ? kWhite : kDim, [this] { saveCurrent(); });

    const auto& list = store_->entries();
    const int n = static_cast<int>(list.size());
    if (n == 0) {
        drawTextCentered(viewW / 2.0f, 200, "Brak zapisów.", 2, kGray);
        drawTextCentered(viewW / 2.0f, 230, "Podczas gry naciśnij " + shortcut("S") + ", aby zapisać stan.", 1,
                         kGray);
    } else {
        const int visible = 3;
        savesSel_ = std::clamp(savesSel_, 0, n - 1);
        if (savesSel_ < savesScroll_) savesScroll_ = savesSel_;
        if (savesSel_ >= savesScroll_ + visible) savesScroll_ = savesSel_ - visible + 1;
        savesScroll_ = std::clamp(savesScroll_, 0, std::max(0, n - visible));
        float y = 56;
        for (int i = savesScroll_; i < std::min(n, savesScroll_ + visible); ++i) {
            const SaveEntry& e = list[i];
            const SDL_FRect row{28, y, viewW - 56.0f, 128};
            if (i == savesSel_) fillRect(row.x, row.y, row.w, row.h, kSelect);
            else if (hovered(row)) fillRect(row.x, row.y, row.w, row.h, {40, 40, 60, 255});
            buttons_.push_back({row, [this, i] { savesSel_ = i; }});
            if (SDL_Texture* t = thumbTexture(e)) {
                const SDL_FRect dst{row.x + 6, row.y + 4, 168, 120};
                SDL_RenderTexture(renderer_, t, nullptr, &dst);
            }
            const float tx = row.x + 190;
            drawText(tx, row.y + 12, "Plansza " + std::to_string(e.level), 2, kWhite);
            drawText(tx, row.y + 40, "Wynik " + e.score + "   Życia " + e.lives, 1, kGray);
            drawText(tx, row.y + 56, formatDate(e.date), 1, kGray);
            const uint64_t id = e.id;
            button(tx, row.y + 86, "Wczytaj", 2, kGreen, [this, i] {
                const auto& l = store_->entries();
                if (i < static_cast<int>(l.size())) loadSave(l[i]);
            });
            button(tx + 150, row.y + 86, "Usuń", 2, kRed, [this, id] { deleteSave(id); });
            y += 132;
        }
        if (n > visible) {
            drawText(viewW - 120, viewH - 40, std::to_string(savesSel_ + 1) + "/" + std::to_string(n), 1, kGray);
        }
    }
    drawTextCentered(viewW / 2.0f, viewH - 28, "Enter: wczytaj  Delete: usuń  S: zapisz  Esc: zamknij", 1, kGray);
}

void App::drawLevels() {
    fillRect(0, 0, viewW, viewH, {0, 0, 0, 170});
    fillRect(8, 8, viewW - 16, viewH - 16, kPanel);
    frameRect(8, 8, viewW - 16, viewH - 16, kDim);
    drawText(24, 18, "Wybierz planetę (56 planet oryginału)", 2, kOrange);
    drawText(24, 40, "Gra zacznie się od wybranej planety z wynikiem 0 i 8 życiami.", 1, kGray);

    const int cols = 8, rowsVisible = 2;
    const int totalRows = (AtariData::levelCount + cols - 1) / cols;
    levelScroll_ = std::clamp(levelScroll_, 0, totalRows - rowsVisible);
    const float cellW = (viewW - 32.0f) / cols, cellH = 188;
    const float tw = 64, th = 124 * 64.0f / 64.0f;   // miniatura: 1/4 z 256×496
    for (int r = 0; r < rowsVisible; ++r) {
        for (int c = 0; c < cols; ++c) {
            const int i = (levelScroll_ + r) * cols + c;
            if (i >= AtariData::levelCount) break;
            const float x = 16 + c * cellW, y = 60 + r * cellH;
            const SDL_FRect cell{x + 2, y, cellW - 4, cellH - 8};
            const bool hot = hovered(cell);
            if (i == levelSel_) fillRect(cell.x, cell.y, cell.w, cell.h, kSelect);
            else if (hot) fillRect(cell.x, cell.y, cell.w, cell.h, {40, 40, 60, 255});
            const SDL_FRect img{x + (cellW - tw) / 2, y + 8, tw, th};
            if (i < static_cast<int>(levelTex_.size()) && levelTex_[i]) {
                SDL_RenderTexture(renderer_, levelTex_[i], nullptr, &img);
            }
            frameRect(img.x - 1, img.y - 1, img.w + 2, img.h + 2, kDim);
            drawTextCentered(x + cellW / 2, y + 8 + th + 10, std::to_string(i + 1), 2, kWhite);
            buttons_.push_back({cell, [this, i] { startLevel(i); }});
        }
    }
    const std::string range = "Plansze " + std::to_string(levelScroll_ * cols + 1) + "-" +
                              std::to_string(std::min(AtariData::levelCount, (levelScroll_ + rowsVisible) * cols));
    drawText(24, viewH - 30, range, 1, kGray);
    if (levelScroll_ > 0) button(viewW - 190, viewH - 34, "Wyżej", 1, kWhite, [this] { levelScroll_ -= 1; });
    if (levelScroll_ < totalRows - rowsVisible)
        button(viewW - 110, viewH - 34, "Niżej", 1, kWhite, [this] { levelScroll_ += 1; });
    drawTextCentered(viewW / 2.0f, viewH - 30, "Strzałki, Enter, Esc", 1, kGray);
}

void App::drawToast() {
    if (message_.empty()) return;
    if (SDL_GetTicks() > messageUntil_) {
        message_.clear();
        return;
    }
    // długie komunikaty skracamy do szerokości okna
    std::string text = message_;
    float scale = 2;
    if (textWidth(text, scale) > viewW - 60) scale = 1;
    const float w = std::min(textWidth(text, scale), viewW - 60.0f) + 28, h = 8 * scale + 16;
    const float x = (viewW - w) / 2, y = 12;   // u góry – nie zasłania paska statusu
    fillRect(x, y, w, h, {0, 0, 0, 200});
    drawText(x + 14, y + 8, text, scale, kWhite);
}

void App::buildFontTexture() {
    if (fontTex_) { SDL_DestroyTexture(fontTex_); fontTex_ = nullptr; }
    if (!data_) return;
    const auto font = data_->font();
    std::vector<uint32_t> px(128 * 64, 0);
    for (int g = 0; g < 128; ++g) {
        const int gx = (g % 16) * 8, gy = (g / 16) * 8;
        for (int line = 0; line < 8; ++line) {
            const uint8_t b = font[g * 8 + line];
            for (int bit = 0; bit < 8; ++bit) {
                if (b & (0x80 >> bit)) px[(gy + line) * 128 + gx + bit] = 0xFFFFFFFFu;
            }
        }
    }
    fontTex_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, 128, 64);
    if (!fontTex_) return;
    SDL_UpdateTexture(fontTex_, nullptr, px.data(), 128 * 4);
    SDL_SetTextureBlendMode(fontTex_, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(fontTex_, SDL_SCALEMODE_NEAREST);
}

SDL_Texture* App::thumbTexture(const SaveEntry& e) {
    auto it = thumbTex_.find(e.id);
    if (it != thumbTex_.end()) return it->second;
    if (e.thumbnail.size() != size_t(SaveEntry::thumbWidth) * SaveEntry::thumbHeight) return nullptr;
    SDL_Texture* t = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC,
                                       SaveEntry::thumbWidth, SaveEntry::thumbHeight);
    if (!t) return nullptr;
    SDL_UpdateTexture(t, nullptr, e.thumbnail.data(), SaveEntry::thumbWidth * 4);
    SDL_SetTextureScaleMode(t, SDL_SCALEMODE_NEAREST);
    thumbTex_[e.id] = t;
    return t;
}

void App::ensureLevelTextures() {
    if (!data_ || !levelTex_.empty()) return;
    for (const Level& lv : data_->levels) {
        const auto px = Renderer::levelPreview(lv, *data_);
        SDL_Texture* t = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC,
                                           Renderer::previewWidth, Renderer::previewHeight);
        if (t) {
            SDL_UpdateTexture(t, nullptr, px.data(), Renderer::previewWidth * 4);
            SDL_SetTextureScaleMode(t, SDL_SCALEMODE_LINEAR);
        }
        levelTex_.push_back(t);
    }
}

void App::freeTextures() {
    for (auto& kv : thumbTex_) SDL_DestroyTexture(kv.second);
    thumbTex_.clear();
    for (auto* t : levelTex_) if (t) SDL_DestroyTexture(t);
    levelTex_.clear();
}

void App::fillRect(float x, float y, float w, float h, SDL_Color c) {
    SDL_SetRenderDrawColor(renderer_, c.r, c.g, c.b, c.a);
    const SDL_FRect r{x, y, w, h};
    SDL_RenderFillRect(renderer_, &r);
}

void App::frameRect(float x, float y, float w, float h, SDL_Color c) {
    SDL_SetRenderDrawColor(renderer_, c.r, c.g, c.b, c.a);
    const SDL_FRect r{x, y, w, h};
    SDL_RenderRect(renderer_, &r);
}

float App::textWidth(const std::string& s, float scale) const {
    return static_cast<float>(utf8Decode(s).size()) * 8.0f * scale;
}

/// Tekst czcionką oryginalnej gry ($2000, z polskimi literami); znaki, których w niej nie ma
/// (oraz wszystko przed wczytaniem danych), rysuje wbudowana czcionka SDL.
void App::drawText(float x, float y, const std::string& s, float scale, SDL_Color c) {
    float cx = x;
    const float step = 8.0f * scale;
    for (char32_t ch : utf8Decode(s)) {
        const int code = fontTex_ ? AtariData::textCode(ch) : -1;
        if (code > 0) {
            const SDL_FRect src{static_cast<float>((code % 16) * 8), static_cast<float>((code / 16) * 8), 8, 8};
            const SDL_FRect dst{cx, y, step, step};
            SDL_SetTextureColorMod(fontTex_, c.r, c.g, c.b);
            SDL_SetTextureAlphaMod(fontTex_, c.a);
            SDL_RenderTexture(renderer_, fontTex_, &src, &dst);
        } else if (code < 0) {
            const char a[2] = {asciiFold(ch), 0};
            if (a[0] != ' ') {
                SDL_SetRenderDrawColor(renderer_, c.r, c.g, c.b, c.a);
                SDL_SetRenderScale(renderer_, scale, scale);
                SDL_RenderDebugText(renderer_, cx / scale, y / scale, a);
                SDL_SetRenderScale(renderer_, 1, 1);
            }
        }
        cx += step;
    }
}

void App::drawTextCentered(float cx, float y, const std::string& s, float scale, SDL_Color c) {
    drawText(cx - textWidth(s, scale) / 2, y, s, scale, c);
}

void App::drawWrapped(float cx, float y, float maxW, const std::string& s, float scale, SDL_Color c, float lineH) {
    std::vector<std::string> words;
    std::string cur;
    for (char ch : s) {
        if (ch == ' ') { if (!cur.empty()) words.push_back(cur); cur.clear(); }
        else cur += ch;
    }
    if (!cur.empty()) words.push_back(cur);
    std::string line;
    for (const auto& w : words) {
        const std::string test = line.empty() ? w : line + " " + w;
        if (textWidth(test, scale) > maxW && !line.empty()) {
            drawTextCentered(cx, y, line, scale, c);
            y += lineH;
            line = w;
        } else {
            line = test;
        }
    }
    if (!line.empty()) drawTextCentered(cx, y, line, scale, c);
}

bool App::button(float x, float y, const std::string& label, float scale, SDL_Color c, std::function<void()> action) {
    const SDL_FRect r{x, y - 5, textWidth(label, scale) + 16, 8 * scale + 10};
    const bool hot = hovered(r);
    fillRect(r.x, r.y, r.w, r.h, hot ? SDL_Color{70, 70, 100, 255} : SDL_Color{36, 36, 52, 255});
    frameRect(r.x, r.y, r.w, r.h, hot ? kOrange : kDim);
    drawText(x + 8, y, label, scale, c);
    buttons_.push_back({r, std::move(action)});
    return hot;
}

bool App::hovered(const SDL_FRect& r) const {
    return mouseX_ >= r.x && mouseX_ < r.x + r.w && mouseY_ >= r.y && mouseY_ < r.y + r.h;
}

// MARK: - Tryb testowy (--shots): zrzuty kolejnych ekranów interfejsu

void App::saveWindowShot(const std::string& path) {
    SDL_Surface* s = SDL_RenderReadPixels(renderer_, nullptr);
    if (!s) return;
    SDL_SavePNG(s, path.c_str());
    SDL_DestroySurface(s);
}

void App::testShots() {
    const Uint64 t = SDL_GetTicks() - startTicks_;
    const std::string dir = options_.shotsDir + "/";
    auto key = [this](SDL_Keycode k, bool down) {
        SDL_Event e{};
        e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
        e.key.key = k;
        e.key.down = down;
        handleEvent(e);
    };
    struct Step { Uint64 at; std::function<void()> fn; };
    const std::vector<Step> steps = {
        {1500, [&] { saveWindowShot(dir + "01_start.png"); }},
        {9000, [&] { saveWindowShot(dir + "02_title.png"); }},
        {9100, [&] { openOverlay(Overlay::menu); }},
        {9300, [&] { saveWindowShot(dir + "03_menu.png"); }},
        {9400, [&] { openOverlay(Overlay::levels); }},
        {9600, [&] { saveWindowShot(dir + "04_levels.png"); }},
        {9700, [&] { startLevel(0); }},
        {12500, [&] { saveWindowShot(dir + "05_level1.png"); }},
        {12600, [&] { key(SDLK_RIGHT, true); }},
        {13400, [&] { key(SDLK_RIGHT, false); key(SDLK_DOWN, true); }},
        {14600, [&] { key(SDLK_DOWN, false); saveWindowShot(dir + "06_moved.png"); }},
        {14700, [&] { saveCurrent(); openOverlay(Overlay::saves); }},
        {14900, [&] { saveWindowShot(dir + "07_saves.png"); }},
        {15000, [&] { closeOverlay(); key(SDLK_TAB, false); key(SDLK_BACKSPACE, true); }},
        {15100, [&] { key(SDLK_TAB, true); }},
        {15300, [&] { key(SDLK_TAB, false); }},
        {21000, [&] { saveWindowShot(dir + "08_demo.png"); }},
        {21100, [&] { running_ = false; }},
    };
    while (shotStep_ < static_cast<int>(steps.size()) && t >= steps[shotStep_].at) {
        steps[shotStep_].fn();
        ++shotStep_;
    }
}
