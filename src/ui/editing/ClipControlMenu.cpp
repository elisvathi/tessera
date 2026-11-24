#include "ClipControlMenu.hpp"

#include "../../module/TesseraModule.hpp"
#include "../ModuleUndo.hpp"

namespace tessera::clipControlMenu {

using namespace clipControl;

namespace {

/// "C3" for note 60, as the piano roll names notes.
std::string noteName(int n) {
    static const char* const names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    return names[n % 12] + std::to_string(n / 12 - 1);
}

/// Changes the clip control settings as one undo step.
void change(TesseraModule* m, const std::string& name, const std::function<void(Settings&)>& f) {
    withModuleUndo(m, name, [&] {
        Settings s = m->clipControlSettings();
        f(s);
        m->setClipControlSettings(s);
    });
}

} // namespace

std::string controlText(const Binding& b) {
    std::string ch = ", ch " + std::to_string(b.channel + 1);
    switch (b.type) {
    case Binding::Type::Cc:
        return "CC " + std::to_string(b.number) + ch;
    case Binding::Type::Program:
        return "Program " + std::to_string(b.number + 1) + ch;
    case Binding::Type::Note:
    default:
        return "Note " + noteName(b.number) + ch;
    }
}

void append(rack::ui::Menu* menu, TesseraModule* m) {
    menu->addChild(rack::createMenuLabel("Launch clips from a MIDI controller"));
    menu->addChild(rack::createSubmenuItem("Controller", layoutName(m->clipControlSettings().layout), [m](rack::ui::Menu* sub) {
        for (int i = 0; i < kLayouts; i++) {
            Layout l = Layout(i);
            sub->addChild(rack::createCheckMenuItem(
                layoutName(l), "", [m, l] { return m->clipControlSettings().layout == l; },
                [m, l] { change(m, "pick clip controller", [l](Settings& s) { s.layout = l; }); }));
        }
    }));
    menu->addChild(rack::createSubmenuItem("Controller input", "", [m](rack::ui::Menu* sub) {
        rack::app::appendMidiMenu(sub, &m->clipControlInput());
    }));
    if (hasLights(m->clipControlSettings().layout))
        menu->addChild(rack::createSubmenuItem("Controller lights (its MIDI out)", "", [m](rack::ui::Menu* sub) {
            sub->addChild(rack::createMenuLabel("Pick the same controller (on a Launchpad, its MIDI port)"));
            rack::app::appendMidiMenu(sub, &m->clipControlOutput());
        }));
    if (m->clipControlSettings().layout == Layout::NoteGrid)
        menu->addChild(rack::createSubmenuItem(
            "Bottom-left pad", noteName(m->clipControlSettings().noteBase), [m](rack::ui::Menu* sub) {
                for (int note : {0, 12, 24, 36, 48, 60})
                    sub->addChild(rack::createCheckMenuItem(
                        noteName(note) + " (" + std::to_string(note) + ")", "",
                        [m, note] { return m->clipControlSettings().noteBase == note; },
                        [m, note] { change(m, "set pad notes", [note](Settings& s) { s.noteBase = note; }); }));
            }));
    int channel = m->clipControlSettings().channel;
    menu->addChild(rack::createSubmenuItem(
        "Channel", channel == 0 ? "Any" : std::to_string(channel), [m](rack::ui::Menu* sub) {
            for (int c = 0; c <= 16; c++)
                sub->addChild(rack::createCheckMenuItem(
                    c == 0 ? "Any" : std::to_string(c), "", [m, c] { return m->clipControlSettings().channel == c; },
                    [m, c] { change(m, "set clip control channel", [c](Settings& s) { s.channel = c; }); }));
        }));
    menu->addChild(rack::createCheckMenuItem(
        "Program changes launch scenes", "", [m] { return m->clipControlSettings().programScenes; },
        [m] {
            change(m, "switch program changes", [](Settings& s) { s.programScenes = !s.programScenes; });
        }));
    size_t learned = m->clipControlSettings().learned.size();
    menu->addChild(rack::createMenuItem(
        "Forget learned controls", learned ? std::to_string(learned) : "",
        [m] { change(m, "forget learned controls", [](Settings& s) { s.learned.clear(); }); }, learned == 0));
    menu->addChild(rack::createMenuLabel("Learn a control: right-click a slot or scene → Learn MIDI"));
}

void appendLearnItems(rack::ui::Menu* menu, TesseraModule* m, const Binding& action) {
    menu->addChild(new rack::ui::MenuSeparator);
    const Binding* has = m->clipControlFor(action);
    bool waiting = m->learningClipControl();
    menu->addChild(rack::createMenuItem(waiting ? "Learn MIDI (waiting…)" : "Learn MIDI",
                                        has ? controlText(*has) : "press a pad or key after",
                                        [m, action] { m->learnClipControl(action); }));
    if (has)
        menu->addChild(rack::createMenuItem("Forget MIDI", "", [m, action] {
            change(m, "forget MIDI control", [&action](Settings& s) {
                s.learned.erase(std::remove_if(s.learned.begin(), s.learned.end(),
                                               [&](const Binding& b) { return b.sameAction(action); }),
                                s.learned.end());
            });
        }));
}

} // namespace tessera::clipControlMenu
