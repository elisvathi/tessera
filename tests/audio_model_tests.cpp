#include "doctest.h"

#include "../src/model/ArrangementEdits.hpp"
#include "../src/model/AudioEdits.hpp"
#include "../src/model/AutomationEdits.hpp"
#include "../src/model/Json.hpp"
#include "../src/model/Serialize.hpp"
#include "RandomSong.hpp"

#include <algorithm>
#include <cmath>

using namespace tessera;

namespace {

/// A counter for new IDs, the way the module hands them out.
NextId idsOf(Song& song) {
    return [&song] { return song.nextId++; };
}

/// A two-bar, 48 kHz stereo loop at 120 BPM, ready to import.
ImportedAudio loop(const std::string& name = "drums_120bpm.wav", double tempo = 120) {
    ImportedAudio f;
    f.source.name = name;
    f.source.file = "audio/" + name;
    f.source.sampleRate = 48000;
    f.source.frames = int64_t(std::llround(8 * 60.0 / tempo * 48000));
    f.source.contentHash = 0x1234;
    f.tempo = tempo;
    return f;
}

/// Applies `command`, checks that revert brings the song back exactly, then applies it again.
void applyAndCheckUndo(Song& song, Command& command) {
    Song before = song;
    command.apply(song);
    Song after = song;
    command.revert(song);
    REQUIRE(song == before);
    command.apply(song);
    REQUIRE(song == after);
}

/// Song -> JSON -> Song.
Song throughJson(const Song& song) {
    json::Ref j(songToJson(song));
    return songFromJson(j.get(), Song::kSchemaVersion);
}

} // namespace

TEST_CASE("an audio clip's length in ticks comes from its frames at the clip tempo") {
    AudioContent a;
    a.sourceTempo = 120;
    // Four beats at 120 BPM are 2 s, 96000 frames at 48 kHz.
    CHECK(ticksForFrames(a, 96000, 48000) == 4 * kPpq);
    a.sourceTempo = 90;
    CHECK(ticksForFrames(a, 96000, 48000) == 3 * kPpq);
    CHECK(framesPerTick(a, 48000) == doctest::Approx(48000 * 60.0 / (90 * kPpq)));
}

TEST_CASE("an Off clip covers more song ticks at a faster song tempo; others follow the song") {
    AudioContent a;
    a.sourceTempo = 100;
    a.follow = TempoFollow::Stretch;
    CHECK(songTicksPerClipTick(a, 150) == 1);
    a.follow = TempoFollow::Off;
    CHECK(songTicksPerClipTick(a, 150) == doctest::Approx(1.5));
}

TEST_CASE("dB and gain convert both ways, with a silent floor") {
    CHECK(dbToGain(0) == doctest::Approx(1));
    CHECK(dbToGain(-6.0206f) == doctest::Approx(0.5).epsilon(1e-4));
    CHECK(dbToGain(kSilentDb) == 0);
    CHECK(gainToDb(1) == doctest::Approx(0));
    CHECK(gainToDb(0) == kSilentDb);
}

TEST_CASE("songs with audio survive a save and load unchanged") {
    int withAudio = 0;
    for (uint32_t seed = 1; seed <= 60; seed++) {
        CAPTURE(seed);
        Song song = testing::RandomSong(seed).make();
        withAudio += hasAudio(song);
        REQUIRE(throughJson(song) == song);
    }
    CHECK(withAudio > 10);
}

TEST_CASE("a source's content hash keeps all 64 bits through JSON") {
    Song song = makeDefaultSong();
    AudioSource s;
    s.id = song.newId<AudioSourceId>();
    s.contentHash = 0xfedcba9876543210ull;
    song.audioSources.push_back(s);
    normalize(song);
    CHECK(throughJson(song).audioSources.at(0).contentHash == 0xfedcba9876543210ull);
}

TEST_CASE("a schema 6 song loads with a master at 0 dB and channels centred at 0 dB") {
    json::Ref j(songToJson(makeDefaultSong()));
    json_object_del(j.get(), "master");
    Song s = songFromJson(j.get(), 6);
    CHECK(s.master.id.valid());
    CHECK(s.master.volumeDb == 0);
    for (const Track& t : s.tracks)
        CHECK(t.channel == Channel{});
    CHECK(s.audioSources.empty());
    CHECK_FALSE(hasAudio(s));
}

TEST_CASE("the master keeps one Volume lane: added like a track's, saved, and trimmed on load") {
    Song song = makeDefaultSong();
    LaneOwner master = LaneOwner::ofTrack(song.master.id);
    REQUIRE(lanesOf(song, master) == &song.master.automation);
    AutomationEnvelope volume = makeLane(song.newId<AutomationId>(), 0, 0);
    volume.drive = LaneDrive::Volume;
    volume.points = {{0, 0.5f}, {kPpq * 4, 0.75f}};
    CommandPtr add = addLane(song, master, volume);
    REQUIRE(add);
    add->apply(song);
    REQUIRE(song.master.automation.size() == 1);
    CHECK(findLane(song, master, volume.id) != nullptr);
    CHECK(throughJson(song) == song);
    add->revert(song);
    CHECK(song.master.automation.empty());

    // Anything but one Volume lane is dropped: the master has no pan and no AUTO outputs.
    AutomationEnvelope pan = volume;
    pan.id = song.newId<AutomationId>();
    pan.drive = LaneDrive::Pan;
    AutomationEnvelope second = volume;
    second.id = song.newId<AutomationId>();
    song.master.automation = {pan, volume, second};
    normalize(song);
    REQUIRE(song.master.automation.size() == 1);
    CHECK(song.master.automation[0].id == volume.id);
}

TEST_CASE("an audio track's clips and the master's mix have a Level and Use, saved and undoable") {
    Song song = makeDefaultSong();
    TrackId track = song.tracks[0].id;
    CommandPtr clips = setSongSource(song, track, {-6, false}, "Clips off");
    REQUIRE(clips);
    clips->apply(song);
    CHECK(song.tracks[0].clips == SourceLevel{-6, false});
    CommandPtr mix = setSongSource(song, song.master.id, {-3, true}, "Mix level");
    REQUIRE(mix);
    mix->apply(song);
    CHECK(song.master.mix.db == -3);
    CHECK(throughJson(song) == song);
    CHECK(setSongSource(song, track, {-6, false}, "same") == nullptr);
    clips->revert(song);
    CHECK(song.tracks[0].clips == SourceLevel{});
    song.tracks[0].clips.db = 99;
    normalize(song);
    CHECK(song.tracks[0].clips.db == kMaxFaderDb);
}

TEST_CASE("normalize keeps clip kinds on matching tracks and clamps audio values") {
    Song song = makeDefaultSong();
    ImportResult made;
    AudioDropTarget last;
    last.index = 4;
    CommandPtr imp = importAudio(song, {loop()}, last, idsOf(song), &made);
    REQUIRE(imp);
    imp->apply(song);
    TrackId audio = made.tracks.at(0);
    ClipId audioClip = made.clips.at(0);

    // An audio clip in a note track's slot, and a note clip on the audio track's timeline, are both invalid.
    song.tracks[0].slots[0].clip = audioClip;
    Clip notes;
    notes.id = song.newId<ClipId>();
    notes.length = kPpq;
    song.clips.emplace(notes.id, notes);
    Track* at = song.findTrack(audio);
    ArrangerClip bad;
    bad.id = song.newId<ArrangementId>();
    bad.clip = notes.id;
    bad.length = kPpq;
    at->arrangement.push_back(bad);
    at->channel = {50, 7};
    song.findClip(audioClip)->audio->gainDb = 99;
    song.findClip(audioClip)->audio->sourceTempo = NAN;
    at->arrangement[0].fadeIn = {at->arrangement[0].length * 2, 3};
    at->arrangement[0].transpose = 5;

    normalize(song);
    at = song.findTrack(audio);
    CHECK_FALSE(song.tracks[0].slots[0].clip.has_value());
    REQUIRE(at->arrangement.size() == 1);
    CHECK(at->arrangement[0].clip == audioClip);
    CHECK(at->arrangement[0].fadeIn.length == at->arrangement[0].length);
    CHECK(at->arrangement[0].fadeIn.curve == 1);
    CHECK(at->arrangement[0].fadeOut.length == 0);
    CHECK(at->arrangement[0].transpose == 0);
    CHECK(at->channel == Channel{kMaxFaderDb, 1});
    CHECK(song.findClip(audioClip)->audio->gainDb == kMaxClipGainDb);
    CHECK(song.findClip(audioClip)->audio->sourceTempo == 120);
}

TEST_CASE("importing onto new tracks makes one audio track per file, on the timeline at the drop point") {
    Song song = makeDefaultSong();
    AudioDropTarget target;
    target.kind = AudioDropTarget::Kind::NewTracks;
    target.index = 1;
    target.at = 4 * kPpq;
    ImportResult made;
    CommandPtr cmd = importAudio(song, {loop("a.wav"), loop("b.wav", 92)}, target, idsOf(song), &made);
    REQUIRE(cmd);
    CHECK(cmd->name() == "Import 2 audio files");
    applyAndCheckUndo(song, *cmd);

    REQUIRE(made.tracks.size() == 2);
    CHECK(song.tracks.size() == 6);
    CHECK(song.tracks[1].id == made.tracks[0]);
    CHECK(song.tracks[2].id == made.tracks[1]);
    CHECK(song.tracks[1].type == TrackType::Audio);
    CHECK(song.tracks[1].name == "a");
    CHECK(song.audioSources.size() == 2);
    const Track& b = song.tracks[2];
    REQUIRE(b.arrangement.size() == 1);
    CHECK(b.arrangement[0].position == 4 * kPpq);
    const Clip* clip = song.findClip(b.arrangement[0].clip);
    REQUIRE(clip);
    REQUIRE(clip->audio);
    // Eight beats of audio at the clip's own tempo are eight beats long, whatever that tempo is.
    CHECK(clip->length == 8 * kPpq);
    CHECK(b.arrangement[0].length == 8 * kPpq);
    CHECK(clip->audio->sourceTempo == 92);
    CHECK(song.sourceOf(*clip)->name == "b.wav");
    CHECK(hasAudio(song));
}

TEST_CASE("importing with Plays = Clips puts each clip in its new track's first slot") {
    Song song = makeDefaultSong();
    AudioDropTarget target;
    target.timeline = false;
    target.index = 4;
    ImportResult made;
    CommandPtr cmd = importAudio(song, {loop()}, target, idsOf(song), &made);
    REQUIRE(cmd);
    applyAndCheckUndo(song, *cmd);
    const Track& t = song.tracks.back();
    CHECK(t.arrangement.empty());
    CHECK(t.slots[0].clip == made.clips[0]);
}

TEST_CASE("several files dropped on an audio track go end to end") {
    Song song = makeDefaultSong();
    ImportResult first;
    importAudio(song, {loop()}, {}, idsOf(song), &first)->apply(song);
    AudioDropTarget target;
    target.kind = AudioDropTarget::Kind::Track;
    target.track = first.tracks[0];
    target.at = 16 * kPpq;
    ImportResult made;
    CommandPtr cmd = importAudio(song, {loop("x.wav"), loop("y.wav")}, target, idsOf(song), &made);
    REQUIRE(cmd);
    applyAndCheckUndo(song, *cmd);
    const Track* t = song.findTrack(first.tracks[0]);
    REQUIRE(t->arrangement.size() == 3);
    CHECK(t->arrangement[1].position == 16 * kPpq);
    CHECK(t->arrangement[2].position == 24 * kPpq);
}

TEST_CASE("files dropped on a slot fill the slots below, and stop at the last scene") {
    Song song = makeDefaultSong();
    ImportResult first;
    importAudio(song, {loop()}, {}, idsOf(song), &first)->apply(song);
    AudioDropTarget target;
    target.kind = AudioDropTarget::Kind::Slot;
    target.track = first.tracks[0];
    target.slot = int(song.scenes.size()) - 2;
    ImportResult made;
    CommandPtr cmd = importAudio(song, {loop("1.wav"), loop("2.wav"), loop("3.wav")}, target, idsOf(song), &made);
    REQUIRE(cmd);
    applyAndCheckUndo(song, *cmd);
    CHECK(made.clips.size() == 2);
    CHECK(made.skipped == 1);
    const Track* t = song.findTrack(first.tracks[0]);
    CHECK(t->slots[size_t(target.slot)].clip == made.clips[0]);
    CHECK(t->slots.back().clip == made.clips[1]);
}

TEST_CASE("audio can't be dropped straight onto a note track's timeline or slots") {
    Song song = makeDefaultSong();
    AudioDropTarget target;
    target.kind = AudioDropTarget::Kind::Track;
    target.track = song.tracks[0].id;
    CHECK_FALSE(importAudio(song, {loop()}, target, idsOf(song)));
    target.kind = AudioDropTarget::Kind::Slot;
    CHECK_FALSE(importAudio(song, {loop()}, target, idsOf(song)));
}

TEST_CASE("a one-shot import plays once; a loop loops") {
    ImportedAudio hit = loop("kick.wav");
    hit.loop = false;
    hit.follow = TempoFollow::Off;
    Clip c = makeAudioClip(hit, ClipId(5));
    CHECK(c.loopMode == LoopMode::Off);
    CHECK(c.playEnd() == c.length);
    CHECK(makeAudioClip(loop(), ClipId(6)).loopMode == LoopMode::Loop);
}

TEST_CASE("audio clip settings are undoable and shared by linked copies") {
    Song song = makeDefaultSong();
    ImportResult made;
    importAudio(song, {loop()}, {}, idsOf(song), &made)->apply(song);
    ClipId clip = made.clips[0];

    CommandPtr gain = editAudio(song, clip, [](AudioContent& a) { a.gainDb = -3; }, "Clip gain");
    REQUIRE(gain);
    applyAndCheckUndo(song, *gain);
    CHECK(song.findClip(clip)->audio->gainDb == -3);
    CHECK_FALSE(editAudio(song, clip, [](AudioContent& a) { a.gainDb = -3; }, "Clip gain"));

    CommandPtr follow = editAudio(song, clip, [](AudioContent& a) { a.follow = TempoFollow::Off; }, "Follow tempo");
    REQUIRE(follow);
    applyAndCheckUndo(song, *follow);

    CommandPtr clamp = editAudio(song, clip, [](AudioContent& a) { a.semitones = 100; }, "Pitch");
    REQUIRE(clamp);
    clamp->apply(song);
    CHECK(song.findClip(clip)->audio->semitones == 24);
}

TEST_CASE("nudging clip gain moves every selected clip, leaves -inf alone and clamps") {
    Song song = makeDefaultSong();
    ImportResult made;
    importAudio(song, {loop("a.wav"), loop("b.wav"), loop("c.wav")}, {}, idsOf(song), &made)->apply(song);
    song.findClip(made.clips[1])->audio->gainDb = kSilentDb;
    song.findClip(made.clips[2])->audio->gainDb = 20;
    CommandPtr cmd = nudgeClipGain(song, made.clips, 6);
    REQUIRE(cmd);
    applyAndCheckUndo(song, *cmd);
    CHECK(song.findClip(made.clips[0])->audio->gainDb == 6);
    CHECK(song.findClip(made.clips[1])->audio->gainDb == kSilentDb);
    CHECK(song.findClip(made.clips[2])->audio->gainDb == kMaxClipGainDb);
}

TEST_CASE("a new clip tempo rescales a Stretch clip and its timeline copies, not an Off clip") {
    Song song = makeDefaultSong();
    ImportResult made;
    importAudio(song, {loop()}, {}, idsOf(song), &made)->apply(song);
    ClipId clip = made.clips[0];
    Track* t = song.findTrack(made.tracks[0]);
    t->arrangement[0].fadeOut = {kPpq, 0};

    CommandPtr half = setClipTempo(song, clip, 60);
    REQUIRE(half);
    applyAndCheckUndo(song, *half);
    t = song.findTrack(made.tracks[0]);
    // The same audio at half the tempo is half as many beats.
    CHECK(song.findClip(clip)->length == 4 * kPpq);
    CHECK(song.findClip(clip)->loopEnd == 4 * kPpq);
    CHECK(t->arrangement[0].length == 4 * kPpq);
    CHECK(t->arrangement[0].fadeOut.length == kPpq / 2);
    CHECK(song.findClip(clip)->audio->sourceTempo == 60);

    editAudio(song, clip, [](AudioContent& a) { a.follow = TempoFollow::Off; }, "Off")->apply(song);
    CommandPtr off = setClipTempo(song, clip, 120);
    REQUIRE(off);
    off->apply(song);
    CHECK(song.findClip(clip)->length == 4 * kPpq);
}

TEST_CASE("fades are clamped to the instance and undoable") {
    Song song = makeDefaultSong();
    ImportResult made;
    importAudio(song, {loop()}, {}, idsOf(song), &made)->apply(song);
    ArrangementId inst = made.instances[0];
    CommandPtr cmd = setFades(song, inst, {kPpq, 0.4f}, {100 * kPpq, -2});
    REQUIRE(cmd);
    CHECK(cmd->name() == "Fade in");
    applyAndCheckUndo(song, *cmd);
    const ArrangerClip& a = song.findTrack(made.tracks[0])->arrangement[0];
    CHECK(a.fadeIn == Fade{kPpq, 0.4f});
    CHECK(a.fadeOut.length == a.length - kPpq);
    CHECK(a.fadeOut.curve == -1);
}

TEST_CASE("mixer edits are undoable; a multi-strip volume drag keeps -inf strips at -inf") {
    Song song = makeDefaultSong();
    CommandPtr pan = setChannel(song, song.tracks[0].id, {-3, 0.5f}, "Pan");
    REQUIRE(pan);
    applyAndCheckUndo(song, *pan);
    CHECK(song.tracks[0].channel == Channel{-3, 0.5f});
    CHECK_FALSE(setChannel(song, song.tracks[0].id, {-3, 0.5f}, "Pan"));

    song.tracks[2].channel.volumeDb = kSilentDb;
    std::vector<SetChannels::Change> start;
    for (int i = 0; i < 3; i++)
        start.push_back({song.tracks[size_t(i)].id, song.tracks[size_t(i)].channel, {}});
    CommandPtr drag = nudgeVolumes(song, start, -2);
    REQUIRE(drag);
    CHECK(drag->name() == "2 tracks volume");
    applyAndCheckUndo(song, *drag);
    CHECK(song.tracks[0].channel.volumeDb == -5);
    CHECK(song.tracks[1].channel.volumeDb == -2);
    CHECK(song.tracks[2].channel.volumeDb == kSilentDb);

    CommandPtr master = setMasterVolume(song, 20);
    REQUIRE(master);
    applyAndCheckUndo(song, *master);
    CHECK(song.master.volumeDb == kMaxFaderDb);
}

TEST_CASE("Remove unused audio takes out only sources no clip plays, as one undo step") {
    Song song = makeDefaultSong();
    ImportResult made;
    importAudio(song, {loop("keep.wav"), loop("drop.wav")}, {}, idsOf(song), &made)->apply(song);
    CHECK_FALSE(removeUnusedAudio(song));
    removeTracks(song, {made.tracks[1]})->apply(song);
    REQUIRE(unusedSources(song).size() == 1);
    CommandPtr cmd = removeUnusedAudio(song);
    REQUIRE(cmd);
    applyAndCheckUndo(song, *cmd);
    REQUIRE(song.audioSources.size() == 1);
    CHECK(song.audioSources[0].name == "keep.wav");
}

TEST_CASE("audio track names come from the file name") {
    CHECK(audioTrackName("drums_92bpm.wav") == "drums_92bpm");
    CHECK(audioTrackName("/home/x/Loop 120 BPM.aif") == "Loop 120 BPM");
    CHECK(audioTrackName(".wav") == ".wav");
    CHECK(audioTrackName("") == "Audio");
}

namespace {

/// A song with one audio track and one 8-beat audio clip, and timeline copies of it at the given spans (in beats).
struct AudioLanes {
    Song song = makeDefaultSong();
    TrackId track;
    ClipId clip;

    AudioLanes() {
        ImportResult made;
        importAudio(song, {loop()}, {}, idsOf(song), &made)->apply(song);
        track = made.tracks[0];
        clip = made.clips[0];
        song.findTrack(track)->arrangement.clear();
    }
    /// A copy of the clip on the track over [start, end) beats.
    arrange::Placed at(double start, double end) {
        ArrangerClip a;
        a.id = ArrangementId(song.nextId++);
        a.clip = clip;
        a.position = Tick(start * kPpq);
        a.length = Tick((end - start) * kPpq);
        return {track, a};
    }
    arrange::IdSource ids() {
        return [this] { return ArrangementId(song.nextId++); };
    }
};

/// The spans [start, end) in ticks of a layout's instances, by position.
std::vector<std::pair<Tick, Tick>> spans(arrange::Layout layout) {
    std::sort(layout.begin(), layout.end(),
              [](const arrange::Placed& x, const arrange::Placed& y) { return x.clip.position < y.clip.position; });
    std::vector<std::pair<Tick, Tick>> out;
    for (const arrange::Placed& p : layout)
        out.push_back({p.clip.position, instanceEnd(p.clip)});
    return out;
}

} // namespace

TEST_CASE("audio overlaps stay as crossfades; a covered audio clip goes") {
    AudioLanes f;
    arrange::Placed a = f.at(0, 8), b = f.at(6, 14);
    arrange::Layout out = arrange::settleAudio({a, b}, {b.clip.id}, f.song.clips, f.ids());
    CHECK(spans(out) == std::vector<std::pair<Tick, Tick>>{{0, 8 * kPpq}, {6 * kPpq, 14 * kPpq}});

    arrange::Placed big = f.at(0, 16);
    out = arrange::settleAudio({a, big}, {big.clip.id}, f.song.clips, f.ids());
    REQUIRE(out.size() == 1);
    CHECK(out[0].clip.id == big.clip.id);
}

TEST_CASE("an audio clip dropped inside another splits it, each part reaching into it for a crossfade") {
    AudioLanes f;
    arrange::Placed outer = f.at(0, 8), inner = f.at(3, 5);
    outer.clip.fadeIn.length = kPpq / 4;
    outer.clip.fadeOut.length = kPpq / 4;
    arrange::Layout out = arrange::settleAudio({outer, inner}, {inner.clip.id}, f.song.clips, f.ids());
    Tick seam = arrange::kSeamCrossfade;
    CHECK(spans(out) ==
          std::vector<std::pair<Tick, Tick>>{{0, 3 * kPpq + seam}, {3 * kPpq, 5 * kPpq}, {5 * kPpq - seam, 8 * kPpq}});
    for (const arrange::Placed& p : out) {
        if (p.clip.position == 0) {
            CHECK(p.clip.id == outer.clip.id); // the left part stays the same instance
            CHECK(p.clip.fadeIn.length == kPpq / 4);
            CHECK(p.clip.fadeOut.length == 0);
        } else if (p.clip.position == 5 * kPpq - seam) {
            CHECK(p.clip.fadeIn.length == 0);
            CHECK(p.clip.fadeOut.length == kPpq / 4);
            // The right part plays on from where it was: its offset moved with its start.
            CHECK(p.clip.offset == 5 * kPpq - seam);
        }
    }
}

TEST_CASE("where three audio clips would sound at once, the earliest stops") {
    AudioLanes f;
    arrange::Placed a = f.at(0, 8), b = f.at(4, 12), c = f.at(6, 14);
    arrange::Layout out = arrange::settleAudio({a, b, c}, {c.clip.id}, f.song.clips, f.ids());
    CHECK(spans(out) ==
          std::vector<std::pair<Tick, Tick>>{{0, 6 * kPpq}, {4 * kPpq, 12 * kPpq}, {6 * kPpq, 14 * kPpq}});

    // When the earliest is the one being moved, the third starts later instead.
    out = arrange::settleAudio({a, b, c}, {a.clip.id}, f.song.clips, f.ids());
    CHECK(spans(out) ==
          std::vector<std::pair<Tick, Tick>>{{0, 8 * kPpq}, {4 * kPpq, 12 * kPpq}, {8 * kPpq, 14 * kPpq}});
}

TEST_CASE("resolveOverlaps leaves audio overlaps to the audio rule and keeps audio clips on one row") {
    AudioLanes f;
    arrange::Placed a = f.at(0, 8), b = f.at(6, 14);
    b.clip.layer = 2;
    arrange::Layout out = arrange::resolveOverlaps({a, b}, {b.clip.id}, f.song.clips, f.ids());
    REQUIRE(out.size() == 2);
    CHECK(spans(out) == std::vector<std::pair<Tick, Tick>>{{0, 8 * kPpq}, {6 * kPpq, 14 * kPpq}});
    for (const arrange::Placed& p : out)
        CHECK(p.clip.layer == 0);
}

TEST_CASE("Normalise lifts a clip's loudest sample in its window to full scale, and only once") {
    Song song = makeDefaultSong();
    ImportResult made;
    importAudio(song, {loop()}, {}, idsOf(song), &made)->apply(song);
    Clip& clip = *song.findClip(made.clips[0]);
    const AudioSource& src = *song.sourceOf(clip);
    // Quiet (0.5) inside the clip's first beat, loud (0.9) after it: the clip plays only its first beat.
    std::vector<std::vector<float>> samples(1, std::vector<float>(size_t(src.frames), 0.f));
    samples[0][1000] = -0.5f;
    samples[0][40000] = 0.9f;
    clip.length = kPpq; // half a second: frames [0, 24000)
    PeakCache peaks(samples);
    float peak = clipPeak(clip, src, peaks);
    CHECK(peak == doctest::Approx(0.5).epsilon(1e-3));
    CommandPtr cmd = normaliseClip(song, clip.id, peak);
    REQUIRE(cmd);
    CHECK(cmd->name() == "Normalise");
    applyAndCheckUndo(song, *cmd);
    CHECK(song.findClip(clip.id)->audio->gainDb == doctest::Approx(6.02).epsilon(1e-2));
    CHECK_FALSE(normaliseClip(song, clip.id, peak));
    CHECK_FALSE(normaliseClip(song, clip.id, 0));
}

TEST_CASE("Replace file keeps the clip's place, length, gain and fades, and plays the new file from its start") {
    Song song = makeDefaultSong();
    ImportResult made;
    importAudio(song, {loop()}, {}, idsOf(song), &made)->apply(song);
    ClipId id = made.clips[0];
    editAudio(
        song, id,
        [](AudioContent& a) {
            a.gainDb = -3;
            a.sourceStart = 500;
        },
        "x")
        ->apply(song);
    setFades(song, made.instances[0], {kPpq / 2, 0}, {kPpq / 4, 0})->apply(song);
    Tick length = song.findClip(id)->length;
    CommandPtr cmd = replaceAudioFile(song, id, loop("other_92bpm.wav", 92), idsOf(song));
    REQUIRE(cmd);
    CHECK(cmd->name() == "Replace file with other_92bpm.wav");
    applyAndCheckUndo(song, *cmd);
    const Clip* c = song.findClip(id);
    CHECK(song.sourceOf(*c)->name == "other_92bpm.wav");
    CHECK(song.audioSources.size() == 2); // the old one stays until Remove unused audio
    CHECK(c->audio->sourceStart == 0);
    CHECK(c->audio->sourceTempo == 92);
    CHECK(c->audio->gainDb == -3);
    CHECK(c->length == length);
    const ArrangerClip& a = song.findTrack(made.tracks[0])->arrangement[0];
    CHECK(a.fadeIn.length == kPpq / 2);
    CHECK(a.fadeOut.length == kPpq / 4);
    CHECK(unusedSources(song).size() == 1);
}

TEST_CASE("the audio window sets what part of the file a clip plays; a whole-window loop follows it") {
    Song song = makeDefaultSong();
    ImportResult made;
    importAudio(song, {loop()}, {}, idsOf(song), &made)->apply(song); // 8 beats, 192000 frames, looping
    ClipId id = made.clips[0];
    REQUIRE(song.findClip(id)->loopEnd == 8 * kPpq);
    // Start one beat (24000 frames) in, end two beats before the file's end.
    CommandPtr cmd = setAudioWindow(song, id, 24000, 144000);
    REQUIRE(cmd);
    applyAndCheckUndo(song, *cmd);
    const Clip* c = song.findClip(id);
    CHECK(c->audio->sourceStart == 24000);
    CHECK(c->length == 5 * kPpq);
    CHECK(c->loopStart == 0);
    CHECK(c->loopEnd == 5 * kPpq);
    CHECK_FALSE(setAudioWindow(song, id, 24000, 144000));

    // A loop over part of the window keeps its place in the file.
    Clip& edit = *song.findClip(id);
    edit.loopStart = kPpq;
    edit.loopEnd = 3 * kPpq;
    setAudioWindow(song, id, 0, 144000)->apply(song);
    CHECK(song.findClip(id)->loopStart == 2 * kPpq);
    CHECK(song.findClip(id)->loopEnd == 4 * kPpq);
}

TEST_CASE("glue joins the split parts of one audio clip back into one, and leaves other audio alone") {
    AudioLanes f;
    arrange::Placed whole = f.at(0, 8);
    whole.clip.fadeIn.length = kPpq / 4;
    whole.clip.fadeOut.length = kPpq / 2;
    f.song.findTrack(f.track)->arrangement = {whole.clip};
    arrange::IdSet right;
    arrange::Layout parts =
        arrange::split(arrange::layoutOf(f.song), {whole.clip.id}, 3 * kPpq, f.song.clips, f.ids(), &right);
    arrange::replaceArrangement(f.song, "split", arrange::layoutOf(f.song), parts)->apply(f.song);
    REQUIRE(f.song.findTrack(f.track)->arrangement.size() == 2);
    arrange::IdSet both = arrange::idsOf(parts);
    CommandPtr cmd = arrange::glue(f.song, both, [&] { return f.song.nextId++; });
    REQUIRE(cmd);
    applyAndCheckUndo(f.song, *cmd);
    const auto& arr = f.song.findTrack(f.track)->arrangement;
    REQUIRE(arr.size() == 1);
    CHECK(arr[0].clip == f.clip);
    CHECK(arr[0].position == 0);
    CHECK(arr[0].length == 8 * kPpq);
    CHECK(arr[0].fadeIn.length == kPpq / 4);
    CHECK(arr[0].fadeOut.length == kPpq / 2);

    // Two copies that don't play on from each other stay apart.
    arrange::Placed a = f.at(0, 4), b = f.at(4, 8);
    f.song.findTrack(f.track)->arrangement = {a.clip, b.clip};
    CHECK_FALSE(arrange::glue(f.song, {a.clip.id, b.clip.id}, [&] { return f.song.nextId++; }));
}

TEST_CASE("a drum track's lane IN levels and pans are undoable, clamped and saved") {
    Song song = makeDefaultSong();
    TrackId drums;
    for (const Track& t : song.tracks)
        if (t.type == TrackType::Drum)
            drums = t.id;
    REQUIRE(drums.valid());
    CommandPtr cmd = setDrumInput(song, drums, 2, {-6, 0.5f}, "Lane level");
    REQUIRE(cmd);
    applyAndCheckUndo(song, *cmd);
    CHECK(song.findTrack(drums)->drumInputs[2] == Channel{-6, 0.5f});
    CHECK_FALSE(setDrumInput(song, drums, 2, {-6, 0.5f}, "same"));
    CHECK_FALSE(setDrumInput(song, drums, 8, {}, "no such lane"));
    setDrumInput(song, drums, 0, {40, 3}, "too far")->apply(song);
    CHECK(song.findTrack(drums)->drumInputs[0] == Channel{kMaxFaderDb, 1});
    Song loaded = throughJson(song);
    CHECK(loaded.findTrack(drums)->drumInputs == song.findTrack(drums)->drumInputs);
}

TEST_CASE("an audio clip's hits fall on the timeline where its transients play, each loop pass too") {
    Clip clip;
    clip.id = ClipId(1);
    clip.audio = AudioContent{};
    clip.audio->sourceTempo = 120; // at 48 kHz a tick is 25 frames
    clip.length = 4 * kPpq;
    clip.loopMode = LoopMode::Off;
    ArrangerClip at;
    at.clip = clip.id;
    at.position = 10 * kPpq;
    at.length = 4 * kPpq;
    std::vector<int64_t> hits{0, 24000, 48000}; // beats 0, 1, 2
    std::vector<Tick> ticks = hitTicks(clip, at, hits, 48000, 120);
    REQUIRE(ticks.size() == 3);
    CHECK(ticks[1] == 11 * kPpq);
    // Looping one beat for four beats: the hit at its start comes four times.
    clip.loopMode = LoopMode::Loop;
    clip.loopStart = 0;
    clip.loopEnd = kPpq;
    CHECK(hitTicks(clip, at, {0}, 48000, 120).size() == 4);
    // Starting half a beat in: the first pass's hit is cut off.
    at.offset = kPpq / 2;
    CHECK(hitTicks(clip, at, {0}, 48000, 120).front() == at.position + kPpq / 2);
}
