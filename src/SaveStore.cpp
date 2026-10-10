//
//  SaveStore.cpp
//  Robbo (C++ / SDL3)
//
//  Format pliku (little endian): "RSV1", liczba zapisów (u32), potem zapisy jeden po drugim.
//

#include "SaveStore.h"

#include "Storage.h"

#include <algorithm>
#include <cstring>

namespace {

class Writer {
public:
    std::vector<uint8_t> out;
    void u8(uint8_t v) { out.push_back(v); }
    void u32(uint32_t v) { for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>(v >> (i * 8))); }
    void u64(uint64_t v) { for (int i = 0; i < 8; ++i) out.push_back(static_cast<uint8_t>(v >> (i * 8))); }
    void i32(int v) { u32(static_cast<uint32_t>(v)); }
    void bytes(const std::vector<uint8_t>& b) { u32(static_cast<uint32_t>(b.size())); out.insert(out.end(), b.begin(), b.end()); }
    void str(const std::string& s) { bytes(std::vector<uint8_t>(s.begin(), s.end())); }
};

class Reader {
public:
    explicit Reader(const std::vector<uint8_t>& b) : b_(b) {}
    bool ok() const { return ok_; }
    uint8_t u8() { if (!need(1)) return 0; return b_[p_++]; }
    uint32_t u32() {
        if (!need(4)) return 0;
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i) v |= uint32_t(b_[p_++]) << (i * 8);
        return v;
    }
    uint64_t u64() {
        if (!need(8)) return 0;
        uint64_t v = 0;
        for (int i = 0; i < 8; ++i) v |= uint64_t(b_[p_++]) << (i * 8);
        return v;
    }
    int i32() { return static_cast<int32_t>(u32()); }
    std::vector<uint8_t> bytes() {
        const uint32_t n = u32();
        if (!need(n)) return {};
        std::vector<uint8_t> v(b_.begin() + p_, b_.begin() + p_ + n);
        p_ += n;
        return v;
    }
    std::string str() { auto v = bytes(); return std::string(v.begin(), v.end()); }

private:
    bool need(size_t n) {
        if (!ok_ || p_ + n > b_.size()) { ok_ = false; return false; }
        return true;
    }
    const std::vector<uint8_t>& b_;
    size_t p_ = 0;
    bool ok_ = true;
};

void writeSnapshot(Writer& w, const GameSnapshot& s) {
    w.bytes(s.m);
    w.bytes(s.scr);
    const int ints[] = {s.x, s.y, s.sx, s.sy, s.rx, s.ry, s.dir, s.v9b, s.alive, s.level, s.keys, s.lives,
                        s.screws, s.lifeLevel, s.ammo, s.score[0], s.score[1], s.score[2], s.moving, s.ticks,
                        s.robboAnim, s.cooldown, s.robboScreenAddr, s.cdtmv3, s.cdtmv4, s.screenTop, s.vscrol,
                        s.scroll99, s.scroll9A, s.checkpoint};
    w.u32(static_cast<uint32_t>(sizeof(ints) / sizeof(ints[0])));
    for (int v : ints) w.i32(v);
    w.bytes(s.shadow);
}

bool readSnapshot(Reader& r, GameSnapshot& s) {
    s.m = r.bytes();
    s.scr = r.bytes();
    int* fields[] = {&s.x, &s.y, &s.sx, &s.sy, &s.rx, &s.ry, &s.dir, &s.v9b, &s.alive, &s.level, &s.keys,
                     &s.lives, &s.screws, &s.lifeLevel, &s.ammo, &s.score[0], &s.score[1], &s.score[2],
                     &s.moving, &s.ticks, &s.robboAnim, &s.cooldown, &s.robboScreenAddr, &s.cdtmv3, &s.cdtmv4,
                     &s.screenTop, &s.vscrol, &s.scroll99, &s.scroll9A, &s.checkpoint};
    const uint32_t n = r.u32();
    if (n != sizeof(fields) / sizeof(fields[0])) return false;
    for (int* f : fields) *f = r.i32();
    s.shadow = r.bytes();
    return r.ok() && s.m.size() == 0x400 && s.scr.size() == 0x800 && s.shadow.size() == 7;
}

}  // namespace

SaveStore::SaveStore(std::string path) : path_(std::move(path)) { load(); }

void SaveStore::load() {
    std::vector<uint8_t> b;
    if (!Storage::readFile(path_, b) || b.size() < 8 || std::memcmp(b.data(), "RSV1", 4) != 0) return;
    Reader r(b);
    for (int i = 0; i < 4; ++i) r.u8();
    const uint32_t count = r.u32();
    for (uint32_t i = 0; i < count && r.ok(); ++i) {
        SaveEntry e;
        e.id = r.u64();
        e.date = static_cast<int64_t>(r.u64());
        e.level = r.i32();
        e.score = r.str();
        e.lives = r.str();
        const auto thumb = r.bytes();
        if (thumb.size() == size_t(SaveEntry::thumbWidth) * SaveEntry::thumbHeight * 4) {
            e.thumbnail.resize(thumb.size() / 4);
            std::memcpy(e.thumbnail.data(), thumb.data(), thumb.size());   // ARGB w kolejności maszyny
        }
        if (!readSnapshot(r, e.snapshot)) break;
        entries_.push_back(std::move(e));
    }
}

void SaveStore::persist() const {
    Writer w;
    for (char c : std::string("RSV1")) w.u8(static_cast<uint8_t>(c));
    w.u32(static_cast<uint32_t>(entries_.size()));
    for (const auto& e : entries_) {
        w.u64(e.id);
        w.u64(static_cast<uint64_t>(e.date));
        w.i32(e.level);
        w.str(e.score);
        w.str(e.lives);
        std::vector<uint8_t> thumb(e.thumbnail.size() * 4);
        if (!thumb.empty()) std::memcpy(thumb.data(), e.thumbnail.data(), thumb.size());
        w.bytes(thumb);
        writeSnapshot(w, e.snapshot);
    }
    Storage::writeFile(path_, w.out);
}

void SaveStore::add(SaveEntry entry) {
    entries_.insert(entries_.begin(), std::move(entry));
    if (entries_.size() > maxEntries) entries_.resize(maxEntries);
    persist();
}

void SaveStore::remove(uint64_t id) {
    entries_.erase(std::remove_if(entries_.begin(), entries_.end(), [id](const SaveEntry& e) { return e.id == id; }),
                   entries_.end());
    persist();
}

std::vector<uint32_t> makeThumbnail(const std::vector<uint32_t>& px, int width, int height) {
    const int w = SaveEntry::thumbWidth, h = SaveEntry::thumbHeight;
    std::vector<uint32_t> out(w * h);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const int sx = std::min(x * width / w, width - 1);
            const int sy = std::min(y * height / h, height - 1);
            out[y * w + x] = px[sy * width + sx];
        }
    }
    return out;
}
