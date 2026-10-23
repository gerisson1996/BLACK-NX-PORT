// The game's language. This build sets English (US) at boot whatever the console's language is; the LANGUAGE setting
// takes its place when the game files have that language's strings, else the game stays in English. A Europe disc
// has UK English strings in place of the US ones, which its own build picks for English.
#include <algorithm>
#include <array>
#include <cstdint>
#include <iterator>
#include <string>
#include <vector>

#include "common/options.h"
#include "core/log.h"
#include "xdk/hle.h"

using namespace kernel;

namespace {

// the language's index into the suffix table at 0x288318 (UK US Ja Ge Fr Sp It ...)
constexpr uint32_t kLanguage = 0x2D0D78;

struct Language {
    const char* code;    // game::kLanguages
    uint32_t index;
    const char* suffix;  // Main%s.bin
};
constexpr Language kLanguages[] = {{"fra", 4, "Fr"}, {"deu", 3, "Ge"}, {"ita", 6, "It"}, {"esp", 5, "Sp"}};
constexpr Language kUs = {"int", 1, "US"};
constexpr Language kUk = {"int", 0, "UK"};

std::string StringsFile(const Language& l) { return std::string("D:\\Language\\Strings\\Main") + l.suffix + ".bin"; }

bool OnDisc(const Language& l) {
    std::vector<uint8_t> file;
    return ReadGameFile(StringsFile(l), file);
}

// the index to boot in
uint32_t Chosen() {
    for (const Language& l : kLanguages) {
        if (game::BootLanguage() != l.code) continue;
        if (OnDisc(l)) {
            LOGI("language: %s", l.suffix);
            return l.index;
        }
        LOGW("language: no %s, the game stays in English", StringsFile(l).c_str());
    }
    bool uk_only = OnDisc(kUk) && !OnDisc(kUs);
    if (!uk_only) return kUs.index;
    LOGI("language: %s", kUk.suffix);
    return kUk.index;
}

uint32_t Index() {
    static const uint32_t index = Chosen();
    return index;
}

}  // namespace

// after the boot's own language store
void GameLanguage(X86Context&, uint8_t*) {
    W32(kLanguage, Index());
}

namespace game {

// the disc has a language besides English: the US release has none, so only English and Indonesian
bool HasLanguages() {
    static const bool any = [] {
        for (const Language& l : ::kLanguages)
            if (OnDisc(l)) return true;
        return false;
    }();
    return any;
}

// English and Indonesian always (Indonesian translates the recomp's text, the game's stays English), the others when
// the disc has their strings
bool LanguageAvailable(const std::string& code) {
    static const auto on_disc = [] {
        std::array<bool, std::size(::kLanguages)> out{};
        for (size_t i = 0; i < out.size(); i++) out[i] = OnDisc(::kLanguages[i]);
        return out;
    }();
    if (code == kUs.code || code == "ind") return true;
    for (size_t i = 0; i < on_disc.size(); i++)
        if (code == ::kLanguages[i].code) return on_disc[i];
    return false;
}

// prompts.cpp: the game shows the LANGUAGE setting's language, not English in its place; Indonesian boots the US
// strings and has them swapped by id, so it counts too
bool Translated() {
    if (game::BootLanguage() == "ind") return true;
    uint32_t index = Index();
    return index != kUs.index && index != kUk.index;
}

// prompts.cpp: the Indonesian text of a game string by its id, null when it has none or the language is another
const char* IndonesianString(uint32_t id) {
    struct String {
        uint32_t id;
        const char* text;
    };
    // sorted by id, made from MainUS.bin's strings
    static constexpr String kStrings[] = {
#include "game/strings_ind.inc"
    };
    static const bool on = game::BootLanguage() == "ind";
    if (!on) return nullptr;
    const String* at = std::lower_bound(std::begin(kStrings), std::end(kStrings), id,
                                        [](const String& s, uint32_t v) { return s.id < v; });
    return at != std::end(kStrings) && at->id == id ? at->text : nullptr;
}

}  // namespace game
