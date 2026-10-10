//
//  Pokey.h
//  Robbo (C++ / SDL3)
//
//  Emulacja układu dźwiękowego POKEY (4 kanały, zegar 64 kHz, liczniki wielomianowe)
//  oraz „głośnika” CONSOL, którym gra klika przy krokach Robbo.
//  Klasa tylko generuje próbki – odtwarzaniem zajmuje się warstwa SDL (wywołuje render()
//  z wątku audio). Rejestry są chronione muteksem.
//

#pragma once

#include <cstdint>
#include <deque>
#include <mutex>
#include <vector>

class Pokey {
public:
    static constexpr double cpuClock = 1773447.0;   // PAL
    static constexpr double baseDivider = 28.0;     // zegar 64 kHz = CPU / 28

    Pokey(const std::vector<uint8_t>& clickBytes, double sampleRate);

    // Interfejs dla gry (wątek główny)
    void write(int channel, int audf, int audc);
    void silence();
    void click();
    void setMuted(bool m);
    bool muted() const;

    /// Synteza `frames` próbek mono float (wątek audio).
    void render(float* out, int frames);

private:
    static std::vector<bool> makePoly(int bits, int tap1, int tap2);

    mutable std::mutex lock_;
    // rejestry ustawiane z wątku gry
    int audf_[4] = {0, 0, 0, 0};
    int audc_[4] = {0, 0, 0, 0};
    std::deque<std::vector<bool>> clickQueue_;
    bool muted_ = false;

    // stan wątku audio
    double sampleRate_;
    double cyclesPerSample_;
    double cycle_ = 0;
    double phase_[4] = {0, 0, 0, 0};
    bool out_[4] = {false, false, false, false};
    std::vector<bool> poly4_, poly5_, poly17_;
    float dcIn_ = 0, dcOut_ = 0;
    std::vector<bool> currentClick_;
    double clickPos_ = 0;

    /// Bity głośnika (bit 3 bajtów $1E50…$1E00, zapisywanych co 13 cykli).
    std::vector<bool> clickPattern_;
};
