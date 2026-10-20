#include "ExportMidi.hpp"

#include "../../model/DawProject.hpp"
#include "../../model/Render.hpp"
#include "../../module/TesseraModule.hpp"
#include "../widgets/SettingsPanel.hpp"

#include <osdialog.h>

#include <memory>

namespace tessera::exportMidi {

using namespace render;

namespace {

/// What the dialog decides beyond the saved options.
struct Choices {
    Options options;
    bool selectedOnly = false;
    bool oneFileForClips = false;
};

/// The dialog's rows, each with how its value lands in the choices.
struct Form {
    std::vector<Setting> rows;
    std::vector<std::function<void(Choices&, double)>> readers;

    void add(Setting s, std::function<void(Choices&, double)> read) {
        rows.push_back(std::move(s));
        readers.push_back(std::move(read));
    }
    void heading(std::string name) { add(settings::heading(std::move(name)), nullptr); }
    Choices read(Choices base, const std::vector<double>& values) const {
        for (size_t i = 0; i < readers.size() && i < values.size(); i++)
            if (readers[i])
                readers[i](base, values[i]);
        return base;
    }
};

/// `daw`: DAWproject has no 14-bit controllers and Tessera doesn't export expressions to it.
void addPlayback(Form& form, const Options& o, bool daw = false) {
    form.add(settings::choice("Timing", {"As written", "As played"}, int(o.timing)),
             [](Choices& c, double v) { c.options.timing = Timing(int(v)); });
    form.add(settings::choice("Note operators", {"As written", "Rendered"}, int(o.operators)),
             [](Choices& c, double v) { c.options.operators = OperatorMode(int(v)); });
    form.add(settings::choice("Devices", {"Applied", "Bypassed"}, int(o.devices)),
             [](Choices& c, double v) { c.options.devices = DeviceMode(int(v)); });
    if (daw) {
        form.add(
            settings::choice("Automation", {"Off", "As CC"}, o.automation == AutomationMode::Off ? 0 : 1),
            [](Choices& c, double v) { c.options.automation = v != 0 ? AutomationMode::Cc : AutomationMode::Off; });
        return;
    }
    form.add(settings::choice("Automation", {"Off", "As CC", "As CC, 14-bit"}, int(o.automation)),
             [](Choices& c, double v) { c.options.automation = AutomationMode(int(v)); });
    form.add(settings::choice("Expressions", {"Off", "MPE", "Flatten"}, int(o.expressions)),
             [](Choices& c, double v) { c.options.expressions = ExpressionMode(int(v)); });
}

/// One row per AUTO channel the exported tracks use, to pick its controller number.
void addLaneCcs(Form& form, const Song& song, const Options& o, const std::vector<const Track*>& tracks) {
    std::array<std::string, kLanes> names;
    auto note = [&](const AutomationEnvelope& lane) {
        if (drivesOutput(lane) && lane.outputIndex >= 0 && lane.outputIndex < kLanes &&
            names[size_t(lane.outputIndex)].empty())
            names[size_t(lane.outputIndex)] = lane.name;
    };
    for (const Track* t : tracks) {
        for (const AutomationEnvelope& lane : t->automation)
            note(lane);
        for (const ArrangerClip& a : t->arrangement)
            if (const Clip* c = song.findClip(a.clip))
                for (const AutomationEnvelope& lane : c->automation)
                    note(lane);
        for (const ClipSlot& s : t->slots)
            if (const Clip* c = s.clip ? song.findClip(*s.clip) : nullptr)
                for (const AutomationEnvelope& lane : c->automation)
                    note(lane);
    }
    bool any = false;
    for (int i = 0; i < kLanes; i++) {
        if (names[size_t(i)].empty())
            continue;
        if (!any)
            form.heading("AUTOMATION AS CONTROLLERS");
        any = true;
        form.add(settings::number("AUTO " + std::to_string(i + 1) + " · " + names[size_t(i)], 0, 119, 1,
                                  o.laneCc[size_t(i)], [](double v) { return settings::printf1("CC %.0f", v); }),
                 [i](Choices& c, double v) { c.options.laneCc[size_t(i)] = int(v); });
    }
}

/// Off: every note goes out on its track's channel. On: each note on the channel set in the piano roll.
void addNoteChannels(Form& form, const Options& o) {
    form.add(settings::toggle("Use note channels", o.noteChannels),
             [](Choices& c, double v) { c.options.noteChannels = v != 0; });
}

void addChannels(Form& form, const Song& song, const Options& o, const std::vector<const Track*>& tracks) {
    // A long list of tracks would make the dialog taller than the window; they keep their automatic channels.
    constexpr size_t kMaxChannelRows = 12;
    if (tracks.empty() || tracks.size() > kMaxChannelRows)
        return;
    form.heading("MIDI CHANNELS");
    for (const Track* t : tracks) {
        TrackId id = t->id;
        form.add(settings::number(t->name, 1, 16, 1, channelOf(song, o, id),
                                  [](double v) { return settings::printf1("ch %.0f", v); }),
                 [id](Choices& c, double v) { c.options.channels[id.value] = int(v); });
    }
}

std::string fileSafe(const std::string& name) {
    std::string out;
    for (char ch : name)
        out += (std::isalnum((unsigned char)ch) || ch == ' ' || ch == '-' || ch == '_') ? ch : '_';
    return out.empty() ? "song" : out;
}

/// Asks where to save; returns an empty path when cancelled. The path always ends in ".mid" (or `extension`).
std::string pickPath(const std::string& name, const char* filter = "MIDI file:mid,midi",
                     const std::string& extension = ".mid") {
    osdialog_filters* filters = osdialog_filters_parse(filter);
    char* chosen = osdialog_file(OSDIALOG_SAVE, nullptr, (fileSafe(name) + extension).c_str(), filters);
    osdialog_filters_free(filters);
    std::string path = chosen ? chosen : "";
    std::free(chosen);
    std::string ext = rack::system::getExtension(path);
    if (!path.empty() && ext != extension && !(extension == ".mid" && ext == ".midi"))
        path += extension;
    return path;
}

std::string stem(const std::string& path) {
    std::string ext = rack::system::getExtension(path);
    return path.substr(0, path.size() - ext.size());
}

Context contextOf(TesseraModule* m) { return Context{m->tempo(), m->instanceName()}; }

std::vector<const Track*> exportedTracks(const Song& song, const std::vector<TrackId>& chosen) {
    std::vector<const Track*> out;
    for (const Track& t : song.tracks)
        if (chosen.empty() || std::find(chosen.begin(), chosen.end(), t.id) != chosen.end())
            out.push_back(&t);
    return out;
}

} // namespace

void openSong(EditorContext& ctx) {
    TesseraModule* m = ctx.module();
    if (!m)
        return;
    const Song& song = m->song();
    EditorState& state = ctx.state();
    Options o = state.exportOptions;
    std::vector<TrackId> selected = state.selectedTracks;
    bool hasRange = !state.range.empty();

    auto form = std::make_shared<Form>();
    if (selected.size() >= 1 && selected.size() < song.tracks.size())
        form->add(settings::choice(
                      "Export", {"Whole arrangement", "Selected tracks (" + std::to_string(selected.size()) + ")"}, 0),
                  [](Choices& c, double v) { c.selectedOnly = v != 0; });
    std::vector<std::string> ranges{"Whole song", "Loop"};
    if (hasRange)
        ranges.push_back("Range selection");
    int range = std::min(int(o.range), int(ranges.size()) - 1);
    form->add(settings::choice("Range", ranges, range),
              [](Choices& c, double v) { c.options.range = RangeMode(int(v)); });
    form->add(settings::choice("Files", {"One file", "One file per track"}, o.split == Split::PerTrack ? 1 : 0),
              [](Choices& c, double v) { c.options.split = v != 0 ? Split::PerTrack : Split::OneFile; });
    addPlayback(*form, o);
    form->add(settings::toggle("Include muted tracks and clips", o.includeMuted),
              [](Choices& c, double v) { c.options.includeMuted = v != 0; });
    addNoteChannels(*form, o);
    addLaneCcs(*form, song, o, exportedTracks(song, {}));
    addChannels(*form, song, o, exportedTracks(song, {}));

    EditorContext* c = &ctx;
    SettingsPanel::Callbacks callbacks;
    callbacks.apply = [c, form, o, selected](const std::vector<double>& values) {
        TesseraModule* mod = c->module();
        if (!mod)
            return;
        Choices choices = form->read(Choices{o}, values);
        Options options = choices.options;
        c->state().exportOptions = options;
        const Song& s = mod->song();
        if (options.range == RangeMode::Selection) {
            options.from = c->state().range.from;
            options.to = c->state().range.to;
        }
        auto [from, to] = rangeOf(s, options);
        if (to <= from) {
            c->toast("Nothing to export: the arrangement is empty. Clips export from their right-click menu.");
            return;
        }
        std::vector<TrackId> chosen = choices.selectedOnly ? selected : std::vector<TrackId>();
        std::string title = mod->instanceName().empty() ? "Tessera" : mod->instanceName();
        std::string path = pickPath(title);
        if (path.empty())
            return;
        Context context = contextOf(mod);
        std::vector<const Track*> tracks;
        for (const Track* t : exportedTracks(s, chosen))
            if (!t->mute || options.includeMuted)
                tracks.push_back(t);
        bool ok = true;
        std::string name = rack::system::getFilename(path);
        if (options.split == Split::PerTrack) {
            for (const Track* t : tracks) {
                std::string one = stem(path) + " - " + fileSafe(t->name) + ".mid";
                ok &= smf::writeFile(renderSong(s, options, context, {t->id}), one);
            }
            c->toast(ok ? "Exported " + std::to_string(tracks.size()) + " files next to " + name
                        : "Couldn't write every file to " + rack::system::getDirectory(path));
        } else {
            ok = smf::writeFile(renderSong(s, options, context, chosen), path);
            c->toast(ok ? "Exported " + std::to_string(tracks.size()) + " track" + (tracks.size() == 1 ? "" : "s") +
                              " to " + name
                        : "Couldn't write " + path);
        }
    };
    SettingsPanel::open("Export MIDI", form->rows, "Export…", std::move(callbacks));
}

void openClips(EditorContext& ctx, std::vector<std::pair<TrackId, ClipId>> clips) {
    TesseraModule* m = ctx.module();
    if (!m || clips.empty())
        return;
    const Song& song = m->song();
    Options o = ctx.state().exportOptions;
    auto form = std::make_shared<Form>();
    if (clips.size() > 1)
        form->add(settings::choice("Files", {"One file per clip", "One file, a track per clip"}, 0),
                  [](Choices& c, double v) { c.oneFileForClips = v != 0; });
    addPlayback(*form, o);
    addNoteChannels(*form, o);
    std::vector<const Track*> tracks;
    for (const auto& [track, clip] : clips)
        if (const Track* t = song.findTrack(track))
            if (std::find(tracks.begin(), tracks.end(), t) == tracks.end())
                tracks.push_back(t);
    addLaneCcs(*form, song, o, tracks);

    EditorContext* c = &ctx;
    SettingsPanel::Callbacks callbacks;
    callbacks.apply = [c, form, o, clips](const std::vector<double>& values) {
        TesseraModule* mod = c->module();
        if (!mod)
            return;
        Choices choices = form->read(Choices{o}, values);
        c->state().exportOptions = choices.options;
        const Song& s = mod->song();
        auto clipName = [&](ClipId id) {
            const Clip* clip = s.findClip(id);
            return clip && !clip->name.empty() ? clip->name : std::string("Clip");
        };
        std::string path = pickPath(clips.size() == 1 ? clipName(clips[0].second) : "Clips");
        if (path.empty())
            return;
        Context context = contextOf(mod);
        // Each clip's track is named after the clip, so a multi-clip file reads clearly in a DAW.
        auto render = [&](const std::pair<TrackId, ClipId>& one) {
            smf::File f = renderClip(s, choices.options, context, one.first, one.second);
            if (f.tracks.size() > 1 && !f.tracks[1].events.empty() && f.tracks[1].events[0].isMeta())
                f.tracks[1].events[0] = smf::trackName(0, clipName(one.second));
            return f;
        };
        bool ok = true;
        std::string name = rack::system::getFilename(path);
        if (clips.size() == 1 || choices.oneFileForClips) {
            smf::File f = render(clips[0]);
            for (size_t i = 1; i < clips.size(); i++) {
                smf::File g = render(clips[i]);
                f.tracks.insert(f.tracks.end(), g.tracks.begin() + 1, g.tracks.end());
            }
            ok = smf::writeFile(f, path);
            c->toast(ok ? "Exported " + std::to_string(clips.size()) + " clip" + (clips.size() == 1 ? "" : "s") +
                              " to " + name
                        : "Couldn't write " + path);
        } else {
            for (const auto& one : clips)
                ok &= smf::writeFile(render(one), stem(path) + " - " + fileSafe(clipName(one.second)) + ".mid");
            c->toast(ok ? "Exported " + std::to_string(clips.size()) + " files next to " + name
                        : "Couldn't write every file to " + rack::system::getDirectory(path));
        }
    };
    std::string title = clips.size() == 1 ? "Export clip as MIDI" : "Export " + std::to_string(clips.size()) + " clips";
    SettingsPanel::open(title, form->rows, "Export…", std::move(callbacks));
}

void openDawProject(EditorContext& ctx) {
    TesseraModule* m = ctx.module();
    if (!m)
        return;
    const Song& song = m->song();
    Options o = ctx.state().exportOptions;
    auto form = std::make_shared<Form>();
    addPlayback(*form, o, true);
    form->add(settings::toggle("Include muted tracks and clips", o.includeMuted),
              [](Choices& c, double v) { c.options.includeMuted = v != 0; });
    addNoteChannels(*form, o);
    addLaneCcs(*form, song, o, exportedTracks(song, {}));

    EditorContext* c = &ctx;
    SettingsPanel::Callbacks callbacks;
    callbacks.apply = [c, form, o](const std::vector<double>& values) {
        TesseraModule* mod = c->module();
        if (!mod)
            return;
        Options options = form->read(Choices{o}, values).options;
        // Only the choices this dialog shows change; the MIDI export's own (range, files...) stay.
        Options& saved = c->state().exportOptions;
        saved.timing = options.timing;
        saved.operators = options.operators;
        saved.devices = options.devices;
        saved.automation = options.automation == AutomationMode::Off ? AutomationMode::Off
                           : saved.automation == AutomationMode::Off ? AutomationMode::Cc
                                                                     : saved.automation;
        saved.includeMuted = options.includeMuted;
        saved.noteChannels = options.noteChannels;
        saved.laneCc = options.laneCc;
        std::string title = mod->instanceName().empty() ? "Tessera" : mod->instanceName();
        std::string path = pickPath(title, "DAWproject:dawproject", ".dawproject");
        if (path.empty())
            return;
        std::string bytes = dawproject::build(mod->song(), options, contextOf(mod), mod->model->plugin->version);
        FILE* f = std::fopen(path.c_str(), "wb");
        bool ok = f && std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
        if (f)
            ok &= std::fclose(f) == 0;
        size_t tracks = 0;
        for (const Track& t : mod->song().tracks)
            tracks += !t.mute || options.includeMuted;
        c->toast(ok ? "Exported " + std::to_string(tracks) + " track" + (tracks == 1 ? "" : "s") + " to " +
                          rack::system::getFilename(path)
                    : "Couldn't write " + path);
    };
    SettingsPanel::open("Export DAWproject", form->rows, "Export…", std::move(callbacks));
}

} // namespace tessera::exportMidi
