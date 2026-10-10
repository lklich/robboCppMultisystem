//
//  Sha256.h
//  Robbo (C++ / SDL3)
//
//  Minimalna implementacja SHA-256 (FIPS 180-4) do weryfikacji danych z kasety.
//

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

/// Skrót SHA-256 jako 64 znaki szesnastkowe (małe litery).
std::string sha256Hex(const uint8_t* data, size_t size);
