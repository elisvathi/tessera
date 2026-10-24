#pragma once

#include "../../model/NoteEdits.hpp"
#include "../../model/Song.hpp"

#include <optional>
#include <string>

namespace tessera {

/// An editor of one clip's notes: the piano roll, which is also the drum editor. The inspector and the drum hit menu
/// work through this, so they edit the selection of the editor that is showing.
class NoteTarget {
  public:
    virtual ~NoteTarget() = default;

    virtual const Clip* clip() = 0;
    /// The notes to draw: a running gesture's preview, or the clip's.
    virtual const edits::NoteList& notes() = 0;
    /// The clip's notes as committed, for gestures to start from.
    virtual const edits::NoteList& committedNotes() = 0;
    virtual void setPreview(edits::NoteList notes) = 0;
    virtual void clearPreview() = 0;

    /// The selected notes of this clip (empty when the selection belongs to another clip).
    virtual edits::IdSet selectedIds() = 0;
    virtual bool isSelected(NoteId id) = 0;
    virtual void select(edits::IdSet ids) = 0;

    /// Records `before` -> `after` as one undo step named `name`. Without `selectAfter` the selection stays, minus
    /// notes that are gone.
    virtual void commit(const std::string& name, const edits::NoteList& before, const edits::NoteList& after,
                        std::optional<edits::IdSet> selectAfter = std::nullopt) = 0;
};

} // namespace tessera
