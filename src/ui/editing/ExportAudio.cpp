#include "ExportAudio.hpp"

#include "../../module/TesseraModule.hpp"
#include "../EditorContext.hpp"

#include <osdialog.h>

#include <memory>

namespace tessera::exportAudio {

namespace {

/// What the dialog has chosen; kept for the next export while Rack runs.
struct Choice {
    bool stems = false;
    bool float32 = false;
};

Choice& lastChoice() {
    static Choice choice;
    return choice;
}

/// Asks where to save, with `name` as the suggestion; "" when cancelled. Adds ".wav" when missing.
std::string pickPath(const std::string& name) {
    osdialog_filters* filters = osdialog_filters_parse("WAV audio:wav");
    char* chosen = osdialog_file(OSDIALOG_SAVE, nullptr, (name + ".wav").c_str(), filters);
    osdialog_filters_free(filters);
    std::string path = chosen ? chosen : "";
    std::free(chosen);
    if (!path.empty() && rack::system::getExtension(path) != ".wav")
        path += ".wav";
    return path;
}

} // namespace

void openDialog(EditorContext& ctx) {
    TesseraModule* m = ctx.module();
    if (!m)
        return;
    if (!m->makesSound()) {
        ctx.toast("Nothing makes sound yet: add audio, or patch a voice into a Track Out's IN");
        return;
    }
    Choice* choice = &lastChoice();
    Tick bar = ticksPerBar(m->song().timeSig);
    std::string what;
    if (m->playMode() == PlayMode::Arranger) {
        bool loop = m->loopEnabled() && m->song().loopEnd > m->song().loopStart;
        Tick from = loop ? m->song().loopStart : 0, to = loop ? m->song().loopEnd : m->songEnd();
        what = std::string(loop ? "The loop" : "The song") + ", bar " + std::to_string(from / bar + 1) + " – " +
               std::to_string(to / bar + 1) + ", in real time";
        if (to <= from)
            what = "The timeline is empty";
    } else {
        what = "The clips you play, from now until you press Stop";
    }
    rack::ui::Menu* menu = rack::createMenu();
    menu->addChild(rack::createMenuLabel("Export audio"));
    menu->addChild(rack::createMenuLabel(what));
    menu->addChild(new rack::ui::MenuSeparator);
    menu->addChild(rack::createCheckMenuItem(
        "The master", "", [choice] { return !choice->stems; }, [choice] { choice->stems = false; }, false, true));
    menu->addChild(rack::createCheckMenuItem(
        "The master and every track (stems)", "", [choice] { return choice->stems; },
        [choice] { choice->stems = true; }, false, true));
    menu->addChild(new rack::ui::MenuSeparator);
    menu->addChild(rack::createCheckMenuItem(
        "24-bit WAV", "", [choice] { return !choice->float32; }, [choice] { choice->float32 = false; }, false, true));
    menu->addChild(rack::createCheckMenuItem(
        "32-bit float WAV", "", [choice] { return choice->float32; }, [choice] { choice->float32 = true; }, false,
        true));
    menu->addChild(new rack::ui::MenuSeparator);
    EditorContext* c = &ctx;
    menu->addChild(rack::createMenuItem("Export…", "", [c, m, choice] {
        std::string name = m->instanceName().empty() ? "Tessera" : m->instanceName();
        std::string path = pickPath(name);
        if (path.empty())
            return;
        if (!m->startExport(path, choice->stems, choice->float32))
            c->toast("Can't export now: the timeline is empty, or a bounce or export is running");
    }));
}

} // namespace tessera::exportAudio
