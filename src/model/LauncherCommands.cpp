#include "LauncherCommands.hpp"

#include <algorithm>
#include <cassert>

namespace tessera {

// ---- Scenes -------------------------------------------------------------------------------------------------

void InsertScene::apply(Song& song) {
    int at = std::clamp(index, 0, int(song.scenes.size()));
    song.scenes.insert(song.scenes.begin() + at, scene);
    for (Track& t : song.tracks)
        t.slots.insert(t.slots.begin() + std::min(size_t(at), t.slots.size()), ClipSlot{});
}

void InsertScene::revert(Song& song) {
    int at = std::clamp(index, 0, int(song.scenes.size()) - 1);
    song.scenes.erase(song.scenes.begin() + at);
    for (Track& t : song.tracks)
        if (size_t(at) < t.slots.size())
            t.slots.erase(t.slots.begin() + at);
}

void RemoveScene::apply(Song& song) {
    assert(index >= 0 && index < int(song.scenes.size()));
    scene = song.scenes[size_t(index)];
    slots.clear();
    clips.clear();
    for (Track& t : song.tracks) {
        if (size_t(index) >= t.slots.size())
            continue;
        ClipSlot slot = t.slots[size_t(index)];
        slots.push_back({t.id, slot});
        t.slots.erase(t.slots.begin() + index);
    }
    // Clips that were only in this scene go with it.
    for (const auto& [track, slot] : slots)
        if (slot.clip && song.clipUses(*slot.clip) == 0)
            if (const Clip* c = song.findClip(*slot.clip)) {
                clips.push_back(*c);
                song.clips.erase(c->id);
            }
    song.scenes.erase(song.scenes.begin() + index);
}

void RemoveScene::revert(Song& song) {
    song.scenes.insert(song.scenes.begin() + std::min(size_t(index), song.scenes.size()), scene);
    for (const Clip& c : clips)
        song.clips[c.id] = c;
    for (const auto& [track, slot] : slots)
        if (Track* t = song.findTrack(track))
            t->slots.insert(t->slots.begin() + std::min(size_t(index), t->slots.size()), slot);
}

CommandPtr insertScene(const Song& song, int index) {
    if (int(song.scenes.size()) >= kMaxScenes)
        return nullptr;
    return std::make_unique<InsertScene>(index, Scene{});
}

CommandPtr removeScene(const Song& song, int index) {
    if (song.scenes.size() <= 1 || index < 0 || index >= int(song.scenes.size()))
        return nullptr;
    return std::make_unique<RemoveScene>(index);
}

CommandPtr setScene(const Song& song, int index, Scene after, const std::string& name) {
    if (index < 0 || index >= int(song.scenes.size()) || song.scenes[size_t(index)] == after)
        return nullptr;
    return std::make_unique<SetScene>(name, index, song.scenes[size_t(index)], std::move(after));
}

// ---- Slots --------------------------------------------------------------------------------------------------

void SetSlot::set(Song& song, std::optional<ClipId> clip) const {
    Track* t = song.findTrack(track);
    assert(t && slot >= 0 && slot < int(t->slots.size()));
    if (t && slot >= 0 && slot < int(t->slots.size()))
        t->slots[size_t(slot)].clip = clip;
}

Clip copyClip(const Clip& clip, const NextId& nextId) {
    Clip copy = clip;
    copy.id = ClipId(nextId());
    for (Note& n : copy.notes)
        n.id = NoteId(nextId());
    for (AutomationEnvelope& a : copy.automation)
        a.id = AutomationId(nextId());
    return copy;
}

namespace {

std::optional<ClipId> slotClip(const Song& song, TrackId track, int slot) {
    const Track* t = song.findTrack(track);
    if (!t || slot < 0 || slot >= int(t->slots.size()))
        return std::nullopt;
    return t->slots[size_t(slot)].clip;
}

bool slotExists(const Song& song, TrackId track, int slot) {
    const Track* t = song.findTrack(track);
    return t && slot >= 0 && slot < int(t->slots.size());
}

/// Empties a slot and drops its clip from the pool when nothing else would use it.
void clearSlot(CompoundCommand& out, const Song& song, TrackId track, int slot, int extraUsesGone = 0) {
    std::optional<ClipId> old = slotClip(song, track, slot);
    if (!old)
        return;
    out.add(std::make_unique<SetSlot>(track, slot, old, std::nullopt));
    if (song.clipUses(*old) - 1 - extraUsesGone <= 0)
        if (const Clip* c = song.findClip(*old))
            out.add(std::make_unique<RemoveClip>(*c));
}

} // namespace

CommandPtr moveSlotClip(const Song& song, TrackId fromTrack, int fromSlot, TrackId toTrack, int toSlot) {
    std::optional<ClipId> clip = slotClip(song, fromTrack, fromSlot);
    if (!clip || !slotExists(song, toTrack, toSlot) || (fromTrack == toTrack && fromSlot == toSlot))
        return nullptr;
    auto compound = std::make_unique<CompoundCommand>("Move clip");
    // Whatever was in the target is replaced.
    if (std::optional<ClipId> target = slotClip(song, toTrack, toSlot); target && *target != *clip)
        clearSlot(*compound, song, toTrack, toSlot);
    compound->add(std::make_unique<SetSlot>(fromTrack, fromSlot, clip, std::nullopt));
    compound->add(std::make_unique<SetSlot>(toTrack, toSlot,
                                            slotClip(song, toTrack, toSlot) == clip ? clip : std::nullopt, clip));
    return compound;
}

CommandPtr copySlotClip(const Song& song, TrackId fromTrack, int fromSlot, TrackId toTrack, int toSlot,
                        const NextId& nextId) {
    std::optional<ClipId> clip = slotClip(song, fromTrack, fromSlot);
    const Clip* source = clip ? song.findClip(*clip) : nullptr;
    if (!source || !slotExists(song, toTrack, toSlot) || (fromTrack == toTrack && fromSlot == toSlot))
        return nullptr;
    auto compound = std::make_unique<CompoundCommand>("Copy clip");
    clearSlot(*compound, song, toTrack, toSlot);
    Clip copy = copyClip(*source, nextId);
    ClipId copyId = copy.id;
    compound->add(std::make_unique<AddClip>(std::move(copy)));
    compound->add(std::make_unique<SetSlot>(toTrack, toSlot, std::nullopt, copyId));
    return compound;
}

CommandPtr duplicateSlotClip(const Song& song, TrackId track, int slot, const NextId& nextId) {
    std::optional<ClipId> clip = slotClip(song, track, slot);
    const Clip* source = clip ? song.findClip(*clip) : nullptr;
    if (!source)
        return nullptr;
    auto compound = std::make_unique<CompoundCommand>("Duplicate clip");
    int target = slot + 1;
    if (!slotExists(song, track, target) || slotClip(song, track, target)) {
        if (int(song.scenes.size()) >= kMaxScenes)
            return nullptr;
        compound->add(std::make_unique<InsertScene>(target, Scene{}));
    }
    Clip copy = copyClip(*source, nextId);
    ClipId id = copy.id;
    compound->add(std::make_unique<AddClip>(std::move(copy)));
    compound->add(std::make_unique<SetSlot>(track, target, std::nullopt, id));
    return compound;
}

CommandPtr deleteSlotClip(const Song& song, TrackId track, int slot) {
    if (!slotClip(song, track, slot))
        return nullptr;
    auto compound = std::make_unique<CompoundCommand>("Delete clip");
    clearSlot(*compound, song, track, slot);
    return compound;
}

CommandPtr importClipToSlot(const Song& song, TrackId track, int slot, const Clip& clip, const NextId& nextId) {
    if (!slotExists(song, track, slot))
        return nullptr;
    auto compound = std::make_unique<CompoundCommand>("Import clip");
    clearSlot(*compound, song, track, slot);
    Clip copy = copyClip(clip, nextId);
    ClipId id = copy.id;
    compound->add(std::make_unique<AddClip>(std::move(copy)));
    compound->add(std::make_unique<SetSlot>(track, slot, std::nullopt, id));
    return compound;
}

} // namespace tessera

namespace tessera {

namespace {

std::string lengthName(Tick length, TimeSig ts) {
    Tick bar = ticksPerBar(ts), beat = ticksPerBeat(ts);
    if (length % bar == 0)
        return std::to_string(length / bar) + (length == bar ? " bar" : " bars");
    if (length % beat == 0)
        return std::to_string(length / beat) + (length == beat ? " beat" : " beats");
    return std::to_string(length) + " ticks";
}

/// The loop of a looping clip, or the whole clip.
std::pair<Tick, Tick> playedSpan(const Clip& c) { return std::make_pair(c.playStart(), c.playEnd()); }

} // namespace

CommandPtr setLoopLength(const Song& song, ClipId id, Tick length) {
    const Clip* c = song.findClip(id);
    // Without a loop the notes set the length.
    if (!c || length < 1 || c->loopMode != LoopMode::Loop)
        return nullptr;
    auto [start, end] = playedSpan(*c);
    Tick newEnd = start + length;
    if (newEnd == end)
        return nullptr;
    auto out = std::make_unique<CompoundCommand>("Clip length " + lengthName(length, song.timeSig));
    if (c->loopMode == LoopMode::Loop)
        out->add(setClipField(song, id, &Clip::loopEnd, newEnd, "Set loop end"));
    // The clip follows a loop that starts at its beginning, and grows for one that runs past its end.
    Tick newLength = start == 0 || newEnd > c->length ? newEnd : c->length;
    out->add(setClipField(song, id, &Clip::length, newLength, "Set clip length"));
    return out->empty() ? nullptr : std::move(out);
}

CommandPtr doubleLoop(const Song& song, ClipId id, const NextId& nextId) {
    const Clip* c = song.findClip(id);
    if (!c)
        return nullptr;
    auto [start, end] = playedSpan(*c);
    Tick length = end - start;
    if (length < 1)
        return nullptr;
    std::vector<Note> after = c->notes;
    for (const Note& n : c->notes) {
        if (n.start < start || n.start >= end)
            continue;
        Note copy = n;
        copy.id = NoteId(nextId());
        copy.start += length;
        after.push_back(copy);
    }
    std::sort(after.begin(), after.end(), noteOrder);
    auto out = std::make_unique<CompoundCommand>("Double loop");
    out->add(replaceNotes("Double loop", id, c->notes, after));
    out->add(setLoopLength(song, id, 2 * length));
    return out;
}

} // namespace tessera
