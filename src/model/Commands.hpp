#pragma once

#include "Song.hpp"

#include <memory>
#include <string>
#include <vector>

namespace tessera {

/// What an edit touched, so the playback snapshot only rebuilds what changed.
struct ChangeSet {
    bool everything = false; // song-level timing, track list or order
    std::vector<TrackId> tracks;
    std::vector<ClipId> clips;

    void merge(const ChangeSet& other);
    bool empty() const { return !everything && tracks.empty() && clips.empty(); }

    static ChangeSet all() {
        ChangeSet c;
        c.everything = true;
        return c;
    }
    static ChangeSet track(TrackId id) { return ChangeSet{false, {id}, {}}; }
    static ChangeSet clip(ClipId id) { return ChangeSet{false, {}, {id}}; }
};

/// One reversible edit. apply() then revert() must leave the song exactly as it was.
/// Commands store the smallest diff they need, never a copy of the whole song.
class Command {
  public:
    virtual ~Command() = default;
    virtual void apply(Song& song) = 0;
    virtual void revert(Song& song) = 0;
    /// Shown in Rack's Edit menu, e.g. "Move 12 notes".
    virtual std::string name() const = 0;
    virtual ChangeSet changes() const = 0;
};

using CommandPtr = std::unique_ptr<Command>;

/// Several commands as one undo step. Reverts in reverse order.
class CompoundCommand : public Command {
  public:
    explicit CompoundCommand(std::string name) : label(std::move(name)) {}
    /// Null commands (edits that changed nothing) are skipped.
    void add(CommandPtr command) {
        if (command)
            commands.push_back(std::move(command));
    }
    bool empty() const { return commands.empty(); }

    void apply(Song& song) override;
    void revert(Song& song) override;
    std::string name() const override { return label; }
    ChangeSet changes() const override;

  private:
    std::string label;
    std::vector<CommandPtr> commands;
};

// ---- Notes --------------------------------------------------------------------------------------------------

/// The one note command: removes some notes and adds others. Adding, deleting, moving, resizing and every
/// transform are all expressed this way. A changed note appears in both lists with the same ID.
class EditNotes : public Command {
  public:
    EditNotes(std::string name, ClipId clip, std::vector<Note> removed, std::vector<Note> added);

    void apply(Song& song) override { swap(song, removed, added); }
    void revert(Song& song) override { swap(song, added, removed); }
    std::string name() const override { return label; }
    ChangeSet changes() const override { return ChangeSet::clip(clip); }

  private:
    void swap(Song& song, const std::vector<Note>& out, const std::vector<Note>& in) const;

    std::string label;
    ClipId clip;
    std::vector<Note> removed;
    std::vector<Note> added;
};

/// Builds an EditNotes holding only the notes that differ between `before` and `after`.
/// Returns nullptr when nothing changed, so no empty undo step is recorded.
CommandPtr replaceNotes(const std::string& name, ClipId clip, const std::vector<Note>& before,
                        const std::vector<Note>& after);
/// replaceNotes for an edit in the piano roll or drum editor. When the clip's loop runs to its end, notes the edit
/// puts past that end (drawn, pasted, duplicated or moved there) grow the loop and the clip in whole bars to hold
/// them, so they play. Notes that were already past the end don't, and neither does a loop
/// that ends inside the clip: that was set on purpose.
CommandPtr editNotes(const Song& song, const std::string& name, ClipId clip, const std::vector<Note>& before,
                     const std::vector<Note>& after);

/// New notes get fresh IDs from the song.
CommandPtr addNotes(Song& song, ClipId clip, std::vector<Note> notes);
CommandPtr deleteNotes(const Song& song, ClipId clip, const std::vector<NoteId>& ids);
CommandPtr moveNotes(const Song& song, ClipId clip, const std::vector<NoteId>& ids, Tick deltaTime, int deltaPitch);
/// Moves the start and/or end of each note. Lengths never drop below one tick.
CommandPtr resizeNotes(const Song& song, ClipId clip, const std::vector<NoteId>& ids, Tick deltaStart, Tick deltaEnd);
CommandPtr setVelocity(const Song& song, ClipId clip, const std::vector<NoteId>& ids, float velocity);

/// "1 note" / "12 notes", for command names.
std::string countNotes(size_t n);

// ---- Clips --------------------------------------------------------------------------------------------------

/// Puts a new clip into the pool and into a track's launcher slot.
class AddClipToSlot : public Command {
  public:
    AddClipToSlot(TrackId track, int slot, Clip clip) : track(track), slot(slot), clip(std::move(clip)) {}

    void apply(Song& song) override;
    void revert(Song& song) override;
    std::string name() const override { return "Create clip"; }
    ChangeSet changes() const override { return ChangeSet{false, {track}, {clip.id}}; }

  private:
    TrackId track;
    int slot;
    Clip clip;
    std::optional<ClipId> previous;
};

/// The name for a new clip: "Clip N", one past the highest N any clip in the song already uses, so names stay
/// unique across tracks and never read like a track name ("Track 1 1").
std::string newClipName(const Song& song);

/// An empty looping clip `id` of `length` ticks in the track's colour, for `slot`. Null when the slot doesn't
/// exist. Get `id` from Song::newId (or the module's newId).
CommandPtr createClip(const Song& song, TrackId track, int slot, Tick length, ClipId id);

// ---- Properties ---------------------------------------------------------------------------------------------

/// Sets one field of a track, e.g. `setTrackField(song, id, &Track::mute, true, "Mute Bass")`.
template <typename T>
class SetTrackField : public Command {
  public:
    SetTrackField(std::string name, TrackId track, T Track::* field, T before, T after)
        : label(std::move(name)), track(track), field(field), before(std::move(before)), after(std::move(after)) {}

    void apply(Song& song) override { set(song, after); }
    void revert(Song& song) override { set(song, before); }
    std::string name() const override { return label; }
    ChangeSet changes() const override { return ChangeSet::track(track); }

  private:
    void set(Song& song, const T& value) const {
        if (Track* t = song.findTrack(track))
            t->*field = value;
    }

    std::string label;
    TrackId track;
    T Track::* field;
    T before, after;
};

template <typename T>
CommandPtr setTrackField(const Song& song, TrackId id, T Track::* field, T value, const std::string& name) {
    const Track* t = song.findTrack(id);
    if (!t || t->*field == value)
        return nullptr;
    return std::make_unique<SetTrackField<T>>(name, id, field, t->*field, std::move(value));
}

/// Sets one field of a clip, e.g. its name, color or loop end.
template <typename T>
class SetClipField : public Command {
  public:
    SetClipField(std::string name, ClipId clip, T Clip::* field, T before, T after)
        : label(std::move(name)), clip(clip), field(field), before(std::move(before)), after(std::move(after)) {}

    void apply(Song& song) override { set(song, after); }
    void revert(Song& song) override { set(song, before); }
    std::string name() const override { return label; }
    ChangeSet changes() const override { return ChangeSet::clip(clip); }

  private:
    void set(Song& song, const T& value) const {
        if (Clip* c = song.findClip(clip))
            c->*field = value;
    }

    std::string label;
    ClipId clip;
    T Clip::* field;
    T before, after;
};

template <typename T>
CommandPtr setClipField(const Song& song, ClipId id, T Clip::* field, T value, const std::string& name) {
    const Clip* c = song.findClip(id);
    if (!c || c->*field == value)
        return nullptr;
    return std::make_unique<SetClipField<T>>(name, id, field, c->*field, std::move(value));
}

/// Sets one song-wide field, e.g. the time signature or swing. Rebuilds all playback.
template <typename T>
class SetSongField : public Command {
  public:
    SetSongField(std::string name, T Song::* field, T before, T after)
        : label(std::move(name)), field(field), before(std::move(before)), after(std::move(after)) {}

    void apply(Song& song) override { song.*field = after; }
    void revert(Song& song) override { song.*field = before; }
    std::string name() const override { return label; }
    ChangeSet changes() const override { return ChangeSet::all(); }

  private:
    std::string label;
    T Song::* field;
    T before, after;
};

template <typename T>
CommandPtr setSongField(const Song& song, T Song::* field, T value, const std::string& name) {
    if (song.*field == value)
        return nullptr;
    return std::make_unique<SetSongField<T>>(name, field, song.*field, std::move(value));
}

} // namespace tessera
