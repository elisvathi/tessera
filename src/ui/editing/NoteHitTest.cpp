#include "NoteHitTest.hpp"

#include "../../model/NoteOperators.hpp"

#include <algorithm>
#include <cmath>

namespace tessera {

namespace {

constexpr double kMinBodyPx = 4;

struct NoteRect {
    double x0, x1, y0, y1;
};

NoteRect rectOf(const Note& n, const TimeAxis& time, const PitchAxis& pitch) {
    double y = pitch.pitchToY(n.pitch);
    return {time.tickToX(double(n.start)), time.tickToX(double(n.end())), y, y + pitch.rowHeight()};
}

} // namespace

NoteHit hitTestNotes(const edits::NoteList& notes, const TimeAxis& time, const PitchAxis& pitch, double x, double y,
                     const edits::IdSet& selected, double edgePx) {
    NoteHit edge, selectedBody, body;
    // Where two notes touch, both edge zones cover the seam. The edge on the side the mouse is on wins, then the
    // nearer one, so the second note's start stays reachable (Trello, 13 Oct).
    bool edgeInside = false;
    double edgeDistance = 0;
    auto offerEdge = [&](NoteHit::Part part, NoteId id, bool inBody, double distance) {
        if (edge.any() && (edgeInside > inBody || (edgeInside == inBody && edgeDistance <= distance)))
            return;
        edge = {part, id};
        edgeInside = inBody;
        edgeDistance = distance;
    };
    // Notes are sorted by start: nothing starting right of the grab zone can be hit.
    double maxTick = time.xToTick(x + edgePx);
    for (const Note& n : notes) {
        if (double(n.start) > maxTick)
            break;
        NoteRect r = rectOf(n, time, pitch);
        if (y < r.y0 || y >= r.y1)
            continue;
        double width = r.x1 - r.x0;
        double inside = std::min(edgePx, width / 3);
        bool inBody = x >= r.x0 && x < r.x1;
        if (x >= r.x1 - inside && x <= r.x1 + edgePx)
            offerEdge(NoteHit::Part::EndEdge, n.id, inBody, std::fabs(x - r.x1));
        else if (x >= r.x0 - edgePx && x <= r.x0 + inside)
            offerEdge(NoteHit::Part::StartEdge, n.id, inBody, std::fabs(x - r.x0));
        double pad = std::max(0.0, (kMinBodyPx - width) / 2);
        if (x >= r.x0 - pad && x < r.x1 + pad) {
            if (selected.count(n.id))
                selectedBody = {NoteHit::Part::Body, n.id};
            else if (!body.any())
                body = {NoteHit::Part::Body, n.id};
        }
    }
    if (edge.any())
        return edge;
    if (selectedBody.any())
        return selectedBody;
    return body;
}

edits::IdSet notesInRect(const edits::NoteList& notes, const TimeAxis& time, const PitchAxis& pitch, double x0,
                         double y0, double x1, double y1) {
    if (x1 < x0)
        std::swap(x0, x1);
    if (y1 < y0)
        std::swap(y0, y1);
    edits::IdSet out;
    for (const Note& n : notes) {
        NoteRect r = rectOf(n, time, pitch);
        if (r.x0 > x1)
            break;
        if (r.x1 >= x0 && r.y1 >= y0 && r.y0 <= y1)
            out.insert(n.id);
    }
    return out;
}

edits::IdSet notesAlongPath(const edits::NoteList& notes, const TimeAxis& time, const PitchAxis& pitch, double x0,
                            double y0, double x1, double y1) {
    // Sample the segment finely enough that no note row or column is skipped.
    double length = std::hypot(x1 - x0, y1 - y0);
    int steps = std::max(1, int(std::ceil(length / 2)));
    edits::IdSet out;
    for (int i = 0; i <= steps; i++) {
        double t = double(i) / steps;
        double x = x0 + (x1 - x0) * t, y = y0 + (y1 - y0) * t;
        for (const Note& n : notes) {
            NoteRect r = rectOf(n, time, pitch);
            if (r.x0 > x)
                break;
            double pad = std::max(0.0, (kMinBodyPx - (r.x1 - r.x0)) / 2);
            if (x >= r.x0 - pad && x < r.x1 + pad && y >= r.y0 && y < r.y1)
                out.insert(n.id);
        }
    }
    return out;
}

std::vector<std::pair<NoteId, double>> sliceLineCuts(const edits::NoteList& notes, const TimeAxis& time,
                                                     const PitchAxis& pitch, double x0, double y0, double x1,
                                                     double y1) {
    std::vector<std::pair<NoteId, double>> cuts;
    for (const Note& n : notes) {
        NoteRect r = rectOf(n, time, pitch);
        double rowY = (r.y0 + r.y1) / 2;
        double x;
        if (std::fabs(y1 - y0) < 1e-9) {
            // A horizontal line only cuts the row it lies in, at its start.
            if (y0 < r.y0 || y0 >= r.y1)
                continue;
            x = x0;
        } else {
            double t = (rowY - y0) / (y1 - y0);
            if (t < 0 || t > 1)
                continue;
            x = x0 + (x1 - x0) * t;
        }
        if (x > r.x0 && x < r.x1)
            cuts.push_back({n.id, time.xToTick(x)});
    }
    return cuts;
}

BendHit hitTestCurves(const edits::NoteList& notes, const TimeAxis& time, expr::Curve curve, const CurveY& yOf,
                      double x, double y, double pointPx, double curvePx,
                      const std::function<bool(const Note&)>& editable, double minSpanPx) {
    BendHit handle, onCurve;
    float rest = expr::info(curve).rest;
    double bestHandle = pointPx * pointPx, bestCurve = curvePx;
    double maxTick = time.xToTick(x + pointPx);
    for (const Note& n : notes) {
        if (double(n.start) > maxTick)
            break;
        double x0 = time.tickToX(double(n.start)), x1 = time.tickToX(double(n.end()));
        if (x1 < x - pointPx || (editable && !editable(n)))
            continue;
        const std::vector<ExprPoint>& points = expr::points(n, curve);
        auto consider = [&](double px, double py, BendHit hit) {
            double d = (px - x) * (px - x) + (py - y) * (py - y);
            // A point wins a tie with an arch handle: points are what most presses are for.
            if (d < bestHandle || (d == bestHandle && !hit.segment)) {
                bestHandle = d;
                handle = hit;
            }
        };
        for (size_t i = 0; i < points.size(); i++) {
            const ExprPoint& p = points[i];
            // The grid hides a pitch curve's resting start: it sits on the note's start edge and its label.
            bool hidden = curve == expr::Curve::Pitch && p.pos == 0 && p.value == 0;
            if (!hidden)
                consider(x0 + double(p.pos) * (x1 - x0), yOf(n, p.value), {n.id, int(i), p.pos, false});
            if (i + 1 < points.size()) {
                const ExprPoint& q = points[i + 1];
                if (q.value == p.value || (double(q.pos - p.pos)) * (x1 - x0) < minSpanPx)
                    continue;
                float mid = (p.pos + q.pos) / 2;
                consider(x0 + double(mid) * (x1 - x0), yOf(n, expressionAt(points, mid, rest)),
                         {n.id, int(i), mid, true});
            }
        }
        if (x < x0 || x > x1 || x1 <= x0)
            continue;
        float pos = float((x - x0) / (x1 - x0));
        double dy = std::fabs(yOf(n, expressionAt(points, pos, rest)) - y);
        if (dy <= bestCurve) {
            bestCurve = dy;
            onCurve = {n.id, -1, pos, false};
        }
    }
    return handle.any() ? handle : onCurve;
}

BendHit hitTestBends(const edits::NoteList& notes, const TimeAxis& time, const PitchAxis& pitch, double x, double y,
                     double pointPx, double curvePx) {
    CurveY yOf = [&pitch](const Note& n, float value) { return pitch.bentPitchToY(n.pitch + double(value)); };
    return hitTestCurves(notes, time, expr::Curve::Pitch, yOf, x, y, pointPx, std::max(curvePx, pitch.rowHeight() / 2));
}

int segmentAt(const std::vector<ExprPoint>& points, float pos) {
    for (size_t i = 0; i + 1 < points.size(); i++)
        if (pos >= points[i].pos && pos <= points[i + 1].pos)
            return int(i);
    return -1;
}

} // namespace tessera
