#include "NoteEdits.hpp"

#include "ExpressionEdits.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace tessera::edits {

namespace {

NoteList sorted(NoteList notes) {
    std::sort(notes.begin(), notes.end(), noteOrder);
    return notes;
}

template <typename F>
NoteList each(const NoteList& notes, const IdSet& ids, F change) {
    NoteList out = notes;
    for (Note& n : out)
        if (ids.count(n.id))
            change(n);
    return sorted(std::move(out));
}

} // namespace

NoteList pick(const NoteList& notes, const IdSet& ids) {
    NoteList out;
    for (const Note& n : notes)
        if (ids.count(n.id))
            out.push_back(n);
    return out;
}

IdSet idsOf(const NoteList& notes) {
    IdSet out;
    for (const Note& n : notes)
        out.insert(n.id);
    return out;
}

NoteList move(const NoteList& notes, const IdSet& ids, Tick deltaTime, int deltaPitch) {
    return each(notes, ids, [&](Note& n) {
        n.start = std::max<Tick>(0, n.start + deltaTime);
        n.pitch = int8_t(std::clamp(n.pitch + deltaPitch, 0, 127));
    });
}

NoteList resizeEnd(const NoteList& notes, const IdSet& ids, Tick delta, Tick minLength) {
    return each(notes, ids, [&](Note& n) {
        Tick oldLength = n.length;
        n.length = std::max(std::max<Tick>(1, minLength), n.length + delta);
        expr::keepInTime(n, n.start, oldLength);
    });
}

NoteList resizeStart(const NoteList& notes, const IdSet& ids, Tick delta, Tick minLength) {
    return each(notes, ids, [&](Note& n) {
        Tick end = n.end(), oldStart = n.start, oldLength = n.length;
        n.start = std::clamp<Tick>(n.start + delta, 0, end - std::max<Tick>(1, minLength));
        n.length = end - n.start;
        expr::keepInTime(n, oldStart, oldLength);
    });
}

NoteList stretchTime(const NoteList& notes, const IdSet& ids, Tick anchor, double factor) {
    factor = std::max(factor, 1e-3);
    return each(notes, ids, [&](Note& n) {
        Tick start = Tick(std::llround(double(anchor) + double(n.start - anchor) * factor));
        Tick end = Tick(std::llround(double(anchor) + double(n.end() - anchor) * factor));
        n.start = std::max<Tick>(0, start);
        n.length = std::max<Tick>(1, end - n.start);
    });
}

NoteList mapPitches(const NoteList& notes, const IdSet& ids, const std::function<int(int)>& f) {
    return each(notes, ids, [&](Note& n) { n.pitch = int8_t(std::clamp(f(n.pitch), 0, 127)); });
}

NoteList stepInScale(const NoteList& notes, const IdSet& ids, const Scale& scale, int steps) {
    return each(notes, ids, [&](Note& n) { n.pitch = int8_t(scale.stepBy(n.pitch, steps)); });
}

NoteList fitToScale(const NoteList& notes, const IdSet& ids, const Scale& scale) {
    return each(notes, ids, [&](Note& n) { n.pitch = int8_t(scale.snap(n.pitch)); });
}

NoteList moveToKey(const NoteList& notes, const IdSet& ids, const Scale& from, const Scale& to) {
    return each(notes, ids, [&](Note& n) { n.pitch = int8_t(tessera::moveToKey(n.pitch, from, to)); });
}

NoteList scalePitch(const NoteList& notes, const IdSet& ids, int fromLow, int fromHigh, int toLow, int toHigh) {
    return each(notes, ids, [&](Note& n) {
        double t = fromHigh > fromLow ? double(n.pitch - fromLow) / double(fromHigh - fromLow) : 0.0;
        int p = int(std::lround(toLow + t * (toHigh - toLow)));
        n.pitch = int8_t(std::clamp(p, 0, 127));
    });
}

NoteList changeVelocity(const NoteList& notes, const IdSet& ids, float delta) {
    return each(notes, ids, [&](Note& n) { n.velocity = std::clamp(n.velocity + delta, 0.f, 1.f); });
}

NoteList velocityLine(const NoteList& notes, const IdSet& ids, Tick t0, float v0, Tick t1, float v1) {
    return each(notes, ids, [&](Note& n) {
        double t = t1 != t0 ? double(n.start - t0) / double(t1 - t0) : 0.0;
        n.velocity = std::clamp(float(v0 + std::clamp(t, 0.0, 1.0) * (v1 - v0)), 0.f, 1.f);
    });
}

NoteList setMuted(const NoteList& notes, const IdSet& ids, bool muted) {
    return each(notes, ids, [&](Note& n) { n.muted = muted; });
}

NoteList duplicate(const NoteList& notes, const IdSet& ids, Tick offset, const IdSource& newId, IdSet* added) {
    NoteList out = notes;
    for (const Note& n : notes) {
        if (!ids.count(n.id))
            continue;
        Note copy = n;
        copy.id = newId();
        copy.start = std::max<Tick>(0, n.start + offset);
        out.push_back(copy);
        if (added)
            added->insert(copy.id);
    }
    return sorted(std::move(out));
}

NoteList slice(const NoteList& notes, NoteId id, Tick at, const IdSource& newId) {
    NoteList out = notes;
    for (size_t i = 0; i < out.size(); i++) {
        Note& n = out[i];
        if (n.id != id || at <= n.start || at >= n.end())
            continue;
        Note right = n;
        right.id = newId();
        right.start = at;
        right.length = n.end() - at;
        n.length = at - n.start;
        expr::keepInTime(right, n.start, right.end() - n.start);
        expr::keepInTime(n, n.start, right.end() - n.start);
        out.push_back(right);
        break;
    }
    return sorted(std::move(out));
}

NoteList sliceEqual(const NoteList& notes, NoteId id, int parts, const IdSource& newId) {
    NoteList out = notes;
    for (const Note& n : notes) {
        if (n.id != id || parts < 2 || n.length < parts)
            continue;
        // Cut from the right so each cut works on the note that is still whole.
        for (int k = parts - 1; k >= 1; k--)
            out = slice(out, id, n.start + n.length * k / parts, newId);
        break;
    }
    return sorted(std::move(out));
}

NoteList resolveOverlaps(const NoteList& notes, const IdSet& changed) {
    if (changed.empty())
        return notes;
    NoteList movers = pick(notes, changed);
    NoteList out;
    out.reserve(notes.size());
    for (Note n : notes) {
        if (changed.count(n.id)) {
            out.push_back(n);
            continue;
        }
        bool keep = true;
        for (const Note& m : movers) {
            if (m.pitch != n.pitch || m.end() <= n.start || m.start >= n.end())
                continue;
            if (m.start <= n.start && m.end() >= n.end()) {
                keep = false; // fully covered: replaced
                break;
            }
            Tick oldStart = n.start, oldLength = n.length;
            if (m.start > n.start) {
                n.length = m.start - n.start; // the newcomer starts inside: trim the old note's tail
            } else {
                Tick end = n.end(); // the newcomer covers the head: the old note starts after it
                n.start = m.end();
                n.length = end - n.start;
            }
            expr::keepInTime(n, oldStart, oldLength);
        }
        if (keep && n.length >= 1)
            out.push_back(n);
    }
    return sorted(std::move(out));
}

Tick duplicateOffset(const NoteList& notes, const IdSet& ids, Tick grid, TimeSig ts) {
    assert(grid > 0);
    auto [lo, hi] = span(notes, ids);
    Tick lastStart = lo;
    for (const Note& n : notes)
        if (ids.count(n.id))
            lastStart = std::max(lastStart, n.start);
    Tick length = hi - lo - grid / 8;
    Tick bar = ticksPerBar(ts), beat = ticksPerBeat(ts);
    Tick step = length > bar / 2 ? std::max(grid, bar) : length > beat / 2 ? std::max(grid, beat) : grid;
    auto stepsUp = [step](Tick t) { return (std::max<Tick>(0, t) + step - 1) / step; };
    Tick byEnd = stepsUp(hi - lo - step / 8);
    Tick byStart = (lastStart - lo) / step + 1;
    return std::max(byEnd, byStart) * step;
}

std::pair<Tick, Tick> span(const NoteList& notes, const IdSet& ids) {
    bool any = false;
    Tick lo = 0, hi = 0;
    for (const Note& n : notes) {
        if (!ids.count(n.id))
            continue;
        lo = any ? std::min(lo, n.start) : n.start;
        hi = any ? std::max(hi, n.end()) : n.end();
        any = true;
    }
    return {lo, hi};
}

} // namespace tessera::edits
