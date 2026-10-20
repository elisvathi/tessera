#include "Recording.hpp"

#include "Json.hpp"
#include "LauncherCommands.hpp"

#include <algorithm>
#include <cmath>

namespace tessera {

using namespace json;

json_t* RecordSettings::toJson() const {
    json_t* j = json_object();
    setInt(j, "countInBars", countInBars);
    setInt(j, "clipBars", clipBars);
    setString(j, "arranger", arranger == ArrangerRecordMode::Replace ? "replace" : "overdub");
    setInt(j, "quantize", quantize);
    setNumber(j, "strength", strength);
    setBool(j, "toKey", toKey);
    return j;
}

RecordSettings RecordSettings::fromJson(const json_t* j) {
    RecordSettings s;
    if (!json_is_object(j))
        return s;
    s.countInBars = int(std::clamp<int64_t>(getInt(j, "countInBars", s.countInBars), 0, kMaxCountInBars));
    s.clipBars = int(std::clamp<int64_t>(getInt(j, "clipBars", s.clipBars), 0, 64));
    s.arranger =
        getString(j, "arranger", "overdub") == "replace" ? ArrangerRecordMode::Replace : ArrangerRecordMode::Overdub;
    s.quantize = std::clamp<Tick>(getInt(j, "quantize", s.quantize), 0, 4 * kPpq);
    s.strength = std::clamp(getFloat(j, "strength", s.strength), 0.f, 1.f);
    s.toKey = getBool(j, "toKey", s.toKey);
    return s;
}

Tick wholeBars(Tick length, TimeSig ts) {
    Tick bar = ticksPerBar(ts);
    return std::max<Tick>(1, (length + bar - 1) / bar) * bar;
}

namespace {

Tick positiveMod(Tick a, Tick b) { return ((a % b) + b) % b; }

Note toNote(const TakeNote& t, Tick start, const NextId& nextId) {
    Note n;
    n.id = NoteId(nextId());
    n.start = start;
    n.length = std::max<Tick>(1, t.length);
    n.pitch = int8_t(std::clamp<int>(t.pitch, 0, 127));
    n.velocity = std::clamp(t.velocity, 0.f, 1.f);
    n.releaseVelocity = std::clamp(t.release, 0.f, 1.f);
    n.channel = uint8_t(std::clamp<int>(t.channel, 1, kMaxNoteChannel));
    return n;
}

std::string takeName(size_t notes) { return "Record " + countNotes(notes); }

} // namespace

CommandPtr recordNewClip(const Song& song, TrackId track, int slot, Tick length, const std::vector<TakeNote>& take,
                         const NextId& nextId, ClipId* clipId, edits::IdSet* noteIds, const std::string& name) {
    const Track* t = song.findTrack(track);
    if (!t || slot < 0 || slot >= int(t->slots.size()) || t->slots[size_t(slot)].clip || length < 1)
        return nullptr;
    Clip clip;
    clip.id = ClipId(nextId());
    clip.name = newClipName(song);
    clip.color = t->color;
    clip.length = length;
    clip.loopEnd = length;
    for (const TakeNote& tn : take) {
        clip.notes.push_back(toNote(tn, positiveMod(tn.start, length), nextId));
        if (noteIds)
            noteIds->insert(clip.notes.back().id);
    }
    clip.sortNotes();
    if (clipId)
        *clipId = clip.id;
    auto out = std::make_unique<CompoundCommand>(name.empty() ? takeName(take.size()) : name);
    out->add(std::make_unique<AddClipToSlot>(track, slot, std::move(clip)));
    return out;
}

CommandPtr recordIntoClip(const Song& song, ClipId clipId, const std::vector<TakeNote>& take, const NextId& nextId,
                          edits::IdSet* noteIds) {
    const Clip* clip = song.findClip(clipId);
    if (!clip || take.empty())
        return nullptr;
    ClipSpan span = clipSpan(*clip);
    std::vector<Note> added;
    for (const TakeNote& tn : take) {
        Tick start = span.looping && span.length() > 0 ? span.start + positiveMod(tn.start - span.start, span.length())
                                                       : std::max<Tick>(0, tn.start);
        added.push_back(toNote(tn, start, nextId));
        if (noteIds)
            noteIds->insert(added.back().id);
    }
    return std::make_unique<EditNotes>(takeName(take.size()), clipId, std::vector<Note>{}, std::move(added));
}

CommandPtr recordToArrangement(const Song& song, TrackId track, Tick from, Tick to, const std::vector<TakeNote>& take,
                               ArrangerRecordMode mode, const NextId& nextId, ClipId* clipId, edits::IdSet* noteIds) {
    const Track* t = song.findTrack(track);
    if (!t || to <= from || from < 0)
        return nullptr;
    Clip clip;
    clip.id = ClipId(nextId());
    clip.name = newClipName(song);
    clip.color = t->color;
    clip.length = to - from;
    clip.loopEnd = clip.length;

    if (mode == ArrangerRecordMode::Overdub) {
        // What the track played there before, baked into the new clip (like glue).
        std::vector<TimedNote> old;
        const std::vector<DrumLane>* lanes = t->type == TrackType::Drum ? &t->drumLanes : nullptr;
        for (const ArrangerClip& a : t->arrangement)
            if (!a.muted && a.position < to && instanceEnd(a) > from)
                if (const Clip* c = song.findClip(a.clip))
                    renderInstance(*c, a, old, 0, 1, lanes);
        for (const TimedNote& tn : old) {
            if (tn.start < from || tn.start >= to)
                continue;
            Note n = *tn.source;
            n.id = NoteId(nextId());
            n.start = tn.start - from;
            n.length = std::max<Tick>(1, std::min(tn.end, to) - tn.start);
            n.pitch = tn.pitch;
            n.velocity = tn.velocity;
            clip.notes.push_back(std::move(n));
        }
    }
    for (const TakeNote& tn : take) {
        if (tn.start < from || tn.start >= to)
            continue;
        clip.notes.push_back(toNote(tn, tn.start - from, nextId));
        if (noteIds)
            noteIds->insert(clip.notes.back().id);
    }
    clip.sortNotes();
    if (clipId)
        *clipId = clip.id;

    arrange::Placed placed{track, ArrangerClip{}};
    placed.clip.id = ArrangementId(nextId());
    placed.clip.clip = clip.id;
    placed.clip.position = from;
    placed.clip.length = clip.length;
    arrange::Layout before = arrange::layoutOf(song);
    arrange::Layout after = before;
    after.push_back(placed);
    after = arrange::resolveOverlaps(after, {placed.clip.id}, song.clips, [&] { return ArrangementId(nextId()); });

    auto out = std::make_unique<CompoundCommand>(takeName(take.size()));
    out->add(std::make_unique<AddClip>(std::move(clip)));
    out->add(arrange::replaceArrangement(song, "Record", before, after));
    return out;
}

edits::NoteList quantizeStarts(const edits::NoteList& notes, const edits::IdSet& ids, Tick grid, float strength) {
    edits::NoteList out = notes;
    if (grid <= 0)
        return out;
    for (Note& n : out) {
        if (!ids.count(n.id))
            continue;
        double target = std::round(double(n.start) / double(grid)) * double(grid);
        n.start = std::max<Tick>(0, Tick(std::llround(double(n.start) + (target - double(n.start)) * strength)));
    }
    std::sort(out.begin(), out.end(), noteOrder);
    return out;
}

CommandPtr quantizeTake(const Song& song, ClipId clipId, const edits::IdSet& ids, Tick grid, float strength) {
    const Clip* clip = song.findClip(clipId);
    if (!clip || grid <= 0 || ids.empty())
        return nullptr;
    return replaceNotes("Quantize recording", clipId, clip->notes, quantizeStarts(clip->notes, ids, grid, strength));
}

} // namespace tessera
