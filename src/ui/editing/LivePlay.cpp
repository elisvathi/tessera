#include "LivePlay.hpp"

#include "../../module/TesseraModule.hpp"
#include "../views/PianoRoll.hpp"

namespace tessera {

namespace {

constexpr int kBasePitch = 60;
constexpr float kVelocityStep = 20.f / 127;

struct KeyNote {
    int key;
    int semitone;
};
// Piano layout: white keys on the home row, black keys on the row above.
const KeyNote kKeys[] = {
    {GLFW_KEY_A, 0},  {GLFW_KEY_W, 1},          {GLFW_KEY_S, 2},           {GLFW_KEY_E, 3},  {GLFW_KEY_D, 4},
    {GLFW_KEY_F, 5},  {GLFW_KEY_T, 6},          {GLFW_KEY_G, 7},           {GLFW_KEY_Y, 8},  {GLFW_KEY_H, 9},
    {GLFW_KEY_U, 10}, {GLFW_KEY_J, 11},         {GLFW_KEY_K, 12},          {GLFW_KEY_O, 13}, {GLFW_KEY_L, 14},
    {GLFW_KEY_P, 15}, {GLFW_KEY_SEMICOLON, 16}, {GLFW_KEY_APOSTROPHE, 17},
};

} // namespace

// ---- Keyboard play ------------------------------------------------------------------------------------------

std::optional<int> KeyboardPlay::pitchFor(int key, int octave) {
    for (const KeyNote& k : kKeys)
        if (k.key == key) {
            int p = kBasePitch + 12 * octave + k.semitone;
            if (p < 0 || p > 127)
                return std::nullopt;
            return p;
        }
    return std::nullopt;
}

bool KeyboardPlay::handleKey(EditorContext& ctx, const rack::widget::Widget::KeyBaseEvent& e) {
    TesseraModule* m = ctx.module();
    if (!m || !ctx.state().keyboardPlay)
        return false;
    EditorState& state = ctx.state();
    // With Ctrl or Alt held, keys are shortcuts again (Ctrl+K leaves keyboard play).
    if (e.mods & (GLFW_MOD_CONTROL | GLFW_MOD_ALT | GLFW_MOD_SUPER))
        return false;
    if (e.action == GLFW_RELEASE) {
        auto it = held.find(e.key);
        if (it == held.end())
            return pitchFor(e.key, 0).has_value();
        m->playLive(it->second, 0, false);
        if (heardOn.valid())
            m->audition(heardOn, it->second, 0, false);
        held.erase(it);
        return true;
    }
    if (e.action == GLFW_REPEAT)
        return pitchFor(e.key, 0) || e.key == GLFW_KEY_Z || e.key == GLFW_KEY_X || e.key == GLFW_KEY_C ||
               e.key == GLFW_KEY_V;
    switch (e.key) {
    case GLFW_KEY_Z:
        state.keyboardOctave = std::max(-5, state.keyboardOctave - 1);
        return true;
    case GLFW_KEY_X:
        state.keyboardOctave = std::min(5, state.keyboardOctave + 1);
        return true;
    case GLFW_KEY_C:
        state.keyboardVelocity = std::max(1.f / 127, state.keyboardVelocity - kVelocityStep);
        return true;
    case GLFW_KEY_V:
        state.keyboardVelocity = std::min(1.f, state.keyboardVelocity + kVelocityStep);
        return true;
    default:
        break;
    }
    std::optional<int> pitch = pitchFor(e.key, state.keyboardOctave);
    if (!pitch)
        return false;
    if (held.count(e.key))
        return true;
    held[e.key] = *pitch;
    m->playLive(*pitch, state.keyboardVelocity, true);
    // Armed tracks hear the note through the engine; otherwise play it on the track being edited.
    const Track* t = m->song().findTrack(state.selectedTrack);
    heardOn = t && !t->armed ? t->id : TrackId();
    if (heardOn.valid())
        m->audition(heardOn, *pitch, state.keyboardVelocity, true);
    return true;
}

void KeyboardPlay::releaseAll(EditorContext& ctx) {
    TesseraModule* m = ctx.module();
    if (m)
        for (const auto& [key, pitch] : held) {
            m->playLive(pitch, 0, false);
            if (heardOn.valid())
                m->audition(heardOn, pitch, 0, false);
        }
    held.clear();
}

// ---- Step input ---------------------------------------------------------------------------------------------

Tick StepInput::step(PianoRollView& view) {
    Tick s = view.snapStep();
    return s > 0 ? s : kPpq / 4;
}

void StepInput::start(PianoRollView& view) {
    down.clear();
    chord.clear();
    chordAt = -1;
    history.clear();
    Tick cursor = 0;
    edits::IdSet selected = view.selectedIds();
    if (!selected.empty())
        cursor = edits::span(view.committedNotes(), selected).second;
    view.context().state().stepCursor = cursor;
    if (TesseraModule* m = view.context().module())
        m->takeLiveInput(); // notes played before step input began don't count
}

void StepInput::update(PianoRollView& view) {
    TesseraModule* m = view.context().module();
    if (!m)
        return;
    std::vector<RecordEvent> events = m->takeLiveInput();
    if (events.empty() || !view.clip())
        return;
    EditorState& state = view.context().state();
    for (const RecordEvent& e : events) {
        if (!e.on) {
            down.erase(e.pitch);
            if (down.empty() && chordAt >= 0) {
                state.stepCursor = chordAt + step(view);
                chordAt = -1;
            }
            continue;
        }
        if (down.empty() && chordAt < 0) {
            chordAt = state.stepCursor;
            chord.clear();
            history.emplace_back();
        }
        down.insert(e.pitch);
        Note n;
        n.id = view.newNoteId();
        n.start = chordAt;
        n.length = step(view);
        n.pitch = int8_t(view.placePitch(e.pitch, false));
        n.velocity = e.velocity;
        n.channel = view.drawChannel();
        edits::NoteList before = view.committedNotes();
        edits::NoteList after = before;
        after.push_back(n);
        std::sort(after.begin(), after.end(), noteOrder);
        after = edits::resolveOverlaps(after, {n.id});
        chord.insert(n.id);
        history.back().insert(n.id);
        view.commit("Step input " + pitchName(n.pitch), before, after, chord);
    }
}

void StepInput::rest(PianoRollView& view) {
    view.context().state().stepCursor += step(view);
    history.emplace_back();
}

void StepInput::tie(PianoRollView& view) {
    EditorState& state = view.context().state();
    if (history.empty() || history.back().empty()) {
        rest(view);
        return;
    }
    edits::NoteList before = view.committedNotes();
    view.commit("Tie step", before, edits::resizeEnd(before, history.back(), step(view), 1), history.back());
    state.stepCursor += step(view);
}

void StepInput::undoStep(PianoRollView& view) {
    EditorState& state = view.context().state();
    if (history.empty()) {
        back(view);
        return;
    }
    edits::IdSet last = history.back();
    history.pop_back();
    if (last.empty()) {
        back(view);
        return;
    }
    edits::NoteList before = view.committedNotes();
    edits::NoteList after;
    Tick start = state.stepCursor;
    for (const Note& n : before) {
        if (last.count(n.id))
            start = std::min(start, n.start);
        else
            after.push_back(n);
    }
    view.commit("Remove step", before, after, edits::IdSet{});
    state.stepCursor = start;
}

void StepInput::back(PianoRollView& view) {
    EditorState& state = view.context().state();
    state.stepCursor = std::max<Tick>(0, state.stepCursor - step(view));
}

} // namespace tessera
