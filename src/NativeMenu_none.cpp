//
//  NativeMenu_none.cpp
//  Robbo (C++ / SDL3)
//
//  Systemy bez menu systemowego (Linux): wszystko jest w menu w grze pod F1.
//

#include "NativeMenu.h"

namespace NativeMenu {

void install(SDL_Window*, Uint32) {}
void update(const State&) {}
void setFullscreen(bool) {}
bool available() { return false; }

}  // namespace NativeMenu
