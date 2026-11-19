#pragma once
// RelaXXer – dynamischer Resonanz-Unterdrücker (reiner C++-DSP-Kern, ohne JUCE)
//
// Ablauf pro STFT-Frame (sqrt-Hann, 75 % bzw. 87,5 % Überlappung):
//  1. Leistungsspektrum je Kanal (L/R oder M/S), optional vom Sidechain
//  2. Glatte Spektral-Hüllkurve (gleitender Mittelwert über ~1 Oktave, zweistufig robust gegen Spitzen)
//  3. Überhang = Pegel + Fokus-Gewichtung − Hüllkurve − Selektivitäts-Schwelle  -> Resonanz-Kandidaten
//  4. Absenkung aus dem Überhang (Soft: weiches Knie; Hard: linear + pegelabhängig), Begrenzung durch Tiefe
//  5. Glättung über die Frequenz (Schärfe: schmale tiefe vs. breite weiche Kerben)
//  6. Attack/Release je Bin (frequenzabhängig: Höhen schneller als Bässe)
//  7. Stereo-Link/Balance, Mix, Trim, Delta, dann inverse FFT + Overlap-Add
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <complex>
#include <memory>
#include <vector>

#if RELAXXER_USE_JUCE_FFT
 #include <juce_dsp/juce_dsp.h> // im Plugin: JUCE-FFT (auf dem Mac Apple vDSP, deutlich schneller)
#endif

namespace relaxxer
{
struct Params
{
    float depth       = 5.0f;   // 0..20  Stärke der Absenkung
    float sharpness   = 4.0f;   // 0..10  breite/weiche (0) bis schmale/tiefe (10) Kerben
    float selectivity = 3.5f;   // 0..10  nur sehr markante (10) oder fast alle (0) Resonanzen
    float attackMs    = 5.0f;   // Referenz bei 1 kHz
    float releaseMs   = 60.0f;  // Referenz bei 1 kHz
    bool  hardMode    = false;
    bool  midSide     = false;
    float link        = 1.0f;   // 0..1
    float balance     = 0.0f;   // -1..+1 (L/M .. R/S)
    float mix         = 1.0f;   // 0..1
    float trimDb      = 0.0f;   // nur Nassanteil
    bool  delta       = false;  // nur das Entfernte hören
    bool  useSidechain = false;
    bool  highQuality = false;  // 87,5 % statt 75 % Überlappung
    // inspiriert von Gullfoss:
    float recover     = 0.0f;   // 0..1  verdeckte (zu leise) Bereiche behutsam anheben ("Freilegen")
    float brighten    = 0.0f;   // -1..+1 Erkennung bevorzugt Tiefen (heller) oder Höhen (dunkler)
    float contourDb   = 0.0f;   // -10..+10 Lautheitskontur: + = mehr Bass/Höhen, weniger Mitten
    bool  autoGain    = false;  // wahrgenommene Lautheit halten (fairer A/B-Vergleich)
    float lowCutHz    = 40.0f;  // Arbeitsbereich unten
    float highCutHz   = 20000.0f;
    // zwei Fokus-Bänder: Empfindlichkeit (Sensitivity) in dB, wie die Knoten bei soothe
    float band1Hz = 300.0f,  band1Db = 0.0f, band1Q = 1.0f;
    float band2Hz = 4000.0f, band2Db = 0.0f, band2Q = 1.0f;
};

class Processor
{
public:
    static constexpr int   displayPoints = 160;   // log-verteilte Punkte 20 Hz .. 20 kHz fürs GUI
    static constexpr float safetyClamp   = 4.0f;

    // lowLatency: ~5 ms Fenster (Live-Modus, gröberes Frequenzraster) statt ~21 ms
    void prepare (double sampleRate, int numChannels, bool lowLatency = false)
    {
        sr = std::clamp (sampleRate, 8000.0, 768000.0);
        channels = std::clamp (numChannels, 1, 2);
        fftSize = sr <= 50000.0 ? 1024 : (sr <= 100000.0 ? 2048 : 4096); // ~21 ms Fenster, ~47 Hz Raster
        if (lowLatency) fftSize /= 4;
        bins = fftSize / 2 + 1;
        buildFft();
       #if RELAXXER_USE_JUCE_FFT
        { int lg = 0; while ((1 << lg) < fftSize) ++lg; jfft = std::make_unique<juce::dsp::FFT> (lg); }
        fftTmp.assign ((size_t) fftSize, {});
       #endif
        { // Normierung der inversen FFT einmalig messen (je nach FFT-Engine skaliert oder nicht)
            std::vector<cpx> t ((size_t) fftSize, cpx (0.0f, 0.0f)); t[0] = cpx (1.0f, 0.0f);
            fft (t, false); fft (t, true);
            ifftNorm = (std::isfinite (t[0].real()) && std::fabs (t[0].real()) > 1.0e-6f) ? 1.0f / t[0].real() : 1.0f / (float) fftSize;
        }
        window.resize ((size_t) fftSize);
        for (int i = 0; i < fftSize; ++i)
            window[(size_t) i] = std::sqrt (0.5f - 0.5f * std::cos (2.0f * 3.14159265358979f * (float) i / (float) fftSize));

        for (int c = 0; c < 2; ++c)
        {
            inBuf[c].assign ((size_t) fftSize, 0.0f);
            scBuf[c].assign ((size_t) fftSize, 0.0f);
            outAcc[c].assign ((size_t) fftSize, 0.0f);
            outBuf[c].assign ((size_t) fftSize, 0.0f);
            spec[c].assign ((size_t) fftSize, {});
            levelDb[c].assign ((size_t) bins, -120.0f);
            redOwn[c].assign ((size_t) bins, 0.0f);
            redSmooth[c].assign ((size_t) bins, 0.0f);
        }
        levelSum.assign ((size_t) bins, -120.0f);
        redLink.assign ((size_t) bins, 0.0f);
        work.assign ((size_t) bins, 0.0f);
        work2.assign ((size_t) bins, 0.0f);
        prefix.assign ((size_t) bins + 1, 0.0);
        scratchBuf.assign ((size_t) fftSize, {});
        pow0.assign ((size_t) bins, 0.0f); pow1.assign ((size_t) bins, 0.0f);
        cFocus.clear(); cAtt.clear(); tcRate = -1; cacheKey.fill (-1.0f);
        cContour.assign ((size_t) bins, 1.0f); cLoud.assign ((size_t) bins, 1.0f); contourKey = 1.0e9f;
        boostWork.assign ((size_t) bins, 0.0f);
        makeupDb = 0.0f;
        meterIn.store (-120.0f); meterOut.store (-120.0f); meterBright.store (0.0f); meterMakeup.store (0.0f);
        inMs = outMs = 0.0f; brightIn = brightOut = 0.0f;

        binHz.resize ((size_t) bins);
        for (int k = 0; k < bins; ++k) binHz[(size_t) k] = (float) ((double) k * sr / fftSize);
        hop = fftSize / 4;
        pos = 0;
        for (auto& a : dispLevel) a.store (-120.0f);
        for (auto& a : dispRed)   a.store (0.0f);
        dispSmooth.fill (-120.0f);
        maxRedDb.store (0.0f);
        clipHold = 0;

        // Bereichstabellen vorab anlegen (keine Speicher-Allokation im Audio-Thread)
        for (auto& r : rangeCache)
        {
            r = Ranges();
            r.lo.reserve ((size_t) bins); r.hi.reserve ((size_t) bins); r.inv.reserve ((size_t) bins);
        }
        rangeNext = 0;
        rangesFor (0.5f, 3); rangesFor (0.33f, 3); rangesFor (0.75f, 3); rangesFor (0.083f, 1);
        for (int k = 0; k < bins; ++k) // Lautheits-Gewichtung (grob gehörrichtig, für Auto-Gain und Meter)
        {
            const float f = std::max (binHz[(size_t) k], 1.0f);
            const float hp = 1.0f / (1.0f + std::pow (120.0f / f, 2.0f));
            const float shelf = 1.0f + 0.6f / (1.0f + std::pow (2000.0f / f, 2.0f));
            cLoud[(size_t) k] = hp * shelf;
        }
    }

    // Puffer leeren ohne Speicher neu anzulegen (beim Umschalten Normal/Live)
    void reset()
    {
        for (int c = 0; c < 2; ++c)
        {
            std::fill (inBuf[c].begin(), inBuf[c].end(), 0.0f);
            std::fill (scBuf[c].begin(), scBuf[c].end(), 0.0f);
            std::fill (outAcc[c].begin(), outAcc[c].end(), 0.0f);
            std::fill (outBuf[c].begin(), outBuf[c].end(), 0.0f);
            std::fill (redSmooth[c].begin(), redSmooth[c].end(), 0.0f);
        }
        pos = 0;
        makeupDb = 0.0f;
    }

    // Latenz in Samples (abhängig von der Qualität)
    int getLatencySamples() const { return fftSize; } // gemessen: konstant = FFT-Größe (~21 ms)
    int getFftSize() const { return fftSize; }
    int getNumChannels() const { return channels; }

    // io: Hauptkanäle in-place; sc: optionaler Sidechain (1–2 Kanäle) oder nullptr
    void process (float* const* io, const float* const* sc, int scChannels, int numSamples, const Params& pIn)
    {
        Params p = sanitize (pIn);
        const int newHop = p.highQuality ? fftSize / 8 : fftSize / 4;
        if (newHop != hop) { hop = newHop; pos = 0; for (int c = 0; c < 2; ++c) { std::fill (outAcc[c].begin(), outAcc[c].end(), 0.0f); std::fill (outBuf[c].begin(), outBuf[c].end(), 0.0f); } }
        const int scCh = (sc != nullptr && p.useSidechain) ? std::clamp (scChannels, 0, 2) : 0;
        const bool ms = p.midSide && channels == 2;

        for (int n = 0; n < numSamples; ++n)
        {
            float a = io[0][n], b = channels > 1 ? io[1][n] : 0.0f;
            if (! std::isfinite (a)) a = 0.0f;
            if (! std::isfinite (b)) b = 0.0f;
            if (ms) { const float m = 0.5f * (a + b), s = 0.5f * (a - b); a = m; b = s; }

            const int wi = fftSize - hop + pos;
            inBuf[0][(size_t) wi] = a;
            inBuf[1][(size_t) wi] = b;
            if (scCh > 0)
            {
                float s0 = sc[0][n], s1 = scCh > 1 ? sc[1][n] : s0;
                if (! std::isfinite (s0)) s0 = 0.0f;
                if (! std::isfinite (s1)) s1 = 0.0f;
                if (ms) { const float m = 0.5f * (s0 + s1), s = 0.5f * (s0 - s1); s0 = m; s1 = s; }
                scBuf[0][(size_t) wi] = s0;
                scBuf[1][(size_t) wi] = s1;
            }

            float o0 = outBuf[0][(size_t) pos], o1 = outBuf[1][(size_t) pos];
            if (ms) { const float l = o0 + o1, r = o0 - o1; o0 = l; o1 = r; }

            const float y0 = std::clamp (o0, -safetyClamp, safetyClamp);
            const float y1 = std::clamp (o1, -safetyClamp, safetyClamp);
            if (std::fabs (y0) > 1.0f || std::fabs (y1) > 1.0f) clipHold = (int) (0.5 * sr);
            io[0][n] = y0;
            if (channels > 1) io[1][n] = y1;
            if (clipHold > 0) --clipHold;

            if (++pos >= hop)
            {
                pos = 0;
                processFrame (p, scCh > 0);
                for (int c = 0; c < 2; ++c)
                {
                    std::copy (inBuf[c].begin() + hop, inBuf[c].end(), inBuf[c].begin());
                    std::copy (scBuf[c].begin() + hop, scBuf[c].end(), scBuf[c].begin());
                }
            }
        }
    }

    // ---- fürs GUI (vom Audio-Thread geschrieben, lock-frei gelesen) ----
    static float displayFreq (int i) { return 20.0f * std::pow (1000.0f, (float) i / (float) (displayPoints - 1)); }
    float getDisplayLevel (int i) const { return dispLevel[(size_t) i].load (std::memory_order_relaxed); }
    float getDisplayReduction (int i) const { return dispRed[(size_t) i].load (std::memory_order_relaxed); }
    float getMaxReductionDb() const { return maxRedDb.load (std::memory_order_relaxed); }
    bool  isClipping() const { return clipHold > 0; }
    float getInputDb() const      { return meterIn.load (std::memory_order_relaxed); }   // gewichteter RMS, ~300 ms
    float getOutputDb() const     { return meterOut.load (std::memory_order_relaxed); }
    float getBrightnessDeltaDb() const { return meterBright.load (std::memory_order_relaxed); } // Ausgang heller (+) / dunkler (-)
    float getMakeupDb() const     { return meterMakeup.load (std::memory_order_relaxed); }

    // Fokus-Gewichtung in dB bei Frequenz f (auch fürs GUI, damit Kurve und DSP identisch sind)
    static float focusWeightDb (float f, const Params& p)
    {
        const float lf = std::log2 (std::max (f, 1.0f));
        auto bell = [lf] (float fc, float db, float q)
        {
            const float d = (lf - std::log2 (std::max (fc, 1.0f))) * q * 1.4f;
            return db * std::exp (-0.5f * d * d * 4.0f);
        };
        return bell (p.band1Hz, p.band1Db, p.band1Q) + bell (p.band2Hz, p.band2Db, p.band2Q);
    }
    // Hell/Dunkel: + = Tiefen werden empfindlicher behandelt (Ergebnis heller), - = Höhen (dunkler)
    static float brightenWeightDb (float f, const Params& p)
    {
        return -p.brighten * 2.5f * std::clamp (std::log2 (std::max (f, 1.0f) / 1000.0f), -5.0f, 4.5f);
    }
    // Lautheitskontur (statisch, wie "Boost" bei Gullfoss): Badewannen-Kurve, Mitten-Mittelwert ~0
    static float contourShapeDb (float f, float amountDb)
    {
        const float lf = std::log2 (std::max (f, 1.0f));
        const float bass = 0.5f + 0.5f * std::tanh ((std::log2 (150.0f) - lf) * 1.3f);
        const float treb = 0.5f + 0.5f * std::tanh ((lf - std::log2 (9000.0f)) * 1.3f);
        return amountDb * (bass + 0.6f * treb - 0.25f);
    }
    // Arbeitsbereich 0..1 (weiche Flanken, ca. 1/2 Oktave)
    static float rangeWeight (float f, const Params& p)
    {
        float w = 1.0f;
        if (f < p.lowCutHz)  w *= std::clamp (1.0f - 2.0f * std::log2 (p.lowCutHz / std::max (f, 1.0f)), 0.0f, 1.0f);
        if (f > p.highCutHz) w *= std::clamp (1.0f - 2.0f * std::log2 (f / p.highCutHz), 0.0f, 1.0f);
        return w;
    }

private:
    using cpx = std::complex<float>;

    static float fin (float v, float d) { return std::isfinite (v) ? v : d; }
    static Params sanitize (Params p)
    {
        p.depth       = std::clamp (fin (p.depth, 5.0f), 0.0f, 20.0f);
        p.sharpness   = std::clamp (fin (p.sharpness, 4.0f), 0.0f, 10.0f);
        p.selectivity = std::clamp (fin (p.selectivity, 3.5f), 0.0f, 10.0f);
        p.attackMs    = std::clamp (fin (p.attackMs, 5.0f), 0.1f, 500.0f);
        p.releaseMs   = std::clamp (fin (p.releaseMs, 60.0f), 1.0f, 3000.0f);
        p.link        = std::clamp (fin (p.link, 1.0f), 0.0f, 1.0f);
        p.balance     = std::clamp (fin (p.balance, 0.0f), -1.0f, 1.0f);
        p.mix         = std::clamp (fin (p.mix, 1.0f), 0.0f, 1.0f);
        p.trimDb      = std::clamp (fin (p.trimDb, 0.0f), -24.0f, 24.0f);
        p.recover     = std::clamp (fin (p.recover, 0.0f), 0.0f, 1.0f);
        p.brighten    = std::clamp (fin (p.brighten, 0.0f), -1.0f, 1.0f);
        p.contourDb   = std::clamp (fin (p.contourDb, 0.0f), -10.0f, 10.0f);
        p.lowCutHz    = std::clamp (fin (p.lowCutHz, 40.0f), 10.0f, 22000.0f);
        p.highCutHz   = std::clamp (fin (p.highCutHz, 20000.0f), p.lowCutHz, 40000.0f);
        auto band = [] (float& hz, float& db, float& q)
        {
            hz = std::clamp (fin (hz, 1000.0f), 10.0f, 30000.0f);
            db = std::clamp (fin (db, 0.0f), -24.0f, 24.0f);
            q  = std::clamp (fin (q, 1.0f), 0.1f, 10.0f);
        };
        band (p.band1Hz, p.band1Db, p.band1Q);
        band (p.band2Hz, p.band2Db, p.band2Q);
        return p;
    }

    // ---------- FFT (radix 2, iterativ) ----------
    void buildFft()
    {
        int lg = 0; while ((1 << lg) < fftSize) ++lg;
        rev.resize ((size_t) fftSize);
        for (int i = 0; i < fftSize; ++i)
        {
            int r = 0; for (int b = 0; b < lg; ++b) if (i & (1 << b)) r |= 1 << (lg - 1 - b);
            rev[(size_t) i] = r;
        }
        tw.resize ((size_t) fftSize / 2);
        for (int i = 0; i < fftSize / 2; ++i)
            tw[(size_t) i] = std::polar (1.0f, -2.0f * 3.14159265358979f * (float) i / (float) fftSize);
    }
    void fft (std::vector<cpx>& a, bool inverse)
    {
       #if RELAXXER_USE_JUCE_FFT
        if (jfft != nullptr)
        {
            jfft->perform (a.data(), fftTmp.data(), inverse);
            std::copy (fftTmp.begin(), fftTmp.end(), a.begin());
            return;
        }
       #endif
        const int N = fftSize;
        for (int i = 0; i < N; ++i) { const int r = rev[(size_t) i]; if (i < r) std::swap (a[(size_t) i], a[(size_t) r]); }
        for (int len = 2; len <= N; len <<= 1)
        {
            const int half = len / 2, step = N / len;
            for (int i = 0; i < N; i += len)
                for (int j = 0; j < half; ++j)
                {
                    const cpx w = tw[(size_t) (j * step)];
                    const float wr = w.real(), wi = inverse ? -w.imag() : w.imag();
                    cpx& x0 = a[(size_t) (i + j)];
                    cpx& x1 = a[(size_t) (i + j + half)];
                    const float vr = x1.real() * wr - x1.imag() * wi, vi = x1.real() * wi + x1.imag() * wr; // ohne __mulsc3
                    const float ur = x0.real(), ui = x0.imag();
                    x0 = cpx (ur + vr, ui + vi);
                    x1 = cpx (ur - vr, ui - vi);
                }
        }
    }

    // gleitender Mittelwert über ±halfOct Oktaven (mind. ±minBins) via Präfixsummen
    void smoothLog (const std::vector<float>& in, std::vector<float>& out, float halfOct, int minBins)
    {
        prefix[0] = 0.0;
        for (int k = 0; k < bins; ++k) prefix[(size_t) k + 1] = prefix[(size_t) k] + (double) in[(size_t) k];
        const auto& rg = rangesFor (halfOct, minBins);
        for (int k = 0; k < bins; ++k)
        {
            const int lo = rg.lo[(size_t) k], hi = rg.hi[(size_t) k];
            out[(size_t) k] = (float) ((prefix[(size_t) hi + 1] - prefix[(size_t) lo]) * rg.inv[(size_t) k]);
        }
    }

    struct Ranges { float halfOct = -1; int minBins = -1; std::vector<int> lo, hi; std::vector<double> inv; };
    const Ranges& rangesFor (float halfOct, int minBins)
    {
        for (auto& r : rangeCache) if (r.halfOct == halfOct && r.minBins == minBins && (int) r.lo.size() == bins) return r;
        auto& r = rangeCache[(size_t) (rangeNext++ % rangeCache.size())];
        r.halfOct = halfOct; r.minBins = minBins;
        r.lo.resize ((size_t) bins); r.hi.resize ((size_t) bins); r.inv.resize ((size_t) bins);
        const float fac = std::pow (2.0f, halfOct), df = (float) (sr / fftSize);
        for (int k = 0; k < bins; ++k)
        {
            const float f = std::max (binHz[(size_t) k], df);
            int lo = (int) std::floor ((f / fac) / df), hi = (int) std::ceil ((f * fac) / df);
            lo = std::min (lo, k - minBins); hi = std::max (hi, k + minBins);
            lo = std::max (lo, 0); hi = std::min (hi, bins - 1);
            r.lo[(size_t) k] = lo; r.hi[(size_t) k] = hi; r.inv[(size_t) k] = 1.0 / (double) (hi - lo + 1);
        }
        return r;
    }

    // Absenkung (dB, >= 0) je Bin aus einem Pegelspektrum
    void computeReduction (const std::vector<float>& L, std::vector<float>& R, const Params& p)
    {
        const float envHalfOct = p.hardMode ? 0.33f : 0.5f;
        // robuste Hüllkurve: erst glätten, Spitzen kappen, nochmal glätten
        smoothLog (L, work, envHalfOct, 3);
        for (int k = 0; k < bins; ++k) work2[(size_t) k] = std::min (L[(size_t) k], work[(size_t) k] + 3.0f);
        smoothLog (work2, work, envHalfOct, 3);

        updateCaches (p);
        const float thr      = 1.0f + 1.2f * p.selectivity;            // dB über der Hüllkurve
        const float amount   = p.depth / 5.0f;
        const float deepen   = 0.7f + 0.06f * p.sharpness;              // schärfer = tiefer
        const float cap      = std::min (60.0f, 3.0f * p.depth + 1.0f);
        for (int k = 0; k < bins; ++k)
        {
            const float rw = cRange[(size_t) k];
            float r = 0.0f;
            if (rw > 0.0f && L[(size_t) k] > -100.0f)
            {
                const float x = L[(size_t) k] + cFocus[(size_t) k] - work[(size_t) k] - thr;
                if (x > 0.0f)
                {
                    if (p.hardMode)
                    {
                        const float lvl = std::clamp ((work[(size_t) k] + 70.0f) / 40.0f, 0.25f, 1.6f); // lauter = mehr
                        r = amount * 1.2f * x * lvl;
                    }
                    else
                        r = amount * x * x / (x + 6.0f);                // weiches Knie
                    r = std::min (r * deepen, cap) * rw;
                }
            }
            R[(size_t) k] = r;
        }
        // Breite der Kerben: über die Frequenz verschmieren (Schärfe 10 = fast bin-genau)
        const float widthOct = 0.02f + 0.30f * std::pow (1.0f - p.sharpness / 10.0f, 1.5f);
        smoothLog (R, work, widthOct, 0);
        for (int k = 0; k < bins; ++k) R[(size_t) k] = std::max (work[(size_t) k], 0.6f * R[(size_t) k] * (p.sharpness / 10.0f));

        // Freilegen (Recover): breite Täler unter der Klang-Balance behutsam anheben (max. +6 dB)
        if (p.recover > 0.0f)
        {
            smoothLog (L, work, 0.75f, 3);       // breite Balance (~1,5 Oktaven)
            smoothLog (L, work2, 0.083f, 1);     // feine Struktur (~1/6 Oktave), keine Lücken zwischen Obertönen
            for (int k = 0; k < bins; ++k)
            {
                const float rw = cRange[(size_t) k];
                const float under = work[(size_t) k] - work2[(size_t) k] - 1.5f - cFocus[(size_t) k] * 0.5f;
                float b = 0.0f;
                if (rw > 0.0f && under > 0.0f && work2[(size_t) k] > -85.0f)
                    b = std::min (6.0f, p.recover * 1.0f * under) * rw;
                boostWork[(size_t) k] = b;
            }
            smoothLog (boostWork, work, 0.17f, 1);
            for (int k = 0; k < bins; ++k) R[(size_t) k] -= work[(size_t) k] * (R[(size_t) k] > 0.5f ? 0.0f : 1.0f);
        }
    }

    void processFrame (const Params& p, bool useSc)
    {
        const int nCh = channels;
        const float scale = 2.0f / (float) (fftSize / hop); // sqrt-Hann² summiert sich zu N/(2·hop)
        const float frameRate = (float) (sr / hop);

        // 1) Spektren + Pegel
        for (int c = 0; c < nCh; ++c)
        {
            auto& s = spec[c];
            for (int i = 0; i < fftSize; ++i) s[(size_t) i] = cpx (inBuf[c][(size_t) i] * window[(size_t) i], 0.0f);
            fft (s, false);
        }
        const float norm = 4.0f / ((float) fftSize * (float) fftSize); // ≈ Sinus-Amplitude² -> dBFS
        for (int c = 0; c < nCh; ++c)
        {
            if (useSc)
            {
                for (int i = 0; i < fftSize; ++i) scratch (i) = cpx (scBuf[c][(size_t) i] * window[(size_t) i], 0.0f);
                fft (scratchBuf, false);
                for (int k = 0; k < bins; ++k) (c ? pow1 : pow0)[(size_t) k] = std::norm (scratchBuf[(size_t) k]) * norm;
            }
            else
                for (int k = 0; k < bins; ++k) (c ? pow1 : pow0)[(size_t) k] = std::norm (spec[c][(size_t) k]) * norm;
            if (nCh == 1 || p.link < 1.0f) // Einzelkanal-Pegel nur wenn gebraucht
                for (int k = 0; k < bins; ++k) levelDb[c][(size_t) k] = 10.0f * std::log10 ((c ? pow1 : pow0)[(size_t) k] + 1.0e-14f);
        }
        if (nCh == 2)
            for (int k = 0; k < bins; ++k)
            {
                const float pw = 0.5f * (pow0[(size_t) k] + pow1[(size_t) k]);
                levelSum[(size_t) k] = 10.0f * std::log10 (pw + 1.0e-14f);
            }
        else
            levelSum = levelDb[0];

        // 2) Absenkung: gekoppelt und/oder je Kanal
        const bool needLink = nCh == 1 || p.link > 0.0f;
        const bool needOwn  = nCh == 2 && p.link < 1.0f;
        if (needLink) computeReduction (levelSum, redLink, p);
        if (needOwn) for (int c = 0; c < nCh; ++c) computeReduction (levelDb[c], redOwn[c], p);

        // 3) Zeitverhalten je Bin (Höhen schneller, Bässe langsamer)
        updateTimeCaches (p, frameRate);
        updateContour (p);
        const float trimLin = std::pow (10.0f, p.trimDb / 20.0f);
        float maxRed = 0.0f;
        double eIn = 0.0, eWet = 0.0, hfIn = 0.0, hfWet = 0.0;
        const int hfStart = std::clamp ((int) (2000.0 * fftSize / sr), 1, bins - 1);
        for (int c = 0; c < nCh; ++c)
        {
            float bal = 1.0f;
            if (nCh == 2) bal = c == 0 ? std::min (1.0f, 1.0f - p.balance) : std::min (1.0f, 1.0f + p.balance);
            auto& rs = redSmooth[c];
            const auto& pw = c ? pow1 : pow0;
            for (int k = 0; k < bins; ++k)
            {
                float target = 0.0f;
                if (needLink) target += p.link * redLink[(size_t) k];
                if (needOwn)  target += (1.0f - p.link) * redOwn[c][(size_t) k];
                if (nCh == 1) target = redLink[(size_t) k];
                target *= bal;
                const float a = target > rs[(size_t) k] ? cAtt[(size_t) k] : cRel[(size_t) k];
                float v = a * rs[(size_t) k] + (1.0f - a) * target;
                if (! std::isfinite (v)) v = 0.0f;
                rs[(size_t) k] = v;
                maxRed = std::max (maxRed, v);

                // Energie vorher/nachher (gewichtet) für Lautheit halten + Meter
                const float wet = std::exp (-0.11512925f * v) * cContour[(size_t) k];
                const float e = (useSc ? std::norm (spec[c][(size_t) k]) * (4.0f / ((float) fftSize * (float) fftSize)) : pw[(size_t) k]) * cLoud[(size_t) k];
                eIn  += e;
                eWet += e * wet * wet;
                if (k >= hfStart) { hfIn += e; hfWet += e * wet * wet; }
            }
        }

        // Lautheit halten: Nassanteil so nachregeln, dass die gewichtete Energie gleich bleibt (träge, ±12 dB)
        {
            float target = 0.0f;
            if (p.autoGain && eIn > 1.0e-10 && eWet > 1.0e-14)
                target = std::clamp (10.0f * (float) std::log10 (eIn / eWet), -6.0f, 12.0f);
            const float a = std::exp (-1.0f / (0.4f * frameRate));
            makeupDb = a * makeupDb + (1.0f - a) * target;
            if (! std::isfinite (makeupDb)) makeupDb = 0.0f;
        }
        const float makeup = std::pow (10.0f, makeupDb / 20.0f);

        // 4) Gain anwenden: Mix/Trim/Delta im Spektrum (kein extra Delay nötig)
        for (int c = 0; c < nCh; ++c)
        {
            const auto& rs = redSmooth[c];
            auto& s = spec[c];
            for (int k = 0; k < bins; ++k)
            {
                const float g = std::exp (-0.11512925f * rs[(size_t) k]); // 10^(-dB/20)
                float m;
                if (p.delta) m = p.mix * (1.0f - g);                          // nur das Entfernte (ohne Kontur/Ausgleich)
                else         m = p.mix * g * cContour[(size_t) k] * makeup * trimLin + (1.0f - p.mix);
                s[(size_t) k] *= m;
                if (k > 0 && k < fftSize / 2) s[(size_t) (fftSize - k)] = std::conj (s[(size_t) k]);
            }
            s[(size_t) (fftSize / 2)] = cpx (s[(size_t) (fftSize / 2)].real(), 0.0f);
            s[0] = cpx (s[0].real(), 0.0f);
            fft (s, true);
            const float inv = scale * ifftNorm;
            for (int i = 0; i < fftSize; ++i) outAcc[c][(size_t) i] += s[(size_t) i].real() * window[(size_t) i] * inv;
        }

        // Meter (gewichteter RMS ~300 ms, Helligkeit = Anteil > 2 kHz)
        {
            const float a = std::exp (-1.0f / (0.3f * frameRate));
            const float n = 1.0f / (float) std::max (1, nCh);
            const float gOut = p.delta ? 1.0f : makeup * makeup * trimLin * trimLin;
            inMs  = a * inMs  + (1.0f - a) * (float) eIn * n;
            outMs = a * outMs + (1.0f - a) * (float) (p.delta ? 0.0 : eWet * gOut) * n;
            const float bIn  = hfIn  > 1.0e-14 ? (float) (10.0 * std::log10 (hfIn / std::max (eIn, 1.0e-14))) : 0.0f;
            const float bOut = hfWet > 1.0e-14 ? (float) (10.0 * std::log10 (hfWet / std::max (eWet, 1.0e-14))) : 0.0f;
            brightIn = a * brightIn + (1.0f - a) * bIn;
            brightOut = a * brightOut + (1.0f - a) * bOut;
            meterIn.store (10.0f * std::log10 (inMs + 1.0e-14f), std::memory_order_relaxed);
            meterOut.store (10.0f * std::log10 (outMs + 1.0e-14f), std::memory_order_relaxed);
            meterBright.store (std::isfinite (brightOut - brightIn) ? brightOut - brightIn : 0.0f, std::memory_order_relaxed);
            meterMakeup.store (makeupDb, std::memory_order_relaxed);
        }
        for (int c = 0; c < 2; ++c)
        {
            if (c >= nCh) { std::fill (outBuf[c].begin(), outBuf[c].end(), 0.0f); continue; }
            std::copy (outAcc[c].begin(), outAcc[c].begin() + hop, outBuf[c].begin());
            std::copy (outAcc[c].begin() + hop, outAcc[c].end(), outAcc[c].begin());
            std::fill (outAcc[c].end() - hop, outAcc[c].end(), 0.0f);
        }

        // 5) Anzeige
        maxRedDb.store (maxRed, std::memory_order_relaxed);
        const float df = (float) (sr / fftSize);
        const float dispA = std::exp (-1.0f / (0.08f * frameRate));
        for (int i = 0; i < displayPoints; ++i)
        {
            const float f = displayFreq (i);
            const int k = std::clamp ((int) std::lround (f / df), 1, bins - 1);
            const float lv = levelSum[(size_t) k];
            dispSmooth[(size_t) i] = std::max (lv, dispA * dispSmooth[(size_t) i] + (1.0f - dispA) * lv);
            float red = redSmooth[0][(size_t) k];
            if (nCh == 2) red = std::max (red, redSmooth[1][(size_t) k]);
            dispLevel[(size_t) i].store (dispSmooth[(size_t) i], std::memory_order_relaxed);
            dispRed[(size_t) i].store (red, std::memory_order_relaxed);
        }
    }

    // Gewichtungen nur neu berechnen, wenn sich Parameter ändern (spart CPU)
    void updateCaches (const Params& p)
    {
        const float key[] = { p.lowCutHz, p.highCutHz, p.band1Hz, p.band1Db, p.band1Q, p.band2Hz, p.band2Db, p.band2Q, p.brighten };
        if (std::equal (std::begin (key), std::end (key), cacheKey.begin()) && (int) cFocus.size() == bins) return;
        std::copy (std::begin (key), std::end (key), cacheKey.begin());
        cFocus.resize ((size_t) bins); cRange.resize ((size_t) bins);
        for (int k = 0; k < bins; ++k)
        {
            const float f = std::max (binHz[(size_t) k], 1.0f);
            cFocus[(size_t) k] = focusWeightDb (f, p) + brightenWeightDb (f, p);
            cRange[(size_t) k] = rangeWeight (f, p);
        }
    }
    void updateTimeCaches (const Params& p, float frameRate)
    {
        if (p.attackMs == tcAtt && p.releaseMs == tcRel && frameRate == tcRate && (int) cAtt.size() == bins) return;
        tcAtt = p.attackMs; tcRel = p.releaseMs; tcRate = frameRate;
        cAtt.resize ((size_t) bins); cRel.resize ((size_t) bins);
        for (int k = 0; k < bins; ++k)
        {
            const float fScale = std::clamp (std::sqrt (1000.0f / std::max (binHz[(size_t) k], 20.0f)), 0.35f, 2.5f);
            cAtt[(size_t) k] = std::exp (-1.0f / std::max (0.001f * p.attackMs  * fScale * frameRate, 1.0e-3f));
            cRel[(size_t) k] = std::exp (-1.0f / std::max (0.001f * p.releaseMs * fScale * frameRate, 1.0e-3f));
        }
    }

    void updateContour (const Params& p)
    {
        if (p.contourDb == contourKey) return;
        contourKey = p.contourDb;
        for (int k = 0; k < bins; ++k)
            cContour[(size_t) k] = std::pow (10.0f, contourShapeDb (std::max (binHz[(size_t) k], 1.0f), p.contourDb) / 20.0f);
    }

    cpx& scratch (int i) { return scratchBuf[(size_t) i]; }

    double sr = 48000.0;
    int channels = 2, fftSize = 1024, bins = 513, hop = 256, pos = 0, clipHold = 0;
    std::vector<int> rev;
    std::vector<cpx> tw, scratchBuf;
    std::vector<float> window, binHz, levelSum, redLink, work, work2;
    std::vector<double> prefix;
    float ifftNorm = 1.0f / 1024.0f;
   #if RELAXXER_USE_JUCE_FFT
    std::unique_ptr<juce::dsp::FFT> jfft;
    std::vector<cpx> fftTmp;
   #endif
    std::array<Ranges, 6> rangeCache;
    unsigned rangeNext = 0;
    std::vector<float> pow0, pow1, cFocus, cRange, cAtt, cRel;
    std::array<float, 9> cacheKey { -1, -1, -1, -1, -1, -1, -1, -1, -1 };
    float tcAtt = -1, tcRel = -1, tcRate = -1;
    std::vector<float> cContour, cLoud, boostWork;
    float contourKey = 1.0e9f, makeupDb = 0.0f, inMs = 0.0f, outMs = 0.0f, brightIn = 0.0f, brightOut = 0.0f;
    std::atomic<float> meterIn { -120.0f }, meterOut { -120.0f }, meterBright { 0.0f }, meterMakeup { 0.0f };
    std::vector<float> inBuf[2], scBuf[2], outAcc[2], outBuf[2], levelDb[2], redOwn[2], redSmooth[2];
    std::vector<cpx> spec[2];
    std::array<std::atomic<float>, displayPoints> dispLevel {}, dispRed {};
    std::array<float, displayPoints> dispSmooth {};
    std::atomic<float> maxRedDb { 0.0f };
};
} // namespace relaxxer
