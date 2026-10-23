// The profile's save keeps its name whatever the language. The game names its one save with the FE_MCSAVE_PROFILE
// string ("Profile", Indonesian "Profil", the Europe disc's languages theirs) and the XDK finds a save by its name, so
// a change of language lost the save: it was there, under the other language's name. That string is the name of the
// save on the disk (its SaveMeta.xbx's Name, the newest when there are more), and "Profile" until there is one.
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <system_error>

#include "app/app.h"
#include "core/log.h"
#include "xdk/hle.h"

using namespace kernel;

namespace {

constexpr uint32_t kSaveNameId = 0x7D0DDAB7;  // FE_MCSAVE_PROFILE
constexpr char kEnglish[] = "Profile";         // as MainUS.bin has it
constexpr char kSaves[] = "hdd/E/UDATA/45410083";
constexpr uint32_t kRoom = 128;  // as prompts.cpp's copies: the game copies up to 127 bytes of a string
constexpr size_t kMaxMeta = 4096;

// UTF-16 LE, a BOM first, to UTF-8
std::string Utf8(const std::string& bytes) {
    std::string out;
    for (size_t i = bytes.size() >= 2 && uint8_t(bytes[0]) == 0xFF && uint8_t(bytes[1]) == 0xFE ? 2 : 0;
         i + 1 < bytes.size(); i += 2) {
        uint32_t c = uint8_t(bytes[i]) | uint32_t(uint8_t(bytes[i + 1])) << 8;
        if (c < 0x80) {
            out += char(c);
        } else if (c < 0x800) {
            out += char(0xC0 | c >> 6);
            out += char(0x80 | (c & 0x3F));
        } else {
            out += char(0xE0 | c >> 12);
            out += char(0x80 | (c >> 6 & 0x3F));
            out += char(0x80 | (c & 0x3F));
        }
    }
    return out;
}

// a save's name from its SaveMeta.xbx lines, empty when it has none
std::string MetaName(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    std::string bytes(kMaxMeta, '\0');
    in.read(bytes.data(), std::streamsize(bytes.size()));
    bytes.resize(size_t(in.gcount()));
    std::string text = Utf8(bytes);
    for (size_t at = 0; at < text.size();) {
        size_t end = text.find_first_of("\r\n", at);
        if (end == std::string::npos) end = text.size();
        if (text.compare(at, 5, "Name=") == 0) return text.substr(at + 5, end - at - 5);
        at = end + 1;
    }
    return {};
}

// the newest save's name, "Profile" when there is none
std::string DiskName() {
    std::error_code ec;
    std::string name = kEnglish;
    std::filesystem::file_time_type newest{};
    for (const auto& dir : std::filesystem::directory_iterator(app::ExeDir() / kSaves, ec)) {
        std::filesystem::path meta = dir.path() / "SaveMeta.xbx";
        auto when = std::filesystem::last_write_time(meta, ec);
        if (ec || when < newest) continue;
        std::string found = MetaName(meta);
        if (found.empty() || found.size() >= kRoom) continue;
        newest = when;
        name = found;
    }
    return name;
}

}  // namespace

namespace game {

// prompts.cpp's string lookup: the save's name in guest memory for FE_MCSAVE_PROFILE, 0 for any other string
uint32_t SaveName(uint32_t id) {
    if (id != kSaveNameId) return 0;
    static std::map<std::string, uint32_t> copies;  // a name the game was given keeps its memory
    std::string name = DiskName();
    auto [it, made] = copies.try_emplace(name, 0);
    if (made) {
        it->second = mem::AllocSystem(kRoom);
        if (it->second) {
            std::memset(HostPtr(it->second), 0, kRoom);
            std::memcpy(HostPtr(it->second), name.c_str(), name.size());
        }
        LOGI("save: named \"%s\"", name.c_str());
    }
    return it->second;
}

}  // namespace game
