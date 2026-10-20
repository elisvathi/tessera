#include "Commands.hpp"

#include "ExpressionEdits.hpp"

#include <algorithm>
#include <cassert>
#include <unordered_set>

namespace tessera {

void ChangeSet::merge(const ChangeSet& other) {
    everything |= other.everything;
    tracks.insert(tracks.end(), other.tracks.begin(), other.tracks.end());
    clips.insert(clips.end(), other.clips.begin(), other.clips.end());
}

void CompoundCommand::apply(Song& song) {
    for (CommandPtr& c : commands)
        c->apply(song);
}

void CompoundCommand::revert(Song& song) {
    for (auto it = commands.rbegin(); it != commands.rend(); ++it)
        (*it)->revert(song);
}

ChangeSet CompoundCommand::changes() const {
    ChangeSet all;
    for (const CommandPtr& c : commands)
        all.merge(c->changes());
    return all;
}

EditNotes::EditNotes(std::string name, ClipId clip, std::vector<Note> removed, std::vector<Note> added)
    : label(std::move(name)), clip(clip), removed(std::move(removed)), added(std::move(added)) {}

void EditNotes::swap(Song& song, const std::vector<Note>& out, const std::vector<Note>& in) const {
    Clip* c = song.findClip(clip);
    assert(c && "EditNotes on a missing clip");
    if (!c)
        return;
    std::unordered_set<NoteId> gone;
    for (const Note& n : out)
        gone.insert(n.id);
    auto& notes = c->notes;
    notes.erase(std::remove_if(notes.begin(), notes.end(), [&](const Note& n) { return gone.count(n.id) > 0; }),
                notes.end());
    notes.insert(notes.end(), in.begin(), in.end());
    c->sortNotes();
}

std::string countNotes(size_t n) { return std::to_string(n) + (n == 1 ? " note" : " notes"); }

void AddClipToSlot::apply(Song& song) {
    Track* t = song.findTrack(track);
    assert(t && slot >= 0 && slot < int(t->slots.size()));
    if (!t || slot < 0 || slot >= int(t->slots.size()))
        return;
    previous = t->slots[size_t(slot)].clip;
    song.clips[clip.id] = clip;
    t->slots[size_t(slot)].clip = clip.id;
}

void AddClipToSlot::revert(Song& song) {
    if (Track* t = song.findTrack(track); t && slot >= 0 && slot < int(t->slots.size()))
        t->slots[size_t(slot)].clip = previous;
    song.clips.erase(clip.id);
}

std::string newClipName(const Song& song) {
    static const std::string prefix = "Clip ";
    long highest = 0;
    for (const auto& [id, clip] : song.clips) {
        if (clip.name.compare(0, prefix.size(), prefix) != 0 || clip.name.size() == prefix.size())
            continue;
        std::string digits = clip.name.substr(prefix.size());
        if (digits.size() < 10 &&
            std::all_of(digits.begin(), digits.end(), [](char c) { return c >= '0' && c <= '9'; }))
            highest = std::max(highest, std::stol(digits));
    }
    return prefix + std::to_string(highest + 1);
}

CommandPtr createClip(const Song& song, TrackId track, int slot, Tick length, ClipId id) {
    const Track* t = song.findTrack(track);
    if (!t || slot < 0 || slot >= int(t->slots.size()) || length < 1 || !id.valid())
        return nullptr;
    Clip clip;
    clip.id = id;
    clip.name = newClipName(song);
    clip.color = t->color;
    clip.length = length;
    clip.loopStart = 0;
    clip.loopEnd = length;
    return std::make_unique<AddClipToSlot>(track, slot, std::move(clip));
}

CommandPtr replaceNotes(const std::string& name, ClipId clip, const std::vector<Note>& before,
                        const std::vector<Note>& after) {
    auto findIn = [](const std::vector<Note>& list, NoteId id) -> const Note* {
        for (const Note& n : list)
            if (n.id == id)
                return &n;
        return nullptr;
    };
    std::vector<Note> removed, added;
    for (const Note& b : before) {
        const Note* a = findIn(after, b.id);
        if (!a || !(*a == b))
            removed.push_back(b);
    }
    for (const Note& a : after) {
        const Note* b = findIn(before, a.id);
        if (!b || !(*b == a))
            added.push_back(a);
    }
    if (removed.empty() && added.empty())
        return nullptr;
    return std::make_unique<EditNotes>(name, clip, std::move(removed), std::move(added));
}

CommandPtr editNotes(const Song& song, const std::string& name, ClipId id, const std::vector<Note>& before,
                     const std::vector<Note>& after) {
    CommandPtr notes = replaceNotes(name, id, before, after);
    const Clip* c = song.findClip(id);
    if (!notes || !c || c->loopMode != LoopMode::Loop || c->loopEnd <= c->loopStart || c->loopEnd < c->length)
        return notes;
    Tick end = c->loopEnd, bar = ticksPerBar(song.timeSig);
    std::unordered_set<NoteId> outside;
    for (const Note& n : before)
        if (n.start >= end)
            outside.insert(n.id);
    Tick grown = end;
    for (const Note& n : after) {
        if (n.start < end || outside.count(n.id))
            continue;
        // A strummed or humanized tail that hangs just past a bar line doesn't add a bar.
        Tick bars = std::max(n.start / bar + 1, (n.end() - kPpq / 8 + bar - 1) / bar);
        grown = std::max(grown, bars * bar);
    }
    if (grown == end)
        return notes;
    auto out = std::make_unique<CompoundCommand>(name);
    out->add(std::move(notes));
    out->add(setClipField(song, id, &Clip::loopEnd, grown, "Set loop end"));
    out->add(setClipField(song, id, &Clip::length, grown, "Set clip length"));
    return out;
}

namespace {

/// The current versions of the given notes, in clip order.
std::vector<Note> selectNotes(const Song& song, ClipId clip, const std::vector<NoteId>& ids) {
    std::vector<Note> out;
    const Clip* c = song.findClip(clip);
    if (!c)
        return out;
    std::unordered_set<NoteId> wanted(ids.begin(), ids.end());
    for (const Note& n : c->notes)
        if (wanted.count(n.id))
            out.push_back(n);
    return out;
}

template <typename F>
CommandPtr changeNotes(const Song& song, ClipId clip, const std::vector<NoteId>& ids, const std::string& verb,
                       F change) {
    std::vector<Note> before = selectNotes(song, clip, ids);
    std::vector<Note> after = before;
    for (Note& n : after)
        change(n);
    return replaceNotes(verb + " " + countNotes(before.size()), clip, before, after);
}

} // namespace

CommandPtr addNotes(Song& song, ClipId clip, std::vector<Note> notes) {
    if (notes.empty() || !song.findClip(clip))
        return nullptr;
    for (Note& n : notes)
        n.id = song.newId<NoteId>();
    std::string name = "Add " + countNotes(notes.size());
    return std::make_unique<EditNotes>(name, clip, std::vector<Note>{}, std::move(notes));
}

CommandPtr deleteNotes(const Song& song, ClipId clip, const std::vector<NoteId>& ids) {
    std::vector<Note> notes = selectNotes(song, clip, ids);
    if (notes.empty())
        return nullptr;
    std::string name = "Delete " + countNotes(notes.size());
    return std::make_unique<EditNotes>(name, clip, std::move(notes), std::vector<Note>{});
}

CommandPtr moveNotes(const Song& song, ClipId clip, const std::vector<NoteId>& ids, Tick deltaTime, int deltaPitch) {
    return changeNotes(song, clip, ids, "Move", [&](Note& n) {
        n.start = std::max<Tick>(0, n.start + deltaTime);
        n.pitch = int8_t(std::clamp(n.pitch + deltaPitch, 0, 127));
    });
}

CommandPtr resizeNotes(const Song& song, ClipId clip, const std::vector<NoteId>& ids, Tick deltaStart, Tick deltaEnd) {
    return changeNotes(song, clip, ids, "Resize", [&](Note& n) {
        Tick end = std::max<Tick>(1, n.end() + deltaEnd), oldStart = n.start, oldLength = n.length;
        n.start = std::clamp<Tick>(n.start + deltaStart, 0, end - 1);
        n.length = std::max<Tick>(1, end - n.start);
        expr::keepInTime(n, oldStart, oldLength);
    });
}

CommandPtr setVelocity(const Song& song, ClipId clip, const std::vector<NoteId>& ids, float velocity) {
    float v = std::clamp(velocity, 0.f, 1.f);
    return changeNotes(song, clip, ids, "Set velocity of", [&](Note& n) { n.velocity = v; });
}

} // namespace tessera
