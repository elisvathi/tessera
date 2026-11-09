#include "doctest.h"

#include "../src/model/AudioRegions.hpp"

#include "../src/engine/AudioPlayer.hpp"
#include "../src/engine/AudioSnapshotBuilder.hpp"
#include "../src/engine/FrameRing.hpp"
#include "../src/engine/Snapshot.hpp"
#include "../src/model/AudioEdits.hpp"
#include "../src/model/Json.hpp"
#include "../src/model/Sends.hpp"
#include "../src/model/Serialize.hpp"
#include "../src/module/OutputBus.hpp"

#include <chrono>
#include <cmath>
#include <map>
#include <memory>

using namespace tessera;

namespace {

constexpr double kRate = 48000;
constexpr double kVolts = kImportVolts;

/// A buffer whose samples follow `f(frame)`.
template <typename F>
std::shared_ptr<AudioBuffer> bufferOf(int64_t frames, F f, int channels = 1) {
    auto b = std::make_shared<AudioBuffer>();
    b->channels = channels;
    b->frames = frames;
    for (int c = 0; c < channels; c++) {
        b->data[size_t(c)].resize(size_t(frames));
        for (int64_t i = 0; i < frames; i++)
            b->data[size_t(c)][size_t(i)] = float(f(i));
    }
    return b;
}

/// Hands out buffers the test made, in place of the audio store.
class FakeBuffers : public AudioBuffers {
  public:
    std::map<uint64_t, std::shared_ptr<const AudioBuffer>> sources;
    std::vector<std::pair<RenderKey, std::shared_ptr<const AudioBuffer>>> renders;

    std::shared_ptr<const AudioBuffer> prepared(AudioSourceId s, bool reversed = false) override {
        auto it = sources.find(s.value);
        if (it == sources.end())
            return nullptr;
        if (!reversed)
            return it->second;
        auto flipped = std::make_shared<AudioBuffer>(*it->second);
        for (auto& ch : flipped->data)
            std::reverse(ch.begin(), ch.end());
        return flipped;
    }
    std::shared_ptr<const AudioBuffer> render(const RenderKey& key, RenderKey& found) override {
        for (auto& [k, b] : renders)
            if (k.source == key.source) {
                found = k;
                return b;
            }
        return nullptr;
    }
};

/// A song, an engine and an audio player wired together like Tessera's process().
struct Rig {
    Song song;
    BusSlots slots;
    std::unique_ptr<Snapshot> notes;
    std::unique_ptr<AudioSnapshot> audio;
    std::vector<std::unique_ptr<AudioSnapshot>> old; // kept alive, as the exchange would
    FakeBuffers buffers;
    std::unique_ptr<Engine> engine = std::make_unique<Engine>();
    std::unique_ptr<AudioPlayer> player = std::make_unique<AudioPlayer>();
    std::array<TrackFrame, kMaxTracks> frames{};
    TransportEvents events;
    TrackId track;
    ClipId clip;
    ArrangementId instance;
    AudioSourceId source;
    double tempo = 120;

    /// An audio track with one clip of a `seconds`-long source at 48 kHz, its samples from `f`, at clip tempo 120.
    template <typename F>
    Rig(double seconds, F f, bool timeline = true, double clipTempo = 120, TempoFollow follow = TempoFollow::Stretch) {
        song = makeDefaultSong();
        ImportedAudio file;
        file.source.name = "test.wav";
        file.source.sampleRate = kRate;
        file.source.channels = 1;
        file.source.frames = int64_t(std::llround(seconds * kRate));
        file.tempo = clipTempo;
        file.follow = follow;
        AudioDropTarget target;
        target.index = int(song.tracks.size());
        target.timeline = timeline;
        target.at = 0;
        ImportResult made;
        importAudio(song, {file}, target, [this] { return song.nextId++; }, &made)->apply(song);
        track = made.tracks[0];
        clip = made.clips[0];
        if (!made.instances.empty())
            instance = made.instances[0];
        source = song.audioSources[0].id;
        buffers.sources[source.value] = bufferOf(file.source.frames, f);
        engine->setSampleRate(kRate);
        engine->setTempo(tempo);
        player->setSampleRate(kRate);
        rebuild();
    }

    /// Rebuilds both snapshots after the song changed, as the module does after a commit.
    void rebuild() {
        slots.sync(song);
        notes = buildSnapshot(song, slots);
        engine->setSnapshot(notes.get());
        if (audio)
            old.push_back(std::move(audio));
        std::vector<std::shared_ptr<const AudioBuffer>> keep;
        if (!old.empty())
            keep = old.back()->buffers;
        audio = buildAudioSnapshot(song, buffers, kRate, tempo, keep).snapshot;
        player->setSnapshot(audio.get());
    }

    Track& audioTrack() { return *song.findTrack(track); }
    Clip& audioClip() { return *song.findClip(clip); }

    /// One sample: the engine, then the player. Returns the left output in volts.
    float step(float* right = nullptr) {
        engine->setPlayMode(mode);
        engine->process(frames.data(), events);
        float l, r;
        player->process(*engine, l, r);
        if (right)
            *right = r;
        return l;
    }

    /// `n` samples of the left output.
    std::vector<float> run(int64_t n) {
        std::vector<float> out(static_cast<size_t>(n));
        for (auto& x : out)
            x = step();
        return out;
    }

    PlayMode mode = PlayMode::Arranger;
};

/// The biggest change from one sample to the next: a click shows as a big step.
float biggestStep(const std::vector<float>& x) {
    float most = 0;
    for (size_t i = 1; i < x.size(); i++)
        most = std::max(most, std::abs(x[i] - x[i - 1]));
    return most;
}

/// A steady sound: any jump in it is a click.
double dc(int64_t) { return 1.0; }

/// Samples that start at 0 and rise to 1 across one second: their value says where in the file playback is.
double ramp(int64_t i) { return double(i) / kRate; }

} // namespace

TEST_CASE("an audio clip on the timeline plays its file from the right place, in volts") {
    Rig rig(2.0, ramp);
    rig.engine->play();
    std::vector<float> out = rig.run(24000);
    // At 120 BPM a clip of tempo 120 plays one frame per sample.
    CHECK(out[12000] == doctest::Approx(kVolts * 12000 / kRate).epsilon(1e-4));
    CHECK(out[23999] == doctest::Approx(kVolts * 23999 / kRate).epsilon(1e-4));
    CHECK(rig.player->active());
}

TEST_CASE("a reversed clip plays its window of the file back to front") {
    Rig rig(2.0, ramp);
    Clip& c = rig.audioClip();
    c.audio->reverse = true;
    // The clip's window: the file's second half-second to its first second (clip tick 0 is frame 24000).
    c.audio->sourceStart = 24000;
    c.length = kPpq; // one beat: half a second at 120 BPM
    rig.audioTrack().arrangement[0].length = kPpq;
    rig.rebuild();
    rig.engine->play();
    std::vector<float> out = rig.run(24000);
    // Back to front: it starts near the window's end (frame 48000) and runs down to its start.
    CHECK(out[1000] == doctest::Approx(kVolts * (48000 - 1000 - 1) / kRate).epsilon(1e-3));
    CHECK(out[12000] == doctest::Approx(kVolts * (48000 - 12000 - 1) / kRate).epsilon(1e-3));
    CHECK(out[12000] < out[1000]);
}

namespace {

/// `rig`'s clip cut at each of `at` (ticks), stored as its parts.
regions::List cutClip(Rig& rig, std::vector<Tick> at) {
    regions::NewId newId = [&] { return AudioRegionId(rig.song.nextId++); };
    regions::List list = regions::of(rig.audioClip(), kRate);
    for (AudioRegion& r : list)
        r.id = newId();
    for (Tick t : at)
        list = regions::cut(list, *rig.audioClip().audio, t, kRate, newId);
    return list;
}

/// Stores `list` as `rig`'s parts and rebuilds what the player plays.
void store(Rig& rig, const regions::List& list) {
    regions::setRegions(rig.song, rig.clip, list, "Edit", [&] { return rig.song.nextId++; })->apply(rig.song);
    rig.rebuild();
}

} // namespace

TEST_CASE("a plain cut changes nothing you can hear") {
    Rig rig(2.0, dc);
    store(rig, cutClip(rig, {1920}));
    rig.engine->play();
    std::vector<float> out = rig.run(48000);
    CHECK(biggestStep({out.begin() + 2000, out.end() - 2000}) < 0.001f);
}

TEST_CASE("a gap is silent and a moved part plays its own audio in its new place") {
    Rig rig(4.0, ramp);
    regions::List list = cutClip(rig, {960, 1920});
    // Part 2 played from frame 24000; it now sits 960 ticks later, leaving a gap.
    regions::List two{list[0], list[1]};
    two[1].start = 1920 + 480;
    store(rig, two);
    rig.engine->play();
    std::vector<float> out = rig.run(96000);
    CHECK(out[12000] == doctest::Approx(kVolts * 12000 / kRate).epsilon(1e-3)); // part 1 as before
    CHECK(out[36000] == doctest::Approx(0).epsilon(1e-6));                      // the gap
    // 100 ms into the moved part: frame 24000 + 4800.
    CHECK(out[60000 + 4800] == doctest::Approx(kVolts * 28800 / kRate).epsilon(1e-3));
}

TEST_CASE("a part can be muted, turned down and played backwards") {
    Rig rig(2.0, ramp);
    regions::List list = cutClip(rig, {960, 1920});
    list[0].gainDb = -6.0206f;
    list[1].muted = true;
    list[2].reverse = true;
    store(rig, list);
    rig.engine->play();
    std::vector<float> out = rig.run(90000);
    CHECK(out[12000] == doctest::Approx(0.5 * kVolts * 12000 / kRate).epsilon(1e-3));
    CHECK(out[36000] == doctest::Approx(0).epsilon(1e-6));
    // The last part plays frames 96000 down to 48000 from sample 48000: at sample 72000, frame 72000.
    CHECK(out[72000] == doctest::Approx(kVolts * 72000 / kRate).epsilon(2e-3));
    CHECK(out[60000] > out[80000]);
}

TEST_CASE("parts that overlap crossfade without a click") {
    Rig rig(2.0, dc);
    regions::List list = cutClip(rig, {1920});
    regions::List overlap = list;
    overlap[1] = regions::trimStart(list, list[1].id, 1500, *rig.audioClip().audio, kRate, 96000)[1];
    store(rig, overlap);
    rig.engine->play();
    std::vector<float> out = rig.run(48000);
    CHECK(biggestStep({out.begin() + 2000, out.end() - 2000}) < 0.02f);
    float lowest = *std::min_element(out.begin() + 2000, out.end() - 2000);
    CHECK(lowest > float(kVolts) * 0.65f);
}

TEST_CASE("a launched clip plays its parts too") {
    Rig rig(2.0, ramp, false);
    regions::List list = cutClip(rig, {960});
    list[1].gainDb = kSilentDb;
    store(rig, list);
    rig.mode = PlayMode::Launcher;
    int slot = -1;
    for (size_t i = 0; i < rig.audioTrack().slots.size(); i++)
        if (rig.audioTrack().slots[i].clip)
            slot = int(i);
    REQUIRE(slot >= 0);
    rig.engine->launch({LaunchEvent::Kind::Slot, rig.track.value, slot, true});
    rig.engine->play();
    std::vector<float> out = rig.run(48000);
    float early = *std::max_element(out.begin() + 2000, out.begin() + 20000);
    float late = *std::max_element(out.begin() + 26000, out.end());
    CHECK(early > 0);
    CHECK(late == doctest::Approx(0).epsilon(1e-6));
}

TEST_CASE("a song without audio tracks costs the player nothing") {
    Song song = makeDefaultSong();
    FakeBuffers buffers;
    auto snap = buildAudioSnapshot(song, buffers, kRate, 120).snapshot;
    AudioPlayer player;
    player.setSnapshot(snap.get());
    CHECK_FALSE(player.active());
}

TEST_CASE("clip edges, a stop and the transport starting never click") {
    Rig rig(1.0, dc);
    rig.engine->play();
    std::vector<float> out = rig.run(int64_t(1.2 * kRate)); // the start, the whole clip, its end
    CHECK(out[1] < 0.2f);
    CHECK(out[24000] == doctest::Approx(kVolts));
    CHECK(std::abs(out.back()) < 1e-6f);
    // A 2 ms fade on 5 V moves about 0.05 V a sample.
    CHECK(biggestStep(out) < 0.1f);

    Rig mid(4.0, dc);
    mid.engine->play();
    std::vector<float> a = mid.run(24000);
    mid.engine->stop();
    std::vector<float> b = mid.run(2000);
    a.insert(a.end(), b.begin(), b.end());
    CHECK(biggestStep(a) < 0.1f);
    CHECK(std::abs(a.back()) < 1e-6f);
}

TEST_CASE("a seek into the middle of a clip fades in instead of clicking") {
    Rig rig(4.0, dc);
    rig.engine->play();
    std::vector<float> a = rig.run(4800);
    rig.engine->seek(2 * kPpq);
    std::vector<float> b = rig.run(4800);
    a.insert(a.end(), b.begin(), b.end());
    CHECK(biggestStep(a) < 0.1f);
}

TEST_CASE("a looping clip repeats across its instance and its seams crossfade") {
    // Half a second of rising samples, looping in an instance of two seconds: without a crossfade, each seam jumps
    // from 5 V × 0.5 back to 0.
    Rig rig(0.5, ramp);
    Track& t = rig.audioTrack();
    t.arrangement[0].length = 4 * kPpq;
    rig.rebuild();
    rig.engine->play();
    std::vector<float> out = rig.run(int64_t(1.9 * kRate));
    CHECK(out[size_t(0.25 * kRate)] == doctest::Approx(kVolts * 0.25).epsilon(1e-3));
    CHECK(out[size_t(0.75 * kRate)] == doctest::Approx(kVolts * 0.25).epsilon(1e-3));
    CHECK(out[size_t(1.25 * kRate)] == doctest::Approx(kVolts * 0.25).epsilon(1e-3));
    CHECK(biggestStep(out) < 0.1f);
}

TEST_CASE("mute, solo and the fader ramp instead of jumping") {
    Rig rig(4.0, dc);
    rig.engine->play();
    std::vector<float> out = rig.run(4800);
    rig.audioTrack().mute = true;
    rig.rebuild();
    std::vector<float> muted = rig.run(480);
    CHECK(std::abs(muted.back()) < 1e-6f);
    rig.audioTrack().mute = false;
    rig.audioTrack().channel.volumeDb = -6.0206f;
    rig.rebuild();
    std::vector<float> back = rig.run(480);
    CHECK(back.back() == doctest::Approx(kVolts / 2).epsilon(1e-3));
    out.insert(out.end(), muted.begin(), muted.end());
    out.insert(out.end(), back.begin(), back.end());
    CHECK(biggestStep(out) < 0.2f);
}

TEST_CASE("pan is a balance: centre leaves both sides alone, hard left silences the right") {
    float l, r;
    balanceGains(0, l, r);
    CHECK(l == doctest::Approx(1));
    CHECK(r == doctest::Approx(1));
    balanceGains(-1, l, r);
    CHECK(l == doctest::Approx(1));
    CHECK(r == doctest::Approx(0).epsilon(1e-6));
    balanceGains(0.5f, l, r);
    CHECK(r == doctest::Approx(1));
    CHECK(l < 1);
}

TEST_CASE("the master fader scales MAIN, and meters and the clip light follow it") {
    Rig rig(4.0, dc);
    rig.song.master.volumeDb = 6.0206f;
    rig.rebuild();
    rig.engine->play();
    std::vector<float> out = rig.run(9600);
    CHECK(out.back() == doctest::Approx(2 * kVolts).epsilon(1e-3));
    CHECK(rig.player->masterLevel(0) == doctest::Approx(2 * kVolts).epsilon(1e-3));
    CHECK(rig.player->level(0, 1) == doctest::Approx(kVolts).epsilon(1e-3));
    CHECK(rig.player->masterClipPeak() == doctest::Approx(10).epsilon(1e-3));
    rig.player->clearClip();
    CHECK(rig.player->masterClipPeak() == 0);
}

TEST_CASE("a fader held during a drag plays at once, without a new snapshot") {
    Rig rig(4.0, dc);
    rig.engine->play();
    rig.run(4800);
    rig.player->hold(rig.track.value, 0.5f, 0, AudioPlayer::kHoldVolume);
    CHECK(rig.run(100).back() == doctest::Approx(kVolts / 2).epsilon(1e-3));
    rig.player->releaseHolds();
    CHECK(rig.run(100).back() == doctest::Approx(kVolts).epsilon(1e-3));
}

TEST_CASE("an Off clip plays at its own speed whatever the song tempo") {
    Rig rig(2.0, ramp, true, 120, TempoFollow::Off);
    rig.tempo = 180;
    rig.engine->setTempo(180);
    rig.rebuild();
    rig.engine->play();
    std::vector<float> out = rig.run(24000);
    CHECK(out[12000] == doctest::Approx(kVolts * 12000 / kRate).epsilon(1e-4));
    // A tempo change while it plays doesn't move it either.
    rig.engine->setTempo(90);
    std::vector<float> more = rig.run(100);
    CHECK(more[99] == doctest::Approx(kVolts * (24000 + 99) / kRate).epsilon(1e-4));
}

TEST_CASE("without a render, a Stretch clip plays the quick version: faster, like tape") {
    Rig rig(4.0, ramp);
    rig.tempo = 240;
    rig.engine->setTempo(240);
    rig.rebuild();
    rig.engine->play();
    std::vector<float> out = rig.run(12000);
    // At double the clip tempo it moves two frames a sample. The ramp is straight, so the interpolation is exact.
    CHECK(out[6000] == doctest::Approx(kVolts * 12000 / kRate).epsilon(1e-3));
}

TEST_CASE("a ready render plays instead, and the switch to it crossfades") {
    Rig rig(4.0, dc);
    rig.tempo = 90;
    rig.engine->setTempo(90);
    rig.rebuild();
    rig.engine->play();
    std::vector<float> a = rig.run(9600);
    CHECK(a.back() == doctest::Approx(kVolts).epsilon(1e-3));
    // The render of the clip for 90 BPM: 4/3 as long. A different level shows which buffer plays.
    RenderKey key;
    REQUIRE(wantsRender(*rig.audioClip().audio, 90, key));
    rig.buffers.renders.push_back({key, bufferOf(int64_t(4.0 * kRate * 4 / 3), [](int64_t) { return 0.5; })});
    rig.rebuild();
    std::vector<float> b = rig.run(4800);
    CHECK(b.back() == doctest::Approx(kVolts / 2).epsilon(1e-3));
    a.insert(a.end(), b.begin(), b.end());
    CHECK(biggestStep(a) < 0.1f);
}

TEST_CASE("the builder asks for the render a Stretch clip needs at this tempo") {
    Rig rig(4.0, dc);
    FakeBuffers& buffers = rig.buffers;
    AudioBuild b = buildAudioSnapshot(rig.song, buffers, kRate, 120);
    CHECK(b.wanted.empty());
    b = buildAudioSnapshot(rig.song, buffers, kRate, 100);
    REQUIRE(b.wanted.size() == 1);
    CHECK(b.wanted[0].factor == doctest::Approx(1.2));
    rig.audioClip().audio->follow = TempoFollow::Off;
    CHECK(buildAudioSnapshot(rig.song, buffers, kRate, 100).wanted.empty());
}

TEST_CASE("a launched audio clip plays from the launch and loops") {
    Rig rig(0.5, ramp, false);
    rig.mode = PlayMode::Launcher;
    rig.engine->launch({LaunchEvent::Kind::Slot, rig.track.value, 0, true});
    std::vector<float> out = rig.run(int64_t(1.2 * kRate));
    CHECK(out[size_t(0.25 * kRate)] == doctest::Approx(kVolts * 0.25).epsilon(2e-3));
    CHECK(out[size_t(0.75 * kRate)] == doctest::Approx(kVolts * 0.25).epsilon(2e-3));
    CHECK(biggestStep(out) < 0.1f);
}

TEST_CASE("fades shape the edges, and overlapping clips crossfade at equal power") {
    CHECK(fadeShape(0.5f, 0, false) == doctest::Approx(0.5));
    CHECK(fadeShape(0.5f, 0, true) == doctest::Approx(std::sin(3.14159265 / 4)));
    CHECK(fadeShape(0.5f, 1, false) > 0.5f);
    CHECK(fadeShape(0.5f, -1, false) < 0.5f);

    Rig rig(4.0, dc);
    rig.audioTrack().arrangement[0].fadeIn = {kPpq, 0};
    rig.rebuild();
    rig.engine->play();
    std::vector<float> out = rig.run(24000);
    // Halfway through a one-beat fade (a beat is half a second at 120 BPM).
    CHECK(out[12000] == doctest::Approx(kVolts * 0.5).epsilon(1e-2));

    // A second clip starting a beat before the first ends: over that beat each plays at sin or cos of the way.
    Rig two(1.0, dc);
    ArrangerClip second = two.audioTrack().arrangement[0];
    second.id = two.song.newId<ArrangementId>();
    second.position = kPpq; // the first lasts two beats
    two.audioTrack().arrangement.push_back(second);
    two.rebuild();
    REQUIRE(two.audio->tracks[0].instances.size() == 2);
    CHECK(two.audio->tracks[0].instances[0].overlapOut == doctest::Approx(kPpq));
    two.engine->play();
    std::vector<float> x = two.run(int64_t(1.6 * kRate));
    CHECK(x[size_t(0.75 * kRate)] == doctest::Approx(kVolts * 2 * std::sin(3.14159265 / 4)).epsilon(1e-2));
    CHECK(biggestStep(x) < 0.1f);
}

TEST_CASE("a new snapshot keeps playing voices, and a deleted track fades out") {
    Rig rig(4.0, ramp);
    rig.engine->play();
    std::vector<float> a = rig.run(9600);
    rig.audioClip().name = "renamed";
    rig.rebuild();
    std::vector<float> b = rig.run(480);
    CHECK(b[100] == doctest::Approx(kVolts * (9600 + 100) / kRate).epsilon(1e-4));
    rig.song.tracks.pop_back();
    rig.rebuild();
    std::vector<float> c = rig.run(960);
    CHECK(std::abs(c.back()) < 1e-6f);
    a.insert(a.end(), b.begin(), b.end());
    a.insert(a.end(), c.begin(), c.end());
    CHECK(biggestStep(a) < 0.1f);
}

TEST_CASE("OUT carries a track's clips before its fader; what comes back on IN joins the track before its fader") {
    Rig rig(4.0, dc);
    rig.audioTrack().channel.volumeDb = -6.0206f; // half
    MixPlan plan;
    plan.busSlots = {{rig.track, 5}};
    plan.returns = {{rig.track, 2}};
    rig.old.push_back(std::move(rig.audio)); // the player still plays it until the swap
    rig.audio = buildAudioSnapshot(rig.song, rig.buffers, kRate, rig.tempo, {}, plan).snapshot;
    rig.player->setSnapshot(rig.audio.get());
    OutputBus bus;
    ReturnBus& returns = bus.returns();
    rig.engine->play();
    float left = 0, right = 0;
    for (int64_t n = 0; n < 24000; n++) {
        returns.write(n, 2, rig.track, 1.f, 1.f); // an effect sends 1 V back on IN
        rig.engine->setPlayMode(rig.mode);
        rig.engine->process(rig.frames.data(), rig.events);
        AudioPlayer::Rack rack{&returns, bus.beginAudioWrite(n), n};
        rig.player->process(*rig.engine, left, right, rack);
        bus.publishAudio(n);
    }
    const AudioOutFrame* out = bus.readAudio(24000, 5, rig.track);
    REQUIRE(out);
    CHECK(out->l == doctest::Approx(kVolts)); // the clips at full level: OUT is before the fader
    // MAIN: (clips + IN) through the half fader.
    CHECK(left == doctest::Approx((kVolts + 1) * 0.5).epsilon(1e-3));
    const AudioOutFrame* master = bus.readAudio(24000, OutputBus::kMasterSlot, TrackId(rig.audio->masterId));
    REQUIRE(master);
    CHECK(master->l == doctest::Approx(left).epsilon(1e-3));
}

TEST_CASE("turning Clips off leaves OUT sending them, so an effect keeps playing without the dry sound") {
    Rig rig(4.0, dc);
    rig.audioTrack().clips.use = false;
    MixPlan plan;
    plan.busSlots = {{rig.track, 5}};
    plan.returns = {{rig.track, 2}};
    rig.old.push_back(std::move(rig.audio));
    rig.audio = buildAudioSnapshot(rig.song, rig.buffers, kRate, rig.tempo, {}, plan).snapshot;
    rig.player->setSnapshot(rig.audio.get());
    OutputBus bus;
    ReturnBus& returns = bus.returns();
    rig.engine->play();
    float left = 0, right = 0;
    for (int64_t n = 0; n < 24000; n++) {
        returns.write(n, 2, rig.track, 1.f, 1.f); // the effect sends 1 V back on IN
        rig.engine->setPlayMode(rig.mode);
        rig.engine->process(rig.frames.data(), rig.events);
        AudioPlayer::Rack rack{&returns, bus.beginAudioWrite(n), n};
        rig.player->process(*rig.engine, left, right, rack);
        bus.publishAudio(n);
    }
    const AudioOutFrame* out = bus.readAudio(24000, 5, rig.track);
    REQUIRE(out);
    CHECK(out->l == doctest::Approx(kVolts));        // the effect still gets the clips
    CHECK(left == doctest::Approx(1).epsilon(1e-3)); // and only the effect is heard
}

TEST_CASE("a track's own sources go to OUT, an effect's return doesn't, so an effect loop never feeds back") {
    Song song = makeDefaultSong();
    FakeBuffers buffers;
    TrackId drums = song.tracks.back().id;
    MixPlan plan;
    plan.busSlots = {{drums, 3}};
    plan.returns = {{drums, 0, true}, {drums, 1, false}}; // a lane IN, the kit IN
    auto snap = buildAudioSnapshot(song, buffers, kRate, 120, {}, plan).snapshot;
    REQUIRE(snap->tracks.size() == 1);
    CHECK(snap->tracks[0].sources == std::vector<int>{0});
    CHECK(snap->tracks[0].returns == std::vector<int>{1});
    AudioPlayer player;
    player.setSampleRate(kRate);
    player.setSnapshot(snap.get());
    Engine engine;
    engine.setSampleRate(kRate);
    ReturnBus returns;
    std::array<AudioOutFrame, kMaxTracks + 1> out{};
    std::array<TrackFrame, kMaxTracks> frames{};
    TransportEvents events;
    float left = 0, right = 0;
    for (int64_t n = 0; n < 2000; n++) {
        returns.write(n, 0, drums, 1.f, 1.f);
        returns.write(n, 1, drums, 0.25f, 0.25f);
        engine.process(frames.data(), events);
        player.process(engine, left, right, {&returns, out.data(), n});
    }
    CHECK(out[3].l == doctest::Approx(1.f)); // OUT: the lane's sound only
    CHECK(left == doctest::Approx(1.25f));   // the track: both
}

TEST_CASE("muting a track fades its clips out of OUT too, without a click") {
    Rig rig(4.0, dc);
    MixPlan plan;
    plan.busSlots = {{rig.track, 0}};
    rig.old.push_back(std::move(rig.audio));
    rig.audio = buildAudioSnapshot(rig.song, rig.buffers, kRate, rig.tempo, {}, plan).snapshot;
    rig.player->setSnapshot(rig.audio.get());
    OutputBus bus;
    rig.engine->play();
    std::vector<float> out;
    float l, r;
    auto run = [&](int n) {
        for (int i = 0; i < n; i++) {
            int64_t frame = int64_t(out.size());
            rig.engine->setPlayMode(rig.mode);
            rig.engine->process(rig.frames.data(), rig.events);
            rig.player->process(*rig.engine, l, r, {&bus.returns(), bus.beginAudioWrite(frame), frame});
            bus.publishAudio(frame);
            out.push_back(bus.readAudio(frame + 1, 0, rig.track)->l);
        }
    };
    run(4800);
    CHECK(out.back() == doctest::Approx(kVolts));
    rig.audioTrack().mute = true;
    rig.old.push_back(std::move(rig.audio));
    rig.audio = buildAudioSnapshot(rig.song, rig.buffers, kRate, rig.tempo, {}, plan).snapshot;
    rig.player->setSnapshot(rig.audio.get());
    run(480);
    CHECK(out.back() == 0);
    CHECK(biggestStep(out) < 0.2f);
}

TEST_CASE("a note track whose output module brings audio back on IN makes sound; the master's IN comes after its OUT") {
    Song song = makeDefaultSong();
    FakeBuffers buffers;
    TrackId notes = song.tracks.front().id;
    MixPlan plan;
    plan.returns = {{notes, 0}, {song.master.id, 1}};
    CHECK(makesSound(song.tracks.front(), plan));
    CHECK_FALSE(makesSound(song.tracks[1], plan));
    auto snap = buildAudioSnapshot(song, buffers, kRate, 120, {}, plan).snapshot;
    REQUIRE(snap->tracks.size() == 1);
    CHECK(snap->tracks[0].id == notes);
    CHECK(snap->masterReturns == std::vector<int>{1});

    AudioPlayer player;
    player.setSampleRate(kRate);
    player.setSnapshot(snap.get());
    REQUIRE(player.active());
    Engine engine;
    engine.setSampleRate(kRate);
    ReturnBus returns;
    std::array<AudioOutFrame, kMaxTracks + 1> out{};
    float left = 0, right = 0;
    std::array<TrackFrame, kMaxTracks> frames{};
    TransportEvents events;
    for (int64_t n = 0; n < 2000; n++) {
        returns.write(n, 0, notes, 2.f, 2.f);
        returns.write(n, 1, song.master.id, 0.5f, 0.5f);
        engine.process(frames.data(), events);
        player.process(engine, left, right, {&returns, out.data(), n});
    }
    CHECK(left == doctest::Approx(2.5f));
    CHECK(out[kMaxTracks].l == doctest::Approx(2.f)); // the master's OUT is its mix, before its own IN
    CHECK(player.level(0, 0) > 1.9f);
}

TEST_CASE("the clips' Level and Use scale an audio track's clips, and the mix's Level scales the master") {
    Rig rig(4.0, dc);
    rig.engine->play();
    rig.run(4800);
    rig.audioTrack().clips = {-6.0206f, true};
    rig.rebuild();
    CHECK(rig.run(480).back() == doctest::Approx(kVolts / 2).epsilon(1e-3));
    rig.audioTrack().clips = {0, false};
    rig.rebuild();
    CHECK(std::abs(rig.run(480).back()) < 1e-5f);
    rig.audioTrack().clips = {};
    rig.song.master.mix = {-6.0206f, true};
    rig.rebuild();
    CHECK(rig.run(480).back() == doctest::Approx(kVolts / 2).epsilon(1e-3));
    // A Level drag plays at once, before it is committed.
    rig.player->holdClips(rig.song.master.id.value, 1.f);
    CHECK(rig.run(480).back() == doctest::Approx(kVolts).epsilon(1e-3));
}

TEST_CASE("the master's Volume lane moves the master, a held master fader wins, and a muted lane gives it back") {
    Rig rig(4.0, dc);
    AutomationEnvelope volume;
    volume.id = AutomationId(rig.song.nextId++);
    volume.drive = LaneDrive::Volume;
    volume.points = {{0, faderPosOfDb(-6.0206f)}};
    rig.song.master.automation = {volume};
    rig.rebuild();
    rig.engine->play();
    float right = 0, left = 0;
    for (int i = 0; i < 4800; i++)
        left = rig.step(&right);
    CHECK(left == doctest::Approx(kVolts * 0.5).epsilon(1e-3));
    float db = 0;
    REQUIRE(rig.player->masterAutomated(db));
    CHECK(db == doctest::Approx(-6.02).epsilon(1e-2));

    rig.player->hold(rig.song.master.id.value, 1.f, 0, AudioPlayer::kHoldVolume);
    CHECK(rig.run(480).back() == doctest::Approx(kVolts).epsilon(1e-3));
    rig.player->releaseHolds();

    rig.song.master.automation[0].mute = true;
    rig.rebuild();
    CHECK(rig.run(480).back() == doctest::Approx(kVolts).epsilon(1e-3));
    CHECK_FALSE(rig.player->masterAutomated(db));
}

TEST_CASE("a fader lane moves an audio track's level and a pan lane its pan; a held fader wins over them") {
    Rig rig(4.0, dc);
    AutomationEnvelope volume;
    volume.id = AutomationId(rig.song.nextId++);
    volume.name = "Volume";
    volume.drive = LaneDrive::Volume;
    volume.rangeMin = 0;
    volume.rangeMax = 1;
    volume.points = {{0, faderPosOfDb(-6.0206f)}};
    AutomationEnvelope pan = volume;
    pan.id = AutomationId(rig.song.nextId++);
    pan.name = "Pan";
    pan.drive = LaneDrive::Pan;
    pan.points = {{0, 0.f}}; // hard left
    rig.audioTrack().automation = {volume, pan};
    rig.rebuild();
    rig.engine->play();
    float right = 0, left = 0;
    for (int i = 0; i < 4800; i++)
        left = rig.step(&right);
    CHECK(left == doctest::Approx(kVolts * 0.5).epsilon(1e-3));
    CHECK(std::abs(right) < 1e-4f);
    float db = 0, p = 0;
    REQUIRE(rig.player->automated(0, false, db));
    CHECK(db == doctest::Approx(-6.02).epsilon(1e-2));
    REQUIRE(rig.player->automated(0, true, p));
    CHECK(p == doctest::Approx(-1));

    // While the user drags the pan, only the pan plays where the hand is: the fader lane goes on.
    rig.player->hold(rig.track.value, 1.f, 0.f, AudioPlayer::kHoldPan);
    for (int i = 0; i < 480; i++)
        left = rig.step(&right);
    CHECK(left == doctest::Approx(kVolts * 0.5).epsilon(1e-3));
    CHECK(right == doctest::Approx(kVolts * 0.5).epsilon(1e-3));
    float held = 0;
    CHECK(rig.player->held(rig.track.value, true, held));
    CHECK_FALSE(rig.player->held(rig.track.value, false, held));

    // Holding both, both play where the hand is.
    rig.player->hold(rig.track.value, 1.f, 0.f, AudioPlayer::kHoldVolume | AudioPlayer::kHoldPan);
    for (int i = 0; i < 480; i++)
        left = rig.step(&right);
    CHECK(left == doctest::Approx(kVolts).epsilon(1e-3));
    CHECK(right == doctest::Approx(kVolts).epsilon(1e-3));
}

TEST_CASE("a missing file plays silence") {
    Rig rig(4.0, dc);
    rig.buffers.sources.clear();
    rig.rebuild();
    rig.engine->play();
    std::vector<float> out = rig.run(4800);
    CHECK(biggestStep(out) == 0);
    CHECK(out.back() == 0);
}

TEST_CASE("performance: clips cut into many parts stay inside their budget") {
    auto secondOf = [](int tracks, int cuts) {
        Rig rig(8.0, [](int64_t i) { return std::sin(double(i) * 0.05); });
        if (cuts > 0) {
            std::vector<Tick> at;
            for (int i = 1; i <= cuts; i++)
                at.push_back(Tick(i) * rig.audioClip().length / Tick(cuts + 1));
            regions::List list = cutClip(rig, at);
            // A gain per part stops the parts joining back up, so the player really walks them all.
            for (size_t i = 0; i < list.size(); i++)
                list[i].gainDb = float(i % 3) * -0.5f;
            store(rig, list);
        }
        Track proto = rig.audioTrack();
        for (int t = 1; t < tracks; t++) {
            Track copy = proto;
            copy.id = rig.song.newId<TrackId>();
            for (ArrangerClip& a : copy.arrangement)
                a.id = rig.song.newId<ArrangementId>();
            rig.song.tracks.push_back(copy);
        }
        rig.rebuild();
        rig.engine->play();
        auto start = std::chrono::steady_clock::now();
        for (int s = 0; s < 48000; s++)
            rig.step();
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() * 100;
    };
    double plain = secondOf(16, 0);
    double cut = secondOf(16, 100);
    MESSAGE("16 audio tracks: " << plain << "% of a core plain, " << cut << "% with 100 parts each");
#if defined(NDEBUG) && !defined(__SANITIZE_ADDRESS__)
    CHECK(cut - plain < 1.5); // measured on 9 Nov: 0.5 to 1.1, nearly all of it the 2 ms fades at each seam
#endif
}

TEST_CASE("performance: playing and mixing audio tracks stays inside its budget (§8.8)") {
    auto secondOf = [](int tracks) {
        Rig rig(8.0, [](int64_t i) { return std::sin(double(i) * 0.05); });
        Track proto = rig.audioTrack();
        for (int t = 1; t < tracks; t++) {
            Track copy = proto;
            copy.id = rig.song.newId<TrackId>();
            for (ArrangerClip& a : copy.arrangement)
                a.id = rig.song.newId<ArrangementId>();
            rig.song.tracks.push_back(copy);
        }
        rig.rebuild();
        rig.engine->play();
        auto start = std::chrono::steady_clock::now();
        for (int s = 0; s < 48000; s++)
            rig.step();
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() * 100;
    };
    double sixteen = secondOf(16);
    double sixty = secondOf(60);
    MESSAGE("16 audio tracks: " << sixteen << "% of a core (engine included), 60: " << sixty << "%");
#if defined(NDEBUG) && !defined(__SANITIZE_ADDRESS__)
    // Measured on 3 Nov: about 1.5% for 16 tracks and 9% for 60, the note engine's own 3% for 64 tracks included.
    CHECK(sixteen < 3);
    CHECK(sixty < 12);
#else
    CHECK(sixty < 800);
#endif
}

TEST_CASE("a send goes to the receiving track's OUT, before or after the fader, and follows mute and solo") {
    Song song = makeDefaultSong();
    FakeBuffers buffers;
    TrackId bass = song.tracks[0].id, reverb = song.tracks[1].id;
    song.tracks[0].channel.volumeDb = -6.0206f;
    addSend(song, bass, reverb, 0)->apply(song);
    MixPlan plan;
    plan.returns = {{bass, 0}, {reverb, 1}};
    plan.busSlots = {{bass, 0}, {reverb, 1}};
    Engine engine;
    engine.setSampleRate(kRate);
    AudioPlayer player;
    player.setSampleRate(kRate);
    std::vector<std::unique_ptr<AudioSnapshot>> kept;
    ReturnBus returns;
    std::array<AudioOutFrame, kMaxTracks + 1> out{};
    std::array<TrackFrame, kMaxTracks> frames{};
    TransportEvents events;
    int64_t n = 0;
    float left = 0, right = 0;
    // Bass's voice comes back at 2 V; the reverb's effect at 0.3 V.
    auto play = [&] {
        kept.push_back(buildAudioSnapshot(song, buffers, kRate, 120, {}, plan).snapshot);
        player.setSnapshot(kept.back().get());
        for (int i = 0; i < 2000; i++, n++) {
            returns.write(n, 0, bass, 2.f, 2.f);
            returns.write(n, 1, reverb, 0.3f, 0.3f);
            engine.process(frames.data(), events);
            player.process(engine, left, right, {&returns, out.data(), n});
        }
    };
    play();
    REQUIRE(kept.back()->tracks.size() == 2);
    CHECK(kept.back()->tracks[0].id == bass);              // the sender comes first
    CHECK(out[1].l == doctest::Approx(1.f).epsilon(1e-3)); // after the fader: 2 V at -6 dB
    CHECK(left == doctest::Approx(1.3f).epsilon(1e-3));    // the send never joins the reverb's sound directly
    CHECK(player.outLevel(1) > 0.9f);

    setSend(song, bass, {reverb, 0, true}, "pre")->apply(song);
    play();
    CHECK(out[1].l == doctest::Approx(2.f).epsilon(1e-3));

    song.tracks[0].mute = true; // a muted track's sends stop
    play();
    CHECK(std::abs(out[1].l) < 1e-4f);
    CHECK(left == doctest::Approx(0.3f).epsilon(1e-3));

    song.tracks[0].mute = false;
    song.tracks[1].mute = true; // a muted receiver keeps feeding its effect, but isn't heard
    play();
    CHECK(out[1].l == doctest::Approx(2.f).epsilon(1e-3));
    CHECK(left == doctest::Approx(1.f).epsilon(1e-3));

    song.tracks[1].mute = false;
    song.tracks[0].solo = true; // solo follows the sends: the reverb stays on
    play();
    CHECK(left == doctest::Approx(1.3f).epsilon(1e-3));

    song.tracks[0].solo = false;
    song.tracks[1].solo = true; // solo the reverb: Bass keeps feeding it, but Bass itself is silent
    play();
    CHECK(out[1].l == doctest::Approx(2.f).epsilon(1e-3));
    CHECK(left == doctest::Approx(0.3f).epsilon(1e-3));
    song.tracks[1].solo = false;
    // A Send lane moves the send: -6 dB before the fader.
    AutomationEnvelope lane;
    lane.id = song.newId<AutomationId>();
    lane.drive = LaneDrive::Send;
    lane.sendTo = reverb;
    lane.points = {{0, faderPosOfDb(-6.0206f)}};
    song.tracks[0].automation.push_back(lane);
    play();
    CHECK(out[1].l == doctest::Approx(1.f).epsilon(1e-3));
    float db = 0;
    CHECK(player.sendAutomated(0, 0, db));
    CHECK(db == doctest::Approx(-6.02f).epsilon(1e-2));
    CHECK(player.sendLevel(0, 0) > 0.9f);
}

TEST_CASE("a tap copies a track's sound from a song tick to another, then is done") {
    Rig rig(4.0, dc);
    rig.audioTrack().channel.volumeDb = -6.0206f;
    rig.rebuild();
    FrameRing sound(48000), stem(48000);
    AudioPlayer::TapSpec a;
    a.track = rig.track;
    a.kind = AudioPlayer::TapKind::Sound;
    a.from = kPpq;
    a.to = 2 * kPpq;
    a.ring = &sound;
    rig.player->armTap(0, a);
    AudioPlayer::TapSpec b = a;
    b.kind = AudioPlayer::TapKind::Stem;
    b.ring = &stem;
    rig.player->armTap(1, b);
    rig.engine->play();
    rig.run(50000); // two beats at 120 BPM are 48000 samples: a little more is past the end
    CHECK(rig.player->tapState(0) == AudioPlayer::TapState::Done);
    CHECK(rig.player->tapStart(0) >= kPpq);
    // One beat at 120 BPM: half a second.
    CHECK(std::abs(double(sound.available()) - 24000) < 100);
    std::vector<float> frames(2 * 100);
    sound.pop(frames.data(), 100);
    CHECK(frames[0] == doctest::Approx(kVolts).epsilon(1e-3)); // before the fader
    stem.pop(frames.data(), 100);
    CHECK(frames[0] == doctest::Approx(kVolts / 2).epsilon(1e-3)); // after it
}

TEST_CASE("delay compensation: the other tracks wait for a track's latency, so they meet at the master") {
    Song song = makeDefaultSong();
    FakeBuffers buffers;
    TrackId late = song.tracks[0].id, onTime = song.tracks[1].id;
    song.tracks[0].latencySamples = 100;
    MixPlan plan;
    plan.returns = {{late, 0}, {onTime, 1}};
    plan.busSlots = {{late, 0}, {onTime, 1}};
    auto snap = buildAudioSnapshot(song, buffers, kRate, 120, {}, plan).snapshot;
    REQUIRE(snap->tracks.size() == 2);
    AudioPlayer player;
    player.setSampleRate(kRate);
    player.ensureDelays();
    player.setSnapshot(snap.get());
    Engine engine;
    engine.setSampleRate(kRate);
    ReturnBus returns;
    std::array<AudioOutFrame, kMaxTracks + 1> out{};
    std::array<TrackFrame, kMaxTracks> frames{};
    TransportEvents events;
    // The same click: on time on one track, 100 samples late on the other (its latency).
    std::vector<float> master;
    for (int64_t n = 0; n < 2000; n++) {
        returns.write(n, 0, late, n == 1100 ? 1.f : 0.f, 0.f);
        returns.write(n, 1, onTime, n == 1000 ? 1.f : 0.f, 0.f);
        engine.process(frames.data(), events);
        float l = 0, r = 0;
        player.process(engine, l, r, {&returns, out.data(), n});
        master.push_back(l);
    }
    // Both arrive in the same sample: one click of 2 V, not two of 1 V.
    float peak = *std::max_element(master.begin(), master.end());
    CHECK(peak == doctest::Approx(2.f).epsilon(1e-3));
}

TEST_CASE("a group strip sums its tracks through its own fader and mute on the way to the master") {
    Song song = makeDefaultSong();
    FakeBuffers buffers;
    TrackId a = song.tracks[0].id, b = song.tracks[1].id;
    TrackGroup g;
    g.id = song.newId<GroupId>();
    g.name = "Keys";
    g.tracks = {a, b};
    g.channel.volumeDb = -6.0206f;
    song.groups.push_back(g);
    MixPlan plan;
    plan.returns = {{a, 0}, {b, 1}, {song.tracks[2].id, 2}};
    Engine engine;
    engine.setSampleRate(kRate);
    ReturnBus returns;
    std::array<AudioOutFrame, kMaxTracks + 1> out{};
    std::array<TrackFrame, kMaxTracks> frames{};
    TransportEvents events;
    std::vector<std::unique_ptr<AudioSnapshot>> kept;
    AudioPlayer player;
    player.setSampleRate(kRate);
    int64_t n = 0;
    float left = 0, right = 0;
    auto play = [&] {
        kept.push_back(buildAudioSnapshot(song, buffers, kRate, 120, {}, plan).snapshot);
        player.setSnapshot(kept.back().get());
        for (int i = 0; i < 2000; i++, n++) {
            returns.write(n, 0, a, 1.f, 1.f);
            returns.write(n, 1, b, 1.f, 1.f);
            returns.write(n, 2, song.tracks[2].id, 0.25f, 0.25f); // not in the group
            engine.process(frames.data(), events);
            player.process(engine, left, right, {&returns, out.data(), n});
        }
    };
    play();
    REQUIRE(kept.back()->groups.size() == 1);
    CHECK(left == doctest::Approx(1.25f).epsilon(1e-3)); // (1 + 1) at -6 dB, and the other track as it is
    CHECK(player.groupLevel(0, 0) > 0.9f);
    song.groups[0].mute = true;
    play();
    CHECK(left == doctest::Approx(0.25f).epsilon(1e-3));
}

TEST_CASE("before its render is ready, a warped clip plays its file through the markers") {
    // A ramp: each sample is its own frame number, so the output says where in the file the clip plays.
    Rig rig(4.0, [](int64_t i) { return double(i) / 192000.0; });
    Clip* clip = rig.song.findClip(rig.clip);
    clip->audio->warps = {{0, 0}, {double(kPpq), 12000}}; // the first beat's audio is half a beat long: half speed
    rig.rebuild();
    rig.engine->play();
    std::vector<float> out = rig.run(48000);
    double volts = rig.song.findSource(rig.source)->volts;
    // Half way into the first beat (12000 samples at 120 BPM) it has played 6000 frames of the file.
    CHECK(out[12000] / volts == doctest::Approx(6000.0 / 192000).epsilon(0.02));
    // Past the last marker it plays at the clip tempo again: half a beat on, 12000 + 12000 frames.
    CHECK(out[36000] / volts == doctest::Approx(24000.0 / 192000).epsilon(0.02));
}

TEST_CASE("a timeline copy can play another take of its clip: its file, from the same place") {
    Rig rig(4.0, dc);
    // Take 2: another file, half as loud.
    AudioSource second = rig.song.audioSources[0];
    second.id = rig.song.newId<AudioSourceId>();
    second.name = "take 2.wav";
    rig.song.audioSources.push_back(second);
    rig.buffers.sources[second.id.value] = bufferOf(second.frames, [](int64_t) { return 0.5; });
    rig.song.findClip(rig.clip)->audio->takes = {{second.id, 0}};
    rig.engine->play();
    rig.run(4800);
    CHECK(rig.run(100).back() == doctest::Approx(kVolts).epsilon(1e-3)); // take 1, the clip's own
    for (ArrangerClip& a : rig.audioTrack().arrangement)
        a.take = 0;
    rig.rebuild();
    CHECK(rig.run(4800).back() == doctest::Approx(kVolts / 2).epsilon(1e-3));
    CHECK(unusedSources(rig.song).empty()); // a take's file is in use
    json::Ref j(songToJson(rig.song));
    Song back = songFromJson(j.get(), Song::kSchemaVersion);
    CHECK(back.findClip(rig.clip)->audio->takes.size() == 1);
    CHECK(back.findTrack(rig.track)->arrangement.front().take == 0);
}

TEST_CASE("a silent audio track wakes up on time for a clip that starts inside a block") {
    Rig rig(1.0, [](int64_t) { return 0.5f; });
    // 1000 ticks is 25000 samples at 120 BPM and 48 kHz: 8 samples into a 32-sample block.
    rig.song.findTrack(rig.track)->arrangement[0].position = 1000;
    rig.rebuild();
    rig.mode = PlayMode::Arranger;
    rig.engine->setPlayMode(PlayMode::Arranger);
    rig.engine->reset();
    rig.engine->play();
    int64_t first = -1;
    for (int64_t n = 0; n < 40000 && first < 0; n++)
        if (std::abs(rig.step()) > 1e-6f)
            first = n;
    CHECK(first >= 24998);
    CHECK(first <= 25004);
}

TEST_CASE("a silent audio track keeps its fader moving while it rests") {
    Rig rig(1.0, [](int64_t) { return 0.5f; });
    rig.song.findTrack(rig.track)->arrangement[0].position = 100000;
    rig.rebuild();
    rig.mode = PlayMode::Arranger;
    rig.engine->setPlayMode(PlayMode::Arranger);
    rig.engine->reset();
    rig.engine->play();
    for (int n = 0; n < 4096; n++)
        CHECK(rig.step() == 0.f);
    rig.player->hold(rig.track.value, 0.25f, 0, AudioPlayer::kHoldVolume);
    for (int n = 0; n < 4096; n++)
        CHECK(rig.step() == 0.f);
    CHECK(rig.player->level(0, 0) == 0.f);
}
