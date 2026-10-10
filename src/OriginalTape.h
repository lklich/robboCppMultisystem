//
//  OriginalTape.h
//  Robbo (C++ / SDL3)
//
//  Odczyt danych z kasety oryginalnej gry Robbo (LK Avalon 1989) w formacie .cas.
//
//  Program nie zawiera żadnych danych oryginału. Przy pierwszym uruchomieniu gracz wskazuje
//  plik(i) .cas z obiema stronami kasety, a ten moduł odtwarza z nich obraz pamięci Atari
//  $0000-$BFFF – dokładnie taki, jaki powstaje po wczytaniu gry na prawdziwym sprzęcie.
//
//  Budowa taśmy:
//  • rekordy 600 bodów w standardowym formacie Atari (55 55 + bajt sterujący + 128 bajtów),
//  • rekordy 0-1: blok startowy ładowany pod $0700 (nagłówek: flaga, liczba rekordów, adres),
//  • rekord 2: własny handler urządzenia C: ($0800), który czyta kolejne bajty przez OS
//    i XOR-uje je kluczem z $0720-$081F (indeks klucza = licznik bajtów, liczony od zera),
//  • dalej plik binarny Atari (segmenty: adres początku, adres końca, dane) – strona A
//    do rekordu EOF, strona B to drugi taki plik, czytany tym samym kluczem.
//
//  Wynik jest weryfikowany sumą SHA-256, więc działa każdy wierny zrzut tej kasety.
//

#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

/// Błąd importu z czytelnym komunikatem dla gracza.
class ImportError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct TapeFile {
    std::string name;            // nazwa pliku (do komunikatów)
    std::vector<uint8_t> bytes;  // zawartość
};

namespace OriginalTape {

/// Rozmiar odtwarzanego obrazu pamięci ($0000-$BFFF).
constexpr int imageSize = 0xC000;
/// SHA-256 obrazu pamięci po wczytaniu obu stron oryginalnej kasety.
constexpr const char* expectedSHA256 = "aec526b91b42b70ac7c125e789e5b1e8684b4904fc7042c8093c5fb78ab2827c";

bool isValidImage(const std::vector<uint8_t>& image);

/// Odtwarza obraz pamięci z wybranych plików (.cas – jedna lub obie strony w dowolnej kolejności).
/// Rzuca ImportError.
std::vector<uint8_t> extract(const std::vector<TapeFile>& files);

}  // namespace OriginalTape
