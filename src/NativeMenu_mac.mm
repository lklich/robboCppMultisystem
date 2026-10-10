//
//  NativeMenu_mac.mm
//  Robbo (C++ / SDL3)
//
//  Menu „Gra” na pasku menu macOS (jak CommandMenu("Gra") w wersji SwiftUI).
//

#import <Cocoa/Cocoa.h>

#include "NativeMenu.h"

namespace {
Uint32 gEventType = 0;
NativeMenu::State gState;
}

@interface RobboMenuTarget : NSObject <NSMenuItemValidation>
@end

@implementation RobboMenuTarget

- (void)command:(NSMenuItem*)sender {
    SDL_Event e;
    SDL_zero(e);
    e.type = gEventType;
    e.user.code = static_cast<Sint32>(sender.tag);
    SDL_PushEvent(&e);
}

- (BOOL)validateMenuItem:(NSMenuItem*)item {
    switch (item.tag) {
    case NativeMenu::cmdSave:
        return gState.canSave;
    case NativeMenu::cmdSaves:
    case NativeMenu::cmdLevels:
    case NativeMenu::cmdForget:
        return gState.ready;
    case NativeMenu::cmdMute:
        item.title = gState.muted ? @"Włącz dźwięk" : @"Wyłącz dźwięk";
        return YES;
    default:
        return YES;
    }
}

@end

namespace {
RobboMenuTarget* gTarget = nil;

void addItem(NSMenu* menu, NSString* title, NSString* key, int tag) {
    NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:title action:@selector(command:) keyEquivalent:key];
    item.target = gTarget;
    item.tag = tag;
    [menu addItem:item];
}
}  // namespace

namespace NativeMenu {

void install(SDL_Window*, Uint32 eventType) {
    gEventType = eventType;
    @autoreleasepool {
        NSMenu* main = [NSApp mainMenu];
        if (!main) {
            main = [[NSMenu alloc] init];
            [NSApp setMainMenu:main];
        }
        gTarget = [[RobboMenuTarget alloc] init];   // celowo nigdy nie zwalniany

        NSMenu* game = [[NSMenu alloc] initWithTitle:@"Gra"];
        game.autoenablesItems = YES;
        addItem(game, @"Zapisz stan gry", @"s", cmdSave);
        addItem(game, @"Lista zapisów…", @"l", cmdSaves);
        [game addItem:[NSMenuItem separatorItem]];
        addItem(game, @"Wybierz planszę…", @"p", cmdLevels);
        [game addItem:[NSMenuItem separatorItem]];
        addItem(game, @"Menu w grze (F1)", @"", cmdGameMenu);
        addItem(game, @"Wyłącz dźwięk", @"d", cmdMute);
        [game addItem:[NSMenuItem separatorItem]];
        addItem(game, @"Wczytaj dane z kasety…", @"o", cmdLoadTape);
        addItem(game, @"Usuń dane oryginału z tego Maca", @"", cmdForget);

        NSMenuItem* top = [[NSMenuItem alloc] initWithTitle:@"Gra" action:nil keyEquivalent:@""];
        top.submenu = game;
        // za menu aplikacji (Robbo), przed menu Window
        [main insertItem:top atIndex:(main.numberOfItems > 0 ? 1 : 0)];
    }
}

void update(const State& state) { gState = state; }

void setFullscreen(bool) {}

bool available() { return true; }

}  // namespace NativeMenu
