// Android's input on port 0 beside the controllers: the touch controls, and a keyboard and mouse when one is plugged
// in. It is src/common/mouse_keyboard.h: the game's hooks (mouse look, the menu cursor, the key prompts, the bindings
// on the options pages) read it. SDL's keys arrive as the virtual keys of common/keys.h.
//
// In play the left of the screen is a stick, a drag on the right turns the view as the mouse does and the buttons
// press the controller's. In a menu a touch is the mouse cursor: a tap clicks, a drag on a slider slides it.
#include "common/mouse_keyboard.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string_view>
#include <utility>
#include <vector>

#include <imgui.h>
#include <toml++/toml.hpp>

#include "app/app.h"
#include "common/aspect.h"
#include "common/keys.h"
#include "common/menu.h"
#include "common/options.h"
#include "common/translations.h"
#include "host.h"

namespace input {

namespace {

std::atomic<int> g_dx{0};
std::atomic<int> g_dy{0};
std::atomic<int> g_touch_dx{0};  // the touch screen's look, scaled by its own sensitivity
std::atomic<int> g_touch_dy{0};
std::atomic<uint64_t> g_take_ms{0};     // TakeMouse, asked every tick in play
std::atomic<uint64_t> g_capture_ms{0};  // PressedKey
std::atomic<bool> g_settle{false};
std::atomic<uint64_t> g_confirm_until{0};
std::atomic<bool> g_menu{false};
std::atomic<bool> g_keyboard{false};  // the keyboard or mouse used last
std::atomic<bool> g_touch{true};      // the touch controls used last, not a controller or the keys
std::atomic<bool> g_text{false};
std::atomic<bool> g_text_shown{false};  // the on-screen keyboard, main thread
std::mutex g_chars_lock;
std::u16string g_chars;  // typed, not yet taken
constexpr size_t kMaxChars = 64;
std::atomic<int> g_wheel{0};
constexpr uint64_t kPlayMs = 250;
constexpr uint64_t kTapMs = 80;
constexpr uint64_t kCaptureMs = 250;  // PressedKey asked this recently: a key capture is up
// The wheel binds as two keys, codes no key has. A notch holds what is bound to it for one tap.
constexpr int kWheelUp = 0x0E;
constexpr int kWheelDown = 0x0F;
std::atomic<uint64_t> g_wheel_ms[2] = {};  // the last notch up, down
// the keys and mouse buttons held, by virtual key
std::array<std::atomic<bool>, 256> g_down{};

uint64_t NowMs() {
    auto since = std::chrono::steady_clock::now().time_since_epoch();
    return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(since).count()) + 1;
}

// what an action presses: 0-7 analog A B X Y black white LT RT, 8-15 the buttons bits, 16-19 the move keys,
// 20-23 the look keys
struct Target {
    Action action;
    int button;
};
constexpr int kHostKey = -2;
constexpr int kWalk = -3;
constexpr int kFirstButtonBit = 8;
constexpr int kStart = 12;
constexpr int kFirstMoveKey = 16;
constexpr int kFirstLookKey = 20;
constexpr int kStickMax = 32767;
// the game's default controller layout, as src/common/mouse_keyboard.cpp has it
constexpr Target kActions[] = {
    {{"move_forward", "MOVE FORWARD", "W"}, 16},
    {{"move_back", "MOVE BACK", "S"}, 17},
    {{"strafe_left", "STRAFE LEFT", "A"}, 18},
    {{"strafe_right", "STRAFE RIGHT", "D"}, 19},
    {{"walk", "WALK", "Alt"}, kWalk},
    {{"crouch", "CROUCH", "C"}, 14},
    {{"fire", "FIRE", "LMB"}, 7},
    {{"aim", "AIM", "RMB"}, 15},
    {{"melee", "MELEE", "V"}, 1},
    {{"reload", "RELOAD", "R"}, 0},
    {{"interact", "INTERACT", "F"}, 2},
    {{"grenade", "THROW GRENADE", "G"}, 6},
    {{"weapon_up", "CHANGE WEAPON UP", "E,Wheel Up"}, 11},
    {{"weapon_down", "CHANGE WEAPON DOWN", "Q,Wheel Down"}, 10},
    {{"suppressor", "TOGGLE SUPPRESSOR", "T"}, 3},
    {{"firing_mode", "FIRING MODE / SCOPE ZOOM", "X"}, 8},
    {{"health_pack", "HEALTH PACK", "H"}, 9},
    {{"next_track", "NEXT MUSIC TRACK", "N"}, 5},
    {{"back", "OBJECTIVES", "Tab"}, 13},
    {{"pause", "PAUSE MENU", "Escape"}, 12},
    {{"hide_hud", "TOGGLE HUD", ""}, kHostKey},
    {{"settings_panel", "SETTINGS PANEL", "F10"}, kHostKey},
    {{"look_up", "LOOK UP", "Up"}, 20},
    {{"look_down", "LOOK DOWN", "Down"}, 21},
    {{"look_left", "LOOK LEFT", "Left"}, 22},
    {{"look_right", "LOOK RIGHT", "Right"}, 23},
};
constexpr size_t kCount = std::size(kActions);
constexpr size_t HostAction(std::string_view id) {
    size_t i = 0;
    while (kActions[i].action.id != id) i++;
    return i;
}
constexpr size_t kHudAction = HostAction("hide_hud");
constexpr size_t kPanelAction = HostAction("settings_panel");
struct Name {
    int vk;
    const char* name;
};
constexpr Name kNames[] = {
    {VK_LBUTTON, "LMB"}, {VK_RBUTTON, "RMB"}, {VK_MBUTTON, "MMB"}, {VK_XBUTTON1, "Mouse4"}, {VK_XBUTTON2, "Mouse5"},
    {kWheelUp, "Wheel Up"}, {kWheelDown, "Wheel Down"},
    {VK_SPACE, "Space"}, {VK_RETURN, "Enter"}, {VK_ESCAPE, "Escape"}, {VK_BACK, "Backspace"}, {VK_TAB, "Tab"},
    {VK_SHIFT, "Shift"}, {VK_CONTROL, "Ctrl"}, {VK_MENU, "Alt"}, {VK_CAPITAL, "CapsLock"},
    {VK_UP, "Up"}, {VK_DOWN, "Down"}, {VK_LEFT, "Left"}, {VK_RIGHT, "Right"},
    {VK_INSERT, "Insert"}, {VK_DELETE, "Delete"}, {VK_HOME, "Home"}, {VK_END, "End"},
    {VK_PRIOR, "PageUp"}, {VK_NEXT, "PageDown"},
    {'A', "A"}, {'B', "B"}, {'C', "C"}, {'D', "D"}, {'E', "E"}, {'F', "F"}, {'G', "G"}, {'H', "H"}, {'I', "I"},
    {'J', "J"}, {'K', "K"}, {'L', "L"}, {'M', "M"}, {'N', "N"}, {'O', "O"}, {'P', "P"}, {'Q', "Q"}, {'R', "R"},
    {'S', "S"}, {'T', "T"}, {'U', "U"}, {'V', "V"}, {'W', "W"}, {'X', "X"}, {'Y', "Y"}, {'Z', "Z"},
    {'0', "0"}, {'1', "1"}, {'2', "2"}, {'3', "3"}, {'4', "4"}, {'5', "5"}, {'6', "6"}, {'7', "7"}, {'8', "8"},
    {'9', "9"},
    {VK_F1, "F1"}, {VK_F2, "F2"}, {VK_F4, "F4"}, {VK_F5, "F5"}, {VK_F6, "F6"}, {VK_F7, "F7"},
    {VK_F8, "F8"}, {VK_F9, "F9"}, {VK_F10, "F10"}, {VK_F11, "F11"}, {VK_F12, "F12"},
    {VK_NUMPAD0, "Numpad0"}, {VK_NUMPAD1, "Numpad1"}, {VK_NUMPAD2, "Numpad2"}, {VK_NUMPAD3, "Numpad3"},
    {VK_NUMPAD4, "Numpad4"}, {VK_NUMPAD5, "Numpad5"}, {VK_NUMPAD6, "Numpad6"}, {VK_NUMPAD7, "Numpad7"},
    {VK_NUMPAD8, "Numpad8"}, {VK_NUMPAD9, "Numpad9"}, {VK_ADD, "NumpadPlus"}, {VK_SUBTRACT, "NumpadMinus"},
    {VK_MULTIPLY, "NumpadStar"}, {VK_DIVIDE, "NumpadSlash"}, {VK_DECIMAL, "NumpadPeriod"},
    {VK_OEM_3, "Backtick"}, {VK_OEM_MINUS, "Minus"}, {VK_OEM_PLUS, "Equals"}, {VK_OEM_4, "LBracket"},
    {VK_OEM_6, "RBracket"}, {VK_OEM_5, "Backslash"}, {VK_OEM_1, "Semicolon"}, {VK_OEM_7, "Quote"},
    {VK_OEM_COMMA, "Comma"}, {VK_OEM_PERIOD, "Period"}, {VK_OEM_2, "Slash"},
};

// not bindable: in the game's menus the arrows are the dpad, Enter accepts, Backspace backs out
constexpr int kFixed[] = {VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT, VK_RETURN, VK_BACK};

using Binds = std::array<std::vector<int>, kCount>;
std::mutex g_bind_lock;
Binds g_binds;

int Vk(const std::string& name) {
    for (const Name& n : kNames)
        if (strcasecmp(n.name, name.c_str()) == 0) return n.vk;
    return 0;
}

const char* NameOf(int vk) {
    for (const Name& n : kNames)
        if (n.vk == vk) return n.name;
    return nullptr;
}

std::vector<int> Parse(const std::string& list) {
    std::vector<int> out;
    size_t start = 0;
    while (start <= list.size()) {
        size_t comma = list.find(',', start);
        if (comma == std::string::npos) comma = list.size();
        if (int vk = Vk(list.substr(start, comma - start))) out.push_back(vk);
        start = comma + 1;
    }
    return out;
}

std::string Join(const std::vector<int>& vks) {
    std::string s;
    for (int vk : vks) {
        if (!s.empty()) s += ',';
        s += NameOf(vk);
    }
    return s;
}

bool Active() { return host::Foreground() && !app::MenuOpen(); }
bool Wheel(int vk) { return vk == kWheelUp || vk == kWheelDown; }
bool Down(int vk) {
    if (Wheel(vk)) return NowMs() - g_wheel_ms[vk == kWheelDown] < kTapMs;
    return vk > 0 && vk < 256 && g_down[size_t(vk)];
}
bool Mouse(int vk) {
    bool button = vk == VK_LBUTTON || vk == VK_RBUTTON || vk == VK_MBUTTON || vk == VK_XBUTTON1 || vk == VK_XBUTTON2;
    return button || Wheel(vk);
}
bool Play() { return NowMs() - g_take_ms < kPlayMs; }
bool GameDown(int vk) { return Down(vk) && !(g_menu && vk == VK_DELETE); }
bool KeyCaptureUp() { return NowMs() - g_capture_ms <= kCaptureMs; }

bool HostKeyBound(size_t action, int vk) {
    if (KeyCaptureUp()) return false;
    std::lock_guard lock(g_bind_lock);
    const auto& b = g_binds[action];
    return std::find(b.begin(), b.end(), vk) != b.end();
}

// keys held from before the game had the input wait for release; true once none is held
bool UpdateSettle(const Binds& binds) {
    if (!g_settle) return true;
    for (auto& b : binds)
        for (int vk : b)
            if (Down(vk)) return false;
    for (int vk : kFixed)
        if (Down(vk)) return false;
    g_settle = false;
    return true;
}

struct Held {
    bool used = false;
    bool walk = false;
    int stick[8] = {};  // move forward back left right, look up down left right
};

Held PressActions(const Binds& binds, bool play, Pad& p) {
    Held held;
    auto pressed = [play](int vk) { return GameDown(vk) && (play || !Wheel(vk)); };
    static bool pause_held;
    static bool pause_backs;
    bool pause = false;
    for (size_t i = 0; i < kCount; i++) {
        int b = kActions[i].button;
        if (b == kHostKey || !std::any_of(binds[i].begin(), binds[i].end(), pressed)) continue;
        held.used = true;
        if (g_text && b != kStart) continue;
        if (b == kWalk) {
            held.walk = true;
        } else if (b >= kFirstLookKey && !play) {
            continue;
        } else if (b >= kFirstMoveKey) {
            held.stick[b - kFirstMoveKey] = 1;
        } else if (b < kFirstButtonBit) {
            p.analog[b] = 0xFF;
        } else if (b == kStart) {
            if (!pause_held) pause_backs = g_menu;
            pause = true;
            if (!pause_backs) p.buttons |= uint16_t(1u << (b - kFirstButtonBit));
            else if (g_menu) p.analog[1] = 0xFF;
        } else {
            p.buttons |= uint16_t(1u << (b - kFirstButtonBit));
        }
    }
    pause_held = pause;
    return held;
}

// SDL's scancodes as the virtual keys (common/keys.h) the bindings name, by position as on a US keyboard
int VkOf(SDL_Scancode s) {
    if (s >= SDL_SCANCODE_A && s <= SDL_SCANCODE_Z) return 'A' + (s - SDL_SCANCODE_A);
    if (s >= SDL_SCANCODE_1 && s <= SDL_SCANCODE_9) return '1' + (s - SDL_SCANCODE_1);
    if (s >= SDL_SCANCODE_F1 && s <= SDL_SCANCODE_F12) return VK_F1 + (s - SDL_SCANCODE_F1);
    if (s >= SDL_SCANCODE_KP_1 && s <= SDL_SCANCODE_KP_9) return VK_NUMPAD1 + (s - SDL_SCANCODE_KP_1);
    switch (s) {
        case SDL_SCANCODE_0: return '0';
        case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER: return VK_RETURN;
        case SDL_SCANCODE_ESCAPE: return VK_ESCAPE;
        case SDL_SCANCODE_BACKSPACE: return VK_BACK;
        case SDL_SCANCODE_TAB: return VK_TAB;
        case SDL_SCANCODE_SPACE: return VK_SPACE;
        case SDL_SCANCODE_LSHIFT: case SDL_SCANCODE_RSHIFT: return VK_SHIFT;
        case SDL_SCANCODE_LCTRL: case SDL_SCANCODE_RCTRL: return VK_CONTROL;
        case SDL_SCANCODE_LALT: case SDL_SCANCODE_RALT: return VK_MENU;
        case SDL_SCANCODE_CAPSLOCK: return VK_CAPITAL;
        case SDL_SCANCODE_UP: return VK_UP;
        case SDL_SCANCODE_DOWN: return VK_DOWN;
        case SDL_SCANCODE_LEFT: return VK_LEFT;
        case SDL_SCANCODE_RIGHT: return VK_RIGHT;
        case SDL_SCANCODE_INSERT: return VK_INSERT;
        case SDL_SCANCODE_DELETE: return VK_DELETE;
        case SDL_SCANCODE_HOME: return VK_HOME;
        case SDL_SCANCODE_END: return VK_END;
        case SDL_SCANCODE_PAGEUP: return VK_PRIOR;
        case SDL_SCANCODE_PAGEDOWN: return VK_NEXT;
        case SDL_SCANCODE_KP_0: return VK_NUMPAD0;
        case SDL_SCANCODE_KP_PLUS: return VK_ADD;
        case SDL_SCANCODE_KP_MINUS: return VK_SUBTRACT;
        case SDL_SCANCODE_KP_MULTIPLY: return VK_MULTIPLY;
        case SDL_SCANCODE_KP_DIVIDE: return VK_DIVIDE;
        case SDL_SCANCODE_KP_PERIOD: return VK_DECIMAL;
        case SDL_SCANCODE_GRAVE: return VK_OEM_3;
        case SDL_SCANCODE_MINUS: return VK_OEM_MINUS;
        case SDL_SCANCODE_EQUALS: return VK_OEM_PLUS;
        case SDL_SCANCODE_LEFTBRACKET: return VK_OEM_4;
        case SDL_SCANCODE_RIGHTBRACKET: return VK_OEM_6;
        case SDL_SCANCODE_BACKSLASH: return VK_OEM_5;
        case SDL_SCANCODE_SEMICOLON: return VK_OEM_1;
        case SDL_SCANCODE_APOSTROPHE: return VK_OEM_7;
        case SDL_SCANCODE_COMMA: return VK_OEM_COMMA;
        case SDL_SCANCODE_PERIOD: return VK_OEM_PERIOD;
        case SDL_SCANCODE_SLASH: return VK_OEM_2;
        default: return 0;
    }
}

int VkOfButton(uint8_t b) {
    switch (b) {
        case SDL_BUTTON_LEFT: return VK_LBUTTON;
        case SDL_BUTTON_RIGHT: return VK_RBUTTON;
        case SDL_BUTTON_MIDDLE: return VK_MBUTTON;
        case SDL_BUTTON_X1: return VK_XBUTTON1;
        case SDL_BUTTON_X2: return VK_XBUTTON2;
        default: return 0;
    }
}

}  // namespace

void ReleaseCursor() { g_settle = true; }

bool TakeMouse(float& dx, float& dy) {
    uint64_t now = NowMs();
    bool stale = now - g_take_ms.exchange(now) >= kPlayMs;
    int x = g_dx.exchange(0), tx = g_touch_dx.exchange(0);
    int y = g_dy.exchange(0), ty = g_touch_dy.exchange(0);
    if ((!x && !y && !tx && !ty) || stale || !Active()) return false;
    const game::Options& o = game::Opt();
    dx = float(x) * o.mouse_sensitivity + float(tx) * o.touch_sensitivity;
    dy = float(y) * o.mouse_sensitivity + float(ty) * o.touch_sensitivity;
    if (o.invert_mouse) dy = -dy;
    return true;
}

void Keyboard(Pad& p) {
    if (!Active()) {
        g_settle = true;
        return;
    }
    Binds binds;
    {
        std::lock_guard lock(g_bind_lock);
        binds = g_binds;
    }
    if (!UpdateSettle(binds)) return;
    bool play = Play();
    bool fixed_down = std::any_of(std::begin(kFixed), std::end(kFixed), Down);
    Held held = PressActions(binds, play, p);
    bool confirm = !play && !g_text && (Down(VK_RETURN) || Down(VK_SPACE));
    if (confirm || NowMs() < g_confirm_until) p.analog[0] = 0xFF;
    if ((!play && !g_text && Down(VK_BACK)) || (g_menu && Down(VK_RBUTTON))) p.analog[1] = 0xFF;
    if (!play) {
        for (int i = 0; i < 4; i++) p.buttons |= uint16_t(Down(kFixed[i]) << i);
    }
    const int* stick = held.stick;
    float speed = held.walk ? float(game::Opt().walk_speed) / 100.0f : 1.0f;
    if (int x = stick[3] - stick[2]) p.lx = int16_t(float(x) * speed * float(kStickMax));
    if (int y = stick[0] - stick[1]) p.ly = int16_t(float(y) * speed * float(kStickMax));
    if (int x = stick[7] - stick[6]) p.rx = int16_t(x * kStickMax);
    if (int y = stick[4] - stick[5]) p.ry = int16_t(y * kStickMax);
    if (fixed_down || held.used) g_keyboard = true;
}

void SeenPad(const Pad& p) {
    constexpr int16_t kStickPushed = 16000;
    constexpr uint8_t kAnalogPushed = 0x20;
    auto pushed = [](int16_t v) { return v > kStickPushed || v < -kStickPushed; };
    auto pressed = [](uint8_t a) { return a > kAnalogPushed; };
    bool analog = std::any_of(std::begin(p.analog), std::end(p.analog), pressed);
    bool sticks = pushed(p.lx) || pushed(p.ly) || pushed(p.rx) || pushed(p.ry);
    if (p.buttons || analog || sticks) g_keyboard = false;
}

void SetMenu(bool menu) { g_menu = menu; }

bool KeyboardActive() { return g_keyboard; }

void TapConfirm() { g_confirm_until = NowMs() + kTapMs; }

void SetTextEntry(bool on) {
    if (g_text.exchange(on) == on) return;
    std::lock_guard lock(g_chars_lock);
    g_chars.clear();
}

void OnChar(unsigned c) {
    if (!g_text || !Active() || c > 0xFFFF) return;
    std::lock_guard lock(g_chars_lock);
    if (g_chars.size() < kMaxChars) g_chars += char16_t(c);
}

std::u16string TakeChars() {
    std::lock_guard lock(g_chars_lock);
    return std::exchange(g_chars, {});
}

// a notch a call: notches that came together (a swipe's) step one menu tick each
int TakeWheel() {
    int w = g_wheel.load();
    while (w && !g_wheel.compare_exchange_weak(w, w > 0 ? w - 1 : w + 1)) {
    }
    return w > 0 ? 1 : w < 0 ? -1 : 0;
}

const Action* Actions(size_t* count) {
    static const auto list = [] {
        std::array<Action, kCount> a{};
        for (size_t i = 0; i < kCount; i++) a[i] = kActions[i].action;
        return a;
    }();
    *count = kCount;
    return list.data();
}

std::string Binding(size_t action) {
    std::lock_guard lock(g_bind_lock);
    return action < kCount ? Join(g_binds[action]) : std::string();
}

void Bind(size_t action, int vk, int slot) {
    if (action >= kCount || slot < 0 || slot > 1 || (vk && !NameOf(vk))) return;
    if (kActions[action].button == kHostKey && Mouse(vk)) return;
    {
        std::lock_guard lock(g_bind_lock);
        std::vector<int>& keys = g_binds[action];
        int slots[2] = {0, 0};
        for (size_t i = 0; i < keys.size() && i < 2; i++) slots[i] = keys[i];
        if (vk && slots[1 - slot] == vk) slots[1 - slot] = 0;
        slots[slot] = vk;
        if (!slots[0] && !slots[1] && action == kPanelAction) return;
        keys.clear();
        for (int k : slots) {
            if (k) keys.push_back(k);
        }
        game::Opt().keys[kActions[action].action.id] = Join(keys);
    }
    app::Save();
}

void Unbind(size_t action) {
    if (action >= kCount || action == kPanelAction) return;
    {
        std::lock_guard lock(g_bind_lock);
        g_binds[action].clear();
        game::Opt().keys[kActions[action].action.id].clear();
    }
    app::Save();
}

bool TakeDeletePress() {
    static bool held = false;
    bool down = Down(VK_DELETE);
    bool fresh = down && !held;
    held = down;
    return fresh && Active();
}

void ResetBindings() {
    game::Opt().keys.clear();
    LoadBindings();
    app::Save();
}

void LoadBindings() {
    std::lock_guard lock(g_bind_lock);
    const auto& keys = game::Opt().keys;
    for (size_t i = 0; i < kCount; i++) {
        auto it = keys.find(kActions[i].action.id);
        g_binds[i] = Parse(it != keys.end() ? it->second : kActions[i].action.defaults);
    }
}

std::string ButtonKey(int button) {
    std::lock_guard lock(g_bind_lock);
    for (size_t i = 0; i < kCount; i++) {
        if (kActions[i].button != button || g_binds[i].empty()) continue;
        std::string name = g_binds[i][0] == VK_ESCAPE ? "Esc" : NameOf(g_binds[i][0]);
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return char(std::toupper(c)); });
        return name;
    }
    return {};
}

int PressedKey() {
    g_capture_ms = NowMs();
    for (const Name& n : kNames)
        if (Down(n.vk)) return n.vk;
    return 0;
}

bool PanelKey(int vk) { return HostKeyBound(kPanelAction, vk); }
bool HudKey(int vk) { return HostKeyBound(kHudAction, vk); }

}  // namespace input


// the touch controls
namespace touch {

namespace {

using input::NowMs;

// what a finger on the screen does
enum class Role : uint8_t { None, Stick, Look, Button, Cursor, Drag, Pinch };

// a control of the touch layout: what it shows and presses, on the controller (analog 0-7, button bits from 8) or on
// the host. Where it sits is the layout's, which the player edits and picks presets for
enum : int { kGyro = -2, kPanel = -1, kAnalogA = 0, kAnalogB = 1, kAnalogX = 2, kAnalogY = 3, kAnalogLT = 6, kAnalogRT = 7 };
constexpr int kBit = 8;  // kBit + n: buttons bit n (dpad up down left right, start, back, left thumb, right thumb)
enum class Icon : uint8_t { Bullet, Scope, Mode, Reload, Crouch, Knife, Hand, Grenade, Swap, Health, Map, Pause, Gear, Back, Gyro, Move, Suppressor, Up, Down, Left, Right, Zoom };
// Play while the player has the controls, Game outside the menus (cutscenes too), Menus in the menus and options
enum class When : uint8_t { Play, Menus, Game };
struct Button {
    const char* id;     // its key in layout.toml
    const char* label;  // in the layout editor
    Icon icon;
    int press;
    When when;
    bool look = false;  // a drag from it turns the view as well, as a fire button does
    bool keep = false;  // never hidden: the way back out of the menus
};
// the layout is set in the game's options (Recomp Settings, TOUCH): its preset, opacity, size, and the editor that
// moves the controls, see the functions at the end
constexpr Button kButtons[] = {
    {"fire", "Fire", Icon::Bullet, kAnalogRT, When::Play, true},
    {"fire_left", "Fire (left)", Icon::Bullet, kAnalogRT, When::Play, true},
    {"aim", "Aim", Icon::Scope, kBit + 7, When::Play},
    {"mode", "Firing mode", Icon::Mode, kBit + 0, When::Play},
    {"melee", "Melee", Icon::Knife, kAnalogB, When::Play},
    {"use", "Use", Icon::Hand, kAnalogX, When::Play},
    {"grenade", "Grenade", Icon::Grenade, kAnalogLT, When::Play},
    {"reload", "Reload", Icon::Reload, kAnalogA, When::Play},
    {"crouch", "Crouch", Icon::Crouch, kBit + 6, When::Play},
    {"weapon", "Weapon", Icon::Swap, kBit + 3, When::Play},
    {"health", "Health pack", Icon::Health, kBit + 1, When::Play},
    {"map", "Objectives", Icon::Map, kBit + 5, When::Play},
    {"pause", "Pause", Icon::Pause, kBit + 4, When::Game},
    {"gyro", "Gyroscope", Icon::Gyro, kGyro, When::Play},
    {"back", "Back", Icon::Back, kAnalogB, When::Menus, false, true},
    {"suppressor", "Suppressor", Icon::Suppressor, kAnalogY, When::Play},
    // a dpad in the menus and the options, where a tap on a row is hard to aim
    {"dpad_up", "Up", Icon::Up, kBit + 0, When::Menus},
    {"dpad_down", "Down", Icon::Down, kBit + 1, When::Menus},
    {"dpad_left", "Left", Icon::Left, kBit + 2, When::Menus},
    {"dpad_right", "Right", Icon::Right, kBit + 3, When::Menus},
    // the firing mode's button again, beside use while a scope is up: the scope's level, X2 or X5, and a tap switches
    {"zoom", "Scope zoom", Icon::Zoom, kBit + 0, When::Play},
};
constexpr size_t kZoomButton = std::size(kButtons) - 1;
constexpr float kScoped = 0.6f;  // game::g_zoom under this is a scope's, not a rifle's sights
constexpr float kScopeX5 = 0.36f;  // and under this its X5, between X2's 0.5 and X5's 0.25
constexpr size_t kButtonCount = std::size(kButtons);
constexpr size_t kStick = kButtonCount;  // the stick's place, after the buttons'

// where a control sits: its centre as shares of the screen's width and height, its radius in screen heights
struct Place {
    float x, y, r;
    bool hidden = false;
};
using Layout = std::array<Place, kButtonCount + 1>;
// the thumbs alone: fire under the right thumb, the left fire beside the stick
constexpr Layout kTwoFinger = {{
    {0.924f, 0.66f, 0.095f}, {0.059f, 0.40f, 0.065f}, {0.969f, 0.40f, 0.065f}, {0.91f, 0.40f, 0.045f},
    {0.843f, 0.56f, 0.05f}, {0.789f, 0.40f, 0.055f}, {0.784f, 0.70f, 0.05f}, {0.838f, 0.84f, 0.055f},
    {0.964f, 0.88f, 0.05f}, {0.50f, 0.90f, 0.05f}, {0.559f, 0.90f, 0.045f}, {0.473f, 0.07f, 0.04f},
    {0.527f, 0.07f, 0.04f}, {0.419f, 0.07f, 0.04f},
    {0.036f, 0.08f, 0.045f}, {0.70f, 0.56f, 0.042f},
    {0.11f, 0.60f, 0.048f}, {0.11f, 0.80f, 0.048f}, {0.064f, 0.70f, 0.048f}, {0.156f, 0.70f, 0.048f},
    {0.715f, 0.40f, 0.05f},
    {0.108f, 0.70f, 0.12f},
}};
// claw with the left index finger: fire at the top left, the right thumb turns and aims
constexpr Layout kThreeFinger = {{
    {0.90f, 0.68f, 0.08f}, {0.07f, 0.32f, 0.085f}, {0.955f, 0.42f, 0.06f}, {0.875f, 0.42f, 0.042f},
    {0.80f, 0.60f, 0.045f}, {0.765f, 0.40f, 0.05f}, {0.745f, 0.76f, 0.045f}, {0.84f, 0.87f, 0.05f},
    {0.955f, 0.88f, 0.05f}, {0.50f, 0.90f, 0.05f}, {0.559f, 0.90f, 0.045f}, {0.473f, 0.07f, 0.04f},
    {0.527f, 0.07f, 0.04f}, {0.419f, 0.07f, 0.04f},
    {0.036f, 0.08f, 0.045f}, {0.69f, 0.56f, 0.042f},
    {0.11f, 0.60f, 0.048f}, {0.11f, 0.80f, 0.048f}, {0.064f, 0.70f, 0.048f}, {0.156f, 0.70f, 0.048f},
    {0.69f, 0.40f, 0.045f},
    {0.13f, 0.72f, 0.12f},
}};
// both index fingers on top: fire at the top left, aim and crouch at the top right
constexpr Layout kFourFinger = {{
    {0.90f, 0.70f, 0.075f}, {0.07f, 0.32f, 0.085f}, {0.93f, 0.30f, 0.07f}, {0.97f, 0.55f, 0.04f},
    {0.78f, 0.55f, 0.045f}, {0.73f, 0.38f, 0.05f}, {0.72f, 0.75f, 0.045f}, {0.80f, 0.87f, 0.05f},
    {0.83f, 0.24f, 0.05f}, {0.50f, 0.90f, 0.05f}, {0.559f, 0.90f, 0.045f}, {0.473f, 0.07f, 0.04f},
    {0.527f, 0.07f, 0.04f}, {0.419f, 0.07f, 0.04f},
    {0.036f, 0.08f, 0.045f}, {0.655f, 0.55f, 0.042f},
    {0.11f, 0.60f, 0.048f}, {0.11f, 0.80f, 0.048f}, {0.064f, 0.70f, 0.048f}, {0.156f, 0.70f, 0.048f},
    {0.655f, 0.38f, 0.045f},
    {0.13f, 0.72f, 0.12f},
}};
struct Preset {
    const char* id;
    const char* label;
    const Layout* places;
};
constexpr Preset kPresets[] = {
    {"default", "DEFAULT", &kTwoFinger}, {"three", "3 FINGER", &kThreeFinger}, {"four", "4 FINGER", &kFourFinger}};

constexpr float kStickZone = 0.42f;    // share of the width from the left a touch takes the stick in
constexpr float kLookCounts = 1800.0f;  // mouse counts a swipe across the screen's width turns: 90 degrees
constexpr uint64_t kShownMs = 8000;     // the controls hide this long after the last touch while a pad is plugged in

// a touch in the menus is a tap, a swipe up or down that scrolls, or the cursor held down (a slider dragged): which,
// once it moved or stayed long enough to tell
enum class MenuTouch : uint8_t { Undecided, Cursor, Scroll };
constexpr float kMenuMove = 0.03f;     // screen heights a finger moves before it is a swipe or a drag
constexpr float kScrollNotch = 0.05f;  // screen heights of a swipe a row scrolls, about a row of the menus
constexpr uint64_t kHoldMs = 180;      // a finger still this long is the cursor held down
constexpr int kMaxNotches = 8;         // notches a swipe runs ahead of the menu

struct Finger {
    SDL_FingerID id = 0;
    Role role = Role::None;
    size_t button = 0;     // or kStick, dragged in the editor
    float x0 = 0, y0 = 0;  // where it went down, pixels; in the editor, its offset from the control's centre
    float x = 0, y = 0;    // where it is
    MenuTouch menu = MenuTouch::Undecided;
    uint64_t down_ms = 0;
    float notch_y = 0;  // a scroll's place at its last notch
};

std::mutex g_lock;
std::vector<Finger> g_fingers;
bool g_pressed[kButtonCount] = {};
float g_stick_x = 0, g_stick_y = 0;  // -1 to 1, y up
float g_cursor_x = 0, g_cursor_y = 0;  // the menu cursor, pixels
bool g_cursor_down = false;
bool g_cursor_touch = false;  // the cursor follows a finger, not the mouse
uint64_t g_cursor_tap_until = 0;  // a tap holds the cursor down this long, so the menu sees a click
std::atomic<uint64_t> g_touch_ms{0};
std::atomic<uint64_t> g_back_until{0};  // the back key's tap of Start or B lasts until then
constexpr uint64_t kBackTapMs = 120;

// the layout in use, under g_lock
Layout g_layout = kTwoFinger;
size_t g_preset = 0;
float g_opacity = 0.85f;
float g_scale = 1.0f;  // every control's size, on top of its own
bool g_loaded = false;
// the editor: the layout before it opened (for Cancel), the control picked
std::atomic<bool> g_editing{false};
Layout g_before = kTwoFinger;
float g_before_opacity = 0.85f;
size_t g_before_preset = 0;
int g_selected = -1;

float Width() {
    int w, h;
    host::WindowPixels(w, h);
    return float(std::max(w, 1));
}
float Height() {
    int w, h;
    host::WindowPixels(w, h);
    return float(std::max(h, 1));
}

std::filesystem::path LayoutFile() { return app::ExeDir() / "layout.toml"; }

void LoadLayout() {
    g_loaded = true;
    try {
        toml::table t = toml::parse_file(LayoutFile().string());
        std::string preset = t["preset"].value_or(std::string("default"));
        for (size_t i = 0; i < std::size(kPresets); i++)
            if (preset == kPresets[i].id) g_preset = i;
        g_layout = *kPresets[g_preset].places;
        g_opacity = std::clamp(float(t["opacity"].value_or(double(g_opacity))), 0.2f, 1.0f);
        g_scale = std::clamp(float(t["scale"].value_or(double(g_scale))), 0.5f, 1.5f);
        for (size_t i = 0; i <= kButtonCount; i++) {
            // the menus' controls are not in the editor: always the preset's
            if (i != kStick && kButtons[i].when == When::Menus) continue;
            const char* id = i == kStick ? "stick" : kButtons[i].id;
            const toml::table* p = t["controls"][id].as_table();
            if (!p) continue;
            Place& pl = g_layout[i];
            pl.x = std::clamp(float((*p)["x"].value_or(double(pl.x))), 0.0f, 1.0f);
            pl.y = std::clamp(float((*p)["y"].value_or(double(pl.y))), 0.0f, 1.0f);
            pl.r = std::clamp(float((*p)["r"].value_or(double(pl.r))), 0.02f, 0.25f);
            pl.hidden = (*p)["hidden"].value_or(false) && i != kStick && !kButtons[i].keep;
        }
    } catch (...) {
    }
}

void SaveLayout() {
    toml::table controls;
    for (size_t i = 0; i <= kButtonCount; i++) {
        const Place& p = g_layout[i];
        controls.insert(i == kStick ? "stick" : kButtons[i].id,
                        toml::table{{"x", double(p.x)}, {"y", double(p.y)}, {"r", double(p.r)}, {"hidden", p.hidden}});
    }
    toml::table t{{"preset", kPresets[g_preset].id}, {"opacity", double(g_opacity)}, {"scale", double(g_scale)},
                  {"controls", controls}};
    std::ofstream(LayoutFile()) << t << "\n";
}

void EnsureLayout() {
    if (!g_loaded) LoadLayout();
}

void OpenEditor() {
    g_before = g_layout;
    g_before_opacity = g_opacity;
    g_before_preset = g_preset;
    g_selected = -1;
    g_editing = true;
}

bool InMenus() { return input::g_menu; }

bool ButtonShown(size_t i) {
    const Button& b = kButtons[i];
    if (g_editing) return b.when != When::Menus;
    if (g_layout[i].hidden) return false;
    if (i == kZoomButton && !(game::g_zoom < kScoped)) return false;
    if (b.when == When::Game) return !InMenus();
    if (b.when == When::Menus) return InMenus();
    // in play only while the player has the controls: none over a loading screen or a cutscene. A hint's pause stops
    // the look but waits for a button (reload, health pack), so the HUD still drawing keeps them up
    return !InMenus() && (input::Play() || game::HudShown());
}

void Centre(size_t i, float& cx, float& cy) {
    cx = g_layout[i].x * Width();
    cy = g_layout[i].y * Height();
}

float Radius(size_t i) { return g_layout[i].r * g_scale * Height(); }

// the editor's bar at the top: Reset, smaller and bigger for the picked control, hide or show it, Cancel, Done, in
// screen shares
constexpr const char* kBarLabels[] = {"RESET", "-", "+", "HIDE", "CANCEL", "DONE"};
enum { kBarReset, kBarSmaller, kBarBigger, kBarHide, kBarCancel, kBarDone, kBarCount };
constexpr float kBarY = 0.17f, kBarH = 0.09f, kBarW = 0.14f, kBarGap = 0.02f;
constexpr float kBarSizeW = 0.07f;                       // the - and + buttons
constexpr float kMinRadius = 0.02f, kMaxRadius = 0.25f;  // a control's own radius, in screen heights
constexpr float kSizeStep = 1.1f;                        // a tap on - or +

float BarWidth(int i) { return i == kBarSmaller || i == kBarBigger ? kBarSizeW : kBarW; }

// the picked control can be hidden: not the stick, nor the way back out of the menus
bool Hideable(int i) { return i >= 0 && size_t(i) < kButtonCount && !kButtons[size_t(i)].keep; }

const char* BarLabel(int i) {
    if (i == kBarHide && Hideable(g_selected) && g_layout[size_t(g_selected)].hidden) return "SHOW";
    return kBarLabels[i];
}

// the bar's button under a point, -1 for none
int BarAt(float x, float y, float rect[4] = nullptr, int which = -1) {
    float w = Width(), h = Height();
    float total = (kBarCount - 1) * kBarGap;
    for (int i = 0; i < kBarCount; i++) total += BarWidth(i);
    float left = 0.5f - total / 2;
    for (int i = 0; i < kBarCount; i++) {
        float x0 = left * w, y0 = kBarY * h;
        float x1 = x0 + BarWidth(i) * w, y1 = y0 + kBarH * h;
        left += BarWidth(i) + kBarGap;
        if (i == which && rect) {
            rect[0] = x0, rect[1] = y0, rect[2] = x1, rect[3] = y1;
            return i;
        }
        if (which < 0 && x >= x0 && x <= x1 && y >= y0 && y <= y1) return i;
    }
    return -1;
}

void ApplyPreset(size_t i) {
    g_preset = i;
    g_layout = *kPresets[i].places;
    g_selected = -1;
}

void CloseEditor(bool keep) {
    if (keep) {
        SaveLayout();
    } else {
        g_layout = g_before;
        g_opacity = g_before_opacity;
        g_preset = g_before_preset;
    }
    g_selected = -1;
    g_editing = false;
}

// the button under a point, kButtonCount for none
size_t HitButton(float x, float y) {
    for (size_t i = 0; i < kButtonCount; i++) {
        if (!ButtonShown(i)) continue;
        float cx, cy;
        Centre(i, cx, cy);
        float r = Radius(i) * 1.25f;  // a little slack around the drawn circle
        if ((x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r) return i;
    }
    return kButtonCount;
}

void UpdateStick(const Finger& f) {
    float r = Radius(kStick);
    float dx = (f.x - f.x0) / r, dy = (f.y - f.y0) / r;
    float len = std::sqrt(dx * dx + dy * dy);
    if (len > 1) {
        dx /= len;
        dy /= len;
    }
    g_stick_x = dx;
    g_stick_y = -dy;
}

// a dpad arrow held in the menus: a step as it comes down, then steps at a calm pace once held a while; the game
// steps on every poll the arrow is down, too quick to land on a row
constexpr uint64_t kStepMs = 120, kRepeatAfterMs = 450, kRepeatMs = 180;

bool DpadStep(uint64_t held) {
    if (held < kStepMs) return true;
    return held >= kRepeatAfterMs && (held - kRepeatAfterMs) % kRepeatMs < kStepMs / 2;
}

bool Dpad(size_t i) { return kButtons[i].press >= kBit && kButtons[i].press < kBit + 4 && kButtons[i].when == When::Menus; }

void Recount() {
    std::fill(std::begin(g_pressed), std::end(g_pressed), false);
    bool stick = false;
    for (const Finger& f : g_fingers) {
        if (f.role == Role::Button) g_pressed[f.button] = !Dpad(f.button) || DpadStep(NowMs() - f.down_ms);
        if (f.role == Role::Stick) stick = true;
    }
    if (!stick) g_stick_x = g_stick_y = 0;
}

void Resize(size_t i, float r) { g_layout[i].r = std::clamp(r, kMinRadius, kMaxRadius); }

// the editor: a touch on the bar resets, resizes the picked control, cancels or saves; a second finger while one
// drags a control pinches it bigger or smaller; on a control picks it up to move it, anywhere else lets go of the pick
void EditDown(Finger& f, float x, float y) {
    int bar = BarAt(x, y);
    switch (bar) {
        case kBarReset:
            ApplyPreset(g_preset);
            return;
        case kBarSmaller:
        case kBarBigger:
            if (g_selected >= 0) {
                float r = g_layout[size_t(g_selected)].r;
                Resize(size_t(g_selected), bar == kBarBigger ? r * kSizeStep : r / kSizeStep);
            }
            return;
        case kBarHide:
            if (Hideable(g_selected)) g_layout[size_t(g_selected)].hidden ^= true;
            return;
        case kBarCancel:
            CloseEditor(false);
            return;
        case kBarDone:
            CloseEditor(true);
            return;
        default:
            break;
    }
    for (const Finger& d : g_fingers) {
        if (d.role != Role::Drag) continue;
        // the span between the fingers and the control's size as the pinch began
        f.role = Role::Pinch;
        f.button = d.button;
        f.x0 = std::max(std::hypot(x - d.x, y - d.y), 1.0f);
        f.y0 = g_layout[d.button].r;
        return;
    }
    size_t b = HitButton(x, y);
    float sx = g_layout[kStick].x * Width(), sy = g_layout[kStick].y * Height(), sr = Radius(kStick);
    if (b == kButtonCount && (x - sx) * (x - sx) + (y - sy) * (y - sy) <= sr * sr) b = kStick;
    if (b > kStick) {
        g_selected = -1;
        return;
    }
    g_selected = int(b);
    f.role = Role::Drag;
    f.button = b;
    f.x0 = x - g_layout[b].x * Width();
    f.y0 = y - g_layout[b].y * Height();
}

void FingerDown(SDL_FingerID id, float x, float y) {
    Finger f;
    f.id = id;
    f.x0 = f.x = x;
    f.y0 = f.y = y;
    if (g_editing) {
        EditDown(f, x, y);
        g_fingers.push_back(f);
        return;
    }
    input::g_touch = true;
    size_t b = HitButton(x, y);
    if (b < kButtonCount) {
        f.role = Role::Button;
        f.button = b;
        f.down_ms = NowMs();
        if (kButtons[b].press == kPanel) app::ToggleMenu();
        if (kButtons[b].press == kGyro) gyro::NextMode();
    } else if (InMenus()) {
        // not the cursor yet: a tap clicks where it lifts, a swipe scrolls without moving the cursor
        f.role = Role::Cursor;
        f.menu = MenuTouch::Undecided;
        f.down_ms = NowMs();
        g_cursor_touch = true;
    } else if (x < Width() * kStickZone) {
        f.role = Role::Stick;
    } else {
        f.role = Role::Look;
    }
    g_fingers.push_back(f);
    Recount();
}

// the finger becomes the cursor held down where it is
void MenuHold(Finger& f) {
    f.menu = MenuTouch::Cursor;
    g_cursor_x = f.x;
    g_cursor_y = f.y;
    g_cursor_down = true;
}

// a finger in the menus moved: up or down past kMenuMove scrolls, sideways is the cursor dragging; a scroll gives a
// wheel notch a kScrollNotch, a finger going up shows the rows below as on a phone's lists
void MenuMove(Finger& f) {
    float h = Height();
    float dx = f.x - f.x0, dy = f.y - f.y0;
    if (f.menu == MenuTouch::Undecided) {
        if (std::fabs(dy) > kMenuMove * h && std::fabs(dy) >= std::fabs(dx)) {
            f.menu = MenuTouch::Scroll;
            f.notch_y = f.y0;
        } else if (std::fabs(dx) > kMenuMove * h) {
            MenuHold(f);
        }
    }
    if (f.menu == MenuTouch::Cursor) {
        g_cursor_x = f.x;
        g_cursor_y = f.y;
    }
    if (f.menu != MenuTouch::Scroll) return;
    float notch = kScrollNotch * h;
    while (f.y <= f.notch_y - notch) {
        f.notch_y -= notch;
        if (input::g_wheel > -kMaxNotches) input::g_wheel--;
    }
    while (f.y >= f.notch_y + notch) {
        f.notch_y += notch;
        if (input::g_wheel < kMaxNotches) input::g_wheel++;
    }
}

// a finger in the menus still since it came down: held long enough, it is the cursor held down (a slider dragged)
void MenuHolds() {
    uint64_t now = NowMs();
    for (Finger& f : g_fingers)
        if (f.role == Role::Cursor && f.menu == MenuTouch::Undecided && now - f.down_ms >= kHoldMs) MenuHold(f);
}

void FingerMove(SDL_FingerID id, float x, float y) {
    for (Finger& f : g_fingers) {
        if (f.id != id) continue;
        float dx = x - f.x, dy = y - f.y;
        f.x = x;
        f.y = y;
        if (f.role == Role::Drag) {
            Place& p = g_layout[f.button];
            p.x = std::clamp((x - f.x0) / Width(), 0.0f, 1.0f);
            p.y = std::clamp((y - f.y0) / Height(), 0.0f, 1.0f);
        }
        // a pinch sizes the control by the span between its two fingers, whichever moved
        if (f.role == Role::Drag || f.role == Role::Pinch) {
            for (const Finger& o : g_fingers) {
                const Finger& pinch = f.role == Role::Pinch ? f : o;
                const Finger& drag = f.role == Role::Pinch ? o : f;
                if (&o == &f || pinch.role != Role::Pinch || drag.role != Role::Drag || pinch.button != drag.button) continue;
                Resize(f.button, pinch.y0 * std::hypot(pinch.x - drag.x, pinch.y - drag.y) / pinch.x0);
            }
        }
        if (f.role == Role::Stick) UpdateStick(f);
        if (f.role == Role::Look || (f.role == Role::Button && kButtons[f.button].look)) {
            float k = kLookCounts / Width();
            input::g_touch_dx += int(std::lround(dx * k));
            input::g_touch_dy += int(std::lround(dy * k));
        }
        if (f.role == Role::Cursor) MenuMove(f);
    }
}

void FingerUp(SDL_FingerID id) {
    for (const Finger& f : g_fingers) {
        if (f.id != id || f.role != Role::Cursor) continue;
        if (f.menu == MenuTouch::Undecided) {
            // a tap: the cursor goes there and clicks
            g_cursor_x = f.x;
            g_cursor_y = f.y;
            g_cursor_tap_until = NowMs() + input::kTapMs;
        }
        if (f.menu != MenuTouch::Scroll) g_cursor_down = false;
    }
    std::erase_if(g_fingers, [id](const Finger& f) { return f.id == id; });
    Recount();
}

bool PadPlugged() {
    int n = 0;
    if (SDL_JoystickID* ids = SDL_GetGamepads(&n)) SDL_free(ids);
    return n > 0;
}

void KeyEvent(const SDL_KeyboardEvent& k) {
    // Android's back key: out of the layout editor or the settings panel, else the game's pause menu (its options
    // are the settings), in the game's menus back out of them
    if (k.scancode == SDL_SCANCODE_AC_BACK) {
        if (!k.down || k.repeat) return;
        std::lock_guard lock(g_lock);
        if (g_editing) {
            CloseEditor(false);
        } else if (app::MenuOpen()) {
            app::ToggleMenu();
        } else {
            g_back_until = NowMs() + kBackTapMs;
        }
        return;
    }
    int vk = input::VkOf(k.scancode);
    if (!vk) return;
    input::g_down[size_t(vk)] = k.down;
    if (!k.down) return;
    input::g_keyboard = true;
    input::g_touch = false;
    if (!k.repeat && input::PanelKey(vk)) app::ToggleMenu();
    else if (!k.repeat && !app::MenuOpen() && input::HudKey(vk)) game::g_hide_hud = !game::g_hide_hud;
    else if (vk == VK_ESCAPE && app::MenuOpen() && !app::Capturing()) app::ToggleMenu();
    // a name being typed takes Backspace and Enter as WM_CHAR has them
    if (vk == VK_BACK) input::OnChar('\b');
    if (vk == VK_RETURN) input::OnChar('\r');
}

}  // namespace

bool OnEvent(const SDL_Event& e) {
    switch (e.type) {
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
            KeyEvent(e.key);
            return app::MenuOpen() ? false : true;
        case SDL_EVENT_TEXT_INPUT:
            for (const char* c = e.text.text; c && *c; c++)
                if (uint8_t(*c) < 0x80 && *c >= ' ') input::OnChar(unsigned(*c));
            return !app::MenuOpen();
        case SDL_EVENT_MOUSE_MOTION:
            if (e.motion.which == SDL_TOUCH_MOUSEID) return true;
            input::g_keyboard = true;
            input::g_touch = false;
            if (input::Active() && input::Play()) {
                // the fractions carried over: a held mouse sends small moves, rounded each they were lost
                AddLook(e.motion.xrel, e.motion.yrel);
            } else {
                std::lock_guard lock(g_lock);
                g_cursor_x = e.motion.x;
                g_cursor_y = e.motion.y;
                g_cursor_touch = false;
            }
            return !app::MenuOpen();
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (e.button.which == SDL_TOUCH_MOUSEID) return true;
            if (int vk = input::VkOfButton(e.button.button)) input::g_down[size_t(vk)] = e.button.down;
            input::g_keyboard = true;
            input::g_touch = false;
            return !app::MenuOpen();
        case SDL_EVENT_MOUSE_WHEEL:
            if (e.wheel.y != 0) {
                int notch = e.wheel.y > 0 ? 1 : -1;
                input::g_wheel_ms[notch < 0] = NowMs();
                if (input::Active() && !input::Play() && !input::KeyCaptureUp()) input::g_wheel += notch;
            }
            return !app::MenuOpen();
        case SDL_EVENT_FINGER_DOWN:
        case SDL_EVENT_FINGER_MOTION:
        case SDL_EVENT_FINGER_UP:
        case SDL_EVENT_FINGER_CANCELED: {
            g_touch_ms = NowMs();
            float x = e.tfinger.x * Width(), y = e.tfinger.y * Height();
            std::lock_guard lock(g_lock);
            EnsureLayout();
            if (e.type == SDL_EVENT_FINGER_DOWN) {
                // the settings panel takes the touches, but for the button that closes it
                if (app::MenuOpen() && !g_editing) {
                    size_t b = HitButton(x, y);
                    if (b == kButtonCount || kButtons[b].press != kPanel) return false;
                }
                FingerDown(e.tfinger.fingerID, x, y);
                return true;
            }
            bool ours = std::any_of(g_fingers.begin(), g_fingers.end(),
                                    [&](const Finger& f) { return f.id == e.tfinger.fingerID; });
            if (!ours) return false;
            if (e.type == SDL_EVENT_FINGER_MOTION) FingerMove(e.tfinger.fingerID, x, y);
            else FingerUp(e.tfinger.fingerID);
            return true;
        }
        default:
            return false;
    }
}

void Apply(int port, input::Pad& pad) {
    if (port != 0 || g_editing) return;
    std::lock_guard lock(g_lock);
    Recount();  // a held dpad arrow's steps go by the clock
    bool used = false;
    for (size_t i = 0; i < kButtonCount; i++) {
        if (!g_pressed[i]) continue;
        int p = kButtons[i].press;
        if (p < 0) continue;
        used = true;
        if (p < kBit) pad.analog[p] = 0xFF;
        else pad.buttons |= uint16_t(1u << (p - kBit));
    }
    if (NowMs() < g_back_until) {
        used = true;
        if (InMenus()) pad.analog[kAnalogB] = 0xFF;
        else pad.buttons |= uint16_t(1u << 4);  // Start
    }
    if (g_stick_x || g_stick_y) {
        used = true;
        pad.lx = int16_t(std::clamp(g_stick_x, -1.0f, 1.0f) * 32767.0f);
        pad.ly = int16_t(std::clamp(g_stick_y, -1.0f, 1.0f) * 32767.0f);
    }
    if (used) pad.connected = true;
}

bool Shown() { return !PadPlugged() || NowMs() - g_touch_ms < kShownMs; }

namespace {

constexpr float kPi = 3.14159265f;

// an icon in white line art inside a button of radius r: points in units of r, from its centre, y down
struct Pen {
    ImDrawList* dl;
    ImVec2 c;
    float r;
    ImU32 col;
    float th;  // line thickness
    ImVec2 P(float x, float y) const { return ImVec2(c.x + x * r, c.y + y * r); }
    // a point turned by angle a about the centre
    ImVec2 R(float x, float y, float a) const {
        float s = std::sin(a), k = std::cos(a);
        return P(x * k - y * s, x * s + y * k);
    }
    void Line(float x0, float y0, float x1, float y1) const { dl->AddLine(P(x0, y0), P(x1, y1), col, th); }
    void Ring(float x, float y, float rad) const { dl->AddCircle(P(x, y), rad * r, col, 32, th); }
    void Dot(float x, float y, float rad) const { dl->AddCircleFilled(P(x, y), rad * r, col, 24); }
    void Box(float x0, float y0, float x1, float y1, float round = 0) const {
        dl->AddRectFilled(P(x0, y0), P(x1, y1), col, round * r);
    }
    // a cartridge standing up, its tip at top
    void Cartridge(float x, float y, float w, float len) const {
        float tip = w * 0.5f;
        dl->PathArcTo(P(x, y + tip), tip * r, kPi, 2 * kPi, 12);
        dl->PathLineTo(P(x + tip, y + len));
        dl->PathLineTo(P(x - tip, y + len));
        dl->PathFillConvex(col);
        dl->AddLine(P(x - tip, y + len * 0.42f), P(x + tip, y + len * 0.42f), IM_COL32(0, 0, 0, 120), th * 0.6f);
    }
    // an arrow along a line, its head at the end
    void Arrow(float x0, float y0, float x1, float y1, float head) const {
        Line(x0, y0, x1, y1);
        float a = std::atan2(y1 - y0, x1 - x0);
        for (float side : {-1.0f, 1.0f}) {
            float b = a + kPi + side * 0.55f;
            Line(x1, y1, x1 + std::cos(b) * head, y1 + std::sin(b) * head);
        }
    }
};

void DrawIcon(const Pen& p, Icon icon) {
    switch (icon) {
        case Icon::Bullet:
            p.Cartridge(0, -0.48f, 0.34f, 0.96f);
            break;
        case Icon::Scope:
            p.Ring(0, 0, 0.42f);
            p.Line(-0.62f, 0, -0.16f, 0);
            p.Line(0.16f, 0, 0.62f, 0);
            p.Line(0, -0.62f, 0, -0.16f);
            p.Line(0, 0.16f, 0, 0.62f);
            p.Dot(0, 0, 0.05f);
            break;
        case Icon::Mode:  // three rounds: the firing mode
            for (float x : {-0.3f, 0.0f, 0.3f}) p.Cartridge(x, -0.36f, 0.18f, 0.72f);
            break;
        case Icon::Reload: {
            p.dl->PathArcTo(p.c, 0.42f * p.r, -0.35f * kPi, 1.25f * kPi, 28);
            p.dl->PathStroke(p.col, 0, p.th);
            float a = -0.35f * kPi;
            float x = std::cos(a) * 0.42f, y = std::sin(a) * 0.42f;
            p.dl->AddTriangleFilled(p.P(x + 0.2f, y - 0.06f), p.P(x - 0.08f, y - 0.18f), p.P(x + 0.02f, y + 0.18f), p.col);
            break;
        }
        case Icon::Crouch:  // a figure down on one knee
            p.Dot(0.12f, -0.5f, 0.13f);
            p.Line(0.06f, -0.32f, -0.2f, 0.12f);
            p.Line(-0.2f, 0.12f, 0.28f, 0.2f);
            p.Line(0.28f, 0.2f, 0.18f, 0.58f);
            p.Line(-0.2f, 0.12f, -0.36f, 0.56f);
            p.Line(0.0f, -0.18f, 0.36f, -0.06f);
            break;
        case Icon::Knife: {
            constexpr float a = -kPi / 4;
            ImVec2 blade[] = {p.R(-0.08f, -0.13f, a), p.R(0.62f, -0.06f, a), p.R(0.48f, 0.12f, a), p.R(-0.08f, 0.12f, a)};
            p.dl->AddConvexPolyFilled(blade, 4, p.col);
            p.dl->AddLine(p.R(-0.12f, -0.22f, a), p.R(-0.12f, 0.21f, a), p.col, p.th * 1.2f);
            p.dl->AddLine(p.R(-0.18f, 0, a), p.R(-0.6f, 0, a), p.col, p.th * 2.2f);
            break;
        }
        case Icon::Hand:  // an open hand: use, pick up
            p.Box(-0.3f, -0.05f, 0.3f, 0.5f, 0.12f);
            for (int i = 0; i < 4; i++) {
                float x = -0.24f + i * 0.16f;
                p.Box(x - 0.055f, i == 0 || i == 3 ? -0.38f : -0.5f, x + 0.055f, 0.05f, 0.055f);
            }
            p.dl->AddLine(p.P(0.28f, 0.2f), p.P(0.52f, -0.08f), p.col, p.th * 2.2f);
            break;
        case Icon::Grenade:
            p.Dot(0, 0.14f, 0.36f);
            p.Box(-0.13f, -0.36f, 0.13f, -0.18f, 0.03f);
            p.Line(0.1f, -0.32f, 0.36f, -0.08f);
            p.Ring(-0.24f, -0.42f, 0.11f);
            p.dl->AddLine(p.P(-0.36f, 0.1f), p.P(0.36f, 0.1f), IM_COL32(0, 0, 0, 120), p.th * 0.7f);
            break;
        case Icon::Swap:  // the next weapon
            p.Arrow(-0.48f, -0.18f, 0.48f, -0.18f, 0.2f);
            p.Arrow(0.48f, 0.2f, -0.48f, 0.2f, 0.2f);
            break;
        case Icon::Health:
            p.Box(-0.13f, -0.46f, 0.13f, 0.46f, 0.04f);
            p.Box(-0.46f, -0.13f, 0.46f, 0.13f, 0.04f);
            break;
        case Icon::Map: {
            ImVec2 pts[] = {p.P(-0.55f, -0.36f), p.P(-0.18f, -0.5f), p.P(0.18f, -0.36f), p.P(0.55f, -0.5f),
                            p.P(0.55f, 0.36f),   p.P(0.18f, 0.5f),   p.P(-0.18f, 0.36f), p.P(-0.55f, 0.5f)};
            p.dl->AddPolyline(pts, 8, p.col, ImDrawFlags_Closed, p.th);
            p.Line(-0.18f, -0.5f, -0.18f, 0.36f);
            p.Line(0.18f, -0.36f, 0.18f, 0.5f);
            break;
        }
        case Icon::Pause:
            p.Box(-0.28f, -0.36f, -0.08f, 0.36f, 0.03f);
            p.Box(0.08f, -0.36f, 0.28f, 0.36f, 0.03f);
            break;
        case Icon::Gear:
            for (int i = 0; i < 8; i++) {
                float a = i * kPi / 4;
                p.dl->AddLine(p.R(0.3f, 0, a), p.R(0.52f, 0, a), p.col, p.th * 2.4f);
            }
            p.Dot(0, 0, 0.36f);
            p.dl->AddCircleFilled(p.c, 0.15f * p.r, IM_COL32(0, 0, 0, 200), 20);
            break;
        case Icon::Gyro: {  // a phone on its side between two turning arrows
            p.dl->AddRect(p.P(-0.32f, -0.2f), p.P(0.32f, 0.2f), p.col, 0.06f * p.r, 0, p.th);
            p.Dot(0.24f, 0, 0.035f);
            for (float side : {-1.0f, 1.0f}) {
                float a0 = side < 0 ? 0.62f * kPi : -0.38f * kPi, a1 = a0 + 0.76f * kPi * 0.5f;
                p.dl->PathArcTo(p.c, 0.5f * p.r, a0, a1, 12);
                p.dl->PathStroke(p.col, 0, p.th);
                ImVec2 tip = p.P(std::cos(a1) * 0.5f, std::sin(a1) * 0.5f);
                float t = a1 + kPi / 2;
                p.dl->AddTriangleFilled(tip, ImVec2(tip.x - std::cos(t - 0.5f) * 0.16f * p.r, tip.y - std::sin(t - 0.5f) * 0.16f * p.r),
                                        ImVec2(tip.x - std::cos(t + 0.5f) * 0.16f * p.r, tip.y - std::sin(t + 0.5f) * 0.16f * p.r), p.col);
            }
            break;
        }
        case Icon::Move:  // four arrows out from the middle: move the controls
            for (int i = 0; i < 4; i++) {
                float a = i * kPi / 2, c = std::cos(a), sn = std::sin(a);
                p.Arrow(c * 0.12f, sn * 0.12f, c * 0.5f, sn * 0.5f, 0.17f);
            }
            break;
        case Icon::Suppressor:  // a silencer on a muzzle
            p.Box(-0.18f, -0.17f, 0.56f, 0.17f, 0.08f);
            p.Box(-0.56f, -0.08f, -0.18f, 0.08f, 0.02f);
            for (float x : {0.04f, 0.22f, 0.40f})
                p.dl->AddLine(p.P(x, -0.17f), p.P(x, 0.17f), IM_COL32(0, 0, 0, 140), p.th * 0.6f);
            break;
        case Icon::Back:
        case Icon::Left:
            p.Line(0.14f, -0.36f, -0.22f, 0);
            p.Line(-0.22f, 0, 0.14f, 0.36f);
            break;
        case Icon::Right:
            p.Line(-0.14f, -0.36f, 0.22f, 0);
            p.Line(0.22f, 0, -0.14f, 0.36f);
            break;
        case Icon::Up:
            p.Line(-0.36f, 0.14f, 0, -0.22f);
            p.Line(0, -0.22f, 0.36f, 0.14f);
            break;
        case Icon::Down:
            p.Line(-0.36f, -0.14f, 0, 0.22f);
            p.Line(0, 0.22f, 0.36f, -0.14f);
            break;
        case Icon::Zoom: {  // the scope's level, X2 or X5: the turn scales by a half at X2, a quarter at X5, not a
            // fifth, so by which it is nearer; the editor shows X2
            const char* text = !g_editing && game::g_zoom < kScopeX5 ? "X5" : "X2";
            float size = p.r * 0.8f;
            ImVec2 ts = ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, 0, text);
            p.dl->AddText(ImGui::GetFont(), size, ImVec2(p.c.x - ts.x / 2, p.c.y - ts.y / 2), p.col, text);
            break;
        }
    }
}

// a colour at the layout's opacity
ImU32 Fade(int r, int g, int b, int a) { return IM_COL32(r, g, b, int(float(a) * g_opacity)); }

void DrawStick(ImDrawList* dl, float th) {
    // at rest in its place, else where the thumb came down
    float r = Radius(kStick);
    ImVec2 at(g_layout[kStick].x * Width(), g_layout[kStick].y * Height());
    bool held = false;
    for (const Finger& f : g_fingers)
        if (f.role == Role::Stick) {
            at = ImVec2(f.x0, f.y0);
            held = true;
        }
    dl->AddCircleFilled(at, r, Fade(0, 0, 0, held ? 80 : 55), 64);
    dl->AddCircle(at, r, Fade(255, 255, 255, held ? 110 : 70), 64, th * 0.7f);
    for (int i = 0; i < 4; i++) {  // the four directions
        float a = i * kPi / 2;
        ImVec2 tip(at.x + std::cos(a) * r * 0.86f, at.y + std::sin(a) * r * 0.86f);
        ImVec2 l(at.x + std::cos(a + 0.12f) * r * 0.74f, at.y + std::sin(a + 0.12f) * r * 0.74f);
        ImVec2 rr(at.x + std::cos(a - 0.12f) * r * 0.74f, at.y + std::sin(a - 0.12f) * r * 0.74f);
        dl->AddTriangleFilled(tip, l, rr, Fade(255, 255, 255, held ? 140 : 90));
    }
    ImVec2 knob(at.x + g_stick_x * r, at.y - g_stick_y * r);
    dl->AddCircleFilled(knob, r * 0.42f, Fade(255, 255, 255, held ? 120 : 75), 48);
    dl->AddCircle(knob, r * 0.42f, Fade(255, 255, 255, held ? 180 : 110), 48, th * 0.7f);
}

// a text centred on a point, its height a share of the screen's
void CentredText(ImDrawList* dl, ImVec2 at, float size, ImU32 colour, const char* text) {
    ImVec2 ts = ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, 0, text);
    dl->AddText(ImGui::GetFont(), size, ImVec2(at.x - ts.x / 2, at.y - ts.y / 2), colour, text);
}

// the editor's bar, drawn as the controls are: Reset, Cancel, Done, and what to do under it
void DrawEditorBar(ImDrawList* dl, float th) {
    float h = Height();
    for (int i = 0; i < kBarCount; i++) {
        float r[4];
        BarAt(0, 0, r, i);
        bool done = i == kBarDone;
        bool size = i == kBarSmaller || i == kBarBigger;
        // - and + wait for a control to be picked, hide for one that can be
        int alpha = (size && g_selected < 0) || (i == kBarHide && !Hideable(g_selected)) ? 90 : 240;
        dl->AddRectFilled(ImVec2(r[0], r[1]), ImVec2(r[2], r[3]),
                          done ? IM_COL32(150, 115, 45, 235) : IM_COL32(20, 18, 14, 225), h * 0.015f);
        dl->AddRect(ImVec2(r[0], r[1]), ImVec2(r[2], r[3]), IM_COL32(220, 180, 100, alpha), h * 0.015f, 0, th * 0.7f);
        CentredText(dl, ImVec2((r[0] + r[2]) / 2, (r[1] + r[3]) / 2), h * (size ? 0.06f : 0.042f),
                    IM_COL32(255, 255, 255, alpha), app::Ui(BarLabel(i)).c_str());
    }
    CentredText(dl, ImVec2(Width() / 2, (kBarY + kBarH + 0.05f) * h), h * 0.036f, IM_COL32(255, 255, 255, 220),
                app::Ui("DRAG TO MOVE, PINCH OR - + TO RESIZE").c_str());
    CentredText(dl, ImVec2(Width() / 2, (kBarY + kBarH + 0.1f) * h), h * 0.036f, IM_COL32(255, 255, 255, 220),
                app::Ui("TAP A CONTROL, THEN HIDE OR SHOW").c_str());
}

}  // namespace

void Draw() {
    std::lock_guard lock(g_lock);
    EnsureLayout();
    bool editing = g_editing;
    if (!editing && !Shown()) return;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    float h = Height();
    float th = std::max(2.0f, h / 360.0f);
    if (editing) dl->AddRectFilled(ImVec2(0, 0), ImVec2(Width(), h), IM_COL32(0, 0, 0, 90));
    for (size_t i = 0; i < kButtonCount; i++) {
        const Button& b = kButtons[i];
        if (!ButtonShown(i) || (app::MenuOpen() && b.press != kPanel && !editing)) continue;
        float cx, cy;
        Centre(i, cx, cy);
        float r = Radius(i);
        bool hidden = g_layout[i].hidden;
        bool on = !editing && (g_pressed[i] || (b.press == kGyro && gyro::CurrentMode() != gyro::kOff));
        int fade = hidden ? 40 : 100;  // a hidden control shows faint in the editor
        dl->AddCircleFilled(ImVec2(cx, cy), r, on ? Fade(255, 255, 255, 70) : Fade(0, 0, 0, 80 * fade / 100), 48);
        dl->AddCircle(ImVec2(cx, cy), r, Fade(255, 255, 255, 60 * fade / 100), 48, th * 0.6f);
        DrawIcon({dl, ImVec2(cx, cy), r, on ? Fade(255, 255, 255, 230) : Fade(255, 255, 255, 180 * fade / 100), th}, b.icon);
        if (b.press == kGyro) {
            const char* mode = gyro::ModeName();
            float fs = r * 0.55f;
            ImVec2 ts = ImGui::GetFont()->CalcTextSizeA(fs, FLT_MAX, 0, mode);
            dl->AddText(ImGui::GetFont(), fs, ImVec2(cx - ts.x / 2, cy + r * 1.08f), Fade(255, 255, 255, 180), mode);
        }
        if (editing && int(i) == g_selected) dl->AddCircle(ImVec2(cx, cy), r + th * 2, IM_COL32(255, 200, 60, 255), 48, th * 1.5f);
        if (editing && hidden) {
            float k = r * 0.6f;
            dl->AddLine(ImVec2(cx - k, cy - k), ImVec2(cx + k, cy + k), IM_COL32(255, 80, 60, 220), th);
            dl->AddLine(ImVec2(cx - k, cy + k), ImVec2(cx + k, cy - k), IM_COL32(255, 80, 60, 220), th);
        }
    }
    if (editing || (!InMenus() && !app::MenuOpen() && input::Play())) DrawStick(dl, th);
    if (editing) {
        if (g_selected == int(kStick)) {
            ImVec2 at(g_layout[kStick].x * Width(), g_layout[kStick].y * h);
            dl->AddCircle(at, Radius(kStick) + th * 2, IM_COL32(255, 200, 60, 255), 64, th * 1.5f);
        }
        DrawEditorBar(dl, th);
    }
}

// ---- the layout's settings, for the game's options (Recomp Settings, TOUCH); game thread, each saved at once

namespace {

// the controls moved, resized or hidden away from the preset's places
bool Custom() {
    const Layout& preset = *kPresets[g_preset].places;
    for (size_t i = 0; i <= kButtonCount; i++) {
        const Place& a = g_layout[i];
        const Place& b = preset[i];
        if (std::fabs(a.x - b.x) > 1e-4f || std::fabs(a.y - b.y) > 1e-4f || std::fabs(a.r - b.r) > 1e-4f ||
            a.hidden != b.hidden)
            return true;
    }
    return false;
}

uint32_t Percent(float f) { return uint32_t(std::lround(f * 100.0f)); }

}  // namespace

const char* LayoutName() {
    std::lock_guard lock(g_lock);
    EnsureLayout();
    return Custom() ? "CUSTOM" : kPresets[g_preset].label;
}

// the next or previous preset; from a custom layout first back to its own preset's places, or the one before
void StepLayout(int step) {
    std::lock_guard lock(g_lock);
    EnsureLayout();
    size_t n = std::size(kPresets);
    size_t i = Custom() ? (step > 0 ? g_preset : (g_preset + n - 1) % n) : (g_preset + n + (step > 0 ? 1 : n - 1)) % n;
    ApplyPreset(i);
    SaveLayout();
}

uint32_t ControlsOpacity() {
    std::lock_guard lock(g_lock);
    EnsureLayout();
    return Percent(g_opacity);
}

// 20% to 100% in steps of 5
void StepOpacity(int step) {
    std::lock_guard lock(g_lock);
    EnsureLayout();
    g_opacity = float(std::clamp(int(Percent(g_opacity)) + step * 5, 20, 100)) / 100.0f;
    SaveLayout();
}

uint32_t ControlsSize() {
    std::lock_guard lock(g_lock);
    EnsureLayout();
    return Percent(g_scale);
}

// 50% to 150% in steps of 5
void StepSize(int step) {
    std::lock_guard lock(g_lock);
    EnsureLayout();
    g_scale = float(std::clamp(int(Percent(g_scale)) + step * 5, 50, 150)) / 100.0f;
    SaveLayout();
}

// the controls over the screen to drag where they go, Done or Cancel in the bar at the top, Android's back cancels
void EditLayout() {
    std::lock_guard lock(g_lock);
    EnsureLayout();
    // the touch that picked the row is the menu's, not the editor's
    g_fingers.clear();
    g_cursor_down = false;
    Recount();
    OpenEditor();
}

void AddLook(float dx, float dy) {
    // the fraction of a count carried to the next reading: slow turns add up
    static float rx = 0, ry = 0;
    rx += dx;
    ry += dy;
    int ix = int(rx), iy = int(ry);
    rx -= float(ix);
    ry -= float(iy);
    input::g_dx += ix;
    input::g_dy += iy;
}

void PadUsed() { input::g_touch = false; }

bool LookActive() { return !g_editing && input::Active() && input::Play() && !InMenus(); }

bool Firing() {
    std::lock_guard lock(g_lock);
    for (size_t i = 0; i < kButtonCount; i++)
        if (g_pressed[i] && kButtons[i].press == kAnalogRT) return true;
    return input::Down(VK_LBUTTON);
}

// main thread, each turn of the event loop: the on-screen keyboard while a name is typed
void Pump() {
    gyro::Pump();
    // a mouse in play is captured as on a PC: no pointer over the picture and no screen edge to stop a turn. Let go
    // in the menus, the panel, the background and once the touch screen is used; asked again whenever Android took
    // it away
    SDL_Window* window = host::Window();
    bool capture = input::g_keyboard && !input::g_touch && LookActive();
    if (window && capture != SDL_GetWindowRelativeMouseMode(window)) SDL_SetWindowRelativeMouseMode(window, capture);
    bool want = input::g_text && input::Active();
    if (want == input::g_text_shown) return;
    input::g_text_shown = want;
    if (want) SDL_StartTextInput(host::Window());
    else SDL_StopTextInput(host::Window());
}

}  // namespace touch

namespace input {

bool CursorShown() { return g_menu && (g_keyboard || touch::g_cursor_touch); }

bool Cursor(float& x, float& y, bool& down) {
    if (!Active() || !CursorShown()) return false;
    int w, h;
    host::WindowPixels(w, h);
    if (w <= 0 || h <= 0) return false;
    float px, py;
    bool touch_down;
    {
        std::lock_guard lock(touch::g_lock);
        touch::MenuHolds();
        px = touch::g_cursor_x;
        py = touch::g_cursor_y;
        touch_down = touch::g_cursor_touch && (touch::g_cursor_down || NowMs() < touch::g_cursor_tap_until);
    }
    // the frame is shown in the middle of the window
    float aspect = game::Aspect();
    float vw = std::min(float(w), float(h) * aspect);
    float vh = vw / aspect;
    x = (px - (float(w) - vw) / 2) / vw;
    y = (py - (float(h) - vh) / 2) / vh;
    down = touch_down || Down(VK_LBUTTON);
    return true;
}

bool LeftButton() {
    if (!Active() || g_settle) return false;
    if (Down(VK_LBUTTON)) return true;
    std::lock_guard lock(touch::g_lock);
    return std::any_of(touch::g_fingers.begin(), touch::g_fingers.end(),
                       [](const touch::Finger& f) { return f.role != touch::Role::Button; });
}

}  // namespace input

namespace input {

bool TouchActive() { return g_touch; }

// the touch control that presses the pad's button: 0-7 analog A B X Y black white LT RT, 8-15 the button bits
std::string TouchName(int button) {
    int press = button < touch::kBit ? button : touch::kBit + (button - touch::kBit);
    std::lock_guard lock(touch::g_lock);
    for (size_t i = 0; i < touch::kButtonCount; i++) {
        const touch::Button& b = touch::kButtons[i];
        if (b.press != press || touch::g_layout[i].hidden) continue;
        std::string name = b.label;
        if (size_t paren = name.find(" ("); paren != std::string::npos) name.resize(paren);
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return char(std::toupper(c)); });
        return name;
    }
    return {};
}

}  // namespace input
