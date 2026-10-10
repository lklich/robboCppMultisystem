//
//  Storage.h
//  Robbo (C++ / SDL3)
//
//  Pliki i katalog danych użytkownika (przez SDL – ścieżki UTF-8 na każdym systemie).
//    macOS:   ~/Library/Application Support/Robbo/
//    Windows: %APPDATA%\Robbo (folder Roaming)
//    Linux:   ~/.local/share/Robbo/
//

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Storage {

/// Katalog danych gry (z ukośnikiem na końcu).
std::string dataDir();
std::string dataFile(const std::string& name);

bool readFile(const std::string& path, std::vector<uint8_t>& out);
/// Zapis „atomowy”: najpierw plik tymczasowy, potem zamiana.
bool writeFile(const std::string& path, const std::vector<uint8_t>& bytes);
bool removeFile(const std::string& path);
bool exists(const std::string& path);
/// Sama nazwa pliku ze ścieżki.
std::string baseName(const std::string& path);

}  // namespace Storage
