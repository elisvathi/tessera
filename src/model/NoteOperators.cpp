#include "NoteOperators.hpp"

#include "Automation.hpp"

#include <algorithm>
#include <cmath>

namespace tessera {

float expressionAt(const std::vector<ExprPoint>& points, float pos, float rest) {
    if (points.empty())
        return rest;
    if (pos <= points.front().pos)
        return points.front().value;
    if (pos >= points.back().pos)
        return points.back().value;
    for (size_t i = 0; i + 1 < points.size(); i++) {
        const ExprPoint& a = points[i];
        const ExprPoint& b = points[i + 1];
        if (pos < b.pos) {
            double x = b.pos > a.pos ? double(pos - a.pos) / double(b.pos - a.pos) : 1;
            return float(a.value + (b.value - a.value) * curveShape(CurveType::Linear, a.tension, x));
        }
    }
    return points.back().value;
}

} // namespace tessera

namespace tessera::operators {

namespace {

uint64_t mix(uint64_t x) {
    // splitmix64: cheap, and good enough that neighbouring notes and passes look unrelated.
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

enum Salt : uint32_t { kChance = 1, kVelocity = 2, kTiming = 3 };

} // namespace

double random(uint32_t seed, uint64_t clip, int64_t pass, uint64_t note, uint32_t salt) {
    uint64_t h = mix(seed);
    h = mix(h ^ clip);
    h = mix(h ^ uint64_t(pass));
    h = mix(h ^ note);
    h = mix(h ^ salt);
    return double(h >> 11) * (1.0 / 9007199254740992.0);
}

bool plays(const Note& note, const PassContext& pass) {
    const NoteOperators& ops = note.ops;
    int length = std::clamp<int>(ops.recurrenceLength, 1, 16);
    if (length > 1 && !((ops.recurrenceMask >> (pass.pass % length)) & 1))
        return false;
    switch (ops.occurrence) {
    case Occurrence::First:
        if (pass.pass != 0)
            return false;
        break;
    case Occurrence::NotFirst:
        if (pass.pass == 0)
            return false;
        break;
    case Occurrence::PrevPlayed:
        if (!pass.previousPlayed)
            return false;
        break;
    case Occurrence::PrevNotPlayed:
        if (pass.previousPlayed)
            return false;
        break;
    case Occurrence::Fill:
        if (!pass.fill)
            return false;
        break;
    case Occurrence::NotFill:
        if (pass.fill)
            return false;
        break;
    case Occurrence::Always:
    default:
        break;
    }
    if (ops.chance >= 1)
        return true;
    return random(pass.seed, pass.clip, pass.pass, note.id.value, kChance) < double(ops.chance);
}

float velocity(const Note& note, const PassContext& pass) {
    float spread = note.ops.velocitySpread;
    if (spread <= 0)
        return note.velocity;
    double r = random(pass.seed, pass.clip, pass.pass, note.id.value, kVelocity) * 2 - 1;
    return std::clamp(note.velocity + float(r) * spread, 0.f, 1.f);
}

double timingOffset(const Note& note, const PassContext& pass) {
    double spread = spreadTicks(note);
    if (spread <= 0)
        return 0;
    return (random(pass.seed, pass.clip, pass.pass, note.id.value, kTiming) * 2 - 1) * spread;
}

int hitCount(const Note& note, Tick ticksPerBeat) {
    int r = note.ops.repeats;
    if (r == 0)
        return 1;
    if (r > 0)
        return std::min<int>(r, 16);
    // Per beat: as many as fit in the note at that rate, at least one.
    double perBeat = std::min(16, -r);
    return std::clamp(int(std::floor(double(note.length) * perBeat / double(ticksPerBeat) + 1e-9)), 1, 64);
}

Hit hit(const Note& note, int i, int count) {
    double length = double(note.length);
    if (count <= 1)
        return {0, length, note.velocity};
    // Where hit i starts, as a fraction of the note: an evenly spaced grid bent by the curve. Positive curves
    // squeeze later hits together (accelerating), negative ones spread them out.
    double c = std::clamp(double(note.ops.repeatCurve), -1.0, 1.0);
    double power = std::pow(2.0, -c * 2);
    auto at = [&](int k) { return std::pow(double(k) / double(count), power); };
    double start = at(i) * length;
    double spacing = (at(i + 1) - at(i)) * length;
    double gate = std::clamp(double(note.ops.repeatGate), 0.01, 1.0);
    float x = float(i) / float(count - 1);
    float v = note.velocity * (1 + (note.ops.repeatVelEnd - 1) * x);
    return {start, std::max(1.0, spacing * gate), std::clamp(v, 0.f, 1.f)};
}

} // namespace tessera::operators
