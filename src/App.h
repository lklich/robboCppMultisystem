//
//  App.h
//  Robbo (C++ / SDL3)
//
//  Warstwa systemowa: okno, obraz, dźwięk, klawiatura/gamepad, przeciąganie plików,
//  okna wyboru plików oraz menu rysowane w grze (zastępuje SwiftUI/AppKit z wersji macOS).
//

#pragma once

#include "AtariData.h"
#include "Game.h"
#include "Pokey.h"
#include "SaveStore.h"

#include <SDL3/SDL.h>

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

class App {
public:
    struct Options {
        std::string shotsDir;   // tryb testowy: zrzuty ekranów interfejsu do katalogu i wyjście
    };

    App();
    ~App();

    bool init(const Options& options);
    void run();

    // Logiczna rozdzielczość interfejsu (2 × ekran Atari 336×240).
    static constexpr int viewW = Renderer::width * 2;
    static constexpr int viewH = Renderer::height * 2;

private:
    enum class Overlay { none, menu, saves, levels };
    enum class DialogPurpose { tape, selfTest };

    struct Button {
        SDL_FRect rect;
        std::function<void()> action;
    };

    // SDL
    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture* screenTex_ = nullptr;
    SDL_Texture* fontTex_ = nullptr;
    SDL_AudioStream* audio_ = nullptr;
    std::vector<float> audioBuf_;
    Uint32 dialogEvent_ = 0;
    Uint32 menuEvent_ = 0;
    std::vector<SDL_Gamepad*> pads_;

    // Gra
    std::unique_ptr<AtariData> data_;
    std::unique_ptr<Pokey> pokey_;
    std::unique_ptr<Game> game_;
    std::unique_ptr<SaveStore> store_;
    bool muted_ = false;

    // Stan interfejsu
    bool running_ = true;
    Overlay overlay_ = Overlay::none;
    int menuSel_ = 0;
    bool confirmForget_ = false;
    int savesSel_ = 0, savesScroll_ = 0;
    int levelSel_ = 0, levelScroll_ = 0;
    std::vector<Button> buttons_;
    std::vector<std::function<void()>> menuActions_;
    float mouseX_ = -1, mouseY_ = -1;
    std::string message_;
    Uint64 messageUntil_ = 0;
    std::string importError_;
    std::string windowTitle_ = "Robbo";
    std::vector<std::string> dropped_;
    std::map<uint64_t, SDL_Texture*> thumbTex_;
    std::vector<SDL_Texture*> levelTex_;

    // Wyniki okien wyboru plików (wywołanie zwrotne może przyjść z innego wątku)
    std::mutex dialogLock_;
    std::vector<std::string> dialogFiles_;
    DialogPurpose dialogPurpose_ = DialogPurpose::tape;
    bool dialogOpen_ = false;

    // Wejście: klawiatura, przyciski i gałka gamepada – łączone przed przekazaniem do gry
    static constexpr int keyCount = 10;
    bool kb_[keyCount] = {};
    bool pad_[keyCount] = {};
    bool axis_[keyCount] = {};
    bool applied_[keyCount] = {};

    // Tryb testowy
    Options options_;
    Uint64 startTicks_ = 0;
    int shotStep_ = 0;

    // Dane
    void loadCachedData();
    void setData(std::vector<uint8_t> memory);
    void forgetOriginal();
    void importTape(const std::vector<std::string>& paths);
    void chooseTape();
    void runSelfTest();
    void openDialog(DialogPurpose purpose, bool many);
    static void SDLCALL dialogCallback(void* userdata, const char* const* filelist, int filter);
    void handleDialogResult();
    void handleMenuCommand(int command);
    void confirmForget();

    // Dźwięk
    static void SDLCALL audioCallback(void* userdata, SDL_AudioStream* stream, int additional, int total);
    void replacePokey(std::unique_ptr<Pokey> p);

    // Zdarzenia
    void handleEvent(const SDL_Event& e);
    void handleKeyDown(const SDL_KeyboardEvent& k);
    void handleKeyUp(const SDL_KeyboardEvent& k);
    bool handleShortcut(const SDL_KeyboardEvent& k);
    void handleOverlayKey(const SDL_KeyboardEvent& k);
    void handleGamepad(const SDL_Event& e);
    void syncInput();
    void releaseInput();
    static int gameKeyFor(SDL_Keycode key);

    // Akcje
    void openOverlay(Overlay o);
    void closeOverlay();
    void saveCurrent();
    void loadSave(const SaveEntry& e);
    void deleteSave(uint64_t id);
    void startLevel(int index);
    void toggleFullscreen();
    void toggleMute();
    void show(const std::string& text);

    // Rysowanie
    void draw();
    void drawImportScreen();
    void drawMenu();
    void drawSaves();
    void drawLevels();
    void drawToast();
    void buildFontTexture();
    SDL_Texture* thumbTexture(const SaveEntry& e);
    void ensureLevelTextures();
    void freeTextures();
    void fillRect(float x, float y, float w, float h, SDL_Color c);
    void frameRect(float x, float y, float w, float h, SDL_Color c);
    float textWidth(const std::string& s, float scale) const;
    void drawText(float x, float y, const std::string& s, float scale, SDL_Color c);
    void drawTextCentered(float cx, float y, const std::string& s, float scale, SDL_Color c);
    void drawWrapped(float cx, float y, float maxW, const std::string& s, float scale, SDL_Color c, float lineH);
    bool button(float x, float y, const std::string& label, float scale, SDL_Color c,
                std::function<void()> action);
    bool hovered(const SDL_FRect& r) const;

    void testShots();
    void saveWindowShot(const std::string& path);
};
