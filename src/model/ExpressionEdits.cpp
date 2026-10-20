#include "ExpressionEdits.hpp"

#include "NoteOperators.hpp"

#include <algorithm>
#include <cmath>

namespace tessera::expr {

namespace {

const Info kInfo[] = {
    {"Pitch", -24, 24, 0},
    {"Timbre", 0, 1, 0},
    {"Pressure", 0, 1, 0},
};

} // namespace

const Info& info(Curve c) { return kInfo[std::clamp(int(c), 0, 2)]; }

std::vector<ExprPoint>& points(Note& note, Curve c) {
    switch (c) {
    case Curve::Timbre:
        return note.timbreExpr;
    case Curve::Pressure:
        return note.pressureExpr;
    case Curve::Pitch:
    default:
        return note.pitchExpr;
    }
}

const std::vector<ExprPoint>& points(const Note& note, Curve c) { return points(const_cast<Note&>(note), c); }

size_t addPoint(std::vector<ExprPoint>& curve, float pos, float value, float rest) {
    pos = std::clamp(pos, 0.f, 1.f);
    if (curve.empty() && pos > 0)
        curve.push_back({0, rest, 0});
    auto at = std::upper_bound(curve.begin(), curve.end(), pos,
                               [](float p, const ExprPoint& point) { return p < point.pos; });
    size_t i = size_t(at - curve.begin());
    curve.insert(at, {pos, value, 0});
    return i;
}

void movePoint(std::vector<ExprPoint>& curve, size_t i, float pos, float value) {
    if (i >= curve.size())
        return;
    float lo = i > 0 ? curve[i - 1].pos : 0.f;
    float hi = i + 1 < curve.size() ? curve[i + 1].pos : 1.f;
    curve[i].pos = std::clamp(pos, lo, hi);
    curve[i].value = value;
}

void removePoint(std::vector<ExprPoint>& curve, size_t i) {
    if (i < curve.size())
        curve.erase(curve.begin() + long(i));
}

int findPoint(const std::vector<ExprPoint>& curve, float pos, float value) {
    for (size_t i = 0; i < curve.size(); i++)
        if (std::fabs(curve[i].pos - pos) < 1e-5f && std::fabs(curve[i].value - value) < 1e-5f)
            return int(i);
    return -1;
}

size_t removePoints(std::vector<Note>& notes, Curve c, const std::vector<PointRef>& picked) {
    size_t removed = 0;
    float rest = info(c).rest;
    for (Note& n : notes) {
        std::vector<ExprPoint>& curve = points(n, c);
        bool changed = false;
        for (const PointRef& p : picked) {
            int i = p.note == n.id ? findPoint(curve, p.pos, p.value) : -1;
            if (i < 0)
                continue;
            curve.erase(curve.begin() + i);
            removed++;
            changed = true;
        }
        if (changed && std::all_of(curve.begin(), curve.end(), [rest](const ExprPoint& p) { return p.value == rest; }))
            curve.clear();
    }
    return removed;
}

namespace {

/// keepInTime for one curve.
void keepCurveInTime(std::vector<ExprPoint>& curve, float rest, double oldStart, double oldLength, double newStart,
                     double newLength) {
    if (curve.empty() || oldLength <= 0 || newLength <= 0)
        return;
    auto oldPos = [&](double tick) { return float((tick - oldStart) / oldLength); };
    float headValue = expressionAt(curve, oldPos(newStart), rest);
    float tailValue = expressionAt(curve, oldPos(newStart + newLength), rest);
    std::vector<ExprPoint> kept;
    bool cutHead = false, cutTail = false;
    float headTension = 0;
    for (const ExprPoint& p : curve) {
        float pos = float((oldStart + double(p.pos) * oldLength - newStart) / newLength);
        if (pos < 0) {
            cutHead = true;
            headTension = p.tension;
        } else if (pos > 1) {
            cutTail = true;
        } else {
            kept.push_back({pos, p.value, p.tension});
        }
    }
    if (cutHead && (kept.empty() || kept.front().pos > 0))
        kept.insert(kept.begin(), {0, headValue, headTension});
    if (cutTail && (kept.empty() || kept.back().pos < 1))
        kept.push_back({1, tailValue, 0});
    curve = std::move(kept);
}

} // namespace

void keepInTime(Note& note, Tick oldStart, Tick oldLength) {
    if (note.start == oldStart && note.length == oldLength)
        return;
    for (Curve c : {Curve::Pitch, Curve::Timbre, Curve::Pressure})
        keepCurveInTime(points(note, c), info(c).rest, double(oldStart), double(oldLength), double(note.start),
                        double(note.length));
}

} // namespace tessera::expr
