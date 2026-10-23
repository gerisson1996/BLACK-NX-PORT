// Button legends on keyboard and mouse: a menu prompt's icon goes and its text leads with the key, "(ENTER) confirm".
// Texts naming the pad's buttons name the keys, a hint in play the key bound to its button. A change of device sets
// the texts on screen again. Texts naming the console name the PC, whatever the device. On a touch screen (Android) the
// hints in play name the touch control that presses the button, "HOLD USE TO SWAP", a menu prompt's icon goes as on
// the keyboard and its text leads with the touch, "(TAP) CONFIRM", and the title's "PRESS START" is a tap anywhere.
#include <strings.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

#include "common/mouse_keyboard.h"
#include "common/translations.h"
#include "game/flash.h"
#include "xdk/hle.h"

using namespace kernel;

namespace game {
uint32_t OptionsText(uint32_t id);  // options_menu.cpp
uint32_t SaveName(uint32_t id);  // save_name.cpp
bool Translated();  // language.cpp
const char* IndonesianString(uint32_t id);  // language.cpp
}

namespace {

constexpr uint32_t kResetTexts = 0xECA90;  // every text on screen is made again from its string
// string ids
constexpr uint32_t kConfirm = 0xE0CA7C5F;  // FE_CONFIRM
constexpr uint32_t kBack = 0x64199B75;     // FE_BACK
constexpr uint32_t kSelect = 0x12309FEF;   // FE_SELECT
constexpr uint32_t kBackspace = 0x32D594BD;
constexpr uint32_t kPressStart[] = {0x2FCCE936, 0x905E4EE0, 0x661A3A37};
// a hint's "PRESS (A) :" strings, in the order the keyboard numbers the pad: A B X Y black white, the triggers,
// dpad up down left right, start, back, the thumbs
constexpr uint32_t kButtons[] = {0x53773143, 0xCA7E60F9, 0x371C9983, 0x401BA915, 0x1C8B6988, 0xD7C11E04,
                                 0xDDC13733, 0xBCBBFDF2, 0xBB8906E1, 0xCA47AFA2, 0x53DFD8F1, 0xD4A32971,
                                 0x0A88862F, 0xDC57E320, 0x8D194B52, 0xB900C989};
constexpr uint32_t kHintSize = 32;  // the game expands a hint's button into this many characters
constexpr float kIconWidth = 28.0f;  // a prompt's text starts this far right of its icon's left edge
// "hold (X) to pick up" takes its button from four glyph strings, of Y A B X
constexpr uint32_t kPickupGlyphs = 0x2AB72C;
constexpr int kPickupButtons[] = {3, 0, 1, 2};
constexpr uint32_t kKeySize = 16;
// the HUD's message box copies a string's first 127 bytes whatever its length (0x12B800): a copy of ours has that
// room at least, or the copy reads past the end of guest memory
constexpr uint32_t kStringRoom = 128;

// text in guest memory, zero padded to kStringRoom; 0 when out of memory
uint32_t GuestString(const char* text, size_t size) {
    uint32_t room = uint32_t(std::max<size_t>(size, kStringRoom));
    uint32_t at = mem::AllocSystem(room);
    if (!at) return 0;
    std::memset(HostPtr(at), 0, room);
    std::memcpy(HostPtr(at), text, size);
    return at;
}

void Replace(std::string& text, std::string_view from, std::string_view to) {
    for (size_t at = 0; (at = text.find(from, at)) != std::string::npos; at += to.size())
        text.replace(at, from.size(), to);
}

bool PressStart(uint32_t id) { return std::find(std::begin(kPressStart), std::end(kPressStart), id) != std::end(kPressStart); }

// a string as this PC words it, on the keyboard with its keys, on the touch screen with its touches; empty when it
// stays. In the menus a drag moves the focus, a tap confirms, the touch controls' back button (an arrow) backs out.
std::string Wording(uint32_t id, const std::string& pad, bool keyboard, bool touch) {
    std::string text = pad;
    Replace(text, "XBOX CONSOLE", "PC");
    Replace(text, "XBOX HARD DISK", "HARD DISK");
    Replace(text, "XBOX", "PC");
    Replace(text, "Xbox", "PC");
    if (touch && !keyboard) {
        if (id == kSelect) text = "(DRAG) " + text;
        else if (id == kConfirm) text = "(TAP) " + text;
        else if (id == kBack || id == kBackspace) text = "(<) " + text;
        else if (PressStart(id)) {
            Replace(text, "START", "ANYWHERE");
            Replace(text, "Start", "anywhere");
            Replace(text, "start", "anywhere");
        }
        return text == pad ? std::string() : text;
    }
    if (!keyboard) return text == pad ? std::string() : text;
    if (id == kConfirm) text = "(ENTER) " + text;
    else if (id == kBack || id == kBackspace) text = "(ESC) " + text;
    else if (PressStart(id)) {
        Replace(text, "start", "enter");
        Replace(text, "START", "ENTER");
    } else {
        // the font's A and B button glyphs
        Replace(text, "\xC2\xB2", "ENTER");
        Replace(text, "\xC2\xB9", "ESC");
    }
    return text == pad ? std::string() : text;
}

std::mutex g_lock;
// string id, and in bit 0 the keyboard, in bit 1 the touch controls, to its wording in guest memory, 0 for none
std::unordered_map<uint64_t, uint32_t> g_wordings;

// a prompt is a clip of a "Button" icon and a "Text": on the keyboard the icon goes and the text takes its place
void Prompts(uint32_t clip, bool keyboard, int depth) {
    int left = 4096;
    for (uint32_t child = flash::FirstChild(clip); child && left; child = R32(child + flash::kClipNext), left--) {
        uint32_t icon = 0;
        uint32_t text = 0;
        for (uint32_t part = flash::FirstChild(child); part && left; part = R32(part + flash::kClipNext), left--) {
            if (!flash::IsClip(part)) continue;
            if (strcasecmp(flash::Name(part), "Button") == 0) icon = part;
            if (strcasecmp(flash::Name(part), "Text") == 0) text = part;
        }
        if (!icon || !text) {
            if (depth < 8) Prompts(child, keyboard, depth + 1);
            continue;
        }
        bool shown = R32(icon + flash::kClipFlags) & flash::kClipShown;
        if (shown != keyboard) continue;
        W32(icon + flash::kClipFlags, R32(icon + flash::kClipFlags) ^ flash::kClipShown);
        G<float>(text + flash::kClipX) += keyboard ? -kIconWidth : kIconWidth;
        W32(text + flash::kClipFlags, R32(text + flash::kClipFlags) | flash::kClipPlaced);
    }
}

}  // namespace

// thiscall(id): the string of that id, UTF-8
X86_EXTERN_FUNC(__imp__sub_001F3A80);
X86_FUNC(sub_001F3A80) {
    uint32_t id = R32(ctx.esp + 4);
    __imp__sub_001F3A80(ctx, base);
    if (uint32_t text = game::OptionsText(id)) {
        ctx.eax = text;
        return;
    }
    // the save's name, in no language's words
    if (uint32_t name = game::SaveName(id)) {
        ctx.eax = name;
        return;
    }
    if (!ctx.eax) return;
    bool keyboard = input::KeyboardActive();
    bool touch = !keyboard && input::TouchActive();
    std::lock_guard lock(g_lock);
    // Indonesian: the US string swapped for its translation in guest memory, copied once, before the wording below
    if (const char* ind = game::IndonesianString(id)) {
        static std::unordered_map<uint32_t, uint32_t> copies;
        auto [copy, made] = copies.try_emplace(id, 0);
        if (made) copy->second = GuestString(ind, std::strlen(ind) + 1);
        if (copy->second) ctx.eax = copy->second;
    }
    auto [it, fresh] = g_wordings.try_emplace(uint64_t(id) << 2 | uint64_t(touch) << 1 | keyboard, 0);
    const uint32_t* button = std::find(std::begin(kButtons), std::end(kButtons), id);
    if ((keyboard || touch) && button != std::end(kButtons)) {
        // follows the bindings and the layout: worded at every ask
        int b = int(button - kButtons);
        std::string key = keyboard ? input::ButtonKey(b) : input::TouchName(b);
        if (key.empty() || (!it->second && !(it->second = GuestString("", 1)))) return;
        std::string press = game::Translated() ? app::Ui("PRESS") : "PRESS";
        std::snprintf(static_cast<char*>(HostPtr(it->second)), kHintSize, "%s %s :", press.c_str(), key.c_str());
    } else if (fresh) {
        std::string text = Wording(id, ReadCString(ctx.eax, 1024), keyboard, touch);
        if (!text.empty()) it->second = GuestString(text.c_str(), text.size() + 1);
    }
    if (it->second) ctx.eax = it->second;
}

// the front end's tick, before the Flash player runs
void MenuPrompts(X86Context& ctx, uint8_t*) {
    static int was = -1;
    bool keyboard = input::KeyboardActive();
    bool touch = !keyboard && input::TouchActive();
    int device = keyboard ? 1 : touch ? 2 : 0;
    if (device != was) {
        if (was >= 0 && R32(flash::kPlayerReady) == 1 && R32(flash::kPlayer)) {
            X86Context saved = ctx;
            CallGuest(ctx, kResetTexts, {});
            ctx = saved;
        }
        was = device;
    }
    // the pad's icons go on the touch screen too, its texts say the touch
    Prompts(flash::Level(flash::kMenuLevel), keyboard || touch, 0);
    static uint32_t glyphs[4];
    static uint32_t keys;
    if (!keys && (keys = mem::AllocSystem(4 * kKeySize)))
        for (uint32_t i = 0; i < 4; i++) glyphs[i] = R32(kPickupGlyphs + i * 4);
    for (uint32_t i = 0; keys && i < 4; i++) {
        std::string key = keyboard ? input::ButtonKey(kPickupButtons[i])
                          : touch  ? input::TouchName(kPickupButtons[i])
                                   : std::string();
        std::snprintf(static_cast<char*>(HostPtr(keys + i * kKeySize)), kKeySize, "%s", key.c_str());
        W32(kPickupGlyphs + i * 4, key.empty() ? glyphs[i] : keys + i * kKeySize);
    }
}
