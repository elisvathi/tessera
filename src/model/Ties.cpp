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

/// Adds `offset(t)` (semitones, at tick t) to a pitch curve across a note of [start, start + length) over
/// [from, to], holding `offset(from)` before and `offset(to)` after. Points inside are sampled so a bent glide stays
/// bent.
template <typename Offset>
void addToCurve(std::vector<ExprPoint>& curve, double start, double length, double from, double to, bool bent,
                Offset offset) {
    double len = std::max(1.0, length);
    auto posOf = [&](double t) { return std::clamp((t - start) / len, 0.0, 1.0); };
    auto tickOf = [&](double pos) { return start + pos * len; };
    double p0 = posOf(from), p1 = posOf(to);
    std::vector<double> at;
    for (const ExprPoint& p : curve)
        at.push_back(p.pos);
    int steps = bent ? 8 : 1;
    for (int k = 0; k <= steps; k++)
        at.push_back(p0 + (p1 - p0) * k / steps);
    std::sort(at.begin(), at.end());
    at.erase(std::unique(at.begin(), at.end(), [](double a, double b) { return std::abs(a - b) < 1e-6; }), at.end());
    std::vector<ExprPoint> out;
    for (double pos : at) {
        double t = std::clamp(tickOf(pos), from, to);
        float own = expressionAt(curve, float(pos), 0);
        float tension = 0;
        for (const ExprPoint& p : curve)
            if (std::abs(p.pos - pos) < 1e-6 && (pos < p0 || pos > p1))
                tension = p.tension; // a point outside the glide keeps its bend
        out.push_back({float(pos), std::clamp(own + float(offset(t)), -48.f, 48.f), tension});
    }
    curve = std::move(out);
}

/// When the pitch curves of `a` and `b` meet at the joint (as "untie, keep the glide" leaves them), turns that bend
/// back into the glide of the new tie `b` and drops it from the curves, so the glide and the curves never both move
/// the pitch.
void takeBackGlide(Note& a, Note& b) {
    if (b.glideBefore > 0 || b.glideAfter > 0 || a.pitch == b.pitch || a.pitchExpr.empty() || b.pitchExpr.empty())
        return;
    float atJoint = float(a.pitch) + expressionAt(a.pitchExpr, 1.f, 0);
    if (std::abs(atJoint - (float(b.pitch) + expressionAt(b.pitchExpr, 0.f, 0))) > 1e-3f)
        return;
    auto flat = [](const ExprPoint& p) { return std::abs(p.value) < 1e-4f; };
    float fromPos = 0, toPos = 1;
    for (const ExprPoint& p : a.pitchExpr)
        if (flat(p) && p.pos < 1.f)
            fromPos = p.pos;
    for (auto it = b.pitchExpr.rbegin(); it != b.pitchExpr.rend(); ++it)
        if (flat(*it) && it->pos > 0.f)
            toPos = it->pos;
    Tick from = a.start + Tick(std::llround(fromPos * double(a.length)));
    Tick to = b.start + Tick(std::llround(toPos * double(b.length)));
    Glide g{from, b.start, to, a.pitch, b.pitch, 0};
    // Solve the shape from where the line crosses the joint: shaped() grows with the shape at any inner x.
    double x = double(b.start - from) / double(std::max<Tick>(1, to - from));
    double y = (double(atJoint) - a.pitch) / double(b.pitch - a.pitch);
    if (x > 0 && x < 1) {
        float lo = -1, hi = 1;
        bool rising = shaped(1, x) > shaped(-1, x);
        for (int k = 0; k < 30; k++) {
            float mid = (lo + hi) / 2;
            ((shaped(mid, x) < y) == rising ? lo : hi) = mid;
        }
        g.shape = (lo + hi) / 2;
    }
    a.pitchExpr.erase(std::remove_if(a.pitchExpr.begin(), a.pitchExpr.end(),
                                     [&](const ExprPoint& p) { return p.pos > fromPos + 1e-6f; }),
                      a.pitchExpr.end());
    b.pitchExpr.erase(std::remove_if(b.pitchExpr.begin(), b.pitchExpr.end(),
                                     [&](const ExprPoint& p) { return p.pos < toPos - 1e-6f; }),
                      b.pitchExpr.end());
    for (Note* n : {&a, &b})
        if (std::all_of(n->pitchExpr.begin(), n->pitchExpr.end(), flat))
            n->pitchExpr.clear();
    b.glideBefore = b.start - from;
    b.glideAfter = to - b.start;
    b.glideShape = g.shape;
}

} // namespace

void addGlide(std::vector<ExprPoint>& first, double firstStart, double firstLength, std::vector<ExprPoint>& second,
              double secondStart, double secondLength, const Glide& g) {
    if (!g.moves())
        return;
    bool bent = g.shape != 0;
    addToCurve(first, firstStart, firstLength, double(g.from), double(g.joint), bent,
               [&](double t) { return pitchAt(g, t) - g.p0; });
    addToCurve(second, secondStart, secondLength, double(g.joint), double(g.to), bent,
               [&](double t) { return pitchAt(g, t) - g.p1; });
}

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
        takeBackGlide(a, b);
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
            addGlide(a.pitchExpr, double(a.start), double(a.length), b.pitchExpr, double(b.start),
                     double(b.length), glideOf(a, b));
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
