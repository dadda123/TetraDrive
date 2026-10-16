// Half-band polyphase 2x resamplers, cascaded for 2x/4x/8x oversampling; framework independent.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace td {

inline constexpr int kMaxOsStages = 3;                 // 8x
inline constexpr int kMaxOsFactor = 1 << kMaxOsStages;

// Symmetric half-band FIR of length 2L+1 (L odd): h[0] = 0.5, every even offset is zero.
// Only the P = (L+1)/2 positive odd taps are stored, c[i] = h[L - 2i], so the even output
// phase of the upsampler and the centre tap of the downsampler are plain delays.
struct HalfBand {
    static constexpr int kRing = 64;             // >= L + 1
    static constexpr int kMask = kRing - 1;
    static constexpr int kMaxTaps = kRing / 2;

    int L = 0, P = 0, pad = 0;
    std::array<float, kMaxTaps> up{};    // 2 h[L - 2i]
    std::array<float, kMaxTaps> down{};  // h[L - 2i]

    // Kaiser-windowed ideal half-band, odd taps scaled so the DC gain is exactly 1.
    // pad delays the downsampler output by whole samples so that a cascade's total
    // latency is an integer at the base rate.
    void design(int l, double beta, int padSamples) {
        constexpr double kPi = 3.14159265358979323846;
        L = l;
        P = (L + 1) / 2;
        pad = padSamples;
        const double i0b = besselI0(beta);
        std::array<double, kMaxTaps> h{};
        double sum = 0.0;
        for (int i = 0; i < P; ++i) {
            const int k = L - 2 * i;                                   // odd offset, L .. 1
            const double r = static_cast<double>(k) / static_cast<double>(L + 1);
            const double w = besselI0(beta * std::sqrt(std::max(0.0, 1.0 - r * r))) / i0b;
            const double sinc = std::sin(kPi * k * 0.5) / (kPi * k);
            h[static_cast<std::size_t>(i)] = sinc * w;
            sum += h[static_cast<std::size_t>(i)];
        }
        const double norm = 0.25 / sum;                                // 0.5 + 2 * sum(odd) == 1
        for (int i = 0; i < P; ++i) {
            down[static_cast<std::size_t>(i)] = static_cast<float>(h[static_cast<std::size_t>(i)] * norm);
            up[static_cast<std::size_t>(i)] = 2.0f * down[static_cast<std::size_t>(i)];
        }
    }

    // Group delay of one up + down pass, in samples at the stage's input rate.
    int latency() const { return L + pad; }

private:
    static double besselI0(double x) {
        double sum = 1.0, term = 1.0;
        for (int k = 1; k < 64; ++k) {
            const double t = x / (2.0 * k);
            term *= t * t;
            sum += term;
            if (term < 1e-14 * sum) break;
        }
        return sum;
    }
};

struct HalfBandState {
    std::array<float, HalfBand::kRing> x{}, even{}, odd{};
    std::array<float, 4> padBuf{};
    int upPos = 0, downPos = 0, padPos = 0;
    void reset() { *this = HalfBandState{}; }
};

// n input samples -> 2n output samples.
inline void upsample2(const HalfBand& k, HalfBandState& st, const float* in, float* out, int n) {
    constexpr int R = HalfBand::kRing, M = HalfBand::kMask;
    const int L = k.L, P = k.P, half = (L - 1) / 2;
    int pos = st.upPos;
    for (int i = 0; i < n; ++i) {
        pos = (pos + 1) & M;
        st.x[static_cast<std::size_t>(pos)] = in[i];
        float acc = 0.0f;
        for (int j = 0; j < P; ++j)
            acc += k.up[static_cast<std::size_t>(j)] *
                   (st.x[static_cast<std::size_t>((pos - j + R) & M)] + st.x[static_cast<std::size_t>((pos - (L - j) + R) & M)]);
        out[2 * i] = acc;
        out[2 * i + 1] = st.x[static_cast<std::size_t>((pos - half + R) & M)];
    }
    st.upPos = pos;
}

// 2n input samples -> n output samples.
inline void downsample2(const HalfBand& k, HalfBandState& st, const float* in, float* out, int n) {
    constexpr int R = HalfBand::kRing, M = HalfBand::kMask;
    const int L = k.L, P = k.P;
    int pos = st.downPos;
    for (int i = 0; i < n; ++i) {
        pos = (pos + 1) & M;
        st.even[static_cast<std::size_t>(pos)] = in[2 * i];
        st.odd[static_cast<std::size_t>(pos)] = in[2 * i + 1];
        float acc = 0.5f * st.odd[static_cast<std::size_t>((pos - P + R) & M)];
        for (int j = 0; j < P; ++j)
            acc += k.down[static_cast<std::size_t>(j)] *
                   (st.even[static_cast<std::size_t>((pos - j + R) & M)] + st.even[static_cast<std::size_t>((pos - (L - j) + R) & M)]);
        if (k.pad > 0) {
            std::swap(acc, st.padBuf[static_cast<std::size_t>(st.padPos)]);
            if (++st.padPos >= k.pad) st.padPos = 0;
        }
        out[i] = acc;
    }
    st.downPos = pos;
}

// The three cascaded stages. The first runs at the base rate and needs the sharpest
// transition (flat to ~0.45 fs); later stages only guard a narrow audible band and get shorter.
struct OsFilters {
    std::array<HalfBand, kMaxOsStages> stage;

    OsFilters() {
        stage[0].design(47, 8.0, 0);   // 1x -> 2x
        stage[1].design(15, 8.0, 1);   // 2x -> 4x, 15 + 1 = 16 is a whole number of base samples
        stage[2].design(11, 7.0, 1);   // 4x -> 8x, 11 + 1 = 12
    }

    // Total latency in base-rate samples for the given number of active stages.
    int latency(int stages) const {
        int total = 0;
        for (int s = 0; s < std::clamp(stages, 0, kMaxOsStages); ++s) total += stage[static_cast<std::size_t>(s)].latency() >> s;
        return total;
    }
};

}  // namespace td
