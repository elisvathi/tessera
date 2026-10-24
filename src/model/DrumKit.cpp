#include "DrumKit.hpp"

#include <algorithm>
#include <limits>

namespace tessera {

namespace {

DrumLane lane(const char* name, int pitch, int output, int choke = 0) {
    DrumLane l;
    l.name = name;
    l.pitch = int8_t(pitch);
    l.outputIndex = output;
    l.chokeGroup = choke;
    return l;
}

} // namespace

std::vector<DrumLane> drumKit(DrumKitPreset preset) {
    if (preset == DrumKitPreset::Standard8)
        return {lane("Kick", 36, 0),          lane("Snare", 38, 1),       lane("Clap", 39, 2),
                lane("Closed hat", 42, 3, 1), lane("Open hat", 46, 4, 1), lane("Low tom", 45, 5),
                lane("High tom", 50, 6),      lane("Rim", 37, 7)};
    return {lane("Kick", 36, 0),        lane("Rim", 37, 1),           lane("Snare", 38, 2),
            lane("Clap", 39, 3),        lane("Closed hat", 42, 4, 1), lane("Pedal hat", 44, 5, 1),
            lane("Open hat", 46, 6, 1), lane("Low tom", 45, 7),       lane("Mid tom", 47, 8),
            lane("High tom", 50, 9),    lane("Crash", 49, 10),        lane("Ride", 51, 11),
            lane("Snare 2", 40, 12),    lane("Cowbell", 56, 13),      lane("Shaker", 70, 14),
            lane("Tambourine", 54, 15)};
}

const char* drumKitName(DrumKitPreset preset) {
    return preset == DrumKitPreset::Standard8 ? "Standard 8" : "16 GM-ish";
}

int laneOfPitch(const std::vector<DrumLane>& lanes, int pitch) {
    for (size_t i = 0; i < lanes.size(); i++)
        if (lanes[i].pitch == pitch)
            return int(i);
    return -1;
}

Tick laneLoopsEnd(const Clip& clip) { return std::max<Tick>(1, std::max(clip.length, clip.contentEnd())); }

namespace {

/// The length a lane that follows the clip has: the clip's loop, or the whole clip without one.
Tick followLength(const Clip& clip) {
    bool looping = clip.loopMode == LoopMode::Loop && clip.loopEnd > clip.loopStart;
    return looping ? clip.loopEnd - clip.loopStart : laneLoopsEnd(clip);
}

} // namespace

LaneRegion laneRegion(const DrumLane& lane, const Clip& clip) {
    bool looping = clip.loopMode == LoopMode::Loop && clip.loopEnd > clip.loopStart;
    Tick start = looping ? clip.loopStart : 0;
    return {start, lane.loopLength > 0 ? lane.loopLength : followLength(clip)};
}

bool hasOwnLoop(const DrumLane& lane, const Clip& clip) {
    return lane.loopLength > 0 && lane.loopLength != followLength(clip);
}

std::vector<bool> euclid(int hits, int steps, int rotation) {
    std::vector<bool> out(size_t(std::max(steps, 0)), false);
    if (steps <= 0)
        return out;
    hits = std::clamp(hits, 0, steps);
    for (int i = 0; i < steps; i++) {
        int source = ((i - rotation) % steps + steps) % steps;
        out[size_t(i)] = (source * hits) % steps < hits;
    }
    return out;
}

namespace drums {

namespace {

bool inLane(const Note& n, int pitch, LaneRegion r) {
    return n.pitch == pitch && n.start >= r.start && n.start < r.start + r.length;
}

NoteList without(const NoteList& notes, int pitch, LaneRegion r) {
    NoteList out;
    for (const Note& n : notes)
        if (!inLane(n, pitch, r))
            out.push_back(n);
    return out;
}

Note stepNote(int pitch, Tick start, Tick length, float velocity, const IdSource& newId) {
    Note n;
    n.id = newId();
    n.pitch = int8_t(pitch);
    n.start = start;
    n.length = std::max<Tick>(1, length);
    n.velocity = std::clamp(velocity, 0.f, 1.f);
    return n;
}

NoteList sorted(NoteList notes) {
    std::sort(notes.begin(), notes.end(), noteOrder);
    return notes;
}

/// A small deterministic generator, so "randomize" with a seed repeats exactly.
struct Random {
    uint32_t state;
    explicit Random(uint32_t seed) : state(seed ? seed : 0x9E3779B9u) {}
    float next() {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return float(state >> 8) / float(1u << 24);
    }
};

} // namespace

NoteList laneNotes(const NoteList& notes, int pitch, LaneRegion region) {
    NoteList out;
    for (const Note& n : notes)
        if (inLane(n, pitch, region))
            out.push_back(n);
    return out;
}

NoteList setStep(const NoteList& notes, int pitch, Tick stepStart, Tick stepLength, const float* velocity,
                 const IdSource& newId) {
    NoteList out = without(notes, pitch, {stepStart, stepLength});
    if (velocity)
        out.push_back(stepNote(pitch, stepStart, stepLength, *velocity, newId));
    return sorted(std::move(out));
}

NoteList setLoopPattern(const NoteList& notes, int pitch, Tick start, Tick stepLength, const std::vector<bool>& pattern,
                        float velocity, const IdSource& newId) {
    NoteList out = without(notes, pitch, {start, std::numeric_limits<Tick>::max() - start});
    for (size_t i = 0; i < pattern.size(); i++)
        if (pattern[i])
            out.push_back(stepNote(pitch, start + Tick(i) * stepLength, stepLength, velocity, newId));
    return sorted(std::move(out));
}

NoteList applyPattern(const NoteList& notes, int pitch, LaneRegion region, Tick stepLength,
                      const std::vector<bool>& pattern, float velocity, const IdSource& newId) {
    NoteList out = without(notes, pitch, region);
    for (size_t i = 0; i < pattern.size(); i++) {
        Tick at = region.start + Tick(i) * stepLength;
        if (pattern[i] && at < region.start + region.length)
            out.push_back(stepNote(pitch, at, stepLength, velocity, newId));
    }
    return sorted(std::move(out));
}

NoteList rotate(const NoteList& notes, int pitch, LaneRegion region, Tick by) {
    NoteList out = notes;
    for (Note& n : out) {
        if (!inLane(n, pitch, region))
            continue;
        Tick rel = ((n.start - region.start + by) % region.length + region.length) % region.length;
        n.start = region.start + rel;
    }
    return sorted(std::move(out));
}

NoteList reverse(const NoteList& notes, int pitch, LaneRegion region, Tick stepLength) {
    NoteList out = notes;
    for (Note& n : out) {
        if (!inLane(n, pitch, region))
            continue;
        Tick rel = n.start - region.start;
        n.start = region.start + std::max<Tick>(0, region.length - stepLength - rel);
    }
    return sorted(std::move(out));
}

NoteList randomize(const NoteList& notes, int pitch, LaneRegion region, Tick stepLength, float density, float velLow,
                   float velHigh, uint32_t seed, const IdSource& newId) {
    NoteList out = without(notes, pitch, region);
    Random random(seed);
    Tick steps = region.steps(stepLength);
    for (Tick i = 0; i < steps; i++) {
        float on = random.next();
        float velocity = velLow + (velHigh - velLow) * random.next();
        if (on < density)
            out.push_back(stepNote(pitch, region.start + i * stepLength, stepLength, velocity, newId));
    }
    return sorted(std::move(out));
}

NoteList clear(const NoteList& notes, int pitch, LaneRegion region) { return without(notes, pitch, region); }

NoteList paste(const NoteList& notes, int pitch, LaneRegion region, const NoteList& copied, const IdSource& newId) {
    NoteList out = without(notes, pitch, region);
    for (Note n : copied) {
        if (n.start < 0 || n.start >= region.length)
            continue;
        n.id = newId();
        n.pitch = int8_t(pitch);
        n.start += region.start;
        out.push_back(n);
    }
    return sorted(std::move(out));
}

} // namespace drums
} // namespace tessera
