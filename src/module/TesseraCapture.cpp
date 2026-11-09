#include "TesseraModule.hpp"

#include "CommandAction.hpp"
#include "../engine/Snapshot.hpp"
#include "../engine/AudioSnapshotBuilder.hpp"

#include <chrono>
#include <cstring>
#include <limits>
#include <filesystem>

// Recording audio, bouncing and exporting (docs/design/AUDIO_MIXER.md §8.6): the UI-thread side. The audio thread
// fills rings through the player's taps; the writer thread turns them into files; this turns files into clips.
namespace tessera {

namespace {

/// Tessera's own part of the delay between a note going out and its sound coming back: one sample to the output
/// module, one back on the return bus.
constexpr int kKnownLatency = 2;

/// Seconds of audio each ring holds before the writer must have taken it.
constexpr double kRingSeconds = 5;

} // namespace

int TesseraModule::freeTap() const {
    for (int n = 0; n < AudioPlayer::kMaxTaps; n++)
        if (std::none_of(captures.begin(), captures.end(), [n](const Capture& c) { return c.tap == n; }))
            return n;
    return -1;
}

double TesseraModule::ticksPerSampleNow() const {
    double rate = audioRate.load() > 0 ? double(audioRate.load()) : 48000;
    double bpm = tempo() > 0 ? tempo() : 120;
    return bpm / 60.0 * double(kPpq) / rate;
}

std::vector<TesseraModule::LiveTake> TesseraModule::liveTakes() const {
    std::vector<LiveTake> out;
    if (!captureWriter)
        return out;
    double rate = audioRate.load() > 0 ? double(audioRate.load()) : 48000;
    for (const Capture& c : captures) {
        if (c.kind == Capture::Kind::Export || player->tapState(c.tap) != AudioPlayer::TapState::Running)
            continue;
        LiveTake t;
        t.track = c.kind == Capture::Kind::Bounce && bounce ? bounce->placed : c.track;
        t.startTick = player->tapStart(c.tap);
        t.lengthTicks = double(captureWriter->written(c.writer)) / rate * (tempo() > 0 ? tempo() : 120) / 60.0 *
                        double(kPpq);
        t.peaks = captureWriter->peaks(c.writer);
        out.push_back(std::move(t));
    }
    return out;
}

void TesseraModule::captureStep() {
    glueStep();
    if (!audio || !player->active())
        return;
    if (!captureWriter)
        captureWriter = std::make_unique<CaptureWriter>();
    double rate = audioRate.load() > 0 ? double(audioRate.load()) : 48000;
    // Each armed audio track keeps a tap on its IN, ready for the next pass.
    for (const Track& t : songData.tracks) {
        if (t.type != TrackType::Audio || !t.armed)
            continue;
        bool ready = std::any_of(captures.begin(), captures.end(), [&](const Capture& c) {
            return c.kind == Capture::Kind::Take && c.track == t.id && !c.finishing;
        });
        int tap = ready ? -1 : freeTap();
        if (tap < 0)
            continue;
        Capture c;
        c.kind = Capture::Kind::Take;
        c.track = t.id;
        c.tap = tap;
        c.ring = std::make_shared<FrameRing>(size_t(kRingSeconds * rate));
        std::filesystem::path dir = std::filesystem::temp_directory_path() / ("tessera-" + std::to_string(id));
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        c.path = (dir / (t.name + " take " + std::to_string(++takeCounter) + ".wav")).string();
        c.writer = captureWriter->start(c.ring, c.path, rate, CaptureWriter::Format::Float32);
        AudioPlayer::TapSpec spec;
        bool fromTrack = t.recordInput == RecordInput::OtherTrack && t.recordFrom.valid();
        spec.track = fromTrack ? t.recordFrom : t.id;
        spec.kind = !fromTrack                          ? AudioPlayer::TapKind::In
                    : t.recordFrom == songData.master.id ? AudioPlayer::TapKind::Master
                                                         : AudioPlayer::TapKind::Sound;
        spec.whileRecording = true;
        spec.ring = c.ring.get();
        player->armTap(tap, spec);
        captures.push_back(std::move(c));
    }
    std::vector<Take> takes;
    for (auto it = captures.begin(); it != captures.end();) {
        Capture& c = *it;
        if (c.kind != Capture::Kind::Take) {
            ++it;
            continue;
        }
        const Track* t = songData.findTrack(c.track);
        AudioPlayer::TapState state = player->tapState(c.tap);
        // Disarmed (or deleted) before it recorded: nothing to keep.
        if ((!t || !t->armed) && state == AudioPlayer::TapState::Armed) {
            player->disarmTap(c.tap);
            captureWriter->cancel(c.writer);
            it = captures.erase(it);
            continue;
        }
        if (!c.finishing && (state == AudioPlayer::TapState::Done || captureWriter->failed(c.writer))) {
            if (captureWriter->failed(c.writer))
                takeProblem = "Recording stopped: the take was kept up to there";
            c.finishing = true;
            c.startTick = player->tapStart(c.tap);
            player->disarmTap(c.tap);
        }
        int64_t frames = 0;
        if (c.finishing && captureWriter->finish(c.writer, &frames)) {
            if (frames > 0 && t) {
                // Moved earlier by the delays known to come with it (§8.6).
                int late = kKnownLatency + recorder.settings.recordOffsetSamples + t->latencySamples;
                Take take;
                take.path = c.path;
                take.track = c.track;
                take.at = std::max<Tick>(0, Tick(std::llround(c.startTick - late * ticksPerSampleNow())));
                take.slot = playMode() == PlayMode::Launcher;
                takes.push_back(take);
            } else {
                std::error_code ec;
                std::filesystem::remove(c.path, ec);
            }
            it = captures.erase(it);
            continue;
        }
        ++it;
    }
    if (!takes.empty())
        importTakes(takes);
    bounceStep();
    exportStep();
    measureStep();
}

bool TesseraModule::startBounce(TrackId track, Tick from, Tick to, bool untilSilent, bool muteOriginal,
                                const std::vector<ArrangementId>& only) {
    const Track* t = songData.findTrack(track);
    if (!t || bounce || exporting || !audio || !player->active() || to <= from)
        return false;
    if (!captureWriter)
        captureWriter = std::make_unique<CaptureWriter>();
    int tap = freeTap();
    if (tap < 0)
        return false;
    double rate = audioRate.load() > 0 ? double(audioRate.load()) : 48000;
    Capture c;
    c.kind = Capture::Kind::Bounce;
    c.track = track;
    c.tap = tap;
    c.ring = std::make_shared<FrameRing>(size_t(kRingSeconds * rate));
    std::filesystem::path dir = std::filesystem::temp_directory_path() / ("tessera-" + std::to_string(id));
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    std::string fileName = t->name + " (bounce).wav";
    std::replace(fileName.begin(), fileName.end(), '/', '-');
    c.path = (dir / fileName).string();
    c.writer = captureWriter->start(c.ring, c.path, rate, CaptureWriter::Format::Float32);
    captures.push_back(std::move(c));
    BounceJob job;
    job.track = track;
    job.from = from;
    job.to = to;
    job.untilSilent = untilSilent;
    job.muteOriginal = muteOriginal;
    job.capture = int(captures.size()) - 1;
    // Only this track plays, so nothing else reaches the effects patched after it. This is a passing playback state,
    // put back when the bounce ends, so it is not an undo step.
    for (Track& other : songData.tracks) {
        if (other.solo)
            job.soloBefore.push_back(other.id);
        other.solo = other.id == track;
        // With clips selected, only those play: an unselected clip stacked over them stays out of the bounce.
        if (other.id != track || only.empty())
            continue;
        for (ArrangerClip& a : other.arrangement)
            if (!a.muted && std::find(only.begin(), only.end(), a.id) == only.end()) {
                a.muted = true;
                job.mutedClips.push_back(a.id);
            }
    }
    publish(ChangeSet::all());
    // The new track shows from the start, with the bounce growing on it. `t` is not used after this: adding a track
    // can move the song's tracks in memory.
    int index = songData.trackIndex(track) + 1;
    std::string title = "Bounce " + t->name, placedName = t->name + " (bounce)";
    NoteSelection selectionBefore = editorState.selection;
    if (CommandPtr add = addTrack(songData, TrackType::Audio, index, ids())) {
        auto made = std::make_unique<CompoundCommand>(title);
        add->apply(songData);
        made->add(std::move(add));
        job.placed = songData.tracks[size_t(index)].id;
        if (CommandPtr rename = setTrackField(songData, job.placed, &Track::name, placedName, "Rename")) {
            rename->apply(songData);
            made->add(std::move(rename));
        }
        publish(ChangeSet::all());
        audio->sync(songData);
        job.placedAction = new rack::history::ComplexAction;
        job.placedAction->name = "bounce";
        job.placedAction->push(new CommandAction(id, std::move(made), selectionBefore, editorState.selection));
        APP->history->push(job.placedAction);
        // Selecting it scrolls it into view, so a long track list can't hide it.
        editorState.selectedTrack = job.placed;
        editorState.selectedTracks = {job.placed};
    }
    // It plays the timeline, from a bar early so delays and reverbs are already sounding.
    job.restoreLauncher = playMode() == PlayMode::Launcher;
    params[MODE_PARAM].setValue(1.f);
    // A loop would keep the playhead from ever reaching the end of the bounce.
    job.restoreLoop = loopEnabled();
    params[LOOP_PARAM].setValue(0.f);
    requestSeek(double(std::max<Tick>(0, from - ticksPerBar(songData.timeSig))));
    if (!playing())
        requestTransport(kRequestPlay);
    bounce = job;
    return true;
}

bool TesseraModule::measureOffset() {
    if (measure || !player->active())
        return false;
    for (const Track& t : songData.tracks)
        if (t.type == TrackType::Audio && t.armed) {
            Measure job;
            job.track = t.id;
            job.nextAt = rack::system::getTime();
            measure = job;
            return true;
        }
    return false;
}

void TesseraModule::measureStep() {
    if (!measure)
        return;
    Measure& job = *measure;
    double now = rack::system::getTime();
    if (job.waiting) {
        int64_t got = player->measured();
        if (got == -2)
            return;
        job.waiting = false;
        if (got < 0) {
            measure.reset();
            exportResult = "Nothing came back: patch CLICK out through the sound card and back into the armed "
                           "track's Track Out IN";
            return;
        }
        job.results.push_back(got);
        job.nextAt = now + 0.3;
    }
    if (job.results.size() >= 4) {
        std::vector<int64_t> r = job.results;
        std::sort(r.begin(), r.end());
        int64_t middle = (r[1] + r[2]) / 2;
        RecordSettings s = recorder.settings;
        s.recordOffsetSamples = int(std::max<int64_t>(0, middle - kKnownLatency));
        setRecordSettings(s);
        double rate = audioRate.load() > 0 ? double(audioRate.load()) : 48000;
        char text[96];
        snprintf(text, sizeof text, "Recording offset: %.1f ms (%d samples), measured",
                 s.recordOffsetSamples * 1000.0 / rate, s.recordOffsetSamples);
        exportResult = text;
        measure.reset();
        return;
    }
    if (now >= job.nextAt) {
        player->startMeasure(job.track.value);
        measureClick.store(true, std::memory_order_release);
        job.waiting = true;
    }
}

bool TesseraModule::glueAudio(TrackId track, const std::vector<ArrangementId>& instances) {
    const Track* t = songData.findTrack(track);
    if (!t || glue || !audio || instances.empty())
        return false;
    Song solo = songData;
    Track* only = solo.findTrack(track);
    // Only those clips, as they are, at 0 dB: what the glued clip should hold.
    std::vector<ArrangerClip> kept;
    Tick from = std::numeric_limits<Tick>::max(), to = 0;
    for (const ArrangerClip& a : only->arrangement)
        if (std::find(instances.begin(), instances.end(), a.id) != instances.end()) {
            const Clip* clip = solo.findClip(a.clip);
            if (!clip || !clip->audio)
                return false;
            kept.push_back(a);
            from = std::min(from, a.position);
            to = std::max(to, a.position + a.length);
        }
    if (kept.size() != instances.size() || to <= from)
        return false;
    only->arrangement = kept;
    only->channel = Channel{};
    only->clips = SourceLevel{};
    only->mute = only->solo = false;
    only->sends.clear();
    only->automation.clear();
    solo.tracks = {*only};
    solo.master.volumeDb = 0;
    solo.master.automation.clear();
    solo.master.mix = SourceLevel{};
    double rate = audioRate.load() > 0 ? double(audioRate.load()) : 48000;
    double bpm = tempo() > 0 ? tempo() : params[BPM_PARAM].getValue();
    auto job = std::make_unique<GlueJob>();
    job->track = track;
    job->instances = instances;
    job->from = from;
    std::filesystem::path dir = std::filesystem::temp_directory_path() / ("tessera-" + std::to_string(id));
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    job->path = (dir / (t->name + " glued.wav")).string();
    // The snapshot holds the samples it plays, so the worker needs nothing else from the UI thread.
    BusSlots slots;
    slots.sync(solo);
    std::shared_ptr<Snapshot> notes = buildSnapshot(solo, slots);
    std::shared_ptr<AudioSnapshot> sound = buildAudioSnapshot(solo, *audio, rate, bpm, {}, MixPlan{}).snapshot;
    GlueJob* raw = job.get();
    job->worker = std::thread([raw, notes, sound, rate, bpm, from, to] {
        Engine engine;
        engine.setSampleRate(float(rate));
        engine.setTempo(bpm);
        engine.setSnapshot(notes.get());
        engine.seek(double(from));
        engine.play();
        AudioPlayer player;
        player.setSampleRate(float(rate));
        player.setSnapshot(sound.get());
        std::array<TrackFrame, kMaxTracks> frames{};
        TransportEvents events;
        auto ring = std::make_shared<FrameRing>(1 << 16);
        CaptureWriter writer;
        int id = writer.start(ring, raw->path, rate, CaptureWriter::Format::Float32);
        int64_t total = int64_t(double(to - from) / (bpm / 60.0 * double(kPpq)) * rate);
        for (int64_t n = 0; n < total; n++) {
            engine.process(frames.data(), events);
            float l = 0, r = 0;
            player.process(engine, l, r);
            while (!ring->push(l, r))
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        int64_t written = 0;
        while (!writer.finish(id, &written))
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        raw->ok = written > 0;
        raw->done = true;
    });
    glue = std::move(job);
    return true;
}

void TesseraModule::glueStep() {
    if (!glue || !glue->done)
        return;
    glue->worker.join();
    std::unique_ptr<GlueJob> job = std::move(glue);
    const Track* t = songData.findTrack(job->track);
    if (!job->ok || !t)
        return;
    double songTempo = tempo() > 0 ? tempo() : params[BPM_PARAM].getValue();
    AudioImport imp = audio->importFile(job->path, songTempo, songData.timeSig);
    std::error_code ec;
    std::filesystem::remove(job->path, ec);
    if (!imp.ok)
        return;
    ImportedAudio f;
    f.source = imp.source;
    f.source.volts = kFullScaleVolts;
    f.tempo = songTempo;
    f.follow = TempoFollow::Stretch;
    f.sound = imp.guess.sound;
    f.loop = false;
    // The glued clips go first, then the new one takes their place, in one undo step.
    std::vector<ArrangerClip> rest;
    for (const ArrangerClip& a : t->arrangement)
        if (std::find(job->instances.begin(), job->instances.end(), a.id) == job->instances.end())
            rest.push_back(a);
    auto compound = std::make_unique<CompoundCommand>("Glue audio");
    NoteSelection before = editorState.selection;
    if (CommandPtr remove = setTrackField(songData, job->track, &Track::arrangement, rest, "Glue audio")) {
        remove->apply(songData);
        compound->add(std::move(remove));
    }
    AudioDropTarget target;
    target.kind = AudioDropTarget::Kind::Track;
    target.track = job->track;
    target.at = job->from;
    ImportResult made;
    if (CommandPtr place = importAudio(songData, {f}, target, ids(), &made)) {
        place->apply(songData);
        compound->add(std::move(place));
    }
    publish(ChangeSet::all());
    audio->sync(songData);
    auto* complex = new rack::history::ComplexAction;
    complex->name = "glue audio";
    complex->push(new CommandAction(id, std::move(compound), before, editorState.selection));
    APP->history->push(complex);
    editorState.clipSelection.clear();
    for (ArrangementId a : made.instances)
        editorState.clipSelection.insert(a);
}

Tick TesseraModule::songEnd() const {
    Tick end = 0;
    for (const Track& t : songData.tracks)
        for (const ArrangerClip& a : t.arrangement)
            end = std::max(end, a.position + a.length);
    Tick bar = ticksPerBar(songData.timeSig);
    return (end + bar - 1) / bar * bar;
}

bool TesseraModule::startExport(const std::string& path, bool stems, CaptureWriter::Format format) {
    if (bounce || exporting || !audio || !player->active())
        return false;
    if (!captureWriter)
        captureWriter = std::make_unique<CaptureWriter>();
    double rate = audioRate.load() > 0 ? double(audioRate.load()) : 48000;
    ExportJob job;
    job.timeline = playMode() == PlayMode::Arranger;
    job.name = rack::system::getFilename(path);
    if (job.timeline) {
        bool loop = loopEnabled() && songData.loopEnd > songData.loopStart;
        job.from = loop ? songData.loopStart : 0;
        job.to = loop ? songData.loopEnd : songEnd();
        if (job.to <= job.from)
            return false;
    }
    auto add = [&](TrackId track, const std::string& file) {
        int tap = freeTap();
        if (tap < 0)
            return;
        Capture c;
        c.kind = Capture::Kind::Export;
        c.track = track;
        c.tap = tap;
        c.ring = std::make_shared<FrameRing>(size_t(kRingSeconds * rate));
        c.path = file;
        c.writer = captureWriter->start(c.ring, c.path, rate, format);
        captures.push_back(std::move(c));
        job.captures.push_back(int(captures.size()) - 1);
    };
    add(songData.master.id, path);
    if (stems) {
        std::string ext = rack::system::getExtension(path);
        std::string base = path.substr(0, path.size() - ext.size());
        for (const Track& t : songData.tracks) {
            if (soundIndex(t.id) < 0)
                continue;
            if (t.mute) {
                job.skipped.push_back(t.name);
                continue;
            }
            std::string safe;
            for (char ch : t.name)
                safe += std::strchr("/\\:*?\"<>|", ch) ? '_' : ch;
            add(t.id, base + " - " + safe + ext);
        }
    }
    if (job.timeline) {
        requestSeek(double(job.from));
        if (!playing())
            requestTransport(kRequestPlay);
    } else if (!playing()) {
        requestTransport(kRequestPlay);
    }
    exporting = job;
    return true;
}

void TesseraModule::exportStep() {
    if (!exporting)
        return;
    ExportJob& job = *exporting;
    if (!job.armed) {
        // On the timeline the taps are armed at once and start right at the jump to `from`.
        for (size_t k = 0; k < job.captures.size(); k++) {
            Capture& c = captures[size_t(job.captures[k])];
            AudioPlayer::TapSpec spec;
            spec.track = c.track;
            spec.kind = k == 0 ? AudioPlayer::TapKind::Master : AudioPlayer::TapKind::Stem;
            spec.from = job.timeline ? double(job.from) : -1;
            spec.to = job.timeline ? double(job.to) : -1;
            spec.startWithin = job.timeline ? 64 * ticksPerSampleNow() : -1;
            spec.ring = c.ring.get();
            player->armTap(c.tap, spec);
        }
        job.armed = true;
    }
    if (playing())
        job.started = true;
    Capture& master = captures[size_t(job.captures[0])];
    if (!job.finishing) {
        bool done = player->tapState(master.tap) == AudioPlayer::TapState::Done;
        bool stopped = job.started && !playing();
        if (job.timeline && (done || stopped)) {
            // Stopped before the end: nothing is kept.
            if (playheadTicks() < double(job.to) - 1 && stopped && !done) {
                cancelCapture();
                return;
            }
            job.finishing = true;
            requestTransport(kRequestStop);
        } else if (!job.timeline && (done || stopped)) {
            job.finishing = true;
        }
        if (job.finishing)
            for (int index : job.captures)
                player->disarmTap(captures[size_t(index)].tap);
    }
    if (!job.finishing)
        return;
    bool all = true;
    for (int index : job.captures)
        all &= captureWriter->finish(captures[size_t(index)].writer);
    if (!all)
        return;
    int stems = int(job.captures.size()) - 1;
    std::string result = "Exported " + job.name;
    if (stems > 0)
        result += " and " + std::to_string(stems) + (stems == 1 ? " stem" : " stems");
    if (!job.skipped.empty())
        result += " · muted, left out: " + std::to_string(job.skipped.size());
    // Erase from the back so the indices stay right.
    std::vector<int> indices = job.captures;
    std::sort(indices.rbegin(), indices.rend());
    for (int index : indices)
        captures.erase(captures.begin() + index);
    exporting.reset();
    exportResult = result;
}

void TesseraModule::cancelCapture() {
    if (exporting) {
        std::vector<int> indices = exporting->captures;
        std::sort(indices.rbegin(), indices.rend());
        for (int index : indices) {
            player->disarmTap(captures[size_t(index)].tap);
            captureWriter->cancel(captures[size_t(index)].writer);
            captures.erase(captures.begin() + index);
        }
        exporting.reset();
        requestTransport(kRequestStop);
        return;
    }
    if (!bounce)
        return;
    Capture& c = captures[size_t(bounce->capture)];
    player->disarmTap(c.tap);
    captureWriter->cancel(c.writer);
    captures.erase(captures.begin() + bounce->capture);
    if (bounce->restoreLauncher)
        params[MODE_PARAM].setValue(0.f);
    if (bounce->restoreLoop)
        params[LOOP_PARAM].setValue(1.f);
    restoreBounceSolo(*bounce);
    removeBounceTrack(*bounce);
    exportResult = "Bounce cancelled";
    bounce.reset();
    requestTransport(kRequestStop);
}

void TesseraModule::restoreBounceSolo(const BounceJob& job) {
    for (Track& t : songData.tracks) {
        t.solo = std::find(job.soloBefore.begin(), job.soloBefore.end(), t.id) != job.soloBefore.end();
        for (ArrangerClip& a : t.arrangement)
            if (std::find(job.mutedClips.begin(), job.mutedClips.end(), a.id) != job.mutedClips.end())
                a.muted = false;
    }
    publish(ChangeSet::all());
}

void TesseraModule::removeBounceTrack(const BounceJob& job) {
    if (!job.placed.valid() || !songData.findTrack(job.placed))
        return;
    rack::history::State* history = APP->history;
    if (job.placedAction && !history->actions.empty() && history->actionIndex == int(history->actions.size()) &&
        history->actions.back() == job.placedAction) {
        // Nothing happened since: leave no undo step behind for a track that never held anything.
        history->undo();
        history->actions.pop_back();
        delete job.placedAction;
        return;
    }
    if (CommandPtr remove = removeTracks(songData, {job.placed}))
        perform(std::move(remove));
}

std::string TesseraModule::captureStatus() const {
    if (exporting) {
        if (!exporting->timeline)
            return "Exporting the clips you play · press Stop to finish";
        Tick bar = ticksPerBar(songData.timeSig);
        int at = int(std::max(0.0, playheadTicks() - double(exporting->from)) / double(bar)) + 1;
        int of = int((exporting->to - exporting->from + bar - 1) / bar);
        return "Exporting · bar " + std::to_string(std::min(at, of)) + " of " + std::to_string(of);
    }
    if (!bounce)
        return "";
    const Track* t = songData.findTrack(bounce->track);
    Tick bar = ticksPerBar(songData.timeSig);
    double now = playheadTicks();
    int at = int(std::max(0.0, now - double(bounce->from)) / double(bar)) + 1;
    int of = int((bounce->to - bounce->from + bar - 1) / bar);
    std::string what = "Bouncing " + (t ? t->name : std::string("track"));
    if (now < double(bounce->from))
        return what + " · starting";
    if (bounce->untilSilent && now >= double(bounce->to))
        return what + " · waiting for silence";
    return what + " · bar " + std::to_string(std::min(at, std::max(1, of))) + " of " + std::to_string(std::max(1, of));
}

void TesseraModule::bounceStep() {
    if (!bounce)
        return;
    BounceJob& job = *bounce;
    Capture& c = captures[size_t(job.capture)];
    // The tap is armed once the jump to the start has happened, so it never starts at the old playhead.
    if (!job.armed) {
        if (seekPending.load() || !playing())
            return;
        AudioPlayer::TapSpec spec;
        spec.track = job.track;
        spec.kind = AudioPlayer::TapKind::Sound;
        spec.from = double(job.from);
        spec.to = job.untilSilent ? -1 : double(job.to);
        spec.ring = c.ring.get();
        player->armTap(c.tap, spec);
        job.armed = true;
    }
    AudioPlayer::TapState state = player->tapState(c.tap);
    if (playing())
        job.started = true;
    double now = playheadTicks();
    if (now >= double(job.to))
        job.reachedEnd = true;
    Tick bar = ticksPerBar(songData.timeSig);
    if (!c.finishing) {
        if (job.untilSilent && state == AudioPlayer::TapState::Running && now >= double(job.to)) {
            // Until silent: below -60 dB for a second, or 8 bars past the end at most.
            bool quiet = player->tapLevel(c.tap) < kFullScaleVolts * 0.001f;
            double t = rack::system::getTime();
            if (!quiet)
                job.quietSince = -1;
            else if (job.quietSince < 0)
                job.quietSince = t;
            if ((job.quietSince >= 0 && t - job.quietSince >= 1.0) || now >= double(job.to + 8 * bar)) {
                job.keep = true;
                c.finishing = true;
                player->disarmTap(c.tap);
            }
        } else if (state == AudioPlayer::TapState::Done) {
            // Done at its end, or stopped by hand: either way what was played stays. Cancel is what discards it.
            job.keep = true;
            c.finishing = true;
            player->disarmTap(c.tap);
        } else if (job.started && !playing()) {
            job.keep = true;
            c.finishing = true;
            player->disarmTap(c.tap);
        }
        if (c.finishing)
            requestTransport(kRequestStop);
    }
    int64_t frames = 0;
    if (!c.finishing || !captureWriter->finish(c.writer, &frames))
        return;
    std::string path = c.path;
    double startTick = player->tapStart(c.tap);
    captures.erase(captures.begin() + job.capture);
    BounceJob done = job;
    bounce.reset();
    if (done.restoreLauncher)
        params[MODE_PARAM].setValue(0.f);
    if (done.restoreLoop)
        params[LOOP_PARAM].setValue(1.f);
    restoreBounceSolo(done);
    const Track* t = songData.findTrack(done.track);
    if (!t || frames == 0) {
        std::error_code ec;
        std::filesystem::remove(path, ec);
        removeBounceTrack(done);
        exportResult = "Bounce stopped before any sound was recorded";
        return;
    }
    double songTempo = tempo() > 0 ? tempo() : params[BPM_PARAM].getValue();
    AudioImport imp = audio->importFile(path, songTempo, songData.timeSig);
    std::error_code ec;
    std::filesystem::remove(path, ec);
    if (!imp.ok) {
        removeBounceTrack(done);
        exportResult = "Bounce failed: the recorded file couldn't be read";
        return;
    }
    ImportedAudio f;
    f.source = imp.source;
    f.source.volts = kFullScaleVolts;
    f.tempo = songTempo;
    f.follow = TempoFollow::Stretch;
    f.sound = imp.guess.sound;
    f.loop = !done.untilSilent && done.reachedEnd;
    bool onPlaced = done.placed.valid() && songData.findTrack(done.placed);
    AudioDropTarget target;
    target.kind = onPlaced ? AudioDropTarget::Kind::Track : AudioDropTarget::Kind::NewTracks;
    target.track = done.placed;
    target.index = songData.trackIndex(done.track) + 1;
    target.timeline = true;
    // What came back on IN is late by Tessera's own delay and the track's: move it back into time.
    int late = kKnownLatency + t->latencySamples;
    target.at = std::max<Tick>(0, Tick(std::llround((startTick >= 0 ? startTick : double(done.from)) -
                                                    late * ticksPerSampleNow())));
    std::string name = t->name + " (bounce)";
    ImportResult made;
    CommandPtr command = importAudio(songData, {f}, target, ids(), &made);
    if (!command) {
        removeBounceTrack(done);
        exportResult = "Bounce failed: there is no room for another track";
        return;
    }
    // Adding the track can move the song's tracks in memory, so `t` is not used after the apply below.
    std::string title = "Bounce " + t->name;
    auto compound = std::make_unique<CompoundCommand>(title);
    TrackId original = done.track;
    compound->add(std::move(command));
    NoteSelection before = editorState.selection;
    compound->apply(songData);
    // Name the new track, and mute the original when asked, in the same undo step.
    auto more = std::make_unique<CompoundCommand>(title);
    if (!made.tracks.empty())
        if (CommandPtr rename = setTrackField(songData, made.tracks[0], &Track::name, name, "Rename"))
            more->add(std::move(rename));
    if (done.muteOriginal)
        if (CommandPtr mute = setTrackField(songData, original, &Track::mute, true, "Mute"))
            more->add(std::move(mute));
    more->apply(songData);
    compound->add(std::move(more));
    publish(ChangeSet::all());
    audio->sync(songData);
    auto* complex = new rack::history::ComplexAction;
    complex->name = "bounce";
    complex->push(new CommandAction(id, std::move(compound), before, editorState.selection));
    if (onPlaced && done.placedAction) {
        // One undo step takes away the clip and the track together.
        done.placedAction->push(complex);
    } else {
        APP->history->push(complex);
    }
    if (onPlaced)
        editorState.selectedTrack = done.placed;
    else if (!made.tracks.empty())
        editorState.selectedTrack = made.tracks[0];
    exportResult = "Bounced to a new track: " + name;
}

void TesseraModule::importTakes(const std::vector<Take>& takes) {
    double songTempo = tempo() > 0 ? tempo() : params[BPM_PARAM].getValue();
    auto* complex = new rack::history::ComplexAction;
    complex->name = takes.size() == 1 ? "record audio" : "record " + std::to_string(takes.size()) + " takes";
    for (const Take& take : takes) {
        const Track* t = songData.findTrack(take.track);
        AudioImport imp = t ? audio->importFile(take.path, songTempo, songData.timeSig) : AudioImport{};
        std::error_code ec;
        std::filesystem::remove(take.path, ec);
        if (!imp.ok)
            continue;
        // Stack mode: a take that starts where an audio clip starts (a loop pass) becomes another take of that clip,
        // and that copy plays it; splitting the clip and picking a take per part comps them (§3.5). Only a clip at
        // the tempo the take was recorded at, so the take lines up with it.
        if (!take.slot && recorder.settings.arranger == ArrangerRecordMode::Stack) {
            const ArrangerClip* over = nullptr;
            for (const ArrangerClip& a : t->arrangement)
                if (a.offset == 0 && std::abs(double(a.position - take.at)) <= double(kPpq / 8))
                    if (const Clip* clip = songData.findClip(a.clip);
                        clip && clip->audio && !clip->audio->reverse &&
                        std::abs(clip->audio->sourceTempo - songTempo) < 0.01)
                        over = &a;
            if (over) {
                AudioSource src = imp.source;
                src.id = newId<AudioSourceId>();
                src.volts = kFullScaleVolts;
                ClipId clipId = over->clip;
                ArrangementId instance = over->id;
                // The take's frame at the clip's start: the take may have started a little before it.
                int64_t start = std::max<int64_t>(
                    0, std::llround(double(over->position - take.at) * secondsPerTick(songTempo) * src.sampleRate));
                start = std::min<int64_t>(start, std::max<int64_t>(0, src.frames - 1));
                int index = int(songData.findClip(clipId)->audio->takes.size());
                auto stack = std::make_unique<CompoundCommand>("Record take " + std::to_string(index + 2));
                NoteSelection before = editorState.selection;
                auto step = [&](CommandPtr cmd) {
                    if (cmd) {
                        cmd->apply(songData);
                        stack->add(std::move(cmd));
                    }
                };
                step(std::make_unique<AddAudioSources>(std::vector<AudioSource>{src}));
                step(editAudio(songData, clipId, [&](AudioContent& a) { a.takes.push_back({src.id, start}); },
                               "Record take"));
                step(setInstanceTake(songData, take.track, instance, index));
                publish(ChangeSet::all());
                complex->push(new CommandAction(id, std::move(stack), before, editorState.selection));
                continue;
            }
        }
        ImportedAudio f;
        f.source = imp.source;
        // A take stores what reached IN as V / 10, so it plays back at 10 V per full scale (§7.4).
        f.source.volts = kFullScaleVolts;
        f.tempo = songTempo;
        f.follow = TempoFollow::Stretch;
        f.sound = imp.guess.sound;
        f.loop = false;
        AudioDropTarget target;
        target.track = take.track;
        target.at = take.at;
        if (take.slot) {
            target.kind = AudioDropTarget::Kind::Slot;
            target.slot = 0;
            for (size_t s = 0; s < t->slots.size(); s++)
                if (!t->slots[s].clip) {
                    target.slot = int(s);
                    break;
                }
        } else {
            target.kind = AudioDropTarget::Kind::Track;
        }
        ImportResult made;
        CommandPtr command = importAudio(songData, {f}, target, ids(), &made);
        if (!command)
            continue;
        NoteSelection before = editorState.selection;
        command->apply(songData);
        publish(command->changes());
        complex->push(new CommandAction(id, std::move(command), before, editorState.selection));
    }
    audio->sync(songData);
    if (complex->isEmpty()) {
        delete complex;
        return;
    }
    APP->history->push(complex);
}

} // namespace tessera
