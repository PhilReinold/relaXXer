// Test des DSP-Kerns: g++ -std=c++17 -O2 -fsanitize=address,undefined -ISource Tests/dsp_test.cpp -o dsp_test
#include "RelaxxerDSP.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <random>
#include <vector>

using Buf = std::vector<float>;
static const double PI = 3.141592653589793;

static Buf noise (size_t n, float db, unsigned seed)
{
    std::mt19937 g (seed); std::normal_distribution<float> d (0.0f, 1.0f);
    Buf v (n); const float a = std::pow (10.0f, db / 20.0f);
    for (auto& x : v) x = a * d (g);
    return v;
}
static void addSine (Buf& v, float hz, float db, double sr)
{
    const float a = std::pow (10.0f, db / 20.0f) * 1.41421356f;
    for (size_t i = 0; i < v.size(); ++i) v[i] += a * (float) std::sin (2.0 * PI * hz * (double) i / sr);
}
// Pegel einer Frequenz (Goertzel-ähnlich per DFT-Bin) in dB
static float toneDb (const Buf& v, size_t a, size_t b, float hz, double sr)
{
    double re = 0, im = 0;
    for (size_t i = a; i < b; ++i) { const double w = 0.5 - 0.5 * std::cos (2 * PI * (double) (i - a) / (double) (b - a)); re += v[i] * w * std::cos (2 * PI * hz * i / sr); im += v[i] * w * std::sin (2 * PI * hz * i / sr); }
    return 10.0f * (float) std::log10 ((re * re + im * im) / ((b - a) * (b - a)) + 1e-30);
}
static float rmsDb (const Buf& v, size_t a, size_t b) { double s = 0; for (size_t i = a; i < b; ++i) s += (double) v[i] * v[i]; return 10.0f * (float) std::log10 (s / (double) (b - a) + 1e-30); }

// Prozessor über Blöcke wechselnder Größe laufen lassen
static void run (relaxxer::Processor& r, Buf& l, Buf* rgt, const relaxxer::Params& p, const Buf* sc = nullptr)
{
    size_t i = 0; unsigned blk = 7;
    while (i < l.size())
    {
        const int n = (int) std::min<size_t> (l.size() - i, 1 + (blk = blk * 1103515245u + 12345u) % 700);
        float* io[2] = { l.data() + i, rgt ? rgt->data() + i : nullptr };
        const float* s[2] = { sc ? sc->data() + i : nullptr, sc ? sc->data() + i : nullptr };
        r.process (io, sc ? s : nullptr, sc ? 1 : 0, n, p);
        i += (size_t) n;
    }
}

int main()
{
    bool ok = true;
    const double sr = 48000.0;
    const size_t N = (size_t) (sr * 3);

    relaxxer::Processor probe; probe.prepare (sr, 1);
    const int lat = probe.getLatencySamples(), latHQ = lat;

    // 1) Transparenz: Tiefe 0 => Ausgang = Eingang (verzögert um die gemeldete Latenz)
    for (int hq = 0; hq < 2; ++hq)
    {
        relaxxer::Processor r; r.prepare (sr, 2);
        relaxxer::Params p; p.depth = 0.0f; p.highQuality = hq;
        Buf in = noise (N, -20, 1), l = in, rr = in;
        run (r, l, &rr, p);
        const int L = hq ? latHQ : lat; double err = 0, ref = 0;
        for (size_t i = 4096; i < N; ++i) { const double d = l[i] - in[i - (size_t) L]; err += d * d; ref += (double) in[i - (size_t) L] * in[i - (size_t) L]; }
        const double e = 10 * std::log10 (err / ref + 1e-30);
        std::printf ("1) Transparenz (%s, Latenz %d Samples = %.1f ms): Fehler %.1f dB (erwartet < -90)\n", hq ? "Hoch" : "Normal", L, 1000.0 * L / sr, e);
        ok &= e < -90.0;
    }

    // 2) Resonanz: Rauschen + starker 3-kHz-Ton -> Ton deutlich leiser, Rauschen kaum verändert
    {
        relaxxer::Processor r; r.prepare (sr, 1);
        relaxxer::Params p; p.depth = 8.0f;
        Buf in = noise (N, -30, 2); addSine (in, 3000.0f, -12.0f, sr);
        Buf out = in; run (r, out, nullptr, p);
        const size_t a = N / 2, b = N;
        const float toneRed = toneDb (in, a - lat, b - lat, 3000.0f, sr) - toneDb (out, a, b, 3000.0f, sr);
        const float nIn = toneDb (in, a - lat, b - lat, 700.0f, sr), nOut = toneDb (out, a, b, 700.0f, sr);
        const float nIn2 = toneDb (in, a - lat, b - lat, 9000.0f, sr), nOut2 = toneDb (out, a, b, 9000.0f, sr);
        std::printf ("2) Resonanz 3 kHz: -%.1f dB | Rauschen bei 700 Hz %+.1f dB, 9 kHz %+.1f dB (erwartet Ton > 6 dB leiser, Rest < 1,5 dB)\n",
                     toneRed, nOut - nIn, nOut2 - nIn2);
        ok &= toneRed > 6.0f && std::fabs (nOut - nIn) < 1.5f && std::fabs (nOut2 - nIn2) < 1.5f;
    }

    // 3) Selektivität: höher = weniger Eingriff bei mäßiger Resonanz
    {
        float red[2];
        for (int i = 0; i < 2; ++i)
        {
            relaxxer::Processor r; r.prepare (sr, 1);
            relaxxer::Params p; p.depth = 8.0f; p.selectivity = i ? 9.0f : 1.0f;
            Buf in = noise (N, -30, 3); addSine (in, 2000.0f, -38.0f, sr);
            Buf out = in; run (r, out, nullptr, p);
            red[i] = toneDb (in, N / 2 - lat, N - lat, 2000.0f, sr) - toneDb (out, N / 2, N, 2000.0f, sr);
        }
        std::printf ("3) Selektivität 1 vs 9: Absenkung %.1f dB vs %.1f dB (erwartet: niedrig > hoch)\n", red[0], red[1]);
        ok &= red[0] > red[1] + 1.0f;
    }

    // 4) Delta + Mix: Normal + Delta = Original; Mix 0 = Original
    {
        relaxxer::Params p; p.depth = 10.0f;
        Buf in = noise (N, -30, 4); addSine (in, 1500.0f, -10.0f, sr);
        Buf a = in, b = in, c = in;
        { relaxxer::Processor r; r.prepare (sr, 1); run (r, a, nullptr, p); }
        { relaxxer::Processor r; r.prepare (sr, 1); auto q = p; q.delta = true; run (r, b, nullptr, q); }
        { relaxxer::Processor r; r.prepare (sr, 1); auto q = p; q.mix = 0.0f; run (r, c, nullptr, q); }
        double e1 = 0, e2 = 0, ref = 0;
        for (size_t i = 4096; i < N; ++i) { const double o = in[i - (size_t) lat]; e1 += (a[i] + b[i] - o) * (a[i] + b[i] - o); e2 += (c[i] - o) * (c[i] - o); ref += o * o; }
        const double d1 = 10 * std::log10 (e1 / ref + 1e-30), d2 = 10 * std::log10 (e2 / ref + 1e-30);
        std::printf ("4) Normal+Delta-Original: %.1f dB | Mix 0 - Original: %.1f dB (erwartet < -90)\n", d1, d2);
        ok &= d1 < -90 && d2 < -90;
    }

    // 5) Sidechain: Resonanz im Sidechain steuert die Absenkung im Hauptsignal
    {
        relaxxer::Processor r; r.prepare (sr, 1);
        relaxxer::Params p; p.depth = 10.0f; p.useSidechain = true;
        Buf in = noise (N, -30, 5), sc = noise (N, -30, 6); addSine (sc, 2500.0f, -10.0f, sr);
        Buf out = in; run (r, out, nullptr, p, &sc);
        const float red = toneDb (in, N / 2 - lat, N - lat, 2500.0f, sr) - toneDb (out, N / 2, N, 2500.0f, sr);
        std::printf ("5) Sidechain-Resonanz 2,5 kHz -> Hauptsignal dort -%.1f dB (erwartet > 4)\n", red);
        ok &= red > 4.0f;
    }

    // 6) Arbeitsbereich: Resonanz außerhalb (High Cut 2 kHz) bleibt unberührt
    {
        relaxxer::Processor r; r.prepare (sr, 1);
        relaxxer::Params p; p.depth = 10.0f; p.highCutHz = 2000.0f;
        Buf in = noise (N, -30, 7); addSine (in, 6000.0f, -10.0f, sr);
        Buf out = in; run (r, out, nullptr, p);
        const float red = toneDb (in, N / 2 - lat, N - lat, 6000.0f, sr) - toneDb (out, N / 2, N, 6000.0f, sr);
        std::printf ("6) Resonanz 6 kHz bei High Cut 2 kHz: -%.2f dB (erwartet ~0)\n", red);
        ok &= std::fabs (red) < 0.5f;
    }

    // 7) Fokus-Band: Empfindlichkeit -12 dB um 3 kHz schwächt den Eingriff dort ab
    {
        float red[2];
        for (int i = 0; i < 2; ++i)
        {
            relaxxer::Processor r; r.prepare (sr, 1);
            relaxxer::Params p; p.depth = 8.0f; p.band2Hz = 3000.0f; p.band2Db = i ? -12.0f : 0.0f;
            Buf in = noise (N, -30, 8); addSine (in, 3000.0f, -40.0f, sr);
            Buf out = in; run (r, out, nullptr, p);
            red[i] = toneDb (in, N / 2 - lat, N - lat, 3000.0f, sr) - toneDb (out, N / 2, N, 3000.0f, sr);
        }
        std::printf ("7) Fokus-Band 3 kHz 0 dB vs -12 dB: Absenkung %.1f vs %.1f dB\n", red[0], red[1]);
        ok &= red[0] > red[1] + 2.0f;
    }

    // 8) Stereo M/S + Link 0: nur die Seite hat eine Resonanz -> Mitte bleibt (fast) unberührt
    {
        relaxxer::Processor r; r.prepare (sr, 2);
        relaxxer::Params p; p.depth = 10.0f; p.midSide = true; p.link = 0.0f;
        Buf m = noise (N, -30, 9), s = noise (N, -40, 10); addSine (s, 4000.0f, -12.0f, sr);
        Buf l (N), rr (N); for (size_t i = 0; i < N; ++i) { l[i] = m[i] + s[i]; rr[i] = m[i] - s[i]; }
        Buf ol = l, orr = rr; run (r, ol, &orr, p);
        Buf om (N), os (N); for (size_t i = 0; i < N; ++i) { om[i] = 0.5f * (ol[i] + orr[i]); os[i] = 0.5f * (ol[i] - orr[i]); }
        const float sRed = toneDb (s, N / 2 - lat, N - lat, 4000.0f, sr) - toneDb (os, N / 2, N, 4000.0f, sr);
        const float mChg = rmsDb (om, N / 2, N) - rmsDb (m, N / 2 - lat, N - lat);
        std::printf ("8) M/S, Link 0: Seite bei 4 kHz -%.1f dB, Mitte %+.2f dB (erwartet Seite > 4, Mitte < 0,5)\n", sRed, mChg);
        ok &= sRed > 4.0f && std::fabs (mChg) < 0.5f;
    }

    // 9) Robustheit: alle Sample-Raten, Mono/Stereo, NaN/Inf, Riesenpegel, Extrem-Parameter, Qualitätswechsel
    {
        std::mt19937 g (42); std::uniform_real_distribution<float> u (0.0f, 1.0f);
        auto rnd = [&] (float a, float b) { return a + (b - a) * u (g); };
        const double rates[] = { 8000, 22050, 44100, 48000, 88200, 96000, 192000 };
        bool finiteAll = true; float maxAbs = 0;
        for (double rate : rates)
            for (int chs = 1; chs <= 2; ++chs)
            {
                relaxxer::Processor r; r.prepare (rate, chs, chs == 2 && rate > 40000);
                for (int it = 0; it < 8; ++it)
                {
                    relaxxer::Params p;
                    p.depth = rnd (-5, 40); p.sharpness = rnd (-5, 20); p.selectivity = rnd (-5, 20);
                    p.attackMs = rnd (-10, 1000); p.releaseMs = rnd (-10, 5000); p.hardMode = it & 1; p.midSide = it & 2;
                    p.link = rnd (-1, 2); p.balance = rnd (-2, 2); p.mix = rnd (-1, 2); p.trimDb = rnd (-40, 40);
                    p.delta = it == 5; p.useSidechain = it & 4; p.highQuality = it & 1;
                    p.lowCutHz = rnd (-100, 30000); p.highCutHz = rnd (-100, 30000);
                    p.band1Hz = rnd (-10, 50000); p.band1Db = rnd (-60, 60); p.band1Q = rnd (-1, 30);
                    p.recover = rnd (-1, 3); p.brighten = rnd (-3, 3); p.contourDb = rnd (-30, 30); p.autoGain = it & 2;
                    if (it == 7) { p.depth = std::numeric_limits<float>::quiet_NaN(); p.mix = std::numeric_limits<float>::infinity(); }
                    const int n = 3000 + (int) (u (g) * 4000);
                    Buf a (n), b (n), sc (n);
                    for (int i = 0; i < n; ++i)
                    {
                        float v = rnd (-1, 1);
                        switch (it % 4) { case 1: v *= 1.0e6f; break; case 2: if (i % 89 == 0) v = std::numeric_limits<float>::quiet_NaN(); break;
                                          case 3: if (i % 97 == 0) v = -std::numeric_limits<float>::infinity(); break; default: break; }
                        a[i] = v; b[i] = -v; sc[i] = (it == 6) ? std::numeric_limits<float>::quiet_NaN() : rnd (-1, 1);
                    }
                    float* io[2] = { a.data(), chs > 1 ? b.data() : nullptr };
                    const float* s[2] = { sc.data(), sc.data() };
                    r.process (io, s, 2, n, p);
                    for (int i = 0; i < n; ++i) { finiteAll &= std::isfinite (a[i]); maxAbs = std::max (maxAbs, std::fabs (a[i])); if (chs > 1) { finiteAll &= std::isfinite (b[i]); maxAbs = std::max (maxAbs, std::fabs (b[i])); } }
                    for (int i = 0; i < relaxxer::Processor::displayPoints; ++i) finiteAll &= std::isfinite (r.getDisplayLevel (i)) && std::isfinite (r.getDisplayReduction (i));
                }
            }
        std::printf ("9) Fuzz: alles endlich = %d, max |Ausgang| = %.2f (Sicherung %.1f)\n", (int) finiteAll, maxAbs, relaxxer::Processor::safetyClamp);
        ok &= finiteAll && maxAbs <= relaxxer::Processor::safetyClamp + 1e-4f;
    }


    // 11) Live-Modus: ~5 ms Latenz und trotzdem transparent
    {
        relaxxer::Processor r; r.prepare (sr, 2, true);
        relaxxer::Params p; p.depth = 0.0f;
        Buf in = noise (N, -20, 21), l = in, rr = in;
        run (r, l, &rr, p);
        const int L = r.getLatencySamples(); double err = 0, ref = 0;
        for (size_t i = 4096; i < N; ++i) { const double d = l[i] - in[i - (size_t) L]; err += d * d; ref += (double) in[i - (size_t) L] * in[i - (size_t) L]; }
        const double e = 10 * std::log10 (err / ref + 1e-30);
        relaxxer::Processor r2; r2.prepare (sr, 1, true);
        relaxxer::Params p2; p2.depth = 8.0f;
        Buf in2 = noise (N, -30, 22); addSine (in2, 3000.0f, -12.0f, sr);
        Buf o2 = in2; run (r2, o2, nullptr, p2);
        const float red = toneDb (in2, N / 2 - L, N - L, 3000.0f, sr) - toneDb (o2, N / 2, N, 3000.0f, sr);
        std::printf ("11) Live: Latenz %d Samples = %.1f ms, Transparenz %.1f dB, Resonanz 3 kHz -%.1f dB\n", L, 1000.0 * L / sr, e, red);
        ok &= L <= 300 && e < -90.0 && red > 4.0f;
    }

    // 12) Freilegen (Recover): ein Loch im Spektrum (verdeckter Bereich) wird angehoben
    {
        float lift[2];
        for (int i = 0; i < 2; ++i)
        {
            relaxxer::Processor r; r.prepare (sr, 1);
            relaxxer::Params p; p.depth = 0.0f; p.recover = i ? 1.0f : 0.0f;
            // dichter Multiton (Rauschen-ähnlich) mit einem Loch von 1,6 bis 2,5 kHz
            Buf in (N, 0.0f);
            std::mt19937 gg (23); std::uniform_real_distribution<float> ph (0.0f, 6.2831853f);
            for (double f = 101.3; f < 10000.0; f += 37.7)
            {
                if (f > 1600.0 && f < 2500.0) continue;
                const float phase = ph (gg), amp = 0.0015f;
                const double w = 2 * PI * f / sr;
                for (size_t k = 0; k < N; ++k) in[k] += amp * (float) std::sin (w * (double) k + phase);
            }
            addSine (in, 2010.0f, -62.0f, sr); // leiser Rest im Loch (der "verdeckte" Anteil)
            Buf out = in; run (r, out, nullptr, p);
            lift[i] = toneDb (out, N / 2, N, 2010.0f, sr) - toneDb (in, N / 2 - lat, N - lat, 2010.0f, sr);
            if (i == 0) std::printf ("    (Loch-Tiefe: %.1f dB unter 1 kHz)\n", toneDb (in, N / 2, N, 1006.4f, sr) - toneDb (in, N / 2, N, 2010.0f, sr));
        }
        std::printf ("12) Freilegen: Loch bei 2 kHz %+.1f dB (aus) vs %+.1f dB (an)\n", lift[0], lift[1]);
        ok &= std::fabs (lift[0]) < 0.5f && lift[1] > 1.0f && lift[1] <= 6.5f;
    }

    // 13) Lautheit halten: nach starker Absenkung bleibt der gewichtete Pegel fast gleich
    {
        float diff[2];
        for (int i = 0; i < 2; ++i)
        {
            relaxxer::Processor r; r.prepare (sr, 1);
            relaxxer::Params p; p.depth = 14.0f; p.selectivity = 1.0f; p.autoGain = i == 1;
            Buf in = noise (N, -30, 24); addSine (in, 2500.0f, -16.0f, sr); addSine (in, 5000.0f, -18.0f, sr);
            Buf out = in; run (r, out, nullptr, p);
            diff[i] = r.getOutputDb() - r.getInputDb();
        }
        std::printf ("13) Lautheit halten: Pegeländerung aus %+.1f dB, an %+.1f dB\n", diff[0], diff[1]);
        ok &= diff[0] < -1.0f && std::fabs (diff[1]) < 0.7f;
    }

    // 14) Kontur + Hell/Dunkel: Kontur hebt Bass relativ zu Mitten; Brighten macht das Ergebnis heller
    {
        relaxxer::Processor r; r.prepare (sr, 1);
        relaxxer::Params p; p.depth = 0.0f; p.contourDb = 6.0f;
        Buf in = noise (N, -30, 25); Buf out = in; run (r, out, nullptr, p);
        const float bass = toneDb (out, N / 2, N, 60.0f, sr) - toneDb (in, N / 2 - lat, N - lat, 60.0f, sr);
        const float mid  = toneDb (out, N / 2, N, 1500.0f, sr) - toneDb (in, N / 2 - lat, N - lat, 1500.0f, sr);
        float bright[2];
        for (int i = 0; i < 2; ++i)
        {
            relaxxer::Processor q; q.prepare (sr, 1);
            relaxxer::Params pp; pp.depth = 10.0f; pp.selectivity = 0.5f; pp.brighten = i ? 1.0f : -1.0f;
            Buf x = noise (N, -30, 26); addSine (x, 200.0f, -20.0f, sr); addSine (x, 6000.0f, -20.0f, sr);
            Buf y = x; run (q, y, nullptr, pp);
            bright[i] = q.getBrightnessDeltaDb();
        }
        std::printf ("14) Kontur +6: Bass %+.1f dB, Mitten %+.1f dB | Hell/Dunkel -100%%: %+.2f dB, +100%%: %+.2f dB Helligkeit\n", bass, mid, bright[0], bright[1]);
        ok &= bass > mid + 3.0f && bright[1] > bright[0] + 0.5f;
    }

    // 10) Rechenlast
    {
        relaxxer::Processor r; r.prepare (48000.0, 2);
        relaxxer::Params p;
        Buf a = noise (512, -20, 12), b = a;
        const int blocks = 48000 * 20 / 512;
        auto t0 = std::chrono::steady_clock::now();
        for (int k = 0; k < blocks; ++k) { float* io[2] = { a.data(), b.data() }; r.process (io, nullptr, 0, 512, p); }
        const double sec = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        std::printf ("10) Last (typisch: Stereo, 48 kHz, Normal): %.1f %% eines Kerns (ohne vDSP)\n", sec / 20.0 * 100.0);
    }
    {
        relaxxer::Processor r; r.prepare (96000.0, 2);
        relaxxer::Params p; p.highQuality = true; p.link = 0.5f;
        Buf a = noise (512, -20, 11), b = a;
        const int blocks = 96000 * 20 / 512;
        auto t0 = std::chrono::steady_clock::now();
        for (int k = 0; k < blocks; ++k) { float* io[2] = { a.data(), b.data() }; r.process (io, nullptr, 0, 512, p); }
        const double sec = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        std::printf ("10) Last (worst case: Stereo, 96 kHz, Hoch, Link 50%%): %.1f %% eines Kerns\n", sec / 20.0 * 100.0);
    }

    std::printf (ok ? "ALLE TESTS OK\n" : "TEST FEHLGESCHLAGEN\n");
    return ok ? 0 : 1;
}
