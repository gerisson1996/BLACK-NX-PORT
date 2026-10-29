#pragma once
// The few SDL3 names Black-Android's game sources use (src/common/menu.cpp: the display's refresh rate;
// src/game/exit_menu.cpp: closing the app), answered with libnx on the Switch so those sources build unchanged.
#include <switch.h>

#include <cstdint>

struct SDL_Window;
typedef uint32_t SDL_DisplayID;

struct SDL_DisplayMode {
    int w, h;
    float pixel_density;
    float refresh_rate;
};

inline SDL_DisplayID SDL_GetPrimaryDisplay() { return 1; }
inline SDL_DisplayID SDL_GetDisplayForWindow(SDL_Window*) { return 1; }
// the Switch's screen and its TV output both run at 60 Hz
inline const SDL_DisplayMode* SDL_GetCurrentDisplayMode(SDL_DisplayID) {
    static SDL_DisplayMode m{1280, 720, 1.0f, 60.0f};
    return &m;
}

enum : uint32_t { SDL_EVENT_QUIT = 0x100 };

union SDL_Event {
    uint32_t type;
    uint8_t padding[128];
};

namespace kernel {
[[noreturn]] void Quit();
}

// the quit option of the game's exit menu: the title leaves on its own, the system told first. appletRequestExitToSelf
// does nothing for a homebrew run through hbloader
inline bool SDL_PushEvent(SDL_Event* e) {
    if (e && e->type == SDL_EVENT_QUIT) kernel::Quit();
    return true;
}
