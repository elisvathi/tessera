#pragma once

#include "TrackCommands.hpp"

/// Commands for the clip launcher: scenes (rows) and the clips in each track's slots (DESIGN.md §10).
namespace tessera {

/// Inserts an empty scene: one new empty slot on every track.
class InsertScene : public Command {
  public:
    InsertScene(int index, Scene scene) : index(index), scene(std::move(scene)) {}
    void apply(Song& song) override;
    void revert(Song& song) override;
    std::string name() const override { return "Add scene"; }
    ChangeSet changes() const override { return ChangeSet::all(); }

  private:
    int index;
    Scene scene;
};

/// Removes a scene with its slots. Clips used nowhere else leave the pool; undo brings everything back.
class RemoveScene : public Command {
  public:
    explicit RemoveScene(int index) : index(index) {}
    void apply(Song& song) override;
    void revert(Song& song) override;
    std::string name() const override { return "Delete scene"; }
    ChangeSet changes() const override { return ChangeSet::all(); }

  private:
    int index;
    Scene scene;
    std::vector<std::pair<TrackId, ClipSlot>> slots;
    std::vector<Clip> clips;
};

/// Changes a scene's name, colour or tempo.
class SetScene : public Command {
  public:
    SetScene(std::string name, int index, Scene before, Scene after)
        : label(std::move(name)), index(index), before(std::move(before)), after(std::move(after)) {}
    void apply(Song& song) override { song.scenes[size_t(index)] = after; }
    void revert(Song& song) override { song.scenes[size_t(index)] = before; }
    std::string name() const override { return label; }
    ChangeSet changes() const override { return ChangeSet::all(); }

  private:
    std::string label;
    int index;
    Scene before, after;
};

/// Sets which clip a launcher slot holds.
class SetSlot : public Command {
  public:
    SetSlot(TrackId track, int slot, std::optional<ClipId> before, std::optional<ClipId> after)
        : track(track), slot(slot), before(before), after(after) {}
    void apply(Song& song) override { set(song, after); }
    void revert(Song& song) override { set(song, before); }
    std::string name() const override { return "Set slot"; }
    ChangeSet changes() const override { return ChangeSet::track(track); }

  private:
    void set(Song& song, std::optional<ClipId> clip) const;
    TrackId track;
    int slot;
    std::optional<ClipId> before, after;
};

/// Puts a clip into the pool (no slot) / takes one out.
class AddClip : public Command {
  public:
    explicit AddClip(Clip clip) : clip(std::move(clip)) {}
    void apply(Song& song) override { song.clips[clip.id] = clip; }
    void revert(Song& song) override { song.clips.erase(clip.id); }
    std::string name() const override { return "Add clip"; }
    ChangeSet changes() const override { return ChangeSet::clip(clip.id); }

  private:
    Clip clip;
};

class RemoveClip : public Command {
  public:
    explicit RemoveClip(Clip clip) : clip(std::move(clip)) {}
    void apply(Song& song) override { song.clips.erase(clip.id); }
    void revert(Song& song) override { song.clips[clip.id] = clip; }
    std::string name() const override { return "Remove clip"; }
    ChangeSet changes() const override { return ChangeSet::clip(clip.id); }

  private:
    Clip clip;
};

/// An independent copy of a clip: new IDs for the clip, its notes and its automation lanes.
Clip copyClip(const Clip& clip, const NextId& nextId);

CommandPtr insertScene(const Song& song, int index);
/// Null when it is the last scene.
CommandPtr removeScene(const Song& song, int index);
CommandPtr setScene(const Song& song, int index, Scene after, const std::string& name);

/// Moves the clip in one slot to another (empty or not; a clip there is replaced and, if unused elsewhere,
/// removed). Null when the source is empty or it is the same slot.
CommandPtr moveSlotClip(const Song& song, TrackId fromTrack, int fromSlot, TrackId toTrack, int toSlot);
/// Copies a slot's clip as an independent clip into another slot.
CommandPtr copySlotClip(const Song& song, TrackId fromTrack, int fromSlot, TrackId toTrack, int toSlot,
                        const NextId& nextId);
/// Copies a slot's clip into the slot below: into it when it is empty, otherwise into a new scene inserted
/// there. Null when the slot is empty or there is no room for another scene.
CommandPtr duplicateSlotClip(const Song& song, TrackId track, int slot, const NextId& nextId);
/// Empties a slot; its clip leaves the pool when nothing else uses it.
CommandPtr deleteSlotClip(const Song& song, TrackId track, int slot);
/// Sets the clip's loop (or, when it doesn't loop, the clip) to `length` ticks from the loop start. The clip
/// grows to hold the loop; notes stay where they are. Null when nothing changes.
CommandPtr setLoopLength(const Song& song, ClipId clip, Tick length);
/// Doubles the loop and copies its notes into the new half.
CommandPtr doubleLoop(const Song& song, ClipId clip, const NextId& nextId);

/// Puts a clip (e.g. imported from a file) into an empty slot, with fresh IDs.
CommandPtr importClipToSlot(const Song& song, TrackId track, int slot, const Clip& clip, const NextId& nextId);

} // namespace tessera
