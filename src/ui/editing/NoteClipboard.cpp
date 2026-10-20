#include "NoteClipboard.hpp"

#include <algorithm>

namespace tessera {

NoteClipboard& NoteClipboard::get() {
    static NoteClipboard clipboard;
    return clipboard;
}

void NoteClipboard::copy(const edits::NoteList& source) {
    notes = source;
    if (notes.empty())
        return;
    Tick first = notes.front().start;
    for (const Note& n : notes)
        first = std::min(first, n.start);
    for (Note& n : notes)
        n.start -= first;
}

edits::NoteList NoteClipboard::pasteAt(Tick at, const edits::IdSource& newId) const {
    edits::NoteList out = notes;
    for (Note& n : out) {
        n.id = newId();
        n.start += std::max<Tick>(0, at);
    }
    return out;
}

} // namespace tessera
