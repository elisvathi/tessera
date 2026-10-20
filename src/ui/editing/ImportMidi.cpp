#include "ImportMidi.hpp"

#include "../../model/LauncherCommands.hpp"
#include "../../model/MidiImport.hpp"
#include "../../module/TesseraModule.hpp"
#include "ClipActions.hpp"

#include <osdialog.h>

#include <memory>

namespace tessera::importMidi {

std::string pickFile() {
    osdialog_filters* filters = osdialog_filters_parse("MIDI file:mid,midi,smf");
    char* path = osdialog_file(OSDIALOG_OPEN, nullptr, nullptr, filters);
    osdialog_filters_free(filters);
    std::string result = path ? path : "";
    std::free(path);
    return result;
}

void chooseClip(EditorContext& ctx, const std::string& path, std::function<void(Clip)> use) {
    TesseraModule* m = ctx.module();
    if (!m || path.empty())
        return;
    std::string file = rack::system::getFilename(path);
    auto in = std::make_shared<midiImport::Imported>();
    if (!midiImport::readFile(path, *in)) {
        ctx.toast(file + " is not a MIDI file Tessera can read");
        return;
    }
    if (midiImport::noteCount(*in) == 0) {
        ctx.toast("No notes in " + file);
        return;
    }
    std::string stem = rack::system::getStem(path);
    EditorContext* c = &ctx;
    auto make = [c, in, stem, use](int part) {
        TesseraModule* mod = c->module();
        if (!mod)
            return;
        std::string name = part >= 0 && in->parts.size() > 1 ? in->parts[size_t(part)].name : stem;
        use(midiImport::clipOf(*in, part, mod->song().timeSig, mod->ids(), name));
    };
    if (in->parts.size() == 1) {
        make(0);
        return;
    }
    auto notes = [](size_t n) { return std::to_string(n) + (n == 1 ? " note" : " notes"); };
    rack::ui::Menu* menu = rack::createMenu();
    menu->addChild(rack::createMenuLabel("Import from " + file));
    menu->addChild(rack::createMenuItem("All parts, as one clip", notes(midiImport::noteCount(*in)), [make] { make(-1); }));
    menu->addChild(new rack::ui::MenuSeparator);
    for (size_t i = 0; i < in->parts.size(); i++) {
        int part = int(i);
        std::string name = in->parts[i].name + (in->parts[i].drum ? "  (drums)" : "");
        menu->addChild(rack::createMenuItem(name, notes(in->parts[i].notes.size()), [make, part] { make(part); }));
    }
}

void toSlot(EditorContext& ctx, TrackId track, int slot) {
    std::string path = pickFile(); // the dialog blocks; the module may be gone when it returns
    if (path.empty() || !ctx.module())
        return;
    EditorContext* c = &ctx;
    chooseClip(ctx, path, [c, track, slot](Clip clip) {
        TesseraModule* m = c->module();
        if (!m)
            return;
        m->perform(importClipToSlot(m->song(), track, slot, clip, m->ids()));
        // Open the new clip, as creating one does.
        const Track* t = m->song().findTrack(track);
        if (t && slot >= 0 && slot < int(t->slots.size()) && t->slots[size_t(slot)].clip)
            clipActions::select(*c, track, *t->slots[size_t(slot)].clip);
    });
}

void asTracks(TesseraModule* m, projectFile::Say say) {
    std::string path = pickFile();
    if (path.empty() || !m)
        return;
    std::string file = rack::system::getFilename(path);
    midiImport::Imported in;
    if (!midiImport::readFile(path, in)) {
        say(file + " is not a MIDI file Tessera can read", true);
        return;
    }
    if (in.parts.empty()) {
        say("No notes in " + file, true);
        return;
    }
    int room = kMaxTracks - int(m->song().tracks.size());
    if (room <= 0) {
        say("No room for more tracks (64 at most)", true);
        return;
    }
    // Tempo is the module's knob, not part of the song, so it is its own undo step, before the tracks.
    if (in.tempo) {
        rack::engine::ParamQuantity* q = m->paramQuantities[TesseraModule::BPM_PARAM];
        float bpm = rack::math::clamp(float(*in.tempo), q->getMinValue(), q->getMaxValue());
        if (std::fabs(bpm - m->params[TesseraModule::BPM_PARAM].getValue()) > 0.005f) {
            char name[48];
            snprintf(name, sizeof(name), "Set tempo to %.2f BPM", double(bpm));
            m->setParamWithUndo(TesseraModule::BPM_PARAM, bpm, name);
        }
    }
    m->perform(midiImport::importTracks(m->song(), in, m->ids(), file));
    int added = std::min(room, int(in.parts.size()));
    std::string tracks = std::to_string(added) + (added == 1 ? " track" : " tracks");
    if (added < int(in.parts.size()))
        say("Imported " + tracks + " of " + std::to_string(in.parts.size()) + " from " + file + " (64 tracks at most)",
            false);
    else
        say("Imported " + tracks + " from " + file, false);
}

} // namespace tessera::importMidi
