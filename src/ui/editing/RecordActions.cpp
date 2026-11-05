#include "RecordActions.hpp"

#include "../../module/TesseraModule.hpp"
#include "../interaction/Shortcuts.hpp"
#include "../widgets/MenuTextField.hpp"

namespace tessera::recordActions {

namespace {

/// A check item for one setting value; picking it changes the module's record settings.
template <typename T>
rack::ui::MenuItem* settingItem(EditorContext* c, const std::string& label, T RecordSettings::* field, T value) {
    return rack::createCheckMenuItem(
        label, "", [c, field, value] { return c->module() && c->module()->recordSettings().*field == value; },
        [c, field, value] {
            if (TesseraModule* m = c->module()) {
                RecordSettings s = m->recordSettings();
                s.*field = value;
                m->setRecordSettings(s);
            }
        });
}

std::string bars(int n) { return std::to_string(n) + (n == 1 ? " bar" : " bars"); }

} // namespace

void capture(EditorContext& ctx) {
    TesseraModule* m = ctx.module();
    if (!m || !ctx.state().selectedTrack.valid())
        return;
    ClipId clip = m->capture(ctx.state().selectedTrack);
    if (clip.valid())
        ctx.state().selectedClip = clip;
}

void openMenu(EditorContext& ctx) {
    TesseraModule* m = ctx.module();
    if (!m)
        return;
    EditorContext* c = &ctx;
    const RecordSettings& s = m->recordSettings();
    rack::ui::Menu* menu = rack::createMenu();
    menu->addChild(rack::createMenuLabel("Recording"));

    menu->addChild(
        rack::createSubmenuItem("Count-in", s.countInBars == 0 ? "Off" : bars(s.countInBars), [c](rack::ui::Menu* sub) {
            sub->addChild(settingItem(c, "Off", &RecordSettings::countInBars, 0));
            for (int b = 1; b <= RecordSettings::kMaxCountInBars; b++)
                sub->addChild(settingItem(c, bars(b), &RecordSettings::countInBars, b));
        }));
    static const char* const kMetronome[] = {"Off", "Count-in only", "While recording", "Always"};
    menu->addChild(rack::createSubmenuItem(
        "Metronome", kMetronome[int(s.metronome)], [c](rack::ui::Menu* sub) {
            sub->addChild(rack::createMenuLabel("A click on MAIN only, never on OUT or in exports"));
            for (int i = 0; i < 4; i++)
                sub->addChild(settingItem(c, kMetronome[i], &RecordSettings::metronome, MetronomeMode(i)));
            sub->addChild(new rack::ui::MenuSeparator);
            sub->addChild(rack::createMenuLabel("Level"));
            for (float db : {-24.f, -18.f, -12.f, -6.f, 0.f})
                sub->addChild(settingItem(c, std::to_string(int(db)) + " dB", &RecordSettings::metronomeDb, db));
        }));
    double rate = APP->engine->getSampleRate() > 0 ? double(APP->engine->getSampleRate()) : 48000;
    char offset[32];
    snprintf(offset, sizeof offset, "%.1f ms", s.recordOffsetSamples * 1000.0 / rate);
    menu->addChild(rack::createSubmenuItem("Recording offset", offset, [c, rate](rack::ui::Menu* sub) {
        sub->addChild(rack::createMenuLabel("How late audio comes back through the sound card; takes move earlier"));
        sub->addChild(rack::createMenuItem("Measure it", "", [c] {
            TesseraModule* m = c->module();
            if (!m || !m->measureOffset())
                c->toast("Arm an audio track first, and patch CLICK out through the sound card into its Track Out IN");
            else
                c->toast("Measuring: four clicks on CLICK");
        }));
        sub->addChild(rack::createMenuLabel("First patch CLICK through the sound card into the armed track's IN"));
        sub->addChild(new rack::ui::MenuSeparator);
        sub->addChild(rack::createMenuLabel("Type it (ms)"));
        sub->addChild(new MenuTextField("", [c, rate](const std::string& text) {
            char* end = nullptr;
            double ms = std::strtod(text.c_str(), &end);
            if (TesseraModule* m = c->module(); m && end != text.c_str()) {
                RecordSettings next = m->recordSettings();
                next.recordOffsetSamples = int(std::clamp(std::lround(ms * rate / 1000.0), 0L, 96000L));
                m->setRecordSettings(next);
            }
        }));
        sub->addChild(settingItem(c, "None (0 ms)", &RecordSettings::recordOffsetSamples, 0));
    }));
    menu->addChild(rack::createSubmenuItem(
        "New launcher clips", s.clipBars == 0 ? "As long as played" : bars(s.clipBars), [c](rack::ui::Menu* sub) {
            sub->addChild(settingItem(c, "As long as played (rounded up to the bar)", &RecordSettings::clipBars, 0));
            for (int b : {1, 2, 4, 8})
                sub->addChild(settingItem(c, bars(b) + ", then loop", &RecordSettings::clipBars, b));
        }));
    static const char* const kArrangerModes[] = {"Merge", "Stack", "Replace"};
    menu->addChild(rack::createSubmenuItem(
        "Recording over arranger clips", kArrangerModes[size_t(s.arranger)], [c](rack::ui::Menu* sub) {
            sub->addChild(settingItem(c, "Merge (add to the clip that is there)", &RecordSettings::arranger,
                                      ArrangerRecordMode::Merge));
            sub->addChild(settingItem(c, "Stack (a new clip on top, the old one kept)", &RecordSettings::arranger,
                                      ArrangerRecordMode::Stack));
            sub->addChild(settingItem(c, "Replace (cut out what was there)", &RecordSettings::arranger,
                                      ArrangerRecordMode::Replace));
        }));
    menu->addChild(rack::createSubmenuItem(
        "MIDI controls with no lane", s.newControlLanes ? "Add a lane" : "Leave out", [c](rack::ui::Menu* sub) {
            sub->addChild(settingItem(c, "Add a lane in the clip", &RecordSettings::newControlLanes, true));
            sub->addChild(
                settingItem(c, "Leave out (only lanes I added record)", &RecordSettings::newControlLanes, false));
        }));
    struct Grid {
        const char* name;
        Tick ticks;
    };
    static const Grid grids[] = {{"Off", 0}, {"1/4", kPpq}, {"1/8", kPpq / 2}, {"1/16", kPpq / 4}, {"1/32", kPpq / 8}};
    std::string current = "Off";
    for (const Grid& g : grids)
        if (g.ticks == s.quantize)
            current = g.name;
    menu->addChild(rack::createSubmenuItem("Input quantize", current, [c](rack::ui::Menu* sub) {
        for (const Grid& g : grids)
            sub->addChild(settingItem(c, g.name, &RecordSettings::quantize, g.ticks));
        sub->addChild(new rack::ui::MenuSeparator);
        sub->addChild(rack::createMenuLabel("Strength (undo brings back the played timing)"));
        for (float strength : {0.5f, 0.75f, 1.f})
            sub->addChild(
                settingItem(c, std::to_string(int(strength * 100)) + "%", &RecordSettings::strength, strength));
    }));
    menu->addChild(rack::createCheckMenuItem(
        "Snap incoming notes to the key", "", [c] { return c->module() && c->module()->recordSettings().toKey; },
        [c] {
            if (TesseraModule* m = c->module()) {
                RecordSettings s = m->recordSettings();
                s.toKey = !s.toKey;
                m->setRecordSettings(s);
            }
        }));
    menu->addChild(rack::createCheckMenuItem(
        "MPE input (each note on its own channel)", "",
        [c] { return c->module() && c->module()->recordSettings().mpe; },
        [c] {
            if (TesseraModule* m = c->module()) {
                RecordSettings s = m->recordSettings();
                s.mpe = !s.mpe;
                m->setRecordSettings(s);
            }
        }));

    menu->addChild(new rack::ui::MenuSeparator);
    auto* captureItem =
        rack::createMenuItem("Capture what I just played", shortcutLabel(Action::CaptureMidi), [c] { capture(*c); });
    captureItem->disabled = !m->canCapture();
    menu->addChild(captureItem);
    menu->addChild(rack::createSubmenuItem("MIDI input", "", [c](rack::ui::Menu* sub) {
        if (TesseraModule* m = c->module())
            rack::app::appendMidiMenu(sub, &m->midiInput());
    }));
}

} // namespace tessera::recordActions
