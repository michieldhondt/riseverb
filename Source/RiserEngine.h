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
    double tremoloDiv   = 16.0;  // 4 = 1/4 notes, 8 = 1/8, 16 = 1/16, 32 = 1/32
    float  tremoloDepth = 0.0f;  // 0..1
    float  outputDb     = 0.0f;  // -24..+6, applied after normalising to -1 dBFS
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
} // namespace detail

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

    // 5. Stereo width (mid/side).
    for (size_t i = 0; i < L; ++i)
    {
        const float m = 0.5f * (out.L[i] + out.R[i]);
        const float sd = 0.5f * (out.L[i] - out.R[i]) * (s.width * 1.4f);
        out.L[i] = m + sd;
        out.R[i] = m - sd;
    }

    // 6. Tremolo, locked to the grid counted back from the end (= the downbeat where the vocal enters).
    if (s.tremoloDepth > 0.001f)
    {
        const double period = (4.0 / s.tremoloDiv) * 60.0 / s.bpm * s.sampleRate;
        for (size_t i = 0; i < L; ++i)
        {
            const double fromEnd = (double) (L - i);
            const double ph = std::fmod (fromEnd, period) / period;          // 0..1
            double g = 0.5 + 0.5 * std::cos (2 * M_PI * ph);                 // 1 on the grid
            g = std::pow (g, 0.6);                                           // slightly choppier
            const float gain = (float) (1.0 - s.tremoloDepth * (1.0 - g));
            out.L[i] *= gain;
            out.R[i] *= gain;
        }
    }

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
