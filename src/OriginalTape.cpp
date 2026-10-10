//
//  OriginalTape.cpp
//  Robbo (C++ / SDL3)
//

#include "OriginalTape.h"

#include "Sha256.h"

#include <algorithm>
#include <cstring>

namespace {

struct Record {
    uint8_t control = 0;           // $FC pełny, $FA niepełny, $FE koniec pliku
    std::vector<uint8_t> payload;  // 128 bajtów danych
};

/// Rekordy danych z pliku .cas (bloki „data”, każdy z rekordem 55 55 + sterujący + 128 + suma).
bool recordsFromCAS(const std::vector<uint8_t>& bytes, std::vector<Record>& out) {
    if (bytes.size() < 8 || std::memcmp(bytes.data(), "FUJI", 4) != 0) return false;
    size_t p = 0;
    while (p + 8 <= bytes.size()) {
        const size_t length = size_t(bytes[p + 4]) | (size_t(bytes[p + 5]) << 8);
        const size_t start = p + 8, end = start + length;
        if (end > bytes.size()) break;
        if (std::memcmp(bytes.data() + p, "data", 4) == 0 && length >= 131 && bytes[start] == 0x55 &&
            bytes[start + 1] == 0x55) {
            Record r;
            r.control = bytes[start + 2];
            r.payload.assign(bytes.begin() + start + 3, bytes.begin() + start + 131);
            out.push_back(std::move(r));
        }
        p = end;
    }
    return true;
}

/// Blok startowy kasety: liczba rekordów 2, ładowanie pod $0700.
bool isBoot(const std::vector<Record>& tape) {
    if (tape.empty()) return false;
    const Record& r = tape.front();
    if (r.control != 0xFC) return false;
    return r.payload[1] == 0x02 && r.payload[2] == 0x00 && r.payload[3] == 0x07;
}

/// Segmenty: [$FFFF] start(lo,hi) koniec(lo,hi) dane… – nagłówek $FFFF jest opcjonalny.
void applySegments(const std::vector<uint8_t>& d, std::vector<uint8_t>& memory) {
    size_t p = 0;
    while (p + 4 <= d.size()) {
        if (d[p] == 0xFF && d[p + 1] == 0xFF) { p += 2; continue; }
        const size_t start = size_t(d[p]) | (size_t(d[p + 1]) << 8);
        const size_t end = size_t(d[p + 2]) | (size_t(d[p + 3]) << 8);
        p += 4;
        if (end < start) return;
        const size_t count = end - start + 1;
        if (p + count > d.size()) return;
        std::copy_n(d.begin() + p, count, memory.begin() + start);
        p += count;
    }
}

}  // namespace

namespace OriginalTape {

bool isValidImage(const std::vector<uint8_t>& image) {
    return image.size() == static_cast<size_t>(imageSize) &&
           sha256Hex(image.data(), image.size()) == expectedSHA256;
}

std::vector<uint8_t> extract(const std::vector<TapeFile>& files) {
    std::vector<std::vector<Record>> tapes;
    for (const auto& f : files) {
        // Gotowy obraz pamięci (np. wcześniej zapisany) – przyjmujemy, jeśli suma się zgadza.
        if (isValidImage(f.bytes)) return f.bytes;
        std::vector<Record> records;
        if (!recordsFromCAS(f.bytes, records)) {
            throw ImportError("Plik „" + f.name + "” nie jest obrazem kasety Atari (.cas).");
        }
        tapes.push_back(std::move(records));
    }
    // Strona z blokiem startowym idzie pierwsza, reszta w kolejności wyboru.
    std::stable_sort(tapes.begin(), tapes.end(),
                     [](const auto& a, const auto& b) { return isBoot(a) && !isBoot(b); });
    std::vector<Record> all;
    for (auto& t : tapes) all.insert(all.end(), t.begin(), t.end());

    const char* noBoot = "Nie znaleziono bloku startowego – wskaż stronę A kasety Robbo.";
    if (all.empty() || !isBoot(all)) throw ImportError(noBoot);
    const size_t bootCount = all.front().payload[1];
    if (bootCount < 1 || all.size() <= bootCount + 1) throw ImportError(noBoot);

    // Pamięć $0700-$087F: rekordy startowe i rekord z handlerem; klucz to $0720-$081F.
    std::vector<uint8_t> low;
    for (size_t i = 0; i <= bootCount; ++i) low.insert(low.end(), all[i].payload.begin(), all[i].payload.end());
    if (low.size() < 0x120) throw ImportError(noBoot);
    const std::vector<uint8_t> key(low.begin() + 0x20, low.begin() + 0x120);

    // Kolejne pliki na taśmie (rozdzielone rekordami EOF), zaczynając za handlerem.
    std::vector<std::vector<uint8_t>> streams;
    std::vector<uint8_t> current;
    for (size_t i = bootCount + 1; i < all.size(); ++i) {
        const Record& r = all[i];
        switch (r.control) {
        case 0xFC:
            current.insert(current.end(), r.payload.begin(), r.payload.end());
            break;
        case 0xFA: {
            const size_t n = std::min<size_t>(r.payload[127], 127);
            current.insert(current.end(), r.payload.begin(), r.payload.begin() + n);
            break;
        }
        case 0xFE:
            if (!current.empty()) streams.push_back(current);
            current.clear();
            break;
        default:
            break;
        }
    }
    if (!current.empty()) streams.push_back(current);

    std::vector<uint8_t> memory(0x10000, 0);
    for (auto stream : streams) {
        for (size_t i = 0; i < stream.size(); ++i) stream[i] ^= key[i & 0xFF];
        applySegments(stream, memory);
    }

    std::vector<uint8_t> image(memory.begin(), memory.begin() + imageSize);
    if (!isValidImage(image)) {
        if (streams.size() < 2) {
            throw ImportError("Odczytano tylko stronę A. Wskaż oba pliki naraz (strona A i strona B) "
                              "albo plik zawierający obie strony.");
        }
        throw ImportError("To nie jest oryginalna kaseta Robbo (LK Avalon) albo plik jest uszkodzony – "
                          "dane się nie zgadzają.");
    }
    return image;
}

}  // namespace OriginalTape
