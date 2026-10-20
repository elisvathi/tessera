#pragma once

#include "../../model/NoteEdits.hpp"

namespace tessera {

/// Copied notes, shared by every Tessera and editor in the session (copy in one clip, paste in another).
/// Notes are stored relative to the earliest one, so pasting keeps their timing (EDITOR_UX §4.3).
class NoteClipboard {
  public:
    static NoteClipboard& get();

    void copy(const edits::NoteList& notes);
    bool empty() const { return notes.empty(); }

    /// The clipboard's notes placed at `at`, with fresh IDs.
    edits::NoteList pasteAt(Tick at, const edits::IdSource& newId) const;

  private:
    edits::NoteList notes; // starts relative to the first note
};

} // namespace tessera
