#include "TesseraModule.hpp"

#include "CommandAction.hpp"

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
        if (c.kind != Capture::Kind::Take || player->tapState(c.tap) != AudioPlayer::TapState::Running)
            continue;
        LiveTake t;
        t.track = c.track;
        t.startTick = player->tapStart(c.tap);
        t.lengthTicks = double(captureWriter->written(c.writer)) / rate * (tempo() > 0 ? tempo() : 120) / 60.0 *
                        double(kPpq);
        t.peaks = captureWriter->peaks(c.writer);
        out.push_back(std::move(t));
    }
    return out;
}

void TesseraModule::captureStep() {
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
        spec.track = t.id;
        spec.kind = AudioPlayer::TapKind::In;
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
