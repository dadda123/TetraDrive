// TetraDrive DSP core: 4 band-pass bands -> per-band waveshaper -> sum/solo; framework independent.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "td_tables.h"

namespace td {

inline constexpr int kBands = 4;
inline constexpr int kSectionsPerBand = 2;  // order-4 Butterworth prototype -> 2 sections
inline constexpr int kMaxChannels = 2;

// 4th-order IIR section, direct form I, double state.
struct Section {
    double b0 = 0, b2 = 0, b4 = 0;       // numerator (b1 = b3 = 0)
    double a1 = 0, a2 = 0, a3 = 0, a4 = 0;  // denominator (a0 = 1)
};

struct SectionState {
    double x1 = 0, x2 = 0, x3 = 0, x4 = 0;
    double y1 = 0, y2 = 0, y3 = 0, y4 = 0;
    void reset() { *this = SectionState{}; }
};

// Butterworth band-pass design: order 4, pre-warped edges, bilinear transform.
inline void designBandpass(double sampleRate, float loHz, float hiHz,
                           std::array<Section, kSectionsPerBand>& out) {
    constexpr double kPi = 3.14159265358979323846;
    constexpr int kOrder = 4;

    if (hiHz < loHz) std::swap(loHz, hiHz);                         // swap reversed edges
    if (sampleRate * 0.5 < static_cast<double>(hiHz))
        hiHz = static_cast<float>(sampleRate * 0.495);               // keep below Nyquist
    if (loHz < 0.0078125f) loHz = 1.0f;                              // avoid zero edge

    const float srf = static_cast<float>(sampleRate);
    const double wLo = std::tan(static_cast<double>(loHz / srf) * kPi);
    const double wHi = std::tan(static_cast<double>(hiHz / srf) * kPi);
    const double B = wHi - wLo;
    const double W0sq = wLo * wHi;

    for (int k = 1; k <= kSectionsPerBand; ++k) {
        const double a1p = -2.0 * std::cos((kOrder - 1 + 2 * k) * kPi / (2.0 * kOrder));

        // analog band-pass: D(s) and N(s) = B^2 s^2
        const double d[5] = {1.0, a1p * B, 2.0 * W0sq + B * B, a1p * B * W0sq, W0sq * W0sq};
        const double n2 = B * B;

        // bilinear s = (z-1)/(z+1)
        auto polyMul = [](const std::array<double, 5>& p, int dp, const double* q, int dq) {
            std::array<double, 5> r{};
            for (int i = 0; i <= dp; ++i)
                for (int j = 0; j <= dq; ++j) r[static_cast<std::size_t>(i + j)] += p[static_cast<std::size_t>(i)] * q[j];
            return r;
        };
        const double zm1[2] = {-1.0, 1.0};  // (z - 1), ascending powers of z
        const double zp1[2] = {1.0, 1.0};   // (z + 1)
        std::array<double, 5> den{};         // ascending powers of z
        for (int kk = 0; kk <= 4; ++kk) {
            std::array<double, 5> t{};
            t[0] = d[4 - kk];  // d index is power of s descending: coefficient of s^kk is d[4-kk]
            int deg = 0;
            for (int i = 0; i < kk; ++i) { t = polyMul(t, deg, zm1, 1); ++deg; }
            for (int i = 0; i < 4 - kk; ++i) { t = polyMul(t, deg, zp1, 1); ++deg; }
            for (int i = 0; i <= 4; ++i) den[static_cast<std::size_t>(i)] += t[static_cast<std::size_t>(i)];
        }
        // numerator n2 * (z^4 - 2 z^2 + 1)
        const double lead = den[4];
        Section s;
        s.b0 = n2 / lead;
        s.b2 = -2.0 * n2 / lead;
        s.b4 = n2 / lead;
        s.a1 = den[3] / lead;
        s.a2 = den[2] / lead;
        s.a3 = den[1] / lead;
        s.a4 = den[0] / lead;
        out[static_cast<std::size_t>(k - 1)] = s;
    }
}

// Runs one section over a block (in-place allowed); float output, double recursion.
inline void runSection(const Section& s, SectionState& st, const float* in, float* out, int n) {
    double x1 = st.x1, x2 = st.x2, x3 = st.x3, x4 = st.x4;
    double y1 = st.y1, y2 = st.y2, y3 = st.y3, y4 = st.y4;
    for (int i = 0; i < n; ++i) {
        const double x = static_cast<double>(in[i]);
        const double y = s.b0 * x + s.b2 * x2 + s.b4 * x4 - s.a1 * y1 - s.a2 * y2 - s.a3 * y3 - s.a4 * y4;
        out[i] = static_cast<float>(y);
        x4 = x3; x3 = x2; x2 = x1; x1 = x;
        y4 = y3; y3 = y2; y2 = y1; y1 = y;
    }
    // flush near-zero output state
    if (y1 < 1e-15 && -1e-15 < y1) {
        y1 = y2 = y3 = y4 = 0.0;
    }
    st.x1 = x1; st.x2 = x2; st.x3 = x3; st.x4 = x4;
    st.y1 = y1; st.y2 = y2; st.y3 = y3; st.y4 = y4;
}

inline float dbToGain(float db) {
    return static_cast<float>(std::pow(10.0, static_cast<double>(db) * 0.05));  // 10^(db/20)
}

// Table lookup: round the index, interpolate toward the next entry.
inline float shapeLookup(const float* table, float x) {
    constexpr float kScale = 254.0f;
    if (x >= 0.0f) {
        if (x <= 1.0f) {
            const float p = x * kScale;
            const int i = static_cast<int>(std::nearbyintf(p));
            return (p - static_cast<float>(i)) * (table[i + 1] - table[i]) + table[i];
        }
        return 1.0f;
    }
    if (-x <= 1.0f) {
        const float p = -x * kScale;
        const int i = static_cast<int>(std::nearbyintf(p));
        return -((p - static_cast<float>(i)) * (table[i + 1] - table[i]) + table[i]);
    }
    return -1.0f;
}

// Linear ramp toward a target; returns the exact target once finished.
struct Ramp {
    float value = 0.0f, target = 0.0f, step = 0.0f;
    int left = 0;

    void snap(float v) { value = target = v; step = 0.0f; left = 0; }
    void setTarget(float t, int len) {
        if (t == target && (left > 0 || value == t)) return;
        target = t;
        if (len <= 0) { snap(t); return; }
        step = (t - value) / static_cast<float>(len);
        left = len;
    }
    bool active() const { return left > 0; }
    float next() {
        if (left > 0) { value += step; if (--left == 0) value = target; }
        return value;
    }
    void advance(int n) {
        if (left <= 0) return;
        if (n >= left) { value = target; left = 0; return; }
        value += step * static_cast<float>(n);
        left -= n;
    }
    // Writes n per-sample values.
    void fill(float* dst, int n) {
        if (left <= 0) { std::fill(dst, dst + n, value); return; }
        for (int i = 0; i < n; ++i) dst[i] = next();
    }
};

class Engine {
public:
    static constexpr int kEdgeUpdateInterval = 16;    // samples between coefficient updates while gliding
    static constexpr double kMinEdgeGapOctaves = 1.0 / 12.0;

    explicit Engine(double sampleRate = 44100.0, int channels = 2) { prepare(sampleRate, channels); }

    void prepare(double sampleRate, int channels = 2) {
        sampleRate_ = sampleRate;
        channels_ = std::clamp(channels, 1, kMaxChannels);
        // default edges: 20 Hz, log-spaced, Nyquist
        edges_[0] = 20.0;
        const double x = sampleRate * 0.025;
        for (int i = 1; i < kBands; ++i)
            edges_[static_cast<std::size_t>(i)] = 20.0 * std::pow(x, static_cast<double>(i) / kBands);
        edges_[kBands] = sampleRate * 0.5;
        for (int i = 0; i <= kBands; ++i) snapEdge(i, edges_[static_cast<std::size_t>(i)]);
        for (int b = 0; b < kBands; ++b) designBand(b, /*viaSetter=*/false, /*clearState=*/true);
        resetState();
        setShape(1);
        snapToTargets();
    }

    // Pre-allocates band and ramp buffers for blocks up to n.
    void reserve(int n) {
        for (auto& band : scratch_)
            for (auto& ch : band)
                if (static_cast<int>(ch.size()) < n) ch.assign(static_cast<std::size_t>(n), 0.0f);
        for (auto& r : rampBuf_)
            if (static_cast<int>(r.size()) < n) r.assign(static_cast<std::size_t>(n), 0.0f);
    }

    void resetState() {
        for (auto& band : state_) for (auto& ch : band) for (auto& s : ch) s.reset();
    }

    // ---- immediate setters (no smoothing) ---------------------------------------------------
    void setBandGainDb(int band, float db) { bandGain_[static_cast<std::size_t>(band)].snap(dbToGain(db)); }
    void setBandDriveDb(int band, float db) { bandDrive_[static_cast<std::size_t>(band)].snap(dbToGain(db)); }
    void setInputGainDb(float db) { inGain_.snap(dbToGain(db)); }
    void setOutputGainDb(float db) { outGain_.snap(dbToGain(db)); }
    void setSolo(int band) { solo_ = (band >= 0 && band < kBands) ? band : -1; snapRoute(); }
    void setBypass(bool b) { bypass_ = b; snapRoute(); }

    void setShape(int shape) {
        if (shape < 0 || shape >= kShapeCount) shape = 0;
        shape_ = prevShape_ = shape;
        compGain_ = prevCompGain_ = shapeMakeup(shape);
        shapeFade_.snap(1.0f);
    }

    // Sets one crossover edge (0..kBands) and redesigns the neighbouring bands.
    void setEdgeHz(int idx, float hz) {
        if (idx < 0 || idx > kBands) return;
        snapEdge(idx, static_cast<double>(hz));
        if (idx < kBands) designBand(idx, true, true);
        if (idx > 0) designBand(idx - 1, true, true);
    }
    void setEdgesFromPreset(const float edges[kBands + 1]) {
        for (int i = 0; i <= kBands; ++i) snapEdge(i, static_cast<double>(edges[i]));
        for (int b = 0; b < kBands; ++b) designBand(b, true, true);
    }

    // ---- smoothed targets (click free; rampSamples < 0 uses the default time) ----------------
    void setBandGainTarget(int band, float db, int rampSamples = -1) {
        bandGain_[static_cast<std::size_t>(band)].setTarget(dbToGain(db), rampLen(rampSamples, kGainRampMs));
    }
    void setBandDriveTarget(int band, float db, int rampSamples = -1) {
        bandDrive_[static_cast<std::size_t>(band)].setTarget(dbToGain(db), rampLen(rampSamples, kGainRampMs));
    }
    void setInputGainTarget(float db, int rampSamples = -1) { inGain_.setTarget(dbToGain(db), rampLen(rampSamples, kGainRampMs)); }
    void setOutputGainTarget(float db, int rampSamples = -1) { outGain_.setTarget(dbToGain(db), rampLen(rampSamples, kGainRampMs)); }

    // Crossfades to a new waveshaper table.
    void setShapeTarget(int shape) {
        if (shape < 0 || shape >= kShapeCount) shape = 0;
        if (shape == shape_) return;
        prevShape_ = shapeFade_.active() ? prevShape_ : shape_;
        prevCompGain_ = shapeMakeup(prevShape_);
        shape_ = shape;
        compGain_ = shapeMakeup(shape);
        shapeFade_.snap(0.0f);
        shapeFade_.setTarget(1.0f, rampLen(-1, kFadeMs));
    }

    // Crossfades between sum, solo and bypass.
    void setRouteTarget(int soloBand, bool bypass) {
        solo_ = (soloBand >= 0 && soloBand < kBands) ? soloBand : -1;
        bypass_ = bypass;
        const int len = rampLen(-1, kFadeMs);
        for (int b = 0; b < kBands; ++b) bandWeight_[static_cast<std::size_t>(b)].setTarget(routeWeight(b), len);
        dryWeight_.setTarget(bypass_ ? 1.0f : 0.0f, len);
    }

    // Glides all edges (Hz) in the log domain; sorted with a minimum gap so bands never invert.
    void setEdgeTargets(const float edges[kBands + 1], int rampSamples = -1) {
        std::array<double, kBands + 1> e{};
        for (int i = 0; i <= kBands; ++i) e[static_cast<std::size_t>(i)] = std::max(1.0, static_cast<double>(edges[i]));
        std::sort(e.begin(), e.end());
        const double gap = std::exp2(kMinEdgeGapOctaves);
        for (int i = 1; i <= kBands; ++i)
            e[static_cast<std::size_t>(i)] = std::max(e[static_cast<std::size_t>(i)], e[static_cast<std::size_t>(i - 1)] * gap);
        const int len = rampLen(rampSamples, kEdgeRampMs);
        for (int i = 0; i <= kBands; ++i) {
            const double hz = e[static_cast<std::size_t>(i)];
            if (hz == edgeTargetHz_[static_cast<std::size_t>(i)]) continue;
            edgeTargetHz_[static_cast<std::size_t>(i)] = hz;
            edgeRamp_[static_cast<std::size_t>(i)].setTarget(static_cast<float>(std::log2(hz)), len);
            if (!edgeRamp_[static_cast<std::size_t>(i)].active()) applyEdge(i);
        }
    }

    // Jumps every smoothed value to its target (for prepare and state loads).
    void snapToTargets() {
        for (auto& r : bandGain_) r.snap(r.target);
        for (auto& r : bandDrive_) r.snap(r.target);
        inGain_.snap(inGain_.target);
        outGain_.snap(outGain_.target);
        shapeFade_.snap(1.0f);
        prevShape_ = shape_;
        prevCompGain_ = compGain_;
        snapRoute();
        bool moved = false;
        for (int i = 0; i <= kBands; ++i) {
            auto& r = edgeRamp_[static_cast<std::size_t>(i)];
            if (r.active()) { r.snap(r.target); moved = true; }
            moved |= applyEdge(i);
        }
        if (moved) for (int b = 0; b < kBands; ++b) designBand(b, true, false);
    }

    // Processes n samples per channel; in-place allowed.
    void process(const float* const* in, float* const* out, int n) {
        if (n <= 0) return;
        if (static_cast<int>(scratch_[0][0].size()) < n || static_cast<int>(rampBuf_[0].size()) < n) reserve(n);

        int pos = 0;
        while (pos < n) {
            const bool gliding = edgesGliding();
            const int len = gliding ? std::min(kEdgeUpdateInterval, n - pos) : n - pos;
            if (gliding) advanceEdges(len);
            const float* ip[kMaxChannels] = {};
            float* op[kMaxChannels] = {};
            for (int c = 0; c < channels_; ++c) { ip[c] = in[c] + pos; op[c] = out[c] + pos; }
            processChunk(ip, op, len);
            pos += len;
        }
    }

    double sampleRate() const { return sampleRate_; }
    int channels() const { return channels_; }
    double edgeHz(int idx) const { return edges_[static_cast<std::size_t>(idx)]; }
    bool edgesGliding() const {
        for (const auto& r : edgeRamp_) if (r.active()) return true;
        return false;
    }

    // Band filter sections for a given edge pair, as the engine applies them (for display).
    static void designBandForDisplay(double sampleRate, double lo, double hi, std::array<Section, kSectionsPerBand>& out) {
        applySetterClamps(sampleRate, lo, hi);
        designBandpass(sampleRate, static_cast<float>(lo), static_cast<float>(hi), out);
    }

private:
    static constexpr double kGainRampMs = 20.0;
    static constexpr double kFadeMs = 10.0;
    static constexpr double kEdgeRampMs = 30.0;
    static constexpr int kMinRamp = 64;           // default-time ramps
    static constexpr int kMinExplicitRamp = 16;   // host automation segments

    enum RampBuf { kBufIn, kBufOut, kBufShape, kBufDry, kBufDrive0, kBufGain0 = kBufDrive0 + kBands,
                   kBufWeight0 = kBufGain0 + kBands, kNumRampBufs = kBufWeight0 + kBands };

    static float shapeMakeup(int shape) { return dbToGain(-kShapeTables[shape][256]); }

    int rampLen(int requested, double defaultMs) const {
        if (requested >= 0) return std::max(requested, kMinExplicitRamp);
        return std::max(kMinRamp, static_cast<int>(sampleRate_ * defaultMs * 0.001));
    }

    float routeWeight(int b) const {
        if (bypass_) return 0.0f;
        return (solo_ < 0 || solo_ == b) ? 1.0f : 0.0f;
    }
    void snapRoute() {
        for (int b = 0; b < kBands; ++b) bandWeight_[static_cast<std::size_t>(b)].snap(routeWeight(b));
        dryWeight_.snap(bypass_ ? 1.0f : 0.0f);
    }

    void snapEdge(int i, double hz) {
        edges_[static_cast<std::size_t>(i)] = hz;
        edgeTargetHz_[static_cast<std::size_t>(i)] = hz;
        edgeRamp_[static_cast<std::size_t>(i)].snap(static_cast<float>(std::log2(std::max(hz, 1e-3))));
    }

    // Copies a ramp's current value into edges_; returns true if it changed.
    bool applyEdge(int i) {
        const auto& r = edgeRamp_[static_cast<std::size_t>(i)];
        const double hz = r.active() ? std::exp2(static_cast<double>(r.value)) : edgeTargetHz_[static_cast<std::size_t>(i)];
        if (hz == edges_[static_cast<std::size_t>(i)]) return false;
        edges_[static_cast<std::size_t>(i)] = hz;
        return true;
    }

    // Moves gliding edges by n samples and redesigns affected bands without clearing state.
    void advanceEdges(int n) {
        std::array<bool, kBands> dirty{};
        for (int i = 0; i <= kBands; ++i) {
            edgeRamp_[static_cast<std::size_t>(i)].advance(n);
            if (applyEdge(i)) {
                if (i < kBands) dirty[static_cast<std::size_t>(i)] = true;
                if (i > 0) dirty[static_cast<std::size_t>(i - 1)] = true;
            }
        }
        for (int b = 0; b < kBands; ++b)
            if (dirty[static_cast<std::size_t>(b)]) designBand(b, true, false);
    }

    void processChunk(const float* const* in, float* const* out, int n) {
        // band split
        for (int b = 0; b < kBands; ++b)
            for (int c = 0; c < channels_; ++c) {
                float* buf = scratch_[static_cast<std::size_t>(b)][static_cast<std::size_t>(c)].data();
                for (int k = 0; k < kSectionsPerBand; ++k) {
                    const float* src = (k == 0) ? in[c] : buf;
                    runSection(sections_[static_cast<std::size_t>(b)][static_cast<std::size_t>(k)],
                               state_[static_cast<std::size_t>(b)][static_cast<std::size_t>(c)][static_cast<std::size_t>(k)],
                               src, buf, n);
                }
            }

        // per-sample gain curves
        float* inG = rampBuf_[kBufIn].data();
        float* outG = rampBuf_[kBufOut].data();
        inGain_.fill(inG, n);
        outGain_.fill(outG, n);
        const bool shapeFading = shapeFade_.active();
        float* mix = rampBuf_[kBufShape].data();
        shapeFade_.fill(mix, n);

        // per-band waveshaper
        const float* table = kShapeTables[shape_];
        const float* prevTable = kShapeTables[prevShape_];
        for (int b = 0; b < kBands; ++b) {
            float* drv = rampBuf_[static_cast<std::size_t>(kBufDrive0 + b)].data();
            float* gn = rampBuf_[static_cast<std::size_t>(kBufGain0 + b)].data();
            bandDrive_[static_cast<std::size_t>(b)].fill(drv, n);
            bandGain_[static_cast<std::size_t>(b)].fill(gn, n);
            for (int c = 0; c < channels_; ++c) {
                float* buf = scratch_[static_cast<std::size_t>(b)][static_cast<std::size_t>(c)].data();
                if (!shapeFading) {
                    for (int i = 0; i < n; ++i) {
                        const float x = buf[i] * inG[i] * drv[i];
                        const float y = shapeLookup(table, x);
                        buf[i] = y * outG[i] * compGain_ * gn[i];
                    }
                } else {
                    for (int i = 0; i < n; ++i) {
                        const float x = buf[i] * inG[i] * drv[i];
                        const float yOld = shapeLookup(prevTable, x) * prevCompGain_;
                        const float yNew = shapeLookup(table, x) * compGain_;
                        buf[i] = (yOld + (yNew - yOld) * mix[i]) * outG[i] * gn[i];
                    }
                }
            }
        }

        // sum, solo or bypass
        bool routeRamping = dryWeight_.active();
        for (const auto& w : bandWeight_) routeRamping |= w.active();
        if (!routeRamping) {
            sumStatic(in, out, n);
            return;
        }
        float* dry = rampBuf_[kBufDry].data();
        dryWeight_.fill(dry, n);
        for (int b = 0; b < kBands; ++b)
            bandWeight_[static_cast<std::size_t>(b)].fill(rampBuf_[static_cast<std::size_t>(kBufWeight0 + b)].data(), n);
        for (int c = 0; c < channels_; ++c) {
            const float* src = in[c];
            float* dst = out[c];
            for (int i = 0; i < n; ++i) {
                float acc = src[i] * dry[i];
                for (int b = 0; b < kBands; ++b)
                    acc += scratch_[static_cast<std::size_t>(b)][static_cast<std::size_t>(c)][static_cast<std::size_t>(i)] *
                           rampBuf_[static_cast<std::size_t>(kBufWeight0 + b)][static_cast<std::size_t>(i)];
                dst[i] = acc;
            }
        }
    }

    // Plain summing when no route crossfade is running.
    void sumStatic(const float* const* in, float* const* out, int n) {
        for (int c = 0; c < channels_; ++c) {
            float* dst = out[c];
            if (bypass_) {
                if (dst != in[c]) std::copy(in[c], in[c] + n, dst);
                continue;
            }
            if (solo_ >= 0) {
                const float* s = scratch_[static_cast<std::size_t>(solo_)][static_cast<std::size_t>(c)].data();
                std::copy(s, s + n, dst);
                continue;
            }
            const float* s0 = scratch_[0][static_cast<std::size_t>(c)].data();
            std::copy(s0, s0 + n, dst);
            for (int b = 1; b < kBands; ++b) {
                const float* s = scratch_[static_cast<std::size_t>(b)][static_cast<std::size_t>(c)].data();
                for (int i = 0; i < n; ++i) dst[i] += s[i];
            }
        }
    }

    // Preset clamps: lo to 0.48 fs then x1.15, hi to 0.49 fs.
    static void applySetterClamps(double sampleRate, double& lo, double& hi) {
        const double srf = static_cast<double>(static_cast<float>(sampleRate));
        const float loLimit = static_cast<float>(srf * 0.48);
        const float hiLimit = static_cast<float>(srf * 0.49);
        if (loLimit < static_cast<float>(lo)) lo = static_cast<double>(loLimit);
        if (hiLimit < static_cast<float>(hi)) hi = static_cast<double>(hiLimit);
        lo = static_cast<double>(static_cast<float>(lo * 1.15));
    }

    // viaSetter applies the preset clamps; clearState resets the band's filter memory.
    void designBand(int band, bool viaSetter, bool clearState) {
        double lo = edges_[static_cast<std::size_t>(band)];
        double hi = edges_[static_cast<std::size_t>(band + 1)];
        if (viaSetter) applySetterClamps(sampleRate_, lo, hi);
        designBandpass(sampleRate_, static_cast<float>(lo), static_cast<float>(hi),
                       sections_[static_cast<std::size_t>(band)]);
        if (clearState)
            for (auto& ch : state_[static_cast<std::size_t>(band)]) for (auto& s : ch) s.reset();
    }

    double sampleRate_ = 44100.0;
    int channels_ = 2;
    std::array<double, kBands + 1> edges_{};
    std::array<double, kBands + 1> edgeTargetHz_{};
    std::array<Ramp, kBands + 1> edgeRamp_{};
    std::array<std::array<Section, kSectionsPerBand>, kBands> sections_{};
    std::array<std::array<std::array<SectionState, kSectionsPerBand>, kMaxChannels>, kBands> state_{};
    std::array<std::array<std::vector<float>, kMaxChannels>, kBands> scratch_{};
    std::array<std::vector<float>, kNumRampBufs> rampBuf_{};
    std::array<Ramp, kBands> bandGain_{{{1.f, 1.f}, {1.f, 1.f}, {1.f, 1.f}, {1.f, 1.f}}};
    std::array<Ramp, kBands> bandDrive_{{{1.f, 1.f}, {1.f, 1.f}, {1.f, 1.f}, {1.f, 1.f}}};
    std::array<Ramp, kBands> bandWeight_{{{1.f, 1.f}, {1.f, 1.f}, {1.f, 1.f}, {1.f, 1.f}}};
    Ramp inGain_{1.0f, 1.0f};
    Ramp outGain_{1.0f, 1.0f};
    Ramp dryWeight_{};
    Ramp shapeFade_{1.0f, 1.0f};
    float compGain_ = 1.0f;
    float prevCompGain_ = 1.0f;
    int shape_ = 1;
    int prevShape_ = 1;
    int solo_ = -1;
    bool bypass_ = false;
};

}  // namespace td
