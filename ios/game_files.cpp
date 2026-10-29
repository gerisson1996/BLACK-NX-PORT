// The game on iOS: its files or a disc image (.iso) in the app's Documents/black/game (or its assets folder). The xbe
// must be the one this build was recompiled from; the Europe xbe (the Black PT-BR one among them) is turned into the
// US one by src/common/europe.patch, embedded by CMake.
#include <sys/stat.h>

#include <chrono>
#include <thread>

#include <algorithm>
#include <cctype>
#include <fstream>

#include "app/app.h"
#include "core/log.h"
#include "image_symbols.h"
#include "kernel/crypto.h"
#include "kernel/disc.h"

extern const char kImageSha1[];  // the xbe recompiled into this app, written by CMake
// src/common/europe.patch, made a byte array by CMake
extern const unsigned char kEuropePatch[];
extern const unsigned long kEuropePatchSize;

namespace app {

namespace {

namespace fs = std::filesystem;

// the game's folder beside the settings, and an assets folder in it
std::vector<fs::path> AssetFolders() {
    return {ExeDir() / "game", ExeDir() / "game" / "assets", ExeDir() / "assets"};
}

std::string Sha1(const std::vector<uint8_t>& data) {
    alignas(8) uint8_t ctx[128];
    uint8_t digest[20];
    crypto::ShaInit(ctx);
    crypto::ShaUpdate(ctx, data.data(), data.size());
    crypto::ShaFinal(ctx, digest);
    std::string hex;
    for (uint8_t b : digest) {
        hex += "0123456789abcdef"[b >> 4];
        hex += "0123456789abcdef"[b & 15];
    }
    return hex;
}

bool ReadHostFile(const fs::path& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), {});
    return true;
}

constexpr char kEuropeSha1[] = "796ad8369354e48aa15644d4707dd70bd3bc3b4c";

class PatchReader {
public:
    PatchReader(const uint8_t* data, size_t size) : at_(data), end_(data + size) {}
    bool Done() const { return at_ == end_; }
    bool Failed() const { return failed_; }

    uint8_t Byte() {
        if (at_ == end_) {
            failed_ = true;
            return 0;
        }
        return *at_++;
    }

    // 7 bits a byte, low first
    uint64_t Number() {
        uint64_t n = 0;
        for (int shift = 0; shift < 64; shift += 7) {
            uint8_t b = Byte();
            n |= uint64_t(b & 0x7F) << shift;
            if (b < 0x80) return n;
        }
        failed_ = true;
        return 0;
    }

private:
    const uint8_t* at_;
    const uint8_t* end_;
    bool failed_ = false;
};

// europe.patch applied to the Europe xbe, block by block
struct EuropePatch {
    const std::vector<uint8_t>& from;
    PatchReader patch;
    uint64_t read = 0;
    std::vector<uint8_t> out;

    // bytes copied from the Europe xbe with a few of them changed, new bytes, then a move of the read position
    bool ApplyBlock() {
        uint64_t copy = patch.Number();
        if (read > from.size() || copy > from.size() - read) return false;
        size_t start = out.size();
        out.insert(out.end(), from.begin() + read, from.begin() + read + copy);
        uint64_t changes = patch.Number();
        uint64_t at = 0;
        for (uint64_t i = 0; i < changes && !patch.Failed(); i++) {
            at += patch.Number();
            if (at >= copy) return false;
            out[start + at] += patch.Byte();
            at++;
        }
        uint64_t added = patch.Number();
        for (uint64_t i = 0; i < added && !patch.Failed(); i++) out.push_back(patch.Byte());
        return MoveRead(read + copy) && !patch.Failed();
    }

    // even moves forward by half, odd back by half plus one
    bool MoveRead(uint64_t from_end) {
        uint64_t move = patch.Number();
        uint64_t distance = move >> 1;
        if (distance > from.size()) return false;
        if (!(move & 1)) {
            read = from_end + distance;
            return true;
        }
        if (distance >= from_end) return false;
        read = from_end - distance - 1;
        return true;
    }
};

// the US xbe in place of the Europe one; false leaves it as it was
bool ConvertEurope(std::vector<uint8_t>& xbe) {
    size_t size = size_t(kEuropePatchSize);
    if (!size) return false;
    EuropePatch europe{xbe, PatchReader(kEuropePatch, size)};
    while (!europe.patch.Done())
        if (!europe.ApplyBlock()) return false;
    xbe = std::move(europe.out);
    return true;
}

// an image cut short (a copy that stopped, a bad download) still opens, its folders being near the start; the game
// then fails a read later and shows its dirty disc screen. Its files are checked against its size, for the log
void CheckImage(const kernel::Disc& disc, const fs::path& iso) {
    struct stat st;
    if (::stat(iso.c_str(), &st)) {
        LOGE("game: %s: its size cannot be read", iso.string().c_str());
        return;
    }
    uint64_t size = uint64_t(st.st_size);
    uint64_t end = 0;
    size_t files = 0, past = 0;
    std::string first;
    std::vector<std::pair<const kernel::Disc::Entry*, std::string>> todo{{disc.Find(std::string_view()), ""}};
    while (!todo.empty()) {
        auto [dir, path] = todo.back();
        todo.pop_back();
        if (!dir) continue;
        for (const kernel::Disc::Entry& e : dir->children) {
            std::string name = path.empty() ? e.name : path + "\\" + e.name;
            if (e.dir) {
                todo.push_back({&e, name});
                continue;
            }
            files++;
            uint64_t file_end = e.offset + e.size;
            end = std::max(end, file_end);
            if (file_end > size && past++ == 0) first = name;
        }
    }
    LOGR("Game: image %llu bytes, %zu files up to byte %llu", (unsigned long long)size, files, (unsigned long long)end);
    if (past)
        LOGE("game: %s is cut short: %zu of its files end past its %llu bytes, %s the first; copy the image again",
             iso.string().c_str(), past, (unsigned long long)size, first.c_str());
}

// another release is refused, with the reason in the log
bool AskOtherRelease(const fs::path& name) {
    LOGE("game: %s is a different release of the game than this build was made from; not started",
         name.filename().string().c_str());
    return false;
}

// 1 found, 0 none there, -1 the player turned down another release
int Search(GameFiles& game) {
    // the first copy with this xbe wins, the extracted files before any image; another release is kept to offer
    GameFiles other;
    std::unique_ptr<kernel::Disc> other_disc;
    fs::path other_name;
    auto consider = [&](const fs::path& assets, const fs::path& name, std::vector<uint8_t> xbe,
                        std::unique_ptr<kernel::Disc> disc) {
        std::string sha = Sha1(xbe);
        bool europe = sha == kEuropeSha1;
        if (europe) europe = ConvertEurope(xbe);
        std::string image_sha = europe ? Sha1(xbe) : sha;
        bool match = image_sha == kImageSha1;
        const char* release = europe ? ", Europe release" : "";
        LOGR("Game: %s in %s, sha1 %s%s%s", IMAGE_XBE, name.string().c_str(), sha.c_str(), release,
             match ? "" : ", another release");
        if (match) {
            game = {assets, std::move(xbe)};
            kernel::MountDisc(std::move(disc));
            return true;
        }
        if (other_name.empty()) {
            other = {assets, std::move(xbe)};
            other_disc = std::move(disc);
            other_name = name;
        }
        return false;
    };

    std::error_code ec;
    for (const fs::path& assets : AssetFolders()) {
        if (!fs::is_directory(assets, ec)) continue;
        std::vector<uint8_t> xbe;
        if (ReadHostFile(assets / IMAGE_XBE, xbe) && consider(assets, assets, std::move(xbe), nullptr)) return 1;
        std::vector<fs::path> isos;
        for (auto& e : fs::directory_iterator(assets, ec)) {
            std::string ext = e.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
            if (ext == ".iso" && e.is_regular_file(ec)) isos.push_back(e.path());
        }
        std::sort(isos.begin(), isos.end());
        for (const fs::path& iso : isos) {
            auto disc = std::make_unique<kernel::Disc>();
            std::string error;
            if (!disc->Open(iso, error)) {
                LOGW("game: %s: %s", iso.string().c_str(), error.c_str());
                continue;
            }
            if (!disc->Load(IMAGE_XBE, xbe)) {
                LOGW("game: %s has no %s", iso.string().c_str(), IMAGE_XBE);
                continue;
            }
            CheckImage(*disc, iso);
            if (consider(assets, iso, std::move(xbe), std::move(disc))) return 1;
        }
    }

    if (other_name.empty()) return 0;
    if (!AskOtherRelease(other_name)) return -1;
    game = std::move(other);
    kernel::MountDisc(std::move(other_disc));
    return 1;
}

}  // namespace

bool FindGame(GameFiles& game) {
    int found = Search(game);
    if (found) return found > 0;
    LOGE("game: no %s in %s, the player must copy the game there first", IMAGE_XBE,
         (ExeDir() / "game").string().c_str());
    return false;
}

}  // namespace app
