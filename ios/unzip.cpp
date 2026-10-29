// A small ZIP reader for the first run: the player's own game archive arrives as a .zip and the app has to put its
// files where the game expects them. Stored and deflated entries, no encryption, no ZIP64: what a game folder zipped
// on Windows or macOS looks like.
#include "unzip.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <vector>

#include <zlib.h>

namespace {

uint16_t U16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }
uint32_t U32(const uint8_t* p) { return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24); }

bool Inflate(const uint8_t* in, size_t in_size, std::vector<uint8_t>& out, size_t expected) {
    out.resize(expected);
    if (!expected) return true;
    z_stream zs{};
    if (inflateInit2(&zs, -MAX_WBITS) != Z_OK) return false;
    zs.next_in = const_cast<Bytef*>(in);
    zs.avail_in = uInt(in_size);
    zs.next_out = out.data();
    zs.avail_out = uInt(out.size());
    const int r = inflate(&zs, Z_FINISH);
    inflateEnd(&zs);
    return r == Z_STREAM_END;
}

// a name inside the archive: no absolute paths, no "..", separators as '/'
bool SafeName(std::string& name) {
    std::replace(name.begin(), name.end(), '\\', '/');
    while (!name.empty() && name.front() == '/') name.erase(name.begin());
    if (name.empty()) return false;
    if (name.find("..") != std::string::npos) return false;
    return true;
}

}  // namespace

bool UnzipTo(const std::filesystem::path& zip, const std::filesystem::path& dest, std::string& error) {
    std::ifstream f(zip, std::ios::binary);
    if (!f) {
        error = "cannot open the archive";
        return false;
    }
    f.seekg(0, std::ios::end);
    const std::streamoff size = f.tellg();
    if (size < 22) {
        error = "the archive is too small";
        return false;
    }
    const size_t tail = size_t(std::min<std::streamoff>(size, 66560));
    std::vector<uint8_t> buf(tail);
    f.seekg(size - std::streamoff(tail));
    f.read(reinterpret_cast<char*>(buf.data()), std::streamsize(tail));
    ssize_t eocd = -1;
    for (size_t i = tail - 22 + 1; i-- > 0;) {
        if (U32(&buf[i]) == 0x06054b50u) {
            eocd = ssize_t(i);
            break;
        }
    }
    if (eocd < 0) {
        error = "no end of central directory";
        return false;
    }
    const uint16_t entries = U16(&buf[eocd + 10]);
    const uint32_t cd_size = U32(&buf[eocd + 12]);
    const uint32_t cd_offset = U32(&buf[eocd + 16]);
    std::vector<uint8_t> cd(cd_size);
    f.seekg(std::streamoff(cd_offset));
    f.read(reinterpret_cast<char*>(cd.data()), std::streamsize(cd.size()));
    if (!f) {
        error = "cannot read the central directory";
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(dest, ec);
    size_t at = 0;
    for (uint16_t i = 0; i < entries; i++) {
        if (at + 46 > cd.size() || U32(&cd[at]) != 0x02014b50u) {
            error = "bad central directory entry";
            return false;
        }
        const uint16_t flags = U16(&cd[at + 8]);
        const uint16_t method = U16(&cd[at + 10]);
        const uint32_t comp = U32(&cd[at + 20]);
        const uint32_t uncomp = U32(&cd[at + 24]);
        const uint16_t name_len = U16(&cd[at + 28]);
        const uint16_t extra_len = U16(&cd[at + 30]);
        const uint16_t comment_len = U16(&cd[at + 32]);
        const uint32_t local = U32(&cd[at + 42]);
        std::string name(reinterpret_cast<const char*>(&cd[at + 46]), name_len);
        at += 46u + name_len + extra_len + comment_len;
        if (!SafeName(name)) continue;
        if (flags & 1) {
            error = "the archive is encrypted";
            return false;
        }
        const std::filesystem::path out = dest / name;
        if (!name.empty() && name.back() == '/') {
            std::filesystem::create_directories(out, ec);
            continue;
        }
        // local header: skip its own name and extra to reach the data
        uint8_t head[30];
        f.seekg(std::streamoff(local));
        f.read(reinterpret_cast<char*>(head), 30);
        if (!f || U32(head) != 0x04034b50u) {
            error = "bad local header";
            return false;
        }
        const std::streamoff data = std::streamoff(local) + 30 + U16(head + 26) + U16(head + 28);
        std::vector<uint8_t> packed(comp);
        f.seekg(data);
        f.read(reinterpret_cast<char*>(packed.data()), std::streamsize(packed.size()));
        if (!f) {
            error = "cannot read an entry";
            return false;
        }
        std::filesystem::create_directories(out.parent_path(), ec);
        if (method == 0) {
            std::ofstream w(out, std::ios::binary | std::ios::trunc);
            w.write(reinterpret_cast<const char*>(packed.data()), std::streamsize(packed.size()));
            if (!w) {
                error = "cannot write " + out.string();
                return false;
            }
            continue;
        }
        if (method != 8) {
            error = "unsupported compression";
            return false;
        }
        std::vector<uint8_t> plain;
        if (!Inflate(packed.data(), packed.size(), plain, uncomp)) {
            error = "cannot inflate " + name;
            return false;
        }
        std::ofstream w(out, std::ios::binary | std::ios::trunc);
        w.write(reinterpret_cast<const char*>(plain.data()), std::streamsize(plain.size()));
        if (!w) {
            error = "cannot write " + out.string();
            return false;
        }
    }
    return true;
}
