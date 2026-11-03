#include "AudioAnalysis.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <cctype>
#include <cstdlib>

namespace tessera {

namespace {

/// A sample as a 16-bit peak value.
int16_t quantize(float x) { return int16_t(std::lround(std::clamp(x, -1.f, 1.f) * 32767.f)); }

/// The average of every channel, one value per frame.
std::vector<float> mixToMono(const std::vector<std::vector<float>>& channels) {
    if (channels.empty())
        return {};
    std::vector<float> mono(channels[0].size(), 0.f);
    float scale = 1.f / float(channels.size());
    for (const auto& ch : channels)
        for (size_t i = 0; i < mono.size() && i < ch.size(); i++)
            mono[i] += ch[i] * scale;
    return mono;
}

/// Where a peak really is, between samples: the offset (-0.5..0.5) of a parabola through three values.
double parabolicOffset(double a, double b, double c) {
    double d = a - 2 * b + c;
    return d < 0 ? std::clamp(0.5 * (a - c) / d, -0.5, 0.5) : 0.0;
}

/// The weakest onset that counts: loudness and brightness together rising about 5 dB within one hop. The ripple of
/// a steady chord reaches about 0.85 (measured on tests/audio/pad.flac) and stays below it.
constexpr double kMinOnset = 1.2;
/// Hops per loudness window: 30 ms, long enough that a chord's own waveform (down to 55 Hz) doesn't flicker.
constexpr int64_t kOnsetWindow = 6;

/// Local maxima of the onset curve that stand out (above mean + 1.5 standard deviations, and above kMinOnset), at
/// least 50 ms apart, in seconds, with their strength.
std::vector<std::pair<double, float>> onsetPeaks(const OnsetCurve& curve) {
    const std::vector<float>& s = curve.strength;
    std::vector<std::pair<double, float>> out;
    if (s.size() < 3)
        return out;
    double mean = std::accumulate(s.begin(), s.end(), 0.0) / double(s.size());
    double var = 0;
    for (float v : s)
        var += (v - mean) * (v - mean);
    double sd = std::sqrt(var / double(s.size()));
    double threshold = std::max(mean + 1.5 * sd, kMinOnset);
    size_t gap = size_t(std::ceil(0.05 / curve.hop));
    for (size_t i = 1; i + 1 < s.size(); i++) {
        if (s[i] <= threshold || s[i] <= s[i - 1] || s[i] < s[i + 1])
            continue;
        // Value i compares window i (kOnsetWindow hops from hop i) with the one before: a sound starting at the
        // window's last hop first shows there.
        double t = (double(i + kOnsetWindow - 1) + parabolicOffset(s[i - 1], s[i], s[i + 1])) * curve.hop;
        if (!out.empty() && t - out.back().first < double(gap) * curve.hop) {
            if (s[i] > out.back().second)
                out.back() = {t, s[i]};
            continue;
        }
        out.push_back({t, s[i]});
    }
    return out;
}

/// Whether `a` and `b` are the same tempo, or one is double the other, within 4%.
bool relatedTempo(double a, double b) {
    for (double f : {1.0, 2.0, 0.5})
        if (std::abs(a * f - b) <= 0.04 * b)
            return true;
    return false;
}

} // namespace

// ---- Peaks --------------------------------------------------------------------------------------------------

PeakCache::PeakCache(const std::vector<std::vector<float>>& channels) {
    if (channels.empty() || channels[0].empty())
        return;
    length = int64_t(channels[0].size());
    std::vector<std::vector<int16_t>> base;
    for (const auto& ch : channels) {
        size_t bins = (ch.size() + kBaseBin - 1) / kBaseBin;
        std::vector<int16_t> mm(bins * 2);
        for (size_t b = 0; b < bins; b++) {
            size_t from = b * kBaseBin, to = std::min(ch.size(), from + kBaseBin);
            auto [lo, hi] = std::minmax_element(ch.begin() + long(from), ch.begin() + long(to));
            mm[2 * b] = quantize(*lo);
            mm[2 * b + 1] = quantize(*hi);
        }
        base.push_back(std::move(mm));
    }
    levels.push_back(std::move(base));
    while (levels.back()[0].size() / 2 > 1) {
        std::vector<std::vector<int16_t>> next;
        for (const auto& mm : levels.back()) {
            size_t bins = (mm.size() / 2 + kLevelStep - 1) / kLevelStep;
            std::vector<int16_t> up(bins * 2);
            for (size_t b = 0; b < bins; b++) {
                int16_t lo = INT16_MAX, hi = INT16_MIN;
                for (size_t k = b * kLevelStep; k < std::min(mm.size() / 2, (b + 1) * kLevelStep); k++) {
                    lo = std::min(lo, mm[2 * k]);
                    hi = std::max(hi, mm[2 * k + 1]);
                }
                up[2 * b] = lo;
                up[2 * b + 1] = hi;
            }
            next.push_back(std::move(up));
        }
        levels.push_back(std::move(next));
    }
}

void PeakCache::range(int ch, double from, double to, float& lo, float& hi) const {
    lo = hi = 0;
    if (levels.empty() || ch < 0 || ch >= channels())
        return;
    from = std::max(0.0, from);
    to = std::min(double(length), to);
    if (to <= from)
        return;
    // The coarsest level whose bins are no longer than the span asked for.
    size_t level = 0;
    double bin = kBaseBin;
    while (level + 1 < levels.size() && bin * kLevelStep <= to - from) {
        level++;
        bin *= kLevelStep;
    }
    const std::vector<int16_t>& mm = levels[level][size_t(ch)];
    size_t a = size_t(from / bin), b = std::max(a + 1, size_t(std::ceil(to / bin)));
    b = std::min(b, mm.size() / 2);
    int16_t l = INT16_MAX, h = INT16_MIN;
    for (size_t k = a; k < b; k++) {
        l = std::min(l, mm[2 * k]);
        h = std::max(h, mm[2 * k + 1]);
    }
    if (l > h)
        return;
    lo = float(l) / 32767.f;
    hi = float(h) / 32767.f;
}

size_t PeakCache::bytes() const {
    size_t n = 0;
    for (const auto& level : levels)
        for (const auto& mm : level)
            n += mm.size() * sizeof(int16_t);
    return n;
}

// ---- Tempo --------------------------------------------------------------------------------------------------

double tempoFromName(const std::string& name) {
    std::string s = name;
    for (char& c : s)
        c = char(std::tolower(static_cast<unsigned char>(c)));
    auto isDigit = [&](size_t i) { return i < s.size() && std::isdigit(static_cast<unsigned char>(s[i])); };
    auto isGap = [&](size_t i) { return i < s.size() && (s[i] == ' ' || s[i] == '_' || s[i] == '-'); };
    // A number of two or three digits (with an optional fraction) that "bpm" follows or comes before, with at
    // most one space, underscore or dash in between.
    auto numberAt = [&](size_t i, size_t& end) {
        size_t j = i;
        while (isDigit(j))
            j++;
        if (j - i < 2 || j - i > 3 || (i > 0 && isDigit(i - 1)))
            return 0.0;
        std::string digits = s.substr(i, j - i);
        if (j + 1 < s.size() && (s[j] == '.' || s[j] == ',') && isDigit(j + 1)) {
            size_t k = j + 1;
            while (isDigit(k))
                k++;
            digits += "." + s.substr(j + 1, k - j - 1);
            j = k;
        }
        end = j;
        return std::atof(digits.c_str());
    };
    for (size_t i = 0; i < s.size(); i++) {
        if (!isDigit(i) || (i > 0 && isDigit(i - 1)))
            continue;
        size_t end = 0;
        double bpm = numberAt(i, end);
        if (bpm == 0)
            continue;
        size_t after = isGap(end) ? end + 1 : end;
        size_t before = i >= 1 && isGap(i - 1) ? i - 1 : i;
        bool tagged = s.compare(after, 3, "bpm") == 0 || (before >= 3 && s.compare(before - 3, 3, "bpm") == 0);
        if (tagged && bpm >= 40 && bpm <= 300)
            return bpm;
    }
    return 0;
}

double tempoFromLength(double seconds, double songTempo, TimeSig ts, int* beats) {
    if (!(seconds > 0))
        return 0;
    static const int kFours[] = {1, 2, 4, 8, 16, 32, 64};
    static const int kThrees[] = {3, 6, 12, 24, 48};
    bool threes = ts.num == 3 || ts.num == 6 || ts.num == 9;
    double best = 0;
    int bestBeats = 0;
    auto consider = [&](int n) {
        double bpm = 60.0 * n / seconds;
        if (bpm < 70 || bpm > 180)
            return;
        if (best == 0 || std::abs(bpm - songTempo) < std::abs(best - songTempo)) {
            best = bpm;
            bestBeats = n;
        }
    };
    if (threes)
        for (int n : kThrees)
            consider(n);
    else
        for (int n : kFours)
            consider(n);
    if (beats)
        *beats = bestBeats;
    return best;
}

AudioGuess quickGuess(const std::string& name, double fileTempo, double seconds, double songTempo, TimeSig ts) {
    AudioGuess g;
    g.loop = seconds <= 30;
    if (fileTempo >= 40 && fileTempo <= 300) {
        g.bpm = fileTempo;
        g.source = TempoSource::File;
    } else if (double n = tempoFromName(name); n > 0) {
        g.bpm = n;
        g.source = TempoSource::Name;
    } else if (double l = tempoFromLength(seconds, songTempo, ts, &g.beats); l > 0 && seconds <= 30) {
        g.bpm = l;
        g.source = TempoSource::Length;
    }
    return g;
}

OnsetCurve onsetCurve(const std::vector<std::vector<float>>& channels, double sampleRate) {
    OnsetCurve curve;
    std::vector<float> mono = mixToMono(channels);
    int64_t hop = std::max<int64_t>(1, std::llround(sampleRate * OnsetCurve::kHopSeconds));
    curve.hop = double(hop) / sampleRate;
    int64_t n = int64_t(mono.size()) / hop;
    if (n < 2)
        return curve;
    // Loudness and brightness (the energy of the sample-to-sample change) per window of kOnsetWindow hops.
    std::vector<double> loud(static_cast<size_t>(n)), bright(static_cast<size_t>(n));
    double most = 0, mostBright = 0;
    for (int64_t f = 0; f < n; f++) {
        double e = 0, b = 0;
        int64_t from = f * hop, to = std::min<int64_t>(int64_t(mono.size()), from + kOnsetWindow * hop);
        for (int64_t i = from; i < to; i++) {
            double x = mono[size_t(i)];
            double d = i > 0 ? x - mono[size_t(i - 1)] : 0;
            e += x * x;
            b += d * d;
        }
        loud[size_t(f)] = e;
        bright[size_t(f)] = b;
        most = std::max(most, e);
        mostBright = std::max(mostBright, b);
    }
    // A floor 60 dB under the loudest window, so changes in the noise floor don't count as onsets.
    double floor = most * 1e-6 + 1e-20, floorBright = mostBright * 1e-6 + 1e-20;
    curve.strength.assign(size_t(n), 0.f);
    for (int64_t f = 1; f < n; f++) {
        double rise = std::log(loud[size_t(f)] + floor) - std::log(loud[size_t(f - 1)] + floor);
        double riseBright = std::log(bright[size_t(f)] + floorBright) - std::log(bright[size_t(f - 1)] + floorBright);
        curve.strength[size_t(f)] = float(std::max(0.0, rise) + std::max(0.0, riseBright));
    }
    return curve;
}

double tempoFromOnsets(const OnsetCurve& curve, double* firstBeat) {
    const std::vector<float>& s = curve.strength;
    size_t n = s.size();
    double hop = curve.hop;
    size_t minLag = size_t(std::floor(60.0 / (200 * hop))), maxLag = size_t(std::ceil(60.0 / (60 * hop)));
    if (n < 2 * maxLag + 2)
        return 0;

    // Autocorrelation of the onsets over the lags of 60 to 200 BPM, weighted gently towards 120.
    std::vector<double> acf(maxLag + 2, 0.0);
    for (size_t lag = minLag; lag <= maxLag + 1; lag++) {
        double sum = 0;
        for (size_t i = 0; i + lag < n; i++)
            sum += double(s[i]) * double(s[i + lag]);
        acf[lag] = sum / double(n - lag);
    }
    size_t bestLag = 0;
    double bestScore = 0;
    std::vector<double> scores;
    for (size_t lag = minLag + 1; lag <= maxLag; lag++) {
        double bpm = 60.0 / (double(lag) * hop);
        double octaves = std::log2(bpm / 120);
        double score = acf[lag] * std::exp(-0.5 * octaves * octaves);
        scores.push_back(score);
        bool peak = acf[lag] >= acf[lag - 1] && acf[lag] >= acf[lag + 1];
        if (peak && score > bestScore) {
            bestScore = score;
            bestLag = lag;
        }
    }
    if (bestLag == 0 || scores.empty())
        return 0;
    std::nth_element(scores.begin(), scores.begin() + long(scores.size() / 2), scores.end());
    double median = scores[scores.size() / 2];
    // A steady sound has a flat autocorrelation: no beat to find.
    if (!(bestScore > 1.5 * median))
        return 0;
    double lag = double(bestLag) + parabolicOffset(acf[bestLag - 1], acf[bestLag], acf[bestLag + 1]);
    double period = lag * hop;

    // Refine with the onsets themselves: number each one by the beat it is nearest, then fit a straight line
    // through time against beat number. Over a whole file that pins the tempo far finer than one hop.
    std::vector<std::pair<double, float>> peaks = onsetPeaks(curve);
    if (peaks.size() < 4)
        return 0;
    double anchor = peaks[0].first;
    float strongest = 0;
    for (const auto& [t, v] : peaks)
        if (t < peaks[0].first + 4 * period && v > strongest) {
            strongest = v;
            anchor = t;
        }
    double intercept = anchor;
    for (int pass = 0; pass < 3; pass++) {
        double sk = 0, st = 0, skk = 0, skt = 0;
        int count = 0;
        for (const auto& [t, v] : peaks) {
            double k = std::round((t - intercept) / period);
            double err = t - intercept - k * period;
            if (std::abs(err) > 0.12 * period)
                continue;
            sk += k;
            st += t;
            skk += k * k;
            skt += k * t;
            count++;
        }
        double det = count * skk - sk * sk;
        if (count < 3 || det <= 0)
            break;
        period = (count * skt - sk * st) / det;
        intercept = (st - period * sk) / count;
    }
    if (!(period > 0))
        return 0;
    double bpm = 60.0 / period;
    if (std::abs(bpm - std::round(bpm)) < 0.05)
        bpm = std::round(bpm);
    if (firstBeat) {
        double first = intercept - std::floor(intercept / period) * period;
        // A beat a hair before the file's start is the beat at its start.
        *firstBeat = first > period - 0.01 ? 0 : first;
    }
    return bpm >= 55 && bpm <= 210 ? bpm : 0;
}

AudioGuess fullGuess(const AudioGuess& quick, const std::vector<std::vector<float>>& channels, double sampleRate,
                     double songTempo) {
    AudioGuess g = quick;
    if (channels.empty() || channels[0].empty() || !(sampleRate > 0))
        return g;
    double seconds = double(channels[0].size()) / sampleRate;
    OnsetCurve curve = onsetCurve(channels, sampleRate);
    std::vector<std::pair<double, float>> peaks = onsetPeaks(curve);

    // How much of the onset strength sits in its sharpest 5%: near 1 for hits and clicks, low for pads and voices.
    std::vector<float> sorted = curve.strength;
    std::sort(sorted.begin(), sorted.end(), std::greater<float>());
    double total = std::accumulate(sorted.begin(), sorted.end(), 0.0);
    double top = std::accumulate(sorted.begin(), sorted.begin() + long(std::max<size_t>(1, sorted.size() / 20)), 0.0);
    g.sound = total > 0 && top / total > 0.6 ? SoundType::Drums : SoundType::FullMix;

    // A one-shot: short, one onset, and fading away.
    std::vector<float> mono = mixToMono(channels);
    auto rms = [&](size_t from, size_t to) {
        double e = 0;
        for (size_t i = from; i < to; i++)
            e += double(mono[i]) * mono[i];
        return to > from ? std::sqrt(e / double(to - from)) : 0.0;
    };
    size_t q = mono.size() / 4;
    bool decays = rms(mono.size() - q, mono.size()) < 0.25 * rms(0, q);
    if (seconds < 1.5 && peaks.size() <= 1 && decays) {
        g.oneShot = true;
        g.loop = false;
        g.bpm = songTempo;
        g.source = TempoSource::None;
        g.beats = 0;
        return g;
    }

    if (g.source == TempoSource::None || g.source == TempoSource::Length) {
        double first = 0;
        double onsets = tempoFromOnsets(curve, &first);
        if (onsets > 0) {
            g.firstBeat = first;
            // A loop's length gives an exact tempo; the onsets only overrule it when they beat at something else.
            if (g.source == TempoSource::None || !relatedTempo(onsets, g.bpm)) {
                g.bpm = onsets;
                g.source = TempoSource::Onsets;
                g.beats = 0;
            }
        }
    }
    return g;
}

int64_t nearestZeroCrossing(const std::vector<std::vector<float>>& channels, int64_t at, int64_t radius) {
    if (channels.empty())
        return at;
    int64_t n = int64_t(channels[0].size());
    auto mono = [&](int64_t i) {
        float sum = 0;
        for (const auto& ch : channels)
            sum += ch[size_t(i)];
        return sum;
    };
    for (int64_t d = 0; d <= radius; d++)
        for (int64_t i : {at - d, at + d}) {
            if (i <= 0 || i >= n)
                continue;
            float a = mono(i - 1), b = mono(i);
            if ((a <= 0 && b >= 0) || (a >= 0 && b <= 0))
                return std::abs(a) < std::abs(b) ? i - 1 : i;
        }
    return at;
}

} // namespace tessera
