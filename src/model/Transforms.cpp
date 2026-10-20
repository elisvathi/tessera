#include "Transforms.hpp"

#include "DrumKit.hpp"
#include "ExpressionEdits.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <random>

namespace tessera::transforms {

namespace {

constexpr double kPi = 3.14159265358979323846;
/// Notes starting this close together count as one chord (a played chord is rarely exact).
constexpr Tick kChordTolerance = kPpq / 32;

NoteList sorted(NoteList notes) {
    std::sort(notes.begin(), notes.end(), noteOrder);
    return notes;
}

/// The chosen notes in time order (then pitch), as indices into `notes`.
std::vector<size_t> chosen(const NoteList& notes, const IdSet& ids) {
    std::vector<size_t> out;
    for (size_t i = 0; i < notes.size(); i++)
        if (ids.count(notes[i].id))
            out.push_back(i);
    std::stable_sort(out.begin(), out.end(), [&](size_t a, size_t b) { return noteOrder(notes[a], notes[b]); });
    return out;
}

/// Chosen notes grouped into chords: runs that start within kChordTolerance of the run's first note.
std::vector<std::vector<size_t>> chords(const NoteList& notes, const IdSet& ids) {
    std::vector<std::vector<size_t>> out;
    for (size_t i : chosen(notes, ids)) {
        if (out.empty() || notes[i].start - notes[out.back().front()].start > kChordTolerance)
            out.emplace_back();
        out.back().push_back(i);
    }
    return out;
}

/// Where a tick lands on a swung grid: every second step is pushed later by up to half a step.
Tick nearestGrid(Tick t, Tick grid, float swing) {
    if (grid <= 0)
        return t;
    Tick swingShift = Tick(std::llround(double(grid) * 0.5 * std::clamp(double(swing), 0.0, 1.0)));
    Tick best = t, bestDistance = -1;
    Tick k0 = t / grid - 1;
    for (Tick k = std::max<Tick>(0, k0); k <= k0 + 2; k++) {
        Tick at = k * grid + (k % 2 == 1 ? swingShift : 0);
        Tick d = std::llabs(at - t);
        if (bestDistance < 0 || d < bestDistance) {
            best = at;
            bestDistance = d;
        }
    }
    return best;
}

Tick toward(Tick from, Tick to, float strength) {
    return from + Tick(std::llround(double(to - from) * std::clamp(double(strength), 0.0, 1.0)));
}

int clampPitch(int p) { return std::clamp(p, 0, 127); }

} // namespace

// ---- Timing ----

NoteList quantize(const NoteList& notes, const IdSet& ids, const QuantizeOptions& o) {
    NoteList out = notes;
    for (Note& n : out) {
        if (!ids.count(n.id))
            continue;
        Tick end = n.end();
        if (o.ends != Ends::End) {
            Tick target = nearestGrid(n.start, o.grid, o.swing);
            if (std::llabs(target - n.start) > o.range)
                n.start = std::max<Tick>(0, toward(n.start, target, o.strength));
        }
        if (o.ends != Ends::Start) {
            Tick target = nearestGrid(end, o.grid, o.swing);
            // An end never snaps back onto or before its start: it takes the first grid line after it.
            if (target <= n.start && o.grid > 0)
                target = nearestGrid((n.start / o.grid + 1) * o.grid, o.grid, o.swing);
            if (std::llabs(target - end) > o.range)
                end = toward(end, target, o.strength);
        } else {
            end = n.start + n.length;
        }
        Tick oldLength = n.length;
        n.length = std::max<Tick>(1, end - n.start);
        expr::keepInTime(n, n.start, oldLength);
    }
    return sorted(std::move(out));
}

NoteList humanize(const NoteList& notes, const IdSet& ids, const HumanizeOptions& o) {
    std::mt19937 rng(o.seed);
    std::uniform_real_distribution<double> unit(-1, 1);
    NoteList out = notes;
    // Draw in time order so the same seed gives the same result whatever the list's order.
    for (size_t i : chosen(notes, ids)) {
        Note& n = out[i];
        double dt = unit(rng), dv = unit(rng), dl = unit(rng);
        n.start = std::max<Tick>(0, n.start + Tick(std::llround(dt * double(o.timing))));
        n.velocity = std::clamp(n.velocity + float(dv) * o.velocity, 0.f, 1.f);
        Tick oldLength = n.length;
        n.length = std::max<Tick>(1, Tick(std::llround(double(n.length) * (1 + dl * double(o.length)))));
        expr::keepInTime(n, n.start, oldLength);
    }
    return sorted(std::move(out));
}

NoteList legato(const NoteList& notes, const IdSet& ids, bool samePitch, Tick overlap) {
    NoteList out = notes;
    std::vector<size_t> order = chosen(notes, ids);
    for (size_t a = 0; a < order.size(); a++) {
        const Note& n = notes[order[a]];
        Tick next = -1;
        for (size_t b = a + 1; b < order.size(); b++) {
            const Note& m = notes[order[b]];
            if (m.start > n.start && (!samePitch || m.pitch == n.pitch)) {
                next = m.start;
                break;
            }
        }
        if (next >= 0) {
            Note& changed = out[order[a]];
            Tick oldLength = changed.length;
            changed.length = std::max<Tick>(1, next - n.start + overlap);
            expr::keepInTime(changed, changed.start, oldLength);
        }
    }
    return sorted(std::move(out));
}

NoteList strum(const NoteList& notes, const IdSet& ids, StrumDirection direction, Tick step, float curve) {
    NoteList out = notes;
    double power = std::pow(2.0, -std::clamp(double(curve), -1.0, 1.0) * 2);
    int chordIndex = 0;
    for (std::vector<size_t> chord : chords(notes, ids)) {
        if (chord.size() < 2) {
            chordIndex++;
            continue;
        }
        bool up = direction == StrumDirection::Up || (direction == StrumDirection::Alternate && chordIndex % 2 == 0);
        std::sort(chord.begin(), chord.end(), [&](size_t a, size_t b) {
            return up ? notes[a].pitch < notes[b].pitch : notes[a].pitch > notes[b].pitch;
        });
        Tick first = notes[chord.front()].start;
        for (size_t i : chord)
            first = std::min(first, notes[i].start);
        double span = double(step) * double(chord.size() - 1);
        for (size_t k = 0; k < chord.size(); k++) {
            Note& n = out[chord[k]];
            Tick end = n.end(), oldStart = n.start, oldLength = n.length;
            double x = double(k) / double(chord.size() - 1);
            n.start = std::min(end - 1, first + Tick(std::llround(span * std::pow(x, power))));
            n.length = end - n.start;
            expr::keepInTime(n, oldStart, oldLength);
        }
        chordIndex++;
    }
    return sorted(std::move(out));
}

NoteList flam(const NoteList& notes, const IdSet& ids, Tick offset, float velocity, const IdSource& newId) {
    NoteList out = notes;
    for (size_t i : chosen(notes, ids)) {
        const Note& n = notes[i];
        if (n.start - offset < 0 || offset <= 0)
            continue;
        Note grace = n;
        grace.id = newId();
        grace.start = n.start - offset;
        grace.length = offset;
        grace.velocity = std::clamp(n.velocity * velocity, 0.f, 1.f);
        grace.pitchExpr.clear();
        grace.timbreExpr.clear();
        grace.pressureExpr.clear();
        out.push_back(grace);
    }
    return sorted(std::move(out));
}

NoteList chop(const NoteList& notes, const IdSet& ids, int parts, Tick grid, float velocityEnd, const IdSource& newId) {
    NoteList out;
    out.reserve(notes.size());
    for (const Note& n : notes) {
        if (!ids.count(n.id)) {
            out.push_back(n);
            continue;
        }
        std::vector<Tick> cuts{n.start};
        if (grid > 0) {
            for (Tick t = (n.start / grid + 1) * grid; t < n.end(); t += grid)
                cuts.push_back(t);
        } else {
            int count = std::clamp<int>(parts, 1, int(std::min<Tick>(n.length, 64)));
            for (int k = 1; k < count; k++)
                cuts.push_back(n.start + n.length * k / count);
        }
        cuts.push_back(n.end());
        size_t pieces = cuts.size() - 1;
        for (size_t k = 0; k < pieces; k++) {
            Note part = n;
            if (k > 0)
                part.id = newId();
            part.start = cuts[k];
            part.length = std::max<Tick>(1, cuts[k + 1] - cuts[k]);
            expr::keepInTime(part, n.start, n.length);
            float x = pieces > 1 ? float(k) / float(pieces - 1) : 0.f;
            part.velocity = std::clamp(n.velocity * (1 + (velocityEnd - 1) * x), 0.f, 1.f);
            out.push_back(part);
        }
    }
    return sorted(std::move(out));
}

NoteList glue(const NoteList& notes, const IdSet& ids) {
    std::map<int, std::vector<Note>> byPitch;
    NoteList out;
    for (const Note& n : notes) {
        if (ids.count(n.id))
            byPitch[n.pitch].push_back(n);
        else
            out.push_back(n);
    }
    for (auto& [pitch, list] : byPitch) {
        std::sort(list.begin(), list.end(), noteOrder);
        Note current = list.front();
        for (size_t i = 1; i < list.size(); i++) {
            if (list[i].start <= current.end()) {
                Tick oldLength = current.length;
                current.length = std::max(current.end(), list[i].end()) - current.start;
                expr::keepInTime(current, current.start, oldLength);
                continue;
            }
            out.push_back(current);
            current = list[i];
        }
        out.push_back(current);
    }
    return sorted(std::move(out));
}

// ---- Pitch ----

NoteList arpeggiate(const NoteList& notes, const IdSet& ids, const ArpOptions& o, const IdSource& newId) {
    if (o.rate <= 0)
        return notes;
    NoteList out;
    for (const Note& n : notes)
        if (!ids.count(n.id))
            out.push_back(n);
    // Like an arpeggiator played live: each step plays from the notes held at that moment, so a voice that moves
    // inside a chord (F to E under a held A and D) joins the same arpeggio instead of starting a second one.
    std::vector<size_t> order = chosen(notes, ids); // by start, then pitch: the order "as played"
    Tick lastEnd = 0;
    for (size_t i : order)
        lastEnd = std::max(lastEnd, notes[i].end());
    std::mt19937 rng(o.seed);
    std::vector<const Note*> held, previous;
    size_t next = 0; // the first note in `order` that hasn't started yet
    int k = 0;
    Tick t = order.empty() ? lastEnd : notes[order.front()].start;
    while (t < lastEnd) {
        // Notes starting a hair late (strummed, humanized) count as held from this step.
        while (next < order.size() && notes[order[next]].start <= t + kChordTolerance)
            next++;
        held.clear();
        // A note ending within a hair of this step (strummed late) is over, unless it only just began.
        for (size_t j = 0; j < next; j++) {
            const Note& n = notes[order[j]];
            if (n.end() > t + kChordTolerance || (n.end() > t && n.start + kChordTolerance >= t))
                held.push_back(&n);
        }
        if (held.empty()) {
            // A rest: the steps start again with the next note.
            if (next >= order.size())
                break;
            t = notes[order[next]].start;
            previous.clear();
            k = 0;
            continue;
        }
        // A new chord (most of what is held just began) starts the pattern from its beginning.
        size_t fresh = 0;
        for (const Note* n : held)
            fresh += std::find(previous.begin(), previous.end(), n) == previous.end();
        if (fresh * 2 > held.size())
            k = 0;
        std::vector<const Note*> base = held;
        if (o.pattern != ArpPattern::AsPlayed)
            std::stable_sort(base.begin(), base.end(),
                             [](const Note* a, const Note* b) { return a->pitch < b->pitch; });
        std::vector<std::pair<int, const Note*>> steps;
        for (int oct = 0; oct < std::max(1, o.octaves); oct++)
            for (const Note* n : base)
                steps.push_back({n->pitch + 12 * oct, n});
        if (o.pattern == ArpPattern::Down)
            std::reverse(steps.begin(), steps.end());
        if (o.pattern == ArpPattern::UpDown && steps.size() > 2)
            for (size_t s = steps.size() - 2; s >= 1; s--)
                steps.push_back(steps[s]);
        size_t index = o.pattern == ArpPattern::Random ? std::uniform_int_distribution<size_t>(0, steps.size() - 1)(rng)
                                                       : size_t(k) % steps.size();
        Tick heldEnd = 0;
        for (const Note* n : held)
            heldEnd = std::max(heldEnd, n->end());
        const auto& [pitch, source] = steps[index];
        Note n = *source;
        n.id = newId();
        n.start = t;
        n.length = std::max<Tick>(1, std::min(heldEnd - t, Tick(std::llround(double(o.rate) * double(o.gate)))));
        n.pitch = int8_t(clampPitch(pitch));
        n.pitchExpr.clear();
        n.timbreExpr.clear();
        n.pressureExpr.clear();
        out.push_back(n);
        previous = held;
        k++;
        t += o.rate;
    }
    return sorted(std::move(out));
}

NoteList connect(const NoteList& notes, const IdSet& ids, const Scale& scale, Tick rate, const IdSource& newId) {
    if (rate <= 0)
        return notes;
    NoteList out = notes;
    std::vector<size_t> order = chosen(notes, ids);
    for (size_t a = 0; a + 1 < order.size(); a++) {
        const Note& from = notes[order[a]];
        const Note& to = notes[order[a + 1]];
        Tick gapStart = from.end();
        if (to.start - gapStart < rate)
            continue;
        int count = int((to.start - gapStart) / rate);
        int fromIndex = scale.indexOf(scale.snap(from.pitch));
        int toIndex = scale.indexOf(scale.snap(to.pitch));
        for (int k = 0; k < count; k++) {
            // Walk the key from one note toward the next, arriving one step before it.
            double x = double(k + 1) / double(count + 1);
            int index = fromIndex + int(std::lround(double(toIndex - fromIndex) * x));
            Note n = from;
            n.id = newId();
            n.start = gapStart + rate * k;
            n.length = rate;
            n.pitch = int8_t(clampPitch(scale.pitchAtIndex(index)));
            n.velocity = from.velocity + (to.velocity - from.velocity) * float(x);
            n.ops = NoteOperators();
            n.pitchExpr.clear();
            n.timbreExpr.clear();
            n.pressureExpr.clear();
            out.push_back(n);
        }
    }
    return sorted(std::move(out));
}

NoteList reverse(const NoteList& notes, const IdSet& ids) {
    auto [from, to] = edits::span(notes, ids);
    NoteList out = notes;
    for (Note& n : out)
        if (ids.count(n.id))
            n.start = from + (to - n.end());
    return sorted(std::move(out));
}

NoteList retrograde(const NoteList& notes, const IdSet& ids) {
    std::vector<size_t> order = chosen(notes, ids);
    NoteList out = notes;
    for (size_t k = 0; k < order.size(); k++)
        out[order[k]].pitch = notes[order[order.size() - 1 - k]].pitch;
    return sorted(std::move(out));
}

NoteList invert(const NoteList& notes, const IdSet& ids, int pivot, const Scale& scale) {
    int pivotIndex = scale.indexOf(scale.snap(pivot));
    return edits::mapPitches(notes, ids, [&](int p) {
        int index = scale.indexOf(scale.snap(p));
        return clampPitch(scale.pitchAtIndex(2 * pivotIndex - index));
    });
}

NoteList transpose(const NoteList& notes, const IdSet& ids, int amount, bool inKey, const Scale& scale) {
    if (inKey)
        return edits::mapPitches(notes, ids, [&](int p) { return clampPitch(scale.stepBy(p, amount)); });
    return edits::mapPitches(notes, ids, [&](int p) { return clampPitch(p + amount); });
}

// ---- Values ----

NoteList scaleVelocity(const NoteList& notes, const IdSet& ids, ScaleMode mode, float amount) {
    NoteList out = notes;
    for (Note& n : out)
        if (ids.count(n.id))
            n.velocity = std::clamp(mode == ScaleMode::Set ? amount : n.velocity * amount, 0.f, 1.f);
    return out;
}

NoteList scaleLength(const NoteList& notes, const IdSet& ids, ScaleMode mode, double amount) {
    NoteList out = notes;
    for (Note& n : out)
        if (ids.count(n.id)) {
            Tick oldLength = n.length;
            n.length =
                std::max<Tick>(1, Tick(std::llround(mode == ScaleMode::Set ? amount : double(n.length) * amount)));
            expr::keepInTime(n, n.start, oldLength);
        }
    return sorted(std::move(out));
}

NoteList shapeVelocity(const NoteList& notes, const IdSet& ids, Shape shape, ShapeMode mode, float depth, float cycles,
                       uint32_t seed) {
    // The shape runs from the first note's start to the last note's start, so both ends get its full range.
    Tick from = 0, to = 0;
    std::vector<size_t> order = chosen(notes, ids);
    if (!order.empty()) {
        from = notes[order.front()].start;
        for (size_t i : order)
            to = std::max(to, notes[i].start);
    }
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> unit(0, 1);
    NoteList out = notes;
    for (size_t i : order) {
        Note& n = out[i];
        double x = to > from ? double(n.start - from) / double(to - from) : 0;
        double phase = std::fmod(x * std::max(0.01, double(cycles)), 1.0);
        if (x >= 1 && phase == 0)
            phase = 1;
        float s = 0; // 0..1
        switch (shape) {
        case Shape::RampUp:
            s = float(phase);
            break;
        case Shape::RampDown:
            s = float(1 - phase);
            break;
        case Shape::Sine:
            s = float(0.5 - 0.5 * std::cos(2 * kPi * phase));
            break;
        case Shape::Triangle:
            s = float(phase < 0.5 ? phase * 2 : 2 - phase * 2);
            break;
        case Shape::Random:
            s = unit(rng);
            break;
        }
        switch (mode) {
        case ShapeMode::Multiply:
            n.velocity *= 1 - depth + depth * s;
            break;
        case ShapeMode::Add:
            n.velocity += depth * (s - 0.5f);
            break;
        case ShapeMode::Replace:
            n.velocity = n.velocity + (s - n.velocity) * depth;
            break;
        }
        n.velocity = std::clamp(n.velocity, 0.f, 1.f);
    }
    return out;
}

NoteList randomize(const NoteList& notes, const IdSet& ids, const RandomizeOptions& o, const Scale& scale) {
    std::mt19937 rng(o.seed);
    std::uniform_real_distribution<double> unit(-1, 1);
    NoteList out = notes;
    for (size_t i : chosen(notes, ids)) {
        Note& n = out[i];
        double dp = unit(rng), dv = unit(rng), dt = unit(rng), dl = unit(rng);
        if (o.pitch > 0) {
            int steps = int(std::lround(dp * o.pitch));
            n.pitch = int8_t(clampPitch(scale.chromatic() ? n.pitch + steps : scale.stepBy(n.pitch, steps)));
        }
        n.velocity = std::clamp(n.velocity + float(dv) * o.velocity, 0.f, 1.f);
        n.start = std::max<Tick>(0, n.start + Tick(std::llround(dt * double(o.timing))));
        Tick oldLength = n.length;
        n.length = std::max<Tick>(1, Tick(std::llround(double(n.length) * (1 + dl * double(o.length)))));
        expr::keepInTime(n, n.start, oldLength);
    }
    return sorted(std::move(out));
}

// ---- Creating and selecting ----

NoteList euclidean(const NoteList& notes, int hits, int steps, int rotation, int pitch, Tick start, Tick stepLength,
                   float velocity, const IdSource& newId, IdSet* added) {
    NoteList out = notes;
    std::vector<bool> pattern = euclid(hits, steps, rotation);
    for (size_t k = 0; k < pattern.size(); k++) {
        if (!pattern[k])
            continue;
        Note n;
        n.id = newId();
        n.start = start + stepLength * Tick(k);
        n.length = std::max<Tick>(1, stepLength);
        n.pitch = int8_t(clampPitch(pitch));
        n.velocity = velocity;
        if (added)
            added->insert(n.id);
        out.push_back(n);
    }
    return sorted(std::move(out));
}

IdSet select(const NoteList& notes, const IdSet& ids, const SelectOptions& o) {
    IdSet out;
    int k = 0;
    for (const Note& n : notes) {
        if (!ids.empty() && !ids.count(n.id))
            continue;
        bool match = false;
        switch (o.by) {
        case SelectBy::SamePitch:
            match = n.pitch == o.pitch;
            break;
        case SelectBy::VelocityRange:
            match = n.velocity >= o.low - 1e-6f && n.velocity <= o.high + 1e-6f;
            break;
        case SelectBy::OnBeats:
            match = o.grid > 0 && n.start % o.grid == 0;
            break;
        case SelectBy::OffBeats:
            match = o.grid > 0 && n.start % o.grid != 0;
            break;
        case SelectBy::EveryNth:
            match = o.every > 0 && (k - o.offset) % o.every == 0 && k >= o.offset;
            break;
        case SelectBy::Muted:
            match = n.muted;
            break;
        case SelectBy::WithOperators:
            match = !(n.ops == NoteOperators());
            break;
        }
        k++;
        if (match)
            out.insert(n.id);
    }
    return out;
}

} // namespace tessera::transforms
