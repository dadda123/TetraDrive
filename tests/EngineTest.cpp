// Headless checks for the framework-free engine: resampler latency/transparency and aliasing reduction.
#include <cmath>
#include <cstdio>
#include <vector>

#include "td_dsp.h"
#include "td_params.h"

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

double db(double x) { return 20.0 * std::log10(x + 1e-30); }

// Power at a bin-centred frequency over an integer number of cycles (rectangular window is exact).
double goertzelPower(const std::vector<float>& x, int start, int n, double freq, double sampleRate) {
    const double w = 2.0 * 3.14159265358979323846 * freq / sampleRate;
    const double c = 2.0 * std::cos(w);
    double s0 = 0.0, s1 = 0.0, s2 = 0.0;
    for (int i = 0; i < n; ++i) {
        s0 = static_cast<double>(x[static_cast<std::size_t>(start + i)]) + c * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    const double re = s1 - s2 * std::cos(w), im = s2 * std::sin(w);
    return (re * re + im * im) / (static_cast<double>(n) * n * 0.25);   // amplitude^2 of a sine
}

// Up and down through `stages` with nothing in between: expect a pure delay of latency() samples.
void testResamplerTransparency() {
    std::printf("Resampler transparency\n");
    const td::OsFilters os;
    for (int stages = 1; stages <= td::kMaxOsStages; ++stages) {
        std::vector<td::HalfBandState> st(static_cast<std::size_t>(stages));
        const int n = 256;
        std::vector<float> in(static_cast<std::size_t>(n), 0.0f), out(static_cast<std::size_t>(n), 0.0f);
        std::vector<float> a(static_cast<std::size_t>(n << stages)), b(static_cast<std::size_t>(n << stages));
        in[0] = 1.0f;

        float* cur = a.data();
        float* other = b.data();
        int len = n;
        td::upsample2(os.stage[0], st[0], in.data(), cur, len);
        len *= 2;
        for (int s = 1; s < stages; ++s) {
            td::upsample2(os.stage[static_cast<std::size_t>(s)], st[static_cast<std::size_t>(s)], cur, other, len);
            std::swap(cur, other);
            len *= 2;
        }
        for (int s = stages - 1; s >= 1; --s) {
            len /= 2;
            td::downsample2(os.stage[static_cast<std::size_t>(s)], st[static_cast<std::size_t>(s)], cur, other, len);
            std::swap(cur, other);
        }
        td::downsample2(os.stage[0], st[0], cur, out.data(), n);

        int peak = 0;
        double sum = 0.0, leak = 0.0;
        for (int i = 0; i < n; ++i) {
            sum += out[static_cast<std::size_t>(i)];
            if (std::fabs(out[static_cast<std::size_t>(i)]) > std::fabs(out[static_cast<std::size_t>(peak)])) peak = i;
        }
        for (int i = 0; i < n; ++i) if (i != peak) leak = std::max(leak, static_cast<double>(std::fabs(out[static_cast<std::size_t>(i)])));
        const int expected = os.latency(stages);
        std::printf("  %dx: latency %d (reported %d), peak %.4f, DC gain %.6f, worst other sample %.1f dB\n",
                    1 << stages, peak, expected, static_cast<double>(out[static_cast<std::size_t>(peak)]), sum, db(leak));
        check(peak == expected, "impulse arrives at the reported latency");
        check(std::fabs(sum - 1.0) < 1e-3, "DC gain is unity");
        // An impulse carries energy right up to Nyquist, which any 2x stage must remove; the peak
        // therefore sits a little under 1 and the rest is band-edge ringing, so bounds are loose.
        check(out[static_cast<std::size_t>(peak)] > 0.95f && leak < 0.2, "passband impulse shape preserved");
    }
}

// Full engine at heavy drive: a 5 kHz sine folds its upper harmonics onto non-harmonic bins at
// 44.1 kHz; oversampling must push those down while leaving the fundamental and 3rd harmonic alone.
void testAliasing() {
    std::printf("Aliasing at +20 dB input, +24 dB drive\n");
    const double sr = 44100.0;
    const int warm = 22050, n = 44100, total = warm + n;
    const double f0 = 5000.0;
    const double aliasBins[] = {19100.0, 9100.0, 900.0, 10900.0, 20900.0};   // 25k, 35k, 45k, 55k, 65k folded

    std::vector<float> in(static_cast<std::size_t>(total));
    for (int i = 0; i < total; ++i) in[static_cast<std::size_t>(i)] = 0.5f * static_cast<float>(std::sin(2.0 * 3.14159265358979323846 * f0 * i / sr));

    double fundamental[td::kMaxOsStages + 1] = {}, third[td::kMaxOsStages + 1] = {}, alias[td::kMaxOsStages + 1] = {};
    for (int stages = 0; stages <= td::kMaxOsStages; ++stages) {
        td::Engine engine(sr, 1);
        engine.reserve(512);
        engine.setInputGainDb(20.0f);
        for (int b = 0; b < td::kBands; ++b) engine.setBandDriveDb(b, 24.0f);
        engine.setOversampling(stages);
        engine.snapToTargets();

        std::vector<float> out(static_cast<std::size_t>(total));
        for (int pos = 0; pos < total; pos += 512) {
            const int len = std::min(512, total - pos);
            const float* ip[1] = {in.data() + pos};
            float* op[1] = {out.data() + pos};
            engine.process(ip, op, len);
        }
        fundamental[stages] = goertzelPower(out, warm, n, f0, sr);
        third[stages] = goertzelPower(out, warm, n, 3.0 * f0, sr);
        for (double f : aliasBins) alias[stages] += goertzelPower(out, warm, n, f, sr);
        std::printf("  %s: fundamental %.1f dB, 3rd %.1f dB, alias sum %.1f dB (%.1f dB below fundamental)\n",
                    td::kOversampleNames[static_cast<std::size_t>(stages)], 10.0 * std::log10(fundamental[stages]), 10.0 * std::log10(third[stages]),
                    10.0 * std::log10(alias[stages] + 1e-30), 10.0 * std::log10(fundamental[stages] / (alias[stages] + 1e-30)));
    }
    const double off = 10.0 * std::log10(alias[0] + 1e-30);
    check(off > -40.0 + 10.0 * std::log10(fundamental[0]), "test signal actually aliases without oversampling");
    for (int s = 1; s <= td::kMaxOsStages; ++s) {
        const double gain = off - 10.0 * std::log10(alias[static_cast<std::size_t>(s)] + 1e-30);
        char msg[96];
        std::snprintf(msg, sizeof msg, "%dx lowers aliasing by %.1f dB (>= 20 dB)", 1 << s, gain);
        check(gain >= 20.0, msg);
        check(std::fabs(10.0 * std::log10(fundamental[s] / fundamental[0])) < 1.0, "fundamental level unchanged");
        check(std::fabs(10.0 * std::log10(third[s] / third[0])) < 1.5, "3rd harmonic level unchanged");
    }
}

// Low level, drive 0: the shaper is almost linear there, so oversampled output must equal the
// plain output delayed by the reported latency.
void testEngineLatency() {
    std::printf("Engine latency at low level\n");
    const double sr = 48000.0;
    const int total = 8192;
    std::vector<float> in(static_cast<std::size_t>(total));
    for (int i = 0; i < total; ++i) in[static_cast<std::size_t>(i)] = 0.01f * static_cast<float>(std::sin(2.0 * 3.14159265358979323846 * 1000.0 * i / sr));

    auto run = [&](int stages) {
        td::Engine engine(sr, 1);
        engine.reserve(256);
        engine.setOversampling(stages);
        engine.snapToTargets();
        std::vector<float> out(static_cast<std::size_t>(total));
        for (int pos = 0; pos < total; pos += 256) {
            const float* ip[1] = {in.data() + pos};
            float* op[1] = {out.data() + pos};
            engine.process(ip, op, 256);
        }
        return out;
    };
    const auto ref = run(0);
    for (int stages = 1; stages <= td::kMaxOsStages; ++stages) {
        td::Engine probe(sr, 1);
        probe.setOversampling(stages);
        const int lat = probe.latencySamples();
        const auto out = run(stages);
        double err = 0.0, sig = 0.0;
        for (int i = 4096; i + lat < total; ++i) {
            const double d = static_cast<double>(out[static_cast<std::size_t>(i + lat)]) - ref[static_cast<std::size_t>(i)];
            err += d * d;
            sig += static_cast<double>(ref[static_cast<std::size_t>(i)]) * ref[static_cast<std::size_t>(i)];
        }
        const double rel = 10.0 * std::log10(err / (sig + 1e-30) + 1e-30);
        char msg[96];
        std::snprintf(msg, sizeof msg, "%dx matches the plain path shifted by %d samples (%.1f dB error)", 1 << stages, lat, rel);
        check(rel < -50.0, msg);
    }
}

// Saturation heat: zero-ish for a quiet band, rising monotonically with drive, same with and
// without oversampling, and independent of the band level (which is applied after the shaper).
void testHeat() {
    std::printf("Saturation heat (distortion energy / linear energy)\n");
    const double sr = 48000.0;
    const int n = 4800;
    std::vector<float> in(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) in[static_cast<std::size_t>(i)] = 0.25f * static_cast<float>(std::sin(2.0 * 3.14159265358979323846 * 1000.0 * i / sr));

    auto heatFor = [&](float driveDb, int stages, float levelDb, int shape) {
        td::Engine engine(sr, 1);
        engine.reserve(480);
        engine.setShape(shape);
        for (int b = 0; b < td::kBands; ++b) { engine.setBandDriveDb(b, driveDb); engine.setBandGainDb(b, levelDb); }
        engine.setOversampling(stages);
        engine.snapToTargets();
        std::vector<float> out(static_cast<std::size_t>(n));
        float heat = 0.0f;
        for (int pos = 0; pos < n; pos += 480) {
            const float* ip[1] = {in.data() + pos};
            float* op[1] = {out.data() + pos};
            engine.process(ip, op, 480);
            if (pos >= n / 2) heat = std::max(heat, engine.takeMeters().heat[2]);   // 1 kHz lives in band 3
            else engine.takeMeters();
        }
        return heat;
    };

    float prev = -1.0f;
    bool monotonic = true;
    for (float drive : {-24.0f, -12.0f, 0.0f, 6.0f, 12.0f, 24.0f}) {
        const float h = heatFor(drive, 0, 0.0f, 0);
        std::printf("  shape 1, drive %+5.1f dB: heat %.5f (%.1f dB)\n", drive, h, db(std::sqrt(h)));
        monotonic &= h >= prev;
        prev = h;
    }
    check(monotonic, "heat rises with drive");
    check(heatFor(-24.0f, 0, 0.0f, 0) < 0.001f, "quiet band reads below -30 dB");
    check(heatFor(24.0f, 0, 0.0f, 0) > 0.3f, "hard-driven band reads above -5 dB");
    const float a = heatFor(6.0f, 0, 0.0f, 0), b = heatFor(6.0f, 2, 0.0f, 0), c = heatFor(6.0f, 0, -20.0f, 0);
    check(std::fabs(10.0 * std::log10(b / a)) < 1.0, "same reading with 4x oversampling");
    check(std::fabs(10.0 * std::log10(c / a)) < 0.01, "band level does not affect the reading");
    for (int shape = 1; shape < td::kShapeCount; ++shape)
        std::printf("  shape %d, drive 0 dB: heat %.5f (%.1f dB)\n", shape + 1, heatFor(0.0f, 0, 0.0f, shape), db(std::sqrt(heatFor(0.0f, 0, 0.0f, shape))));
}

// Flat crossover: the four bands sum to the low cut x high cut alone (all-pass otherwise),
// neighbours cross at -6 dB, and the engine's time-domain path matches the analytic design.
void testFlatCrossover() {
    std::printf("Flat (Linkwitz-Riley) crossover\n");
    const double sr = 48000.0;
    // a low cut right under crossover 1 and a real high cut: the cuts must shape the sum, nothing else
    const double edges[td::kBands + 1] = {90.0, 136.0, 742.0, 4050.0, 9000.0};
    std::array<td::BandFilter, td::kBands> flat{}, classic{};
    for (int b = 0; b < td::kBands; ++b) {
        td::Engine::designBandForDisplay(td::Crossover::Flat, sr, edges, b, flat[static_cast<std::size_t>(b)]);
        td::Engine::designBandForDisplay(td::Crossover::Classic, sr, edges, b, classic[static_cast<std::size_t>(b)]);
    }
    td::BandFilter reference{};
    td::designFlatCuts(sr, edges, reference);

    auto zAt = [&](double hz) { return std::exp(std::complex<double>(0.0, -2.0 * 3.14159265358979323846 * hz / sr)); };
    auto sumAt = [&](const std::array<td::BandFilter, td::kBands>& bank, double hz) {
        std::complex<double> h = 0.0;
        for (const auto& f : bank) h += f.response(zAt(hz));
        return std::abs(h);
    };
    auto cutsAt = [&](double hz) { return std::abs(reference.response(zAt(hz))); };

    double worstFlat = 0.0, worstClassic = 0.0;
    for (double hz = 10.0; hz < 23000.0; hz *= 1.02) {
        worstFlat = std::max(worstFlat, std::fabs(db(sumAt(flat, hz) / cutsAt(hz))));
        worstClassic = std::max(worstClassic, std::fabs(db(sumAt(classic, hz) / cutsAt(hz))));
    }
    std::printf("  summed response deviation from the cuts alone: flat %.4f dB, classic %.2f dB\n", worstFlat, worstClassic);
    check(worstFlat < 0.01, "flat bands sum to the low and high cut alone");
    check(worstClassic > 0.5, "classic bands show ripple (so the mode is meaningful)");

    for (int x = 1; x < td::kBands; ++x) {
        const double lo = db(std::abs(flat[static_cast<std::size_t>(x - 1)].response(zAt(edges[x]))) / cutsAt(edges[x]));
        const double hi = db(std::abs(flat[static_cast<std::size_t>(x)].response(zAt(edges[x]))) / cutsAt(edges[x]));
        std::printf("  crossover %d at %.0f Hz: %.2f dB / %.2f dB\n", x, edges[x], lo, hi);
        check(std::fabs(lo + 6.02) < 0.1 && std::fabs(hi + 6.02) < 0.1, "bands meet at -6 dB");
    }

    // time domain: low drive keeps the shaper linear, so output level tracks the analytic sum
    td::Engine engine(sr, 1);
    engine.reserve(480);
    engine.setShape(0);
    float preset[td::kBands + 1];
    for (int i = 0; i <= td::kBands; ++i) preset[i] = static_cast<float>(edges[i]);
    engine.setEdgesFromPreset(preset);
    for (int b = 0; b < td::kBands; ++b) engine.setBandDriveDb(b, -40.0f);
    engine.setCrossover(td::Crossover::Flat);
    engine.snapToTargets();
    const int n = 48000;
    std::vector<float> in(static_cast<std::size_t>(n)), out(static_cast<std::size_t>(n));
    double worstMatch = 0.0;
    double refGain = 0.0;
    for (double hz : {60.0, 136.0, 300.0, 742.0, 2000.0, 4050.0, 9000.0, 14000.0}) {
        for (int i = 0; i < n; ++i) in[static_cast<std::size_t>(i)] = 0.1f * static_cast<float>(std::sin(2.0 * 3.14159265358979323846 * hz * i / sr));
        engine.resetState();
        for (int pos = 0; pos < n; pos += 480) {
            const float* ip[1] = {in.data() + pos};
            float* op[1] = {out.data() + pos};
            engine.process(ip, op, 480);
        }
        double num = 0.0, den = 0.0;
        for (int i = n / 2; i < n; ++i) {
            num += static_cast<double>(out[static_cast<std::size_t>(i)]) * out[static_cast<std::size_t>(i)];
            den += static_cast<double>(in[static_cast<std::size_t>(i)]) * in[static_cast<std::size_t>(i)];
        }
        const double measured = std::sqrt(num / den) / sumAt(flat, hz);   // shaper slope, constant across hz
        if (refGain == 0.0) refGain = measured;
        worstMatch = std::max(worstMatch, std::fabs(db(measured / refGain)));
    }
    std::printf("  engine output vs analytic sum: %.3f dB spread over 60 Hz - 14 kHz\n", worstMatch);
    check(worstMatch < 0.05, "engine matches the analytic flat design");
}

}  // namespace

int main() {
    testResamplerTransparency();
    testAliasing();
    testEngineLatency();
    testHeat();
    testFlatCrossover();
    std::printf("%s\n", failures == 0 ? "All engine tests passed." : "Engine tests FAILED.");
    return failures == 0 ? 0 : 1;
}
