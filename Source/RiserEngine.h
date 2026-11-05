// RiserEngine — turns a captured vocal into a reverse-reverb riser.
// Pure C++ (no JUCE dependency) so it can be tested offline.
//
// Method: riser = reverse( reverb( reverse(vocal) ) ), keeping only the part
// that comes *before* the vocal starts. The swell therefore grows into the
// first word and ends exactly where the vocal begins.
#pragma once
#include <vector>
#include <complex>
#include <cmath>
#include <random>
#include <algorithm>
#include <cstdint>

namespace riser
{
struct Settings
{
    double sampleRate   = 48000.0;
    double bpm          = 120.0;
    double lengthBars   = 1.0;   // 0.5, 1, 2, 4 (4/4 time)
    float  size         = 0.6f;  // 0..1  decay length / density of the tail
    float  tone         = 0.6f;  // 0..1  dark .. bright
    float  width        = 0.8f;  // 0..1  mono .. wide
    int    stutterMode  = 1;     // 0 = smooth (tremolo), 1 = gate (hard chops), 2 = repeat (slice repeat)
    double tremoloDiv   = 16.0;  // 4 = 1/4 notes, 8 = 1/8, 16 = 1/16, 32 = 1/32, 0 = build (1/4 -> 1/32)
    float  tremoloDepth = 0.0f;  // 0..1
    float  outputDb     = 0.0f;  // -24..+6, applied after normalising to -1 dBFS

    // Automatic sibilance detection: tames "s" / "t" sounds before they hit the reverb
    bool deEss = true;

    // One-click creative effects
    bool shimmer = false, darkOctave = false, pitchRise = false, filterSweep = false,
         lofi = false, radio = false, delay = false, tape = false;
};

struct Stereo
{
    std::vector<float> L, R;
    size_t size() const { return L.size(); }
};

namespace detail
{
using cd = std::complex<double>;

inline void fft (std::vector<cd>& a, bool inverse)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap (a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = 2.0 * M_PI / (double) len * (inverse ? 1.0 : -1.0);
        const cd wlen (std::cos (ang), std::sin (ang));
        for (size_t i = 0; i < n; i += len)
        {
            cd w (1.0);
            for (size_t k = 0; k < len / 2; ++k)
            {
                const cd u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
    if (inverse)
        for (auto& x : a) x /= (double) n;
}

// Convolve one real signal with two real IRs at once (L in real part, R in imag part).
inline void convolveStereo (const std::vector<float>& x,
                            const std::vector<float>& irL, const std::vector<float>& irR,
                            std::vector<float>& outL, std::vector<float>& outR)
{
    const size_t outLen = x.size() + irL.size() - 1;
    size_t n = 1;
    while (n < outLen) n <<= 1;

    std::vector<cd> X (n), H (n);
    for (size_t i = 0; i < x.size(); ++i) X[i] = x[i];
    for (size_t i = 0; i < irL.size(); ++i) H[i] = cd (irL[i], irR[i]);
    fft (X, false);
    fft (H, false);
    for (size_t i = 0; i < n; ++i) X[i] *= H[i];   // x is real, so real/imag stay separated
    fft (X, true);

    outL.resize (outLen);
    outR.resize (outLen);
    for (size_t i = 0; i < outLen; ++i)
    {
        outL[i] = (float) X[i].real();
        outR[i] = (float) X[i].imag();
    }
}

// Decorrelated, exponentially decaying noise IR with frequency-dependent damping.
inline void makeImpulse (const Settings& s, size_t len, std::vector<float>& irL, std::vector<float>& irR)
{
    const double sr    = s.sampleRate;
    const double lenS  = (double) len / sr;
    const double rt60  = lenS * (0.6 + 1.4 * s.size);               // bigger = tail still loud at the start of the riser
    const double startHz = 1800.0 * std::pow (10.0, 1.0 * s.tone);   // 1.8 kHz .. 18 kHz
    const double endHz   = startHz * (0.25 + 0.35 * s.tone);         // tail darkens as it decays

    irL.assign (len, 0.0f);
    irR.assign (len, 0.0f);
    std::mt19937 rng (1234);
    std::normal_distribution<float> nd (0.0f, 1.0f);

    double lpL = 0, lpR = 0, lp2L = 0, lp2R = 0;
    const double predelay = 0.012 * sr;          // short gap keeps the transient of the word clear
    const double attack   = 0.035 * sr * (0.5 + s.size);  // diffuse build-up
    for (size_t i = 0; i < len; ++i)
    {
        const double t = (double) i / sr;
        const double frac = (double) i / (double) len;
        const double fc = startHz * std::pow (endHz / startHz, frac);
        const double a = std::exp (-2.0 * M_PI * std::min (fc, 0.45 * sr) / sr);

        double nL = nd (rng), nR = nd (rng);
        lpL = (1 - a) * nL + a * lpL;  lp2L = (1 - a) * lpL + a * lp2L;
        lpR = (1 - a) * nR + a * lpR;  lp2R = (1 - a) * lpR + a * lp2R;

        double env = std::exp (-6.907755 * t / rt60);
        const double di = (double) i - predelay;
        env *= di <= 0 ? 0.0 : (di < attack ? 0.5 - 0.5 * std::cos (M_PI * di / attack) : 1.0);

        irL[i] = (float) (lp2L * env);
        irR[i] = (float) (lp2R * env);
    }
}

inline void highpass (std::vector<float>& x, double hz, double sr)
{
    // 2nd-order Butterworth high-pass (RBJ)
    const double w0 = 2 * M_PI * hz / sr, c = std::cos (w0), al = std::sin (w0) / (2 * 0.7071);
    const double b0 = (1 + c) / 2, b1 = -(1 + c), b2 = (1 + c) / 2, a0 = 1 + al, a1 = -2 * c, a2 = 1 - al;
    double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    for (auto& v : x)
    {
        const double y = (b0 * v + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2) / a0;
        x2 = x1; x1 = v; y2 = y1; y1 = y; v = (float) y;
    }
}

// Grid steps counted back from the end of the riser. In build mode the rate speeds up
// towards the vocal: 1/4 -> 1/8 -> 1/16 -> 1/32 over four equal parts of the riser.
struct Step { size_t start, len; size_t beat; };

inline std::vector<Step> makeGrid (const Settings& s, size_t L)
{
    const double beatLen = 60.0 / s.bpm * s.sampleRate;
    std::vector<Step> steps;
    double t = (double) L;
    while (t > 0.5)
    {
        double div = s.tremoloDiv;
        if (div <= 0.0)
        {
            const double posFromStart = t / (double) L;   // 0 at start .. 1 at the vocal (t = end of this step)
            div = posFromStart < 0.25 ? 4.0 : posFromStart < 0.5 ? 8.0 : posFromStart < 0.75 ? 16.0 : 32.0;
        }
        const double period = (4.0 / div) * beatLen;
        const double start = std::max (0.0, t - period);
        const size_t beat = (size_t) std::floor (((double) L - t + 0.5) / beatLen);   // beat index from the end
        steps.push_back ({ (size_t) std::llround (start), (size_t) std::llround (t - start), beat });
        t = start;
    }
    std::reverse (steps.begin(), steps.end());
    return steps;
}

inline void applyStutter (Stereo& out, const Settings& s)
{
    const size_t L = out.size();
    const float d = s.tremoloDepth;
    const auto steps = makeGrid (s, L);
    const size_t ramp = (size_t) (0.002 * s.sampleRate);   // 2 ms anti-click

    if (s.stutterMode == 0)   // smooth tremolo
    {
        for (const auto& st : steps)
            for (size_t k = 0; k < st.len && st.start + k < L; ++k)
            {
                double g = 0.5 + 0.5 * std::cos (2 * M_PI * (double) k / (double) st.len);
                g = std::pow (g, 0.6);
                const float gain = (float) (1.0 - d * (1.0 - g));
                out.L[st.start + k] *= gain; out.R[st.start + k] *= gain;
            }
        return;
    }

    if (s.stutterMode == 1)   // gate: on for the first 55 % of every step, then chopped
    {
        for (const auto& st : steps)
        {
            const size_t on = std::max<size_t> (ramp * 2, (size_t) (st.len * 0.55));
            for (size_t k = 0; k < st.len && st.start + k < L; ++k)
            {
                double g;
                if (k < ramp) g = (double) k / ramp;
                else if (k < on) g = 1.0;
                else if (k < on + ramp) g = 1.0 - (double) (k - on) / ramp;
                else g = 0.0;
                const float gain = (float) (1.0 - d * (1.0 - g));
                out.L[st.start + k] *= gain; out.R[st.start + k] *= gain;
            }
        }
        return;
    }

    // repeat: every step replays the first step of its beat (stu-stu-stu-stutter)
    const Stereo src = out;
    size_t i = 0;
    while (i < steps.size())
    {
        const Step& first = steps[i];
        size_t j = i;
        while (j < steps.size() && steps[j].beat == first.beat)
        {
            const Step& st = steps[j];
            const size_t n = (size_t) (0.8 * (double) std::min (st.len, first.len));   // small gap articulates each repeat
            for (size_t k = 0; k < st.len && st.start + k < L; ++k)
            {
                // short fades at each slice so the repeats click rhythmically, not digitally
                double env = 1.0;
                if (k < ramp) env = (double) k / ramp;
                else if (k + ramp > n) env = k < n ? (double) (n - k) / ramp : 0.0;
                const size_t from = first.start + std::min (k, n - 1);
                const float rl = (float) (src.L[from] * env), rr = (float) (src.R[from] * env);
                out.L[st.start + k] = (1.0f - d) * src.L[st.start + k] + d * rl;
                out.R[st.start + k] = (1.0f - d) * src.R[st.start + k] + d * rr;
            }
            ++j;
        }
        i = j;
    }
}
} // namespace detail

// ============================================================ creative effects
namespace fx
{
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    void set (int type, double hz, double q, double sr)   // 0 = LP, 1 = HP
    {
        hz = std::min (hz, 0.45 * sr);
        const double w0 = 2 * M_PI * hz / sr, c = std::cos (w0), al = std::sin (w0) / (2 * q), a0 = 1 + al;
        if (type == 0) { b0 = (1 - c) / 2 / a0; b1 = (1 - c) / a0;  b2 = b0; }
        else           { b0 = (1 + c) / 2 / a0; b1 = -(1 + c) / a0; b2 = b0; }
        a1 = -2 * c / a0; a2 = (1 - al) / a0;
    }
    float process (float x)
    {
        const double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = x; y2 = y1; y1 = y;
        return (float) y;
    }
};

inline void filter (std::vector<float>& x, int type, double hz, double q, double sr)
{
    Biquad b; b.set (type, hz, q, sr);
    for (auto& v : x) v = b.process (v);
}

// Detects sibilant moments (lots of energy above ~5 kHz compared to the whole signal)
// and turns only the high band down there.
inline void deEss (std::vector<float>& x, double sr)
{
    if (x.empty()) return;
    Biquad lp; lp.set (0, 4500.0, 0.707, sr);
    float peak = 0;
    for (auto v : x) peak = std::max (peak, std::abs (v));
    const double atk = std::exp (-1.0 / (0.002 * sr)), rel = std::exp (-1.0 / (0.040 * sr));
    const double gSmooth = std::exp (-1.0 / (0.003 * sr));
    double eH = 0, eA = 0, g = 1;
    for (auto& v : x)
    {
        const float low = lp.process (v), high = v - low;
        const double aH = std::abs (high), aA = std::abs (v);
        eH = aH > eH ? atk * eH + (1 - atk) * aH : rel * eH + (1 - rel) * aH;
        eA = aA > eA ? atk * eA + (1 - atk) * aA : rel * eA + (1 - rel) * aA;
        const bool sibilant = eA > peak * 0.02 && eH > 0.45 * eA;
        const double target = sibilant ? 0.12 : 1.0;
        g = gSmooth * g + (1 - gSmooth) * target;
        v = low + (float) (high * g);
    }
}

// Granular pitch shifter (keeps the length). ratioAt(sample) gives the pitch ratio.
template <typename RatioFn>
inline std::vector<float> pitchShift (const std::vector<float>& in, double sr, RatioFn ratioAt)
{
    const long N = (long) in.size();
    const long G = (long) (0.07 * sr), H = G / 4;
    std::vector<float> out ((size_t) N, 0.0f), wsum ((size_t) N, 0.0f), win ((size_t) G);
    for (long k = 0; k < G; ++k) win[(size_t) k] = (float) (0.5 - 0.5 * std::cos (2 * M_PI * k / (G - 1)));
    for (long t = -G; t < N; t += H)
    {
        const double centre = (double) t + G / 2.0;
        const double r = ratioAt (std::clamp (centre, 0.0, (double) (N - 1)));
        for (long k = 0; k < G; ++k)
        {
            const long o = t + k;
            if (o < 0 || o >= N) continue;
            const double src = centre + ((double) k - G / 2.0) * r;
            float v = 0.0f;
            if (src >= 0 && src < (double) (N - 1))
            {
                const long i0 = (long) src; const double f = src - (double) i0;
                v = (float) (in[(size_t) i0] * (1 - f) + in[(size_t) i0 + 1] * f);
            }
            out[(size_t) o] += win[(size_t) k] * v;
            wsum[(size_t) o] += win[(size_t) k];
        }
    }
    for (long i = 0; i < N; ++i) out[(size_t) i] /= std::max (wsum[(size_t) i], 0.5f);
    return out;
}

inline void apply (Stereo& out, const Settings& s)
{
    const size_t L = out.size();
    if (L < 64) return;
    const double sr = s.sampleRate;
    auto frac = [L] (double i) { return i / (double) L; };

    auto forBoth = [&out] (auto&& fn) { fn (out.L); fn (out.R); };

    if (s.pitchRise)   // climbs an octave up into the vocal
        forBoth ([&] (std::vector<float>& ch)
        {
            ch = pitchShift (ch, sr, [&] (double i) { return std::pow (2.0, -12.0 * std::pow (1.0 - frac (i), 1.4) / 12.0); });
        });

    if (s.shimmer || s.darkOctave)
        forBoth ([&] (std::vector<float>& ch)
        {
            std::vector<float> mix = ch;
            if (s.shimmer)
            {
                auto up = pitchShift (ch, sr, [] (double) { return 2.0; });
                filter (up, 1, 900.0, 0.707, sr);
                for (size_t i = 0; i < L; ++i) mix[i] += 0.65f * up[i];
            }
            if (s.darkOctave)
            {
                auto down = pitchShift (ch, sr, [] (double) { return 0.5; });
                for (size_t i = 0; i < L; ++i) mix[i] += 0.75f * down[i];
            }
            ch = std::move (mix);
        });

    if (s.radio)
        forBoth ([&] (std::vector<float>& ch)
        {
            filter (ch, 1, 450.0, 0.9, sr);
            filter (ch, 0, 3200.0, 0.9, sr);
            float pk = 1e-9f; for (auto v : ch) pk = std::max (pk, std::abs (v));
            for (auto& v : ch) v = std::tanh (3.0f * v / pk);
        });

    if (s.lofi)
        forBoth ([&] (std::vector<float>& ch)
        {
            const double step = sr / 11025.0; double acc = 0; float held = 0;
            float pk = 1e-9f; for (auto v : ch) pk = std::max (pk, std::abs (v));
            const float levels = 20.0f;
            for (auto& v : ch)
            {
                if ((acc += 1.0) >= step)
                {
                    // companded quantisation keeps the quiet start of the swell crunchy instead of clicky
                    acc -= step;
                    const float x = v / pk, c = std::copysign (std::sqrt (std::abs (x)), x);
                    const float q = std::round (c * levels) / levels;
                    held = std::copysign (q * q, q) * pk;
                }
                v = 0.8f * held + 0.2f * v;
            }
            filter (ch, 0, 5000.0, 0.707, sr);
        });

    if (s.tape)
    {
        // wow & flutter (modulated delay) + warm saturation
        const size_t maxD = (size_t) (0.02 * sr);
        for (int c = 0; c < 2; ++c)
        {
            auto& ch = c == 0 ? out.L : out.R;
            const std::vector<float> src = ch;
            float pk = 1e-9f; for (auto v : src) pk = std::max (pk, std::abs (v));
            for (size_t i = 0; i < L; ++i)
            {
                const double t = (double) i / sr;
                const double d = 0.009 * sr + 0.0025 * sr * std::sin (2 * M_PI * 0.6 * t + c * 0.5)
                                            + 0.0003 * sr * std::sin (2 * M_PI * 6.3 * t);
                const double p = (double) i - std::min (d, (double) maxD);
                float v = 0.0f;
                if (p >= 0) { const size_t i0 = (size_t) p; const double f = p - (double) i0;
                              v = (float) (src[i0] * (1 - f) + src[std::min (i0 + 1, L - 1)] * f); }
                ch[i] = std::tanh (1.8f * v / pk) / std::tanh (1.8f) * pk;
            }
            filter (ch, 0, 9000.0, 0.707, sr);
        }
    }

    if (s.delay)
    {
        // tempo-synced ping-pong, dotted 1/8
        const size_t d = (size_t) std::llround (0.75 * 60.0 / s.bpm * sr);
        if (d > 0 && d < L)
        {
            std::vector<float> dl (L, 0.0f), dr (L, 0.0f);
            const float fb = 0.5f, mix = 0.45f;
            Biquad lpL, lpR; lpL.set (0, 6000, 0.707, sr); lpR.set (0, 6000, 0.707, sr);
            for (size_t i = 0; i < L; ++i)
            {
                const float inL = out.L[i], inR = out.R[i];
                const float fromL = i >= d ? dl[i - d] : 0.0f, fromR = i >= d ? dr[i - d] : 0.0f;
                dl[i] = lpL.process (0.5f * (inL + inR) + fb * fromR);   // L feeds R feeds L
                dr[i] = lpR.process (fb * fromL);
                out.L[i] = inL + mix * fromL;
                out.R[i] = inR + mix * fromR;
            }
        }
    }

    if (s.filterSweep)   // resonant low-pass that opens up into the vocal
        forBoth ([&] (std::vector<float>& ch)
        {
            Biquad b;
            for (size_t i = 0; i < L; ++i)
            {
                if ((i & 31) == 0)
                    b.set (0, 180.0 * std::pow (100.0, std::pow (frac ((double) i), 1.6)), 2.2, sr);
                ch[i] = b.process (ch[i]);
            }
        });
}
} // namespace fx

// Index of the first sample louder than -40 dB relative to the peak (start of the vocal).
inline size_t findOnset (const Stereo& in)
{
    float peak = 0;
    for (size_t i = 0; i < in.size(); ++i)
        peak = std::max (peak, std::max (std::abs (in.L[i]), std::abs (in.R[i])));
    if (peak <= 0) return 0;
    const float thr = peak * 0.01f;
    for (size_t i = 0; i < in.size(); ++i)
        if (std::abs (in.L[i]) > thr || std::abs (in.R[i]) > thr)
            return i;
    return 0;
}

inline size_t riserLengthSamples (const Settings& s)
{
    return (size_t) std::llround (s.lengthBars * 4.0 * 60.0 / s.bpm * s.sampleRate);
}

// Renders the riser. Output length = exactly the chosen number of bars, so it
// can be snapped to the grid and end right where the vocal comes in.
inline Stereo render (const Stereo& vocal, const Settings& s)
{
    Stereo out;
    const size_t L = riserLengthSamples (s);
    out.L.assign (L, 0.0f);
    out.R.assign (L, 0.0f);
    if (vocal.size() == 0 || L < 16) return out;

    // 1. Mono vocal from the onset, limited to what can reach the riser.
    const size_t onset = findOnset (vocal);
    const size_t M = std::min (vocal.size() - onset, L);
    std::vector<float> rev (M);
    for (size_t i = 0; i < M; ++i)
    {
        const size_t src = onset + (M - 1 - i);          // reversed
        rev[i] = 0.5f * (vocal.L[src] + vocal.R[src]);
    }
    // automatic sibilance detection: keep harsh "s" sounds out of the reverb
    if (s.deEss) fx::deEss (rev, s.sampleRate);

    // gentle fade at the cut so the reversed signal does not start with a click
    const size_t fade = std::min<size_t> (M / 4, (size_t) (0.02 * s.sampleRate));
    for (size_t i = 0; i < fade; ++i) rev[i] *= (float) i / (float) fade;

    // 2. Reverb on the reversed vocal.
    std::vector<float> irL, irR, cL, cR;
    detail::makeImpulse (s, L + 1, irL, irR);
    detail::convolveStereo (rev, irL, irR, cL, cR);

    // 3. Keep the tail after the reversed vocal, reversed back: it now swells into the onset.
    for (size_t t = 0; t < L; ++t)
    {
        const size_t idx = M + (L - 1 - t);
        if (idx < cL.size()) { out.L[t] = cL[idx]; out.R[t] = cR[idx]; }
    }

    // 4. Clean up low end so it doesn't fight the beat.
    detail::highpass (out.L, 140.0, s.sampleRate);
    detail::highpass (out.R, 140.0, s.sampleRate);

    // 4b. One-click creative effects
    fx::apply (out, s);

    // 5. Stereo width (mid/side).
    for (size_t i = 0; i < L; ++i)
    {
        const float m = 0.5f * (out.L[i] + out.R[i]);
        const float sd = 0.5f * (out.L[i] - out.R[i]) * (s.width * 1.4f);
        out.L[i] = m + sd;
        out.R[i] = m - sd;
    }

    // 6. Stutter / tremolo, locked to the grid counted back from the end (= the downbeat where the vocal enters).
    if (s.tremoloDepth > 0.001f)
        detail::applyStutter (out, s);

    // 7. Fade in the very start, very short fade at the end (no click on the downbeat).
    const size_t fi = L / 6, fo = std::min<size_t> (L / 50, (size_t) (0.004 * s.sampleRate));
    for (size_t i = 0; i < fi; ++i)
    {
        const float g = (float) std::pow ((double) i / fi, 1.5);
        out.L[i] *= g; out.R[i] *= g;
    }
    for (size_t i = 0; i < fo; ++i)
    {
        const float g = (float) i / (float) fo;
        out.L[L - 1 - i] *= g; out.R[L - 1 - i] *= g;
    }

    // 8. Gentle saturation for a denser, smoother swell, then normalise to -1 dBFS + output gain.
    {
        float pk = 0;
        for (size_t i = 0; i < L; ++i) pk = std::max (pk, std::max (std::abs (out.L[i]), std::abs (out.R[i])));
        if (pk > 0)
        {
            const float k = 2.2f, norm = 1.0f / std::tanh (k);
            for (size_t i = 0; i < L; ++i)
            {
                out.L[i] = std::tanh (k * out.L[i] / pk) * norm;
                out.R[i] = std::tanh (k * out.R[i] / pk) * norm;
            }
        }
    }
    float peak = 0;
    for (size_t i = 0; i < L; ++i) peak = std::max (peak, std::max (std::abs (out.L[i]), std::abs (out.R[i])));
    if (peak > 0)
    {
        const float g = 0.891f / peak * std::pow (10.0f, s.outputDb / 20.0f);
        for (size_t i = 0; i < L; ++i) { out.L[i] *= g; out.R[i] *= g; }
    }
    return out;
}
} // namespace riser
