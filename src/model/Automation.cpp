#include "Automation.hpp"

#include <algorithm>
#include <cmath>

namespace tessera {

double curveShape(CurveType curve, float tension, double x) {
    x = std::clamp(x, 0.0, 1.0);
    double t = std::clamp(double(tension), -1.0, 1.0);
    switch (curve) {
    case CurveType::Hold:
        return 0;
    case CurveType::Exponential:
        // A slow start that speeds up; tension makes it gentler (toward a line) or steeper.
        return std::pow(x, std::pow(4.0, 1.0 - t));
    case CurveType::SCurve: {
        double p = 2 * std::pow(3.0, t);
        return x < 0.5 ? 0.5 * std::pow(2 * x, p) : 1 - 0.5 * std::pow(2 - 2 * x, p);
    }
    case CurveType::Linear:
    default:
        return t == 0 ? x : std::pow(x, std::pow(4.0, -t));
    }
}

namespace {

float segmentValue(const AutoPoint& a, const AutoPoint& b, double t) {
    if (b.t <= a.t)
        return b.value;
    double x = (t - double(a.t)) / double(b.t - a.t);
    return float(double(a.value) + (double(b.value) - double(a.value)) * curveShape(a.curve, a.tension, x));
}

} // namespace

float evaluateAt(const std::vector<AutoPoint>& points, double t, size_t& cursor, float rest) {
    if (points.empty())
        return rest;
    if (t <= double(points.front().t))
        return points.front().value;
    if (t >= double(points.back().t))
        return points.back().value;
    // The segment is [points[cursor], points[cursor + 1]): step forward from the last one, or search again
    // after a jump back (a loop wrap).
    if (cursor >= points.size() - 1 || double(points[cursor].t) > t) {
        auto it = std::upper_bound(points.begin(), points.end(), t,
                                   [](double v, const AutoPoint& p) { return v < double(p.t); });
        cursor = size_t(it - points.begin()) - 1;
    }
    while (cursor + 1 < points.size() - 1 && double(points[cursor + 1].t) <= t)
        cursor++;
    return segmentValue(points[cursor], points[cursor + 1], t);
}

float evaluate(const AutomationEnvelope& lane, double t) {
    size_t cursor = 0;
    return evaluateAt(lane.points, t, cursor, restValue(lane));
}

} // namespace tessera
