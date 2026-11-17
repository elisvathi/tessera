#include "Ties.hpp"

#include "Automation.hpp"
#include "ExpressionEdits.hpp"
#include "NoteOperators.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace tessera::ties {

namespace {

/// Whether `a` may be continued by `b`: same channel, starts earlier, ends no sooner than `reach` before `b`
/// starts and before `b` ends.
bool canContinue(const Note& a, const Note& b, Tick reach) {
    return !a.muted && a.channel == b.channel && a.start < b.start && a.end() >= b.start - reach && a.end() < b.end();
}

/// Adds `offset(t)` (semitones, at clip tick t) to `n`'s pitch curve over [from, to], holding `offset(from)`
/// before and `offset(to)` after. Points inside are sampled so a bent glide stays bent.
template <typename Offset>
void addToPitchCurve(Note& n, Tick from, Tick to, bool bent, Offset offset) {
    double len = double(std::max<Tick>(1, n.length));
    auto posOf = [&](double t) { return std::clamp((t - double(n.start)) / len, 0.0, 1.0); };
    auto tickOf = [&](double pos) { return double(n.start) + pos * len; };
    double p0 = posOf(double(from)), p1 = posOf(double(to));
    std::vector<double> at;
    for (const ExprPoint& p : n.pitchExpr)
        at.push_back(p.pos);
    int steps = bent ? 8 : 1;
    for (int k = 0; k <= steps; k++)
        at.push_back(p0 + (p1 - p0) * k / steps);
    std::sort(at.begin(), at.end());
    at.erase(std::unique(at.begin(), at.end(), [](double a, double b) { return std::abs(a - b) < 1e-6; }), at.end());
    std::vector<ExprPoint> out;
    for (double pos : at) {
        double t = std::clamp(tickOf(pos), double(from), double(to));
        float own = expressionAt(n.pitchExpr, float(pos), 0);
        float tension = 0;
        for (const ExprPoint& p : n.pitchExpr)
            if (std::abs(p.pos - pos) < 1e-6 && (pos < p0 || pos > p1))
                tension = p.tension; // a point outside the glide keeps its bend
        out.push_back({float(pos), std::clamp(own + float(offset(t)), -48.f, 48.f), tension});
    }
    n.pitchExpr = std::move(out);
}

} // namespace

std::vector<int> partners(const NoteList& notes, Tick loopStart, Tick loopEnd) {
    std::vector<int> out(notes.size(), -1);
    std::vector<char> taken(notes.size(), 0);
    for (size_t i = 0; i < notes.size(); i++) {
        const Note& b = notes[i];
        if (!b.tied || b.muted)
            continue;
        int best = -1;
        for (size_t j = 0; j < notes.size(); j++) {
            if (j == i || taken[j] || !canContinue(notes[j], b, 0))
                continue;
            if (best < 0 || std::abs(notes[j].pitch - b.pitch) < std::abs(notes[size_t(best)].pitch - b.pitch))
                best = int(j);
        }
        if (best < 0 && loopEnd > loopStart && b.start == loopStart)
            for (size_t j = 0; j < notes.size(); j++) {
                const Note& a = notes[j];
                if (j == i || taken[j] || a.muted || a.channel != b.channel || a.start < loopStart ||
                    a.start >= loopEnd || a.end() < loopEnd)
                    continue;
                if (best < 0 || std::abs(a.pitch - b.pitch) < std::abs(notes[size_t(best)].pitch - b.pitch))
                    best = int(j);
            }
        if (best >= 0) {
            out[i] = best;
            taken[size_t(best)] = 1;
        }
    }
    return out;
}

std::vector<char> continued(const std::vector<int>& partners) {
    std::vector<char> out(partners.size(), 0);
    for (int p : partners)
        if (p >= 0)
            out[size_t(p)] = 1;
    return out;
}

Glide glideOf(const Note& a, const Note& b) {
    Glide g;
    g.joint = b.start;
    g.from = std::max(a.start, b.start - std::max<Tick>(0, b.glideBefore));
    g.to = std::min(b.end(), b.start + std::max<Tick>(0, b.glideAfter));
    g.p0 = a.pitch;
    g.p1 = b.pitch;
    g.shape = b.glideShape;
    return g;
}

double shaped(float shape, double x) { return curveShape(CurveType::Linear, shape, x); }

double pitchAt(const Glide& g, double t) {
    if (t <= double(g.from))
        return g.p0;
    if (t >= double(g.to))
        return g.p1;
    double x = (t - double(g.from)) / double(g.to - g.from);
    return g.p0 + (g.p1 - g.p0) * shaped(g.shape, x);
}

NoteList tie(const NoteList& notes, const IdSet& ids, IdSet* tiedIds) {
    NoteList out = notes;
    std::sort(out.begin(), out.end(), noteOrder);
    size_t chosen = 0;
    for (const Note& n : out)
        chosen += ids.count(n.id);
    // Notes already continued by a tie that stays are not free.
    std::vector<int> before = partners(out);
    std::vector<char> taken(out.size(), 0);
    for (size_t i = 0; i < out.size(); i++)
        if (before[i] >= 0 && !ids.count(out[i].id))
            taken[size_t(before[i])] = 1;
    for (size_t i = 0; i < out.size(); i++) {
        Note& b = out[i];
        if (!ids.count(b.id) || b.muted)
            continue;
        int best = -1;
        for (size_t j = 0; j < out.size(); j++) {
            const Note& a = out[j];
            if (j == i || taken[j] || !canContinue(a, b, kTieReach) || (chosen > 1 && !ids.count(a.id)))
                continue;
            if (best < 0 || std::abs(a.pitch - b.pitch) < std::abs(out[size_t(best)].pitch - b.pitch))
                best = int(j);
        }
        if (best < 0)
            continue;
        Note& a = out[size_t(best)];
        Tick oldStart = a.start, oldLength = a.length;
        a.length = b.start - a.start;
        if (a.length != oldLength)
            expr::keepInTime(a, oldStart, oldLength);
        b.tied = true;
        taken[size_t(best)] = 1;
        if (tiedIds)
            tiedIds->insert(b.id);
    }
    return out;
}

NoteList untie(const NoteList& notes, const IdSet& ids, bool keepGlide) {
    NoteList out = notes;
    std::sort(out.begin(), out.end(), noteOrder);
    std::vector<int> p = partners(out);
    for (size_t i = 0; i < out.size(); i++) {
        Note& b = out[i];
        if (!b.tied || !ids.count(b.id))
            continue;
        if (keepGlide && p[i] >= 0) {
            Note& a = out[size_t(p[i])];
            Glide g = glideOf(a, b);
            if (g.moves()) {
                bool bent = g.shape != 0;
                addToPitchCurve(a, g.from, g.joint, bent, [&](double t) { return pitchAt(g, t) - g.p0; });
                addToPitchCurve(b, g.joint, g.to, bent, [&](double t) { return pitchAt(g, t) - g.p1; });
            }
        }
        b.tied = false;
        b.glideBefore = b.glideAfter = 0;
        b.glideShape = 0;
    }
    return out;
}

NoteList tieHandovers(const NoteList& notes) {
    NoteList out = notes;
    for (int ch = 1; ch <= kMaxNoteChannel; ch++) {
        std::vector<size_t> line;
        for (size_t i = 0; i < out.size(); i++)
            if (out[i].channel == ch && !out[i].muted)
                line.push_back(i);
        // One line: each note overlaps at most the next, briefly, and ends before it.
        bool oneLine = line.size() > 1, handover = false;
        for (size_t k = 0; oneLine && k + 1 < line.size(); k++) {
            const Note& a = out[line[k]];
            const Note& b = out[line[k + 1]];
            if (k + 2 < line.size() && out[line[k + 2]].start < a.end())
                oneLine = false; // three at once: a chord
            if (b.start >= a.end())
                continue;
            Tick overlap = a.end() - b.start;
            if (b.start <= a.start || a.end() >= b.end() || overlap > kPpq / 2 || overlap * 2 > a.length)
                oneLine = false;
            handover = true;
        }
        if (!oneLine || !handover)
            continue;
        for (size_t k = 0; k + 1 < line.size(); k++) {
            Note& a = out[line[k]];
            Note& b = out[line[k + 1]];
            if (b.start >= a.end())
                continue;
            Tick oldStart = a.start, oldLength = a.length;
            a.length = b.start - a.start;
            expr::keepInTime(a, oldStart, oldLength);
            b.tied = true;
        }
    }
    return out;
}

bool allTied(const NoteList& notes, const IdSet& ids) {
    bool any = false;
    for (const Note& n : notes)
        any = any || (n.tied && ids.count(n.id));
    if (!any)
        return false;
    IdSet tied;
    tie(notes, ids, &tied);
    for (const Note& n : notes)
        if (tied.count(n.id) && !n.tied)
            return false;
    return true;
}

} // namespace tessera::ties
