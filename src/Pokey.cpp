//
//  Pokey.cpp
//  Robbo (C++ / SDL3)
//

#include "Pokey.h"

Pokey::Pokey(const std::vector<uint8_t>& clickBytes, double sampleRate)
    : sampleRate_(sampleRate > 0 ? sampleRate : 44100.0) {
    cyclesPerSample_ = cpuClock / sampleRate_;
    for (auto it = clickBytes.rbegin(); it != clickBytes.rend(); ++it) {
        clickPattern_.push_back((*it & 0x08) != 0);
    }
    poly4_ = makePoly(4, 3, 2);
    poly5_ = makePoly(5, 4, 2);
    poly17_ = makePoly(17, 16, 13);
}

std::vector<bool> Pokey::makePoly(int bits, int tap1, int tap2) {
    const int length = (1 << bits) - 1;
    int reg = (1 << bits) - 1;
    std::vector<bool> result(length, false);
    for (int i = 0; i < length; ++i) {
        result[i] = (reg & 1) != 0;
        const int fb = ((reg >> tap1) & 1) ^ ((reg >> tap2) & 1);
        reg = ((reg << 1) | fb) & ((1 << bits) - 1);
    }
    return result;
}

void Pokey::write(int channel, int f, int c) {
    std::lock_guard<std::mutex> g(lock_);
    audf_[channel] = f & 0xFF;
    audc_[channel] = c & 0xFF;
}

void Pokey::silence() {
    std::lock_guard<std::mutex> g(lock_);
    for (int i = 0; i < 4; ++i) { audf_[i] = 0; audc_[i] = 0; }
}

void Pokey::click() {
    std::lock_guard<std::mutex> g(lock_);
    if (clickQueue_.size() < 4) clickQueue_.push_back(clickPattern_);
}

void Pokey::setMuted(bool m) {
    std::lock_guard<std::mutex> g(lock_);
    muted_ = m;
}

bool Pokey::muted() const {
    std::lock_guard<std::mutex> g(lock_);
    return muted_;
}

void Pokey::render(float* data, int frames) {
    int f[4], c[4];
    bool isMuted;
    {
        std::lock_guard<std::mutex> g(lock_);
        for (int i = 0; i < 4; ++i) { f[i] = audf_[i]; c[i] = audc_[i]; }
        isMuted = muted_;
        if (currentClick_.empty() && !clickQueue_.empty()) {
            currentClick_ = std::move(clickQueue_.front());
            clickQueue_.pop_front();
            clickPos_ = 0;
        }
    }

    double periods[4];
    for (int ch = 0; ch < 4; ++ch) periods[ch] = baseDivider * static_cast<double>(f[ch] + 1);

    const long long n4 = static_cast<long long>(poly4_.size());
    const long long n5 = static_cast<long long>(poly5_.size());
    const long long n17 = static_cast<long long>(poly17_.size());

    for (int i = 0; i < frames; ++i) {
        float mix = 0;
        for (int ch = 0; ch < 4; ++ch) {
            const int ctrl = c[ch];
            const float volume = static_cast<float>(ctrl & 0x0F);
            if (volume == 0) continue;
            if (ctrl & 0x10) {              // tylko głośność
                mix += volume;
                continue;
            }
            phase_[ch] += cyclesPerSample_;
            while (phase_[ch] >= periods[ch]) {
                phase_[ch] -= periods[ch];
                const long long at = static_cast<long long>(cycle_ + cyclesPerSample_ - phase_[ch]);
                if ((ctrl & 0x80) == 0 && !poly5_[at % n5]) continue;
                if (ctrl & 0x20) {
                    out_[ch] = !out_[ch];
                } else if (ctrl & 0x40) {
                    out_[ch] = poly4_[at % n4];
                } else {
                    out_[ch] = poly17_[at % n17];
                }
            }
            if (out_[ch]) mix += volume;
        }
        float sample = mix / 60.0f;
        if (!currentClick_.empty()) {
            const size_t idx = static_cast<size_t>(clickPos_ / 13.0);
            if (idx < currentClick_.size()) {
                sample += currentClick_[idx] ? 0.0f : -0.35f;
                clickPos_ += cyclesPerSample_;
            } else {
                currentClick_.clear();
            }
        }
        cycle_ += cyclesPerSample_;
        if (cycle_ > 1e12) cycle_ = 0;
        // filtr usuwający składową stałą
        const float y = sample - dcIn_ + 0.995f * dcOut_;
        dcIn_ = sample;
        dcOut_ = y;
        data[i] = isMuted ? 0.0f : y * 0.8f * 0.6f;   // 0.6 = głośność miksera z wersji macOS
    }
}
