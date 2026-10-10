//
//  SelfTest.h
//  Robbo (C++ / SDL3)
//
//  Test zgodności silnika z oryginałem: odtwarza tury nagrane z oryginalnego kodu 6502
//  (stan przed turą, liczby losowe, odczyty joysticka) i porównuje wynik ze stanem
//  po turze. Plik nagrań: selftest_orig.bin (format "RBO1", jak w wersji macOS).
//

#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct EngineTables;

namespace SelfTest {

struct Result {
    int passed = 0;
    int total = 0;
    std::string message;   // gotowy opis wyniku
    bool ok() const { return total > 0 && passed == total; }
};

Result run(const EngineTables& tables, const std::vector<uint8_t>& recording);

}  // namespace SelfTest
