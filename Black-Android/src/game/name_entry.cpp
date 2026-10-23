// Profile names on the keyboard: on the VirtualKeyboard page a typed character goes into the page script's name at
// its caret as the page's own keys put one in, within stringMaxLength; Backspace, Space and Enter call the page's
// BackSpace, DoSpace and DONE. Only what the keys offer types: small letters, digits and the ASCII symbols.
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "common/mouse_keyboard.h"
#include "game/flash.h"
#include "game/layout.h"
#include "xdk/hle.h"

using namespace kernel;
using namespace layout;

namespace {

constexpr uint32_t kGetVariableText = 0x101720;  // eax path; cdecl (out): the value as text, no size limit
constexpr uint32_t kSetVariableText = 0x101790;  // cdecl (path, text): a string value

constexpr char kPage[] = "VirtualKeyboard";
constexpr char kSymbols[] = "()&_^%\\/@#[]$;:'\"<>?!-*+={},.";
constexpr uint32_t kTextSize = 256;

uint32_t g_texts = 0;  // three guest texts of kTextSize: two arguments and a result
std::u16string g_typed;

uint32_t Put(int slot, const std::string& text) {
    uint32_t at = g_texts + slot * kTextSize;
    std::snprintf(static_cast<char*>(HostPtr(at)), kTextSize, "%s", text.c_str());
    return at;
}

std::string Get(X86Context& ctx, const char* name) {
    uint32_t out = g_texts + 2 * kTextSize;
    G<uint8_t>(out) = 0;
    ctx.eax = Put(0, std::string("_level4.") + name);
    CallGuest(ctx, kGetVariableText, {out});
    return static_cast<const char*>(HostPtr(out));
}

void Call(X86Context& ctx, const char* function) {
    ctx.eax = Put(0, "_level4");
    CallGuest(ctx, flash::kCall, {Put(1, function), 0, 0});
}

void Insert(X86Context& ctx, uint32_t ui, char c) {
    std::string name = Get(ctx, "FINALRESULTSTRING");
    if (name == "undefined") name.clear();
    if (name.size() >= size_t(std::atoi(Get(ctx, "stringMaxLength").c_str()))) return;
    size_t caret = std::min(size_t(std::max(std::atoi(Get(ctx, "stringIndex").c_str()), 0)), name.size());
    name.insert(caret, 1, c);
    CallGuest(ctx, kSetVariableText, {Put(0, "_level4.FINALRESULTSTRING"), Put(1, name)});
    // the page steps stringIndex itself, a number; the game takes one call a tick and UpdateString sends the caret too
    Call(ctx, "CursorForward");
    W32(ui + kCallWaits, 0);
    Call(ctx, "UpdateString");
}

void Type(X86Context& ctx, uint32_t ui, char16_t c) {
    if (c == u'\b') return Call(ctx, "BackSpace");
    if (c == u' ') return Call(ctx, "DoSpace");
    if (c == u'\r') return Call(ctx, "DONE");
    if (c > 0x7F) return;
    char ascii = char(std::tolower(int(c)));
    bool offered = std::isalnum(static_cast<unsigned char>(ascii)) || (ascii && std::strchr(kSymbols, ascii));
    if (offered) Insert(ctx, ui, ascii);
}

bool OnPage(uint32_t ui) {
    if (R32(flash::kPlayerReady) != 1 || !R32(flash::kPlayer)) return false;
    if (R32(ui + kPageReady) != kPageRunning || R32(ui + kPageLeaving)) return false;
    return std::strcmp(static_cast<const char*>(HostPtr(ui + kPageName)), kPage) == 0;
}

}  // namespace

// ebp the front end, before the Flash player runs the page's script; one character a tick, as the page's calls to the
// game take one a tick
void NameEntry(X86Context& ctx, uint8_t*) {
    bool on = OnPage(ctx.ebp);
    input::SetTextEntry(on);
    if (!on) {
        g_typed.clear();
        return;
    }
    g_typed += input::TakeChars();
    if (g_typed.empty() || R32(ctx.ebp + kCallWaits)) return;
    if (!g_texts) g_texts = mem::AllocSystem(3 * kTextSize);
    if (!g_texts) return;
    X86Context saved = ctx;
    Type(ctx, ctx.ebp, g_typed.front());
    g_typed.erase(0, 1);
    ctx = saved;
}
