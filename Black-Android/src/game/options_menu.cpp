// Recomp settings in the game's own options menus: a button a page (GRAPHICS, TOUCH CONTROLS, GENERAL, MOUSE AND
// KEYBOARD, CHEATS) under the stock ones loads the page again with the F10 panel's rows in their place. A row is four
// copies of a stock button clip, its name, its value and an arrow each side of the value; the name's copy stands in
// for the script's button object, so the cursor works, and a tap on an arrow steps the value as the dpad does. Up and
// down move the focus here, over the groups' headings, and arrows over and under the rows show more are hidden. The
// pause menu's options have room for one button, its rows start with the panel's PAGE row. The control setup page
// keeps its own buttons and takes the sticks' rows under them, in its ring.
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

#include "common/menu.h"
#include "common/mouse_keyboard.h"
#include "common/translations.h"
#include "core/log.h"
#include "game/flash.h"
#include "game/layout.h"
#include "xdk/hle.h"

using namespace kernel;
using namespace layout;
using app::rows::Row;
using flash::Button;
using flash::Find;
using flash::FindField;
using flash::Focus;
using flash::Link;
using flash::Member;
using flash::Members;
using flash::Method;
using flash::Set;
using flash::String;
using flash::Text;

namespace {

constexpr uint32_t kHashTable = 0x2AC908;
// as menu.cpp's pages
const char* const kCaptions[] = {"GRAPHICS", "TOUCH CONTROLS", "GENERAL", "MOUSE AND KEYBOARD", "CHEATS"};
constexpr int kEntries = int(std::size(kCaptions));
constexpr int kSlots = 8;
constexpr int kDepth = 100;
constexpr int kEscape = 0x1B;
const Row kPageRow{app::rows::Kind::Value, -1, "PAGE"};

// the selection bar, Main.ButtonSelector: its parts by depth; once shown it stops on frame 10, its data's frame 9
constexpr int kBarRest = 9;
constexpr int kBarBox = 1;
constexpr int kBarLine = 5;
constexpr int kBarTick = 7;
constexpr int kBarScan = 11;
constexpr float kBarLeft = -5.0f;  // the box's left end and the left tick
constexpr float kTickGap = 10.0f;  // past the end of the widest value

// the rows' font, as the name entry's caret measures it
constexpr uint32_t kFindFont = 0x13CD0;   // stdcall (game object, name): a font
constexpr uint32_t kFontName = 0x28E63C;  // "Eurostile LT Std"
constexpr float kTextHeight = 15.0f;      // the stock buttons' text fields
constexpr float kTextLeft = 2.0f;         // the text field in its button's label clip
constexpr float kArrowSpace = 16.0f;      // an arrow and the gap to its value, in the rows' units
const char* const kArrows[] = {"<", ">"};
const char* const kMore[] = {"▲", "▼"};  // over and under the rows while more are hidden that way
constexpr float kMoreAbove = 0.9f;  // the up arrow's place over the first row, in rows
constexpr float kMoreScale = 1.3f;  // the font draws them as small dpad icons: a little bigger than the rows

// the stock bar on frame 10: its box's x scale, the x of its right tick, right line and Scan
struct Bar {
    float box;
    float tick;
    float line;
    float scan;
};

// an options page of the game
struct Host {
    const char* page;
    const char* holder;        // the clip in Main its buttons are in, null for Main
    const char* button;        // a stock button, copied for ours
    const char* under;         // the clip our buttons go under, the first this far down
    float drop;
    const char* first;         // the first and last button objects of its ring
    const char* last;
    int entries;               // our buttons; one opens both pages, none puts the sticks' rows under its own
    // the rows, from the stock button
    float pitch;
    float name_x;
    float value_x;
    float scale;
    const char* hint;          // the string of a prompt our button takes the place of
    const char* stock[14];     // what the rows take the place of
    Bar bar;
};
constexpr Host kHosts[] = {
    {"OptionsMenuXbox", "TextAnim", "_level4.Main.TextAnim.ControllerSetup_Text", "Playerprofile_Text", 25.0f,
     "controllerButton", "Playerprofile", kEntries, 25.0f, -37.0f, 185.0f, 0.9f, nullptr,
     {"ControllerSetup_Text", "SoundSetup_Text", "PictureSetup_Text", "Playerprofile_Text"},
     {1.0f, 245.0f, 445.0f, -5.0f}},
    {"PauseMenuOptionXbox", nullptr, "_level4.Main.ToggleCrouch", "ToggleCrouch", 222.0f,
     "toggleCrouchButton", "musicVolumeButton", 1, 27.0f, 0.0f, 245.0f, 1.0f, "$FE_SELECT",
     {"ToggleCrouch", "ToggleCrouchOn_Text", "ToggleCrouchOff_Text", "InvertLook", "InvertLookOn_Text",
      "InvertLookOff_Text", "Vibration", "VibrationOn_Text", "VibrationOff_Text", "SFXVolume", "Sound_Volume",
      "MusicVolume", "Music_Volume"},
     {1.4f, 343.55f, 443.55f, 168.35f}},
    // the bar taken from the page as it first rests
    {"ControllerSetup", "TextAnim", "_level4.Main.TextAnim.Vibration_Text", "ManuallyConfigure_Text", 25.0f,
     "toggleCrouchButton", "manuallyConfigureButton", 0, 25.0f, 0.0f, 215.0f, 1.0f, nullptr, {}, {}},
};

// a text the page shows from here: its string id, the text, the data of the field showing it
struct Field {
    uint32_t id;
    uint32_t text;
    uint32_t data;
};
enum {
    kTitle,
    kEntry,
    kName = kEntry + kEntries,
    kValue = kName + kSlots,
    kLeft = kValue + kSlots,
    kRight = kLeft + kSlots,
    kScroll = kRight + kSlots,
    kFields = kScroll + 2
};
constexpr uint32_t kFieldSize = 128;  // the HUD's message box reads 127 bytes of a string (0x12B800)
Field g_fields[kFields];

struct Page {
    const Host* host = nullptr;
    int mode = -1;  // 0 the stock buttons and ours, 1 and 2 the rows of each caption
    bool failed = false;
    uint32_t first = 0;  // our first clip
    uint32_t lead = 0;   // the stock clip ours keep their places from
    uint32_t entry[kEntries] = {};
    uint32_t name[kSlots] = {};
    uint32_t value[kSlots] = {};
    uint32_t arrow[2][kSlots] = {};  // left, right
    float right_x[kSlots] = {};      // the right arrow's place, past its row's value
    uint32_t more[2] = {};           // the scroll arrows, up and down
    size_t slots = 0;
    size_t top = 0;
    std::vector<const Row*> rows;  // null restores the defaults
    int capture = 0;  // a key binding: 1 the keys come up, 2 the next key binds or Escape leaves, 3 they come up again
    size_t bind = 0;  // its row
    bool extra = false;  // the sticks' rows under the page's own buttons
    Bar bar{};           // the page's own bar, read once it rests, where the host has none
    bool bar_seen = false;
};
Page g_page;     // game thread
int g_next = 0;  // the mode of the page being loaded
int g_last = 0;  // the mode before it, whose button takes the focus back

// the id of a string by its name
uint32_t Hash(const char* text) {
    uint32_t h = ~0u;
    for (; *text; text++) h = uint32_t(int32_t(h) >> 8) ^ R32(kHashTable + ((h ^ uint8_t(*text)) & 0xFF) * 4);
    return h;
}

// that field shows field n from now on
bool Show(X86Context& ctx, int n, uint32_t clip, const char* shows = nullptr, const char* text = nullptr) {
    Field& f = g_fields[n];
    char name[16];
    std::snprintf(name, sizeof(name), "$RC_%d", n);
    if (!f.text) {
        f.id = Hash(name + 1);
        f.text = mem::AllocSystem(kFieldSize);
        G<char>(f.text) = 0;
    }
    uint32_t field = FindField(clip, shows);
    if (!field) return false;
    f.data = R32(field + flash::kClipData);
    if (text) std::snprintf(static_cast<char*>(HostPtr(f.text)), kFieldSize, "%s", text);
    return Set(ctx, field, "variable", String(ctx, name));
}

void Write(Field& f, const std::string& text) {
    char* shown = static_cast<char*>(HostPtr(f.text));
    if (!f.data || std::strncmp(text.c_str(), shown, kFieldSize - 1) == 0) return;
    std::snprintf(shown, kFieldSize, "%s", text.c_str());
    // made again at the next draw
    W32(f.data + flash::kFieldFlags, (R32(f.data + flash::kFieldFlags) & ~1u) | 0x204);
}

// a copy of the stock button beside it, stopped
uint32_t Copy(X86Context& ctx, uint32_t holder, const char* name, int depth) {
    char number[12];
    std::snprintf(number, sizeof(number), "%d", depth);
    ctx.eax = Text(g_page.host->button);
    CallGuest(ctx, flash::kCall, {Text("duplicateMovieClip"), 0, 2, Text(name), Text(number)});
    uint32_t clip = Member(ctx, holder, name);
    if (flash::Type(clip) != flash::kSprite) return 0;
    uint32_t data = R32(clip + flash::kClipData);
    W32(data + flash::kDataFlags, R32(data + flash::kDataFlags) & ~flash::kDataPlaying);
    return clip;
}

void Hide(uint32_t clip) {
    if (flash::IsClip(clip)) W32(clip + flash::kClipFlags, R32(clip + flash::kClipFlags) & ~flash::kClipShown);
}

// ours keep their places from a stock clip, which the page slides and fades in: x, y, then its colours
void Follow(const Page& p) {
    const Host& h = *p.host;
    auto follow = [&p](uint32_t clip, float x, float y) {
        if (!clip) return;
        G<float>(clip + flash::kClipX) = G<float>(p.lead + flash::kClipX) + x;
        G<float>(clip + flash::kClipY) = G<float>(p.lead + flash::kClipY) + y;
        std::memcpy(HostPtr(clip + flash::kClipColours), HostPtr(p.lead + flash::kClipColours), flash::kColoursSize);
    };
    for (int i = 0; i < kEntries; i++) follow(p.entry[i], 0.0f, h.drop + h.pitch * float(i));
    // the sticks' rows go under the stock clip, the others in its place
    float top = p.extra ? h.drop : 0.0f;
    for (int i = 0; i < kSlots; i++) {
        float y = top + h.pitch * float(i);
        follow(p.name[i], h.name_x, y);
        follow(p.arrow[0][i], h.value_x, y);
        follow(p.value[i], h.value_x + kArrowSpace, y);
        follow(p.arrow[1][i], p.right_x[i] ? p.right_x[i] : h.value_x + 2 * kArrowSpace, y);
    }
    follow(p.more[0], h.name_x, -h.pitch * kMoreAbove);
    follow(p.more[1], h.name_x, h.pitch * float(p.slots));
}

void Sound(X86Context& ctx) {
    ctx.eax = Text("_level4.MyMenu");
    CallGuest(ctx, flash::kCall, {Text("PlayChangeSound"), 0, 0});
}

// our buttons under the stock ones, in their ring
bool Entries(X86Context& ctx, uint32_t root, uint32_t holder) {
    Page& p = g_page;
    const Host& h = *p.host;
    uint32_t top = Member(ctx, root, h.first);
    uint32_t above = Member(ctx, root, h.last);
    p.lead = Member(ctx, holder, h.under);
    if (!top || !above || !flash::IsClip(p.lead)) return false;
    for (int i = 0; i < h.entries; i++) {
        char name[16];
        std::snprintf(name, sizeof(name), "RC_E%d", i);
        if (!(p.entry[i] = Copy(ctx, holder, name, kDepth + i))) return false;
        if (!Show(ctx, kEntry + i, p.entry[i], nullptr, app::Ui(kCaptions[i]).c_str())) return false;
    }
    p.first = p.entry[0];
    Follow(p);
    CallGuest(ctx, flash::kRunActions, {R32(flash::kPlayer)});
    for (int i = 0; i < h.entries; i++) {
        if (!Button(ctx, p.entry[i], top)) return false;
        if (!Set(ctx, p.entry[i], "actionSelectPage", String(ctx, h.page))) return false;
        Link(ctx, above, p.entry[i]);
        above = p.entry[i];
    }
    Link(ctx, above, top);
    if (!h.hint) return true;
    for (uint32_t clip = FindField(holder, h.hint); clip; clip = R32(clip + flash::kClipParent))
        if (R32(clip + flash::kClipParent) == holder) Hide(clip);
    return true;
}

bool IsHeading(const Row* row) { return row && row->kind == app::rows::Kind::Heading; }

// the rows of the page's mode with their groups' headings, behind the PAGE row where one button opens both
void Fill(Page& p) {
    p.rows.clear();
    if (p.host->entries == 1) p.rows.push_back(&kPageRow);
    size_t count = 0;
    const Row* list = p.extra ? app::rows::Controller(&count) : app::rows::Page(p.mode - 1, &count);
    for (size_t i = 0; i < count; i++) p.rows.push_back(&list[i]);
    if (!p.extra && app::rows::Restorable(p.mode - 1)) p.rows.push_back(nullptr);
    p.slots = std::min(p.slots, p.rows.size());
    p.top = 0;
}

// the first row the focus can be on, past the headings
size_t FirstRow(const Page& p) {
    size_t i = 0;
    while (i + 1 < p.rows.size() && IsHeading(p.rows[i])) i++;
    return i;
}

// the panel's rows in place of the stock buttons
bool Rows(X86Context& ctx, uint32_t root, uint32_t holder, uint32_t menu) {
    Page& p = g_page;
    const Host& h = *p.host;
    uint32_t like = Member(ctx, root, h.first);
    uint32_t nothing = Method(ctx, like, "Select");
    p.lead = Member(ctx, holder, std::strrchr(h.button, '.') + 1);
    if (!nothing || !flash::IsClip(p.lead)) return false;
    p.slots = kSlots;
    Fill(p);
    for (size_t i = 0; i < p.slots; i++) {
        char name[16];
        std::snprintf(name, sizeof(name), "RC_N%d", int(i));
        p.name[i] = Copy(ctx, holder, name, kDepth + int(i));
        name[3] = 'V';
        p.value[i] = Copy(ctx, holder, name, kDepth + kSlots + int(i));
        name[3] = 'L';
        p.arrow[0][i] = Copy(ctx, holder, name, kDepth + 2 * kSlots + int(i));
        name[3] = 'R';
        p.arrow[1][i] = Copy(ctx, holder, name, kDepth + 3 * kSlots + int(i));
        if (!p.name[i] || !p.value[i] || !p.arrow[0][i] || !p.arrow[1][i]) return false;
        for (uint32_t clip : {p.name[i], p.value[i], p.arrow[0][i], p.arrow[1][i]}) {
            G<float>(clip + flash::kClipScaleX) = h.scale;
            G<float>(clip + flash::kClipScaleY) = h.scale;
        }
        if (!Show(ctx, kName + int(i), p.name[i]) || !Show(ctx, kValue + int(i), p.value[i])) return false;
        for (int side = 0; side < 2; side++)
            if (!Show(ctx, (side ? kRight : kLeft) + int(i), p.arrow[side][i], nullptr, " ")) return false;
    }
    for (int side = 0; side < 2; side++) {
        char name[16];
        std::snprintf(name, sizeof(name), "RC_S%d", side);
        p.more[side] = Copy(ctx, holder, name, kDepth + 4 * kSlots + side);
        if (!p.more[side] || !Show(ctx, kScroll + side, p.more[side], nullptr, " ")) return false;
        G<float>(p.more[side] + flash::kClipScaleX) = h.scale * kMoreScale;
        G<float>(p.more[side] + flash::kClipScaleY) = h.scale * kMoreScale;
    }
    p.first = p.name[0];
    Follow(p);
    CallGuest(ctx, flash::kRunActions, {R32(flash::kPlayer)});
    for (size_t i = 0; i < p.slots; i++) {
        // a confirm on a button with no action is a back
        if (!Button(ctx, p.name[i], like) || !Set(ctx, p.name[i], "actionSelectFunction", nothing)) return false;
        if (i) Link(ctx, p.name[i - 1], p.name[i]);
    }
    for (const char* stock : h.stock)
        if (stock) Hide(Member(ctx, holder, stock));
    Show(ctx, kTitle, holder, "$FE_OPTIONS");
    // back is the options page, not its own back
    Set(ctx, menu, "mfuncFunctionBack", 0);
    Set(ctx, menu, "mstrPageBack", String(ctx, h.page));
    Focus(ctx, p.name[std::min(FirstRow(p), p.slots - 1)]);
    return true;
}

// the sticks' rows under the page's own buttons, between its last and its first in their ring; all of them show
bool Extras(X86Context& ctx, uint32_t root, uint32_t holder) {
    Page& p = g_page;
    const Host& h = *p.host;
    uint32_t top = Member(ctx, root, h.first);
    uint32_t above = Member(ctx, root, h.last);
    // the last is a plain button, the others choose among values
    uint32_t nothing = Method(ctx, above, "Select");
    p.lead = Member(ctx, holder, h.under);
    if (!top || !above || !nothing || !flash::IsClip(p.lead)) return false;
    p.extra = true;
    p.slots = kSlots;
    Fill(p);
    for (size_t i = 0; i < p.slots; i++) {
        char name[16];
        std::snprintf(name, sizeof(name), "RC_N%d", int(i));
        p.name[i] = Copy(ctx, holder, name, kDepth + int(i));
        name[3] = 'V';
        p.value[i] = Copy(ctx, holder, name, kDepth + kSlots + int(i));
        name[3] = 'L';
        p.arrow[0][i] = Copy(ctx, holder, name, kDepth + 2 * kSlots + int(i));
        name[3] = 'R';
        p.arrow[1][i] = Copy(ctx, holder, name, kDepth + 3 * kSlots + int(i));
        if (!p.name[i] || !p.value[i] || !p.arrow[0][i] || !p.arrow[1][i]) return false;
        if (!Show(ctx, kName + int(i), p.name[i]) || !Show(ctx, kValue + int(i), p.value[i])) return false;
        for (int side = 0; side < 2; side++)
            if (!Show(ctx, (side ? kRight : kLeft) + int(i), p.arrow[side][i], nullptr, " ")) return false;
    }
    p.first = p.name[0];
    Follow(p);
    CallGuest(ctx, flash::kRunActions, {R32(flash::kPlayer)});
    for (size_t i = 0; i < p.slots; i++) {
        // a confirm on a button with no action is a back
        if (!Button(ctx, p.name[i], above) || !Set(ctx, p.name[i], "actionSelectFunction", nothing)) return false;
        Link(ctx, i ? p.name[i - 1] : above, p.name[i]);
    }
    Link(ctx, p.name[p.slots - 1], top);
    return true;
}

// what row i of the page shows as its value
std::string ValueText(const Page& p, size_t i) {
    const Row* row = p.rows[i];
    if ((p.capture == 1 || p.capture == 2) && p.bind == i) return app::Ui("PRESS A KEY");
    if (row == &kPageRow) return app::Ui(kCaptions[p.mode - 1]);
    if (!row || IsHeading(row)) return " ";
    std::string value = app::rows::Value(*row);
    // key names as they are
    if (row->kind == app::rows::Kind::Bind && value != "UNBOUND") return value;
    return app::Ui(value);
}

uint32_t Glyph(uint32_t font, uint16_t code) {
    uint32_t glyph = R32(font + kFontGlyphs + (code & 0x7F) * 4);
    uint32_t last = R32(font + kFontLastGlyph);
    if (!glyph) return last;
    for (int left = 256; G<uint16_t>(glyph + kGlyphCode) != code && glyph != last && left; left--) glyph += kGlyphSize;
    return glyph;
}

// UTF-8 text's characters, as the font codes its glyphs
std::vector<uint16_t> Codes(const std::string& text) {
    std::vector<uint16_t> codes;
    for (size_t i = 0; i < text.size(); i++) {
        uint8_t c = uint8_t(text[i]);
        if (c >= 0xE0 && i + 2 < text.size()) {
            codes.push_back(uint16_t((c & 0x0F) << 12 | (text[i + 1] & 0x3F) << 6 | (text[i + 2] & 0x3F)));
            i += 2;
        } else if (c >= 0xC0 && i + 1 < text.size()) {
            codes.push_back(uint16_t((c & 0x1F) << 6 | (text[i + 1] & 0x3F)));
            i++;
        } else {
            codes.push_back(c);
        }
    }
    return codes;
}

// a text's width at a height of 1, as 0xC3AF0
float Measure(uint32_t font, const std::string& text) {
    std::vector<uint16_t> codes = Codes(text);
    if (!font || codes.empty()) return 0.0f;
    uint32_t glyph = Glyph(font, codes[0]);
    float x = -G<float>(glyph + kGlyphLeft);
    for (size_t i = 1; i < codes.size(); i++) {
        x += G<float>(glyph + kGlyphAdvance);
        glyph = Glyph(font, codes[i]);
    }
    float spacing = R32(glyph + kGlyphSpaced) ? G<float>(font + kFontSpacing) : 1.0f;
    float end = x + G<float>(glyph + kGlyphLeft) + G<float>(glyph + kGlyphWidth) * spacing;
    return G<float>(font + kFontScale) * end;
}

// how far right of the focused name the widest value of the page ends, the label clip scaled as it lights
float Reach(X86Context& ctx, const Page& p, uint32_t focus) {
    const Host& h = *p.host;
    uint32_t font = CallGuest(ctx, kFindFont, {R32(kGame), kFontName});
    uint32_t label = flash::FirstChild(focus);
    float scale = h.scale * (label ? G<float>(label + flash::kClipScaleX) : 1.0f);
    // capture text left out so the bar keeps its width while a key is bound
    Page shown = p;
    shown.capture = 0;
    float widest = 0.0f;
    for (size_t i = 0; i < p.rows.size(); i++) widest = std::max(widest, Measure(font, ValueText(shown, i)));
    // the left arrow, the value, the right arrow
    return h.value_x - h.name_x + 2 * kArrowSpace + (kTextLeft + widest * kTextHeight) * scale + kTickGap;
}

// a row the arrows step: the PAGE row, and a value the panel does not grey out
bool Steppable(const Row* row) {
    if (row == &kPageRow) return true;
    return row && row->kind == app::rows::Kind::Value && app::rows::Enabled(*row) && !app::rows::Opens(*row);
}

// the stock bar as it rests, read from its parts
Bar ReadBar(uint32_t bar) {
    Bar stock{};
    int left = 256;
    for (uint32_t part = flash::FirstChild(bar); part && left; part = R32(part + flash::kClipNext), left--) {
        int depth = flash::Depth(part);
        if (depth == kBarBox) stock.box = G<float>(part + flash::kClipScaleX);
        if (depth == kBarLine) stock.line = G<float>(part + flash::kClipX);
        if (depth == kBarTick) stock.tick = G<float>(part + flash::kClipX);
        if (depth == kBarScan) stock.scan = G<float>(part + flash::kClipX);
    }
    return stock;
}

// the bar on the focused row reaches past the values, its ends and line as stock; on a stock row of a page with
// the sticks' rows it is the stock bar again
void StretchBar(X86Context& ctx, Page& p, uint32_t root, uint32_t focus, bool ours) {
    uint32_t bar = Member(ctx, Member(ctx, root, "Main"), "ButtonSelector");
    if (flash::Type(bar) != flash::kSprite) return;
    uint32_t data = R32(bar + flash::kClipData);
    if (!data || data == flash::kNoData || R32(data + flash::kDataFrame) != kBarRest) return;
    if (p.extra && !p.bar_seen) {
        if (ours) return;
        p.bar = ReadBar(bar);
        p.bar_seen = p.bar.tick > kBarLeft;
        return;
    }
    const Bar& stock = p.extra ? p.bar : p.host->bar;
    float right = ours ? std::max(stock.tick, Reach(ctx, p, focus)) : stock.tick;
    float stretch = (right - kBarLeft) / (stock.tick - kBarLeft);
    int left = 256;
    for (uint32_t part = flash::FirstChild(bar); part && left; part = R32(part + flash::kClipNext), left--) {
        int depth = flash::Depth(part);
        if (depth == kBarBox) G<float>(part + flash::kClipScaleX) = stock.box * stretch;
        if (depth == kBarLine) G<float>(part + flash::kClipX) = stock.line + right - stock.tick;
        if (depth == kBarTick) G<float>(part + flash::kClipX) = right;
        if (depth == kBarScan) {
            G<float>(part + flash::kClipScaleX) = stretch;
            G<float>(part + flash::kClipX) = kBarLeft + (stock.scan - kBarLeft) * stretch;
        }
    }
}

// Delete on a binding row
void Clear(X86Context& ctx, const Row* row) {
    if (!row || row->kind != app::rows::Kind::Bind) return;
    std::string was = app::rows::Value(*row);
    input::Unbind(size_t(row->id));
    if (app::rows::Value(*row) != was) Sound(ctx);
}

const Host* FindHost(const char* name) {
    for (const Host& h : kHosts)
        if (std::strcmp(name, h.page) == 0) return &h;
    return nullptr;
}

// the page's clips made again: the stock buttons and ours, or the rows of the mode asked for
void Build(X86Context& ctx, const Host& host, uint32_t root, uint32_t holder, uint32_t menu) {
    Page& p = g_page;
    p = {};
    for (Field& f : g_fields) f.data = 0;
    p.host = &host;
    if (!host.entries) {
        // its rows as a mode of their own: the cursor and the taps on arrows find them as the pages' rows
        p.mode = 1;
        p.failed = !Extras(ctx, root, holder);
        LOGI("options menu: %s, controller rows%s", host.page, p.failed ? ", failed" : "");
        return;
    }
    p.mode = g_next;
    g_next = 0;
    p.failed = !(p.mode ? Rows(ctx, root, holder, menu) : Entries(ctx, root, holder));
    const char* shown = p.mode ? kCaptions[p.mode - 1] : "buttons";
    LOGI("options menu: %s, %s%s", host.page, shown, p.failed ? ", failed" : "");
    if (!p.failed && !p.mode && g_last) Focus(ctx, p.entry[std::min(g_last, host.entries) - 1]);
    g_last = p.mode;
}

// the key binding's next step, see Page::capture
void UpdateCapture(uint32_t ui, uint32_t id) {
    Page& p = g_page;
    int vk = input::PressedKey();
    if (p.capture == 2 && vk) {
        if (vk != kEscape) input::Bind(size_t(p.rows[p.bind]->id), vk);
        p.capture = 3;
    } else if (p.capture != 2 && !vk) {
        p.capture = p.capture == 1 ? 2 : 0;
    } else if (id == kInputBack) {
        p.capture = 0;  // a controller's back
    }
    W32(ui + kMenuInput, 0);
}

// up and down move the focus here, not in the script: over the headings, round the ends, the rows scrolling to
// show it with its heading; false for any other input
bool UpdateScroll(X86Context& ctx, size_t slot, uint32_t id) {
    if (id != kInputUp && id != kInputDown) return false;
    Page& p = g_page;
    size_t n = p.rows.size();
    size_t at = p.top + slot, to = at;
    do to = id == kInputDown ? (to + 1) % n : (to + n - 1) % n;
    while (IsHeading(p.rows[to]) && to != at);
    size_t shown = to > 0 && IsHeading(p.rows[to - 1]) ? to - 1 : to;
    if (shown < p.top) p.top = shown;
    if (to >= p.top + p.slots) p.top = to + 1 - p.slots;
    Focus(ctx, p.name[to - p.top]);
    return true;
}

// a tap on a scroll arrow (mouse.cpp): the rows move one that way; the focus stays on its row while it shows, else
// takes the nearest one in sight
int g_scroll_tap = 0;  // -1 up, 1 down

bool ScrollMore(const Page& p, int dir) { return dir < 0 ? p.top > 0 : p.top + p.slots < p.rows.size(); }

void ScrollRow(X86Context& ctx, Page& p, int dir, size_t slot) {
    size_t at = slot < p.slots ? p.top + slot : p.top;
    p.top = dir < 0 ? p.top - 1 : p.top + 1;
    size_t first = p.top, last = p.top + p.slots - 1;
    at = std::clamp(at, first, last);
    // off a heading, into the rows in sight
    size_t step = at == last ? size_t(-1) : 1;
    while (IsHeading(p.rows[at]) && at + step >= first && at + step <= last) at += step;
    Focus(ctx, p.name[at - p.top]);
}

// left, right or confirm on a row: the PAGE row turns the page, a value steps, a binding listens, the last restores
void ApplyRowInput(size_t slot, uint32_t id, bool locked) {
    Page& p = g_page;
    const Row* row = p.rows[p.top + slot];
    if (IsHeading(row)) return;
    bool value = row && row->kind == app::rows::Kind::Value;
    bool changes = id >= kInputLeft && id <= kInputConfirm;
    if (row == &kPageRow && changes) {
        // round the pages, left the other way
        p.mode = id == kInputLeft ? (p.mode + kEntries - 2) % kEntries + 1 : p.mode % kEntries + 1;
        Fill(p);
    } else if (value && changes) {
        if (!locked) app::rows::Value(*row, id == kInputLeft ? -1 : 1);
    } else if (id == kInputConfirm && row) {
        p.capture = 1;
        p.bind = p.top + slot;
    } else if (id == kInputConfirm) {
        app::rows::Defaults(p.mode - 1);
    }
}

void UpdateRow(X86Context& ctx, uint32_t ui, size_t slot, uint32_t id, bool clear) {
    const Row* row = g_page.rows[g_page.top + slot];
    if (clear) Clear(ctx, row);
    bool value = row && row->kind == app::rows::Kind::Value;
    bool locked = row && !app::rows::Enabled(*row);  // greyed in the panel
    // the sticks' rows are in the page's ring, its script moves the focus over them
    bool edge = !g_page.extra && UpdateScroll(ctx, slot, id);
    if (edge) {
        W32(ui + kMenuInput, 0);
    } else {
        ApplyRowInput(slot, id, locked);
    }
    bool stepped = value && !locked && (id == kInputLeft || id == kInputRight);
    if (edge || stepped) Sound(ctx);
}

// the title, the rows' names and values, the arrows of the rows that step, the right one just past its value
void UpdateTexts(X86Context& ctx, Page& p) {
    // a value lights up with its name: a clip's matrix, then its colours
    constexpr uint32_t kMatrixAndColours = 0x38;
    constexpr float kArrowGap = 4.0f;
    const Host& h = *p.host;
    uint32_t font = CallGuest(ctx, kFindFont, {R32(kGame), kFontName});
    if (!p.extra) Write(g_fields[kTitle], app::Ui(kCaptions[p.mode - 1]));
    for (size_t i = 0; i < p.slots; i++) {
        const Row* row = p.rows[p.top + i];
        std::string value = ValueText(p, p.top + i);
        bool arrows = Steppable(row) && !p.capture;
        std::string name = app::Ui(row ? row->label : "RESTORE DEFAULTS");
        Write(g_fields[kName + i], IsHeading(row) ? "- " + name + " -" : name);
        Write(g_fields[kValue + i], value);
        for (int side = 0; side < 2; side++) Write(g_fields[(side ? kRight : kLeft) + i], arrows ? kArrows[side] : " ");
        uint32_t from = flash::FirstChild(p.name[i]);
        if (!from) continue;
        for (uint32_t clip : {p.value[i], p.arrow[0][i], p.arrow[1][i]})
            if (uint32_t to = flash::FirstChild(clip))
                std::memcpy(HostPtr(to + flash::kClipMatrix), HostPtr(from + flash::kClipMatrix), kMatrixAndColours);
        float scale = h.scale * G<float>(from + flash::kClipScaleX);
        p.right_x[i] = h.value_x + kArrowSpace + (kTextLeft + Measure(font, value) * kTextHeight) * scale + kArrowGap;
    }
    Write(g_fields[kScroll], p.top > 0 ? kMore[0] : " ");
    Write(g_fields[kScroll + 1], p.top + p.slots < p.rows.size() ? kMore[1] : " ");
}

void Tick(X86Context& ctx, uint32_t ui) {
    // read every tick, so a Delete that answers a capture is not a fresh press once it ends
    bool clear = input::TakeDeletePress();
    Page& p = g_page;
    const char* name = static_cast<const char*>(HostPtr(ui + kPageName));
    const Host* host = FindHost(name);
    bool running = host && R32(flash::kPlayerReady) == 1 && R32(flash::kPlayer) && R32(ui + kPageReady) == kPageRunning;
    if (!running) {
        if (*name && !host) g_next = g_last = 0;
        p = {};
        return;
    }
    uint32_t going = R32(ui + kPageLeaving);
    bool leaving = going || R32(ui + kPageAsked) == 1;
    uint32_t root = flash::Level(flash::kMenuLevel);
    uint32_t holder = Member(ctx, root, "Main");
    uint32_t menu = Member(ctx, root, "MyMenu");
    if (host->holder) holder = Member(ctx, holder, host->holder);
    if (!holder || !menu) return;
    if (p.failed) {
        if (going == 1) p = {};
        return;
    }
    if (p.mode < 0 || Member(ctx, holder, p.mode ? "RC_N0" : "RC_E0") != p.first) {
        if (!leaving) Build(ctx, *host, root, holder, menu);
        return;
    }
    Follow(p);
    if (going == 1) p = {};
    if (leaving) return;
    uint32_t id = InputTaken(ui) ? R32(ui + kMenuInput) : 0;
    uint32_t focus = Member(ctx, menu, "mFocusButton");
    if (!p.mode) {
        const uint32_t* at = std::find(std::begin(p.entry), std::end(p.entry), focus);
        if (id == kInputConfirm && at != std::end(p.entry)) g_next = int(at - p.entry) + 1;
        return;
    }
    size_t slot = size_t(std::find(p.name, p.name + p.slots, focus) - p.name);
    if (int dir = std::exchange(g_scroll_tap, 0); dir && !p.capture && ScrollMore(p, dir)) {
        ScrollRow(ctx, p, dir, slot);
        Sound(ctx);
        W32(ui + kMenuInput, 0);
        UpdateTexts(ctx, p);
        Follow(p);
        return;
    }
    if (p.capture) {
        UpdateCapture(ui, id);
    } else if (slot < p.slots) {
        UpdateRow(ctx, ui, slot, id, clear);
    }
    UpdateTexts(ctx, p);
    Follow(p);
    if (slot < p.slots || p.extra) StretchBar(ctx, p, root, focus, slot < p.slots);
}

}  // namespace

// ebp the front end, after the cursor's input and before the Flash player runs the page's script
void OptionsMenu(X86Context& ctx, uint8_t*) {
    X86Context saved = ctx;
    Tick(ctx, ctx.ebp);
    ctx = saved;
}

namespace game {

// mouse.cpp: a key binding takes the next click
bool OptionsCapturing() { return g_page.capture != 0; }

// mouse.cpp: by a row's button, its value clip and where its arrows are across, from to, in the space of the clip the
// rows are in (the value clip's parent); false for a button not one of ours. A row that does not step has its arrows
// from 0 to 0. Their text fields may be wider than their text, so this and not their boxes.
bool OptionsRow(uint32_t button, uint32_t& value, float (&arrows)[2][2]) {
    const Page& p = g_page;
    if (p.mode <= 0 || p.failed) return false;
    for (size_t i = 0; i < p.slots; i++) {
        if (p.name[i] != button) continue;
        value = p.value[i];
        bool steps = Steppable(p.rows[p.top + i]) && !p.capture;
        for (int side = 0; side < 2; side++) {
            float x = G<float>(p.arrow[side][i] + flash::kClipX);
            arrows[side][0] = steps ? x : 0.0f;
            arrows[side][1] = steps ? x + kArrowSpace : 0.0f;
        }
        return true;
    }
    return false;
}

// mouse.cpp: a group's heading among the rows, which the cursor passes over
bool OptionsHeading(uint32_t button) {
    const Page& p = g_page;
    if (p.mode <= 0 || p.failed) return false;
    for (size_t i = 0; i < p.slots; i++)
        if (p.name[i] == button) return IsHeading(p.rows[p.top + i]);
    return false;
}

// mouse.cpp: the scroll arrow that way (-1 up, 1 down) while it shows, 0 when it does not; a tap on it moves the rows one
uint32_t OptionsScrollArrow(int dir) {
    const Page& p = g_page;
    if (p.mode <= 0 || p.failed || p.capture || !ScrollMore(p, dir)) return 0;
    return p.more[dir > 0];
}
void OptionsScrollTap(int dir) { g_scroll_tap = dir; }

// prompts.cpp's string lookup: a text of the page by its string id, 0 for none
uint32_t OptionsText(uint32_t id) {
    for (const Field& f : g_fields)
        if (f.text && f.id == id) return f.text;
    return 0;
}

}  // namespace game
