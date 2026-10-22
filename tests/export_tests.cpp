#include "doctest.h"

#include "../src/model/AutomationEdits.hpp"
#include "../src/model/Chord.hpp"
#include "../src/model/DawProject.hpp"
#include "../src/model/DeviceTypes.hpp"
#include "../src/model/Devices.hpp"
#include "../src/model/Json.hpp"
#include "../src/model/MidiFile.hpp"
#include "../src/model/MidiImport.hpp"
#include "../src/model/NoteOperators.hpp"
#include "../src/model/Render.hpp"
#include "../src/model/Zip.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>

using namespace tessera;
using namespace tessera::render;

namespace {

constexpr Tick kBeat = kPpq;
constexpr Tick kBar = 4 * kPpq;

Note makeNote(Song& song, Tick start, Tick length, int pitch, float velocity = 0.8f) {
    Note n;
    n.id = song.newId<NoteId>();
    n.start = start;
    n.length = length;
    n.pitch = int8_t(pitch);
    n.velocity = velocity;
    return n;
}

TrackId addTrack(Song& song, TrackType type = TrackType::Instrument, const std::string& name = "Lead") {
    Track t;
    t.id = song.newId<TrackId>();
    t.name = name;
    t.type = type;
    song.tracks.push_back(t);
    return t.id;
}

Clip& addClip(Song& song, Tick length, std::vector<Note> notes) {
    Clip c;
    c.id = song.newId<ClipId>();
    c.length = c.loopEnd = length;
    c.notes = std::move(notes);
    std::sort(c.notes.begin(), c.notes.end(), noteOrder);
    song.clips[c.id] = c;
    return song.clips[c.id];
}

ArrangerClip& place(Song& song, TrackId track, ClipId clip, Tick position, Tick length) {
    ArrangerClip a;
    a.id = song.newId<ArrangementId>();
    a.clip = clip;
    a.position = position;
    a.length = length;
    Track* t = song.findTrack(track);
    t->arrangement.push_back(a);
    return t->arrangement.back();
}

/// Writes, reads back, and returns the file as read.
smf::File roundTrip(const smf::File& f) {
    smf::File back;
    REQUIRE(smf::read(smf::write(f), back));
    return back;
}

struct NoteSeen {
    Tick on, off;
    int channel, pitch, velocity, release;
};

/// Note-on/off pairs of one track as read from a file.
std::vector<NoteSeen> notesOf(const smf::Track& t) {
    std::vector<NoteSeen> out;
    for (size_t i = 0; i < t.events.size(); i++) {
        const smf::Event& e = t.events[i];
        if ((e.status() & 0xF0) != 0x90 || e.bytes[2] == 0)
            continue;
        int ch = e.status() & 15, pitch = e.bytes[1];
        for (size_t k = i + 1; k < t.events.size(); k++) {
            const smf::Event& f = t.events[k];
            if ((f.status() & 0xF0) == 0x80 && (f.status() & 15) == ch && f.bytes[1] == pitch) {
                out.push_back({e.tick, f.tick, ch, pitch, e.bytes[2], f.bytes[2]});
                break;
            }
        }
    }
    return out;
}

std::vector<const smf::Event*> metas(const smf::Track& t, uint8_t type) {
    std::vector<const smf::Event*> out;
    for (const smf::Event& e : t.events)
        if (e.isMeta() && e.bytes[1] == type)
            out.push_back(&e);
    return out;
}

std::string text(const smf::Event& e) { return std::string(e.bytes.begin() + 2, e.bytes.end()); }

std::vector<std::pair<Tick, int>> controllers(const smf::Track& t, int cc) {
    std::vector<std::pair<Tick, int>> out;
    for (const smf::Event& e : t.events)
        if ((e.status() & 0xF0) == 0xB0 && e.bytes[1] == cc)
            out.push_back({e.tick, e.bytes[2]});
    return out;
}

} // namespace

TEST_CASE("smf: written files read back the same") {
    smf::File f;
    smf::Track t;
    t.events.push_back(smf::trackName(0, "Bass"));
    t.events.push_back(smf::noteOn(0, 2, 36, 100));
    t.events.push_back(smf::noteOff(200000, 2, 36, 64)); // a delta that needs three VLQ bytes
    t.events.push_back(smf::controlChange(10, 2, 1, 127));
    t.events.push_back(smf::pitchBend(20, 2, -8192));
    f.tracks.push_back(t);
    smf::File back = roundTrip(f);
    CHECK(back.format == 1);
    CHECK(back.ppq == 960);
    REQUIRE(back.tracks.size() == 1);
    const auto& ev = back.tracks[0].events;
    REQUIRE(ev.size() == 5);
    CHECK(text(ev[0]) == "Bass");
    CHECK(ev[1].tick == 0);
    CHECK(ev[2].tick == 10);
    CHECK(ev[3].tick == 20);
    CHECK(ev[3].bytes == std::vector<uint8_t>{0xE2, 0, 0});
    CHECK(ev[4].tick == 200000);
    CHECK(ev[4].bytes == std::vector<uint8_t>{0x82, 36, 64});
}

TEST_CASE("smf: a note ending where the next starts is written off first") {
    smf::File f;
    smf::Track t;
    t.events.push_back(smf::noteOn(480, 0, 60, 90));
    t.events.push_back(smf::noteOff(480, 0, 60, 0));
    f.tracks.push_back(t);
    smf::File back = roundTrip(f);
    CHECK((back.tracks[0].events[0].status() & 0xF0) == 0x80);
}

TEST_CASE("export: the arrangement as tracks with tempo, names and channels") {
    Song song;
    TrackId lead = addTrack(song);
    TrackId drums = addTrack(song, TrackType::Drum, "Drums");
    Note n = makeNote(song, kBeat, kBeat, 60, 1.f);
    n.releaseVelocity = 0.25f;
    Clip& c = addClip(song, kBar, {n});
    place(song, lead, c.id, kBar, kBar);
    Clip& d = addClip(song, kBar, {makeNote(song, 0, kBeat / 4, 36)});
    place(song, drums, d.id, 0, kBar);
    Marker m;
    m.id = song.newId<MarkerId>();
    m.position = kBar;
    m.name = "Verse";
    song.markers.push_back(m);

    Context ctx{90, "My song"};
    smf::File f = roundTrip(renderSong(song, Options(), ctx));
    REQUIRE(f.tracks.size() == 3);
    CHECK(text(*metas(f.tracks[0], smf::kTrackName)[0]) == "My song");
    const smf::Event& tempo = *metas(f.tracks[0], smf::kTempo)[0];
    CHECK((tempo.bytes[2] << 16 | tempo.bytes[3] << 8 | tempo.bytes[4]) == 666667);
    CHECK(text(*metas(f.tracks[0], smf::kMarker)[0]) == "Verse");
    CHECK(metas(f.tracks[0], smf::kMarker)[0]->tick == kBar);
    CHECK(text(*metas(f.tracks[1], smf::kTrackName)[0]) == "Lead");

    auto lead1 = notesOf(f.tracks[1]);
    REQUIRE(lead1.size() == 1);
    CHECK(lead1[0].on == kBar + kBeat);
    CHECK(lead1[0].off == kBar + 2 * kBeat);
    CHECK(lead1[0].channel == 0);
    CHECK(lead1[0].velocity == 127);
    CHECK(lead1[0].release == 32);
    auto drum = notesOf(f.tracks[2]);
    REQUIRE(drum.size() == 1);
    CHECK(drum[0].channel == 9); // MIDI channel 10
}

TEST_CASE("export: instance transpose, velocity scale and loop repeats are what plays") {
    Song song;
    TrackId lead = addTrack(song);
    Clip& c = addClip(song, kBeat, {makeNote(song, 0, kBeat / 2, 60, 0.8f)});
    c.loopMode = LoopMode::Loop;
    ArrangerClip& a = place(song, lead, c.id, 0, 4 * kBeat);
    a.transpose = 12;
    a.velocityScale = 0.5f;
    auto notes = notesOf(roundTrip(renderSong(song, Options(), Context())).tracks[1]);
    REQUIRE(notes.size() == 4);
    for (size_t i = 0; i < 4; i++) {
        CHECK(notes[i].on == Tick(i) * kBeat);
        CHECK(notes[i].pitch == 72);
        CHECK(notes[i].velocity == smf::velocity7(0.4f));
    }
}

TEST_CASE("export: range, muted tracks and per-track channels") {
    Song song;
    TrackId a = addTrack(song, TrackType::Instrument, "A");
    TrackId b = addTrack(song, TrackType::Instrument, "B");
    Clip& c = addClip(song, 4 * kBar, {makeNote(song, 0, kBeat, 60), makeNote(song, 2 * kBar, kBeat, 62)});
    place(song, a, c.id, 0, 4 * kBar);
    place(song, b, c.id, 0, 4 * kBar);
    song.findTrack(b)->mute = true;
    song.loopStart = 2 * kBar;
    song.loopEnd = 3 * kBar;

    Options o;
    o.range = RangeMode::Loop;
    smf::File f = roundTrip(renderSong(song, o, Context()));
    REQUIRE(f.tracks.size() == 2); // B is muted
    auto notes = notesOf(f.tracks[1]);
    REQUIRE(notes.size() == 1);
    CHECK(notes[0].pitch == 62);
    CHECK(notes[0].on == 0); // the range starts the file
    CHECK(metas(f.tracks[0], smf::kCuePoint).size() == 2);

    o.includeMuted = true;
    o.channels[b.value] = 5;
    smf::File g = roundTrip(renderSong(song, o, Context()));
    REQUIRE(g.tracks.size() == 3);
    CHECK(notesOf(g.tracks[2])[0].channel == 4);
    CHECK(channelOf(song, Options(), b) == 2);

    // Only chosen tracks.
    CHECK(renderSong(song, Options(), Context(), {a}).tracks.size() == 2);
}

TEST_CASE("export: notes keep their own channels when asked") {
    Song song;
    TrackId a = addTrack(song, TrackType::Instrument, "A");
    Note bass = makeNote(song, 0, kBeat, 36);
    bass.channel = 2;
    Clip& c = addClip(song, kBar, {bass, makeNote(song, kBeat, kBeat, 72)});
    place(song, a, c.id, 0, kBar);

    Options o;
    auto notes = notesOf(roundTrip(renderSong(song, o, Context())).tracks[1]);
    REQUIRE(notes.size() == 2);
    CHECK(notes[0].channel == 0); // the track's channel, 1
    CHECK(notes[1].channel == 0);

    o.noteChannels = true;
    notes = notesOf(roundTrip(renderSong(song, o, Context())).tracks[1]);
    CHECK(notes[0].channel == 1); // channel 2
    CHECK(notes[1].channel == 0);
    CHECK(trackNotes(song, a, o, Context())[0].channel == 2);
    CHECK(Options::fromJson(json::Ref(o.toJson()).get()) == o);
}

TEST_CASE("export: instrument channels skip 10, drums take it") {
    Song song;
    std::vector<TrackId> ids;
    for (int i = 0; i < 11; i++)
        ids.push_back(addTrack(song));
    CHECK(channelOf(song, Options(), ids[8]) == 9);
    CHECK(channelOf(song, Options(), ids[9]) == 11);
    TrackId d = addTrack(song, TrackType::Drum);
    CHECK(channelOf(song, Options(), d) == 10);
}

TEST_CASE("export: as played applies micro-timing and swing") {
    Song song;
    TrackId lead = addTrack(song);
    Note n = makeNote(song, kBeat / 4, kBeat / 8, 60);
    n.microOffset = 0.25f;
    Clip& c = addClip(song, kBar, {n});
    place(song, lead, c.id, 0, kBar);
    song.swing = 1;
    Options o;
    CHECK(notesOf(roundTrip(renderSong(song, o, Context())).tracks[1])[0].on == kBeat / 4);
    o.timing = Timing::Played;
    // The off-beat 1/16 moves half a 1/16 later, then the micro offset adds a quarter of one.
    CHECK(notesOf(roundTrip(renderSong(song, o, Context())).tracks[1])[0].on == kBeat / 4 + kBeat / 8 + kBeat / 16);
}

TEST_CASE("export: rendered operators follow the engine's passes and seed") {
    Song song;
    song.seed = 42;
    TrackId lead = addTrack(song);
    Note n = makeNote(song, 0, kBeat / 2, 60);
    n.ops.chance = 0.5f;
    Note r = makeNote(song, kBeat / 2, kBeat / 2, 64);
    r.ops.repeats = 4;
    Note rec = makeNote(song, kBeat / 4, kBeat / 8, 67);
    rec.ops.recurrenceLength = 2;
    rec.ops.recurrenceMask = 0b01;
    Clip& c = addClip(song, kBeat, {n, r, rec});
    c.loopMode = LoopMode::Loop;
    place(song, lead, c.id, 0, 16 * kBeat);

    Options o;
    CHECK(notesOf(roundTrip(renderSong(song, o, Context())).tracks[1]).size() == 48);
    o.operators = OperatorMode::Rendered;
    auto notes = notesOf(roundTrip(renderSong(song, o, Context())).tracks[1]);
    int chance = 0, repeats = 0, recurring = 0, expected = 0;
    for (const NoteSeen& s : notes) {
        chance += s.pitch == 60;
        repeats += s.pitch == 64;
        recurring += s.pitch == 67;
    }
    const Note& stored = song.clips[c.id].notes[0];
    for (int pass = 0; pass < 16; pass++) {
        operators::PassContext ctx;
        ctx.seed = 42;
        ctx.clip = c.id.value;
        ctx.pass = pass;
        expected += operators::plays(stored, ctx);
    }
    CHECK(chance == expected);
    CHECK(chance > 0);
    CHECK(chance < 16);
    CHECK(repeats == 64);  // four hits a pass
    CHECK(recurring == 8); // every second pass
    // The same seed gives the same file.
    CHECK(smf::write(renderSong(song, o, Context())) == smf::write(renderSong(song, o, Context())));
}

TEST_CASE("export: automation lanes as controllers, thinned, and 14-bit") {
    Song song;
    TrackId lead = addTrack(song);
    Clip& c = addClip(song, kBar, {});
    place(song, lead, c.id, 0, kBar);
    AutomationEnvelope lane = makeLane(song.newId<AutomationId>(), 1, 0);
    lane.points = {{0, 0, CurveType::Linear, 0}, {kBar, 1, CurveType::Linear, 0}};
    song.findTrack(lead)->automation.push_back(lane);

    Options o;
    CHECK(controllers(roundTrip(renderSong(song, o, Context())).tracks[1], 2).empty());
    o.automation = AutomationMode::Cc;
    auto cc = controllers(roundTrip(renderSong(song, o, Context())).tracks[1], 2); // AUTO 2 -> CC 2
    REQUIRE(cc.size() > 10);
    CHECK(cc.front().first == 0);
    CHECK(cc.front().second == 0);
    for (size_t i = 1; i < cc.size(); i++) {
        CHECK(cc[i].second > cc[i - 1].second); // rising, and never the same value twice
        CHECK(cc[i].first > cc[i - 1].first);
    }
    CHECK(cc.back().second >= 120); // the last sample is a 1/64 before the lane's end

    o.automation = AutomationMode::Cc14;
    smf::File f = roundTrip(renderSong(song, o, Context()));
    auto msb = controllers(f.tracks[1], 2), lsb = controllers(f.tracks[1], 34);
    REQUIRE(msb.size() == lsb.size());
    CHECK(msb.size() == cc.size());
    CHECK(msb[1].second * 128 + lsb[1].second == 16383 / 64 + 1);
}

TEST_CASE("export: clip lanes follow their instance and modulate the track lane") {
    Song song;
    TrackId lead = addTrack(song);
    Clip& c = addClip(song, kBar, {});
    AutomationEnvelope cl = makeLane(song.newId<AutomationId>(), 0, 0);
    cl.points = {{0, 1, CurveType::Hold, 0}};
    c.automation.push_back(cl);
    place(song, lead, c.id, kBar, kBar);
    Options o;
    o.automation = AutomationMode::Cc;
    auto cc = controllers(roundTrip(renderSong(song, o, Context())).tracks[1], 1);
    REQUIRE(cc.size() == 1);
    CHECK(cc[0].first == kBar);
    CHECK(cc[0].second == 127);
}

TEST_CASE("export: MPE spreads notes over member channels with their curves") {
    Song song;
    TrackId lead = addTrack(song);
    Note a = makeNote(song, 0, kBeat, 60);
    a.pitchExpr = {{0, 0, 0}, {1, 12, 0}};
    Note b = makeNote(song, 0, kBeat, 64);
    b.timbreExpr = {{0, 1, 0}};
    Clip& c = addClip(song, kBar, {a, b});
    place(song, lead, c.id, 0, kBar);
    Options o;
    o.expressions = ExpressionMode::Mpe;
    smf::File f = roundTrip(renderSong(song, o, Context()));
    auto notes = notesOf(f.tracks[1]);
    REQUIRE(notes.size() == 2);
    CHECK(notes[0].channel != notes[1].channel);
    CHECK(notes[0].channel >= 1);
    // The zone setup: RPN 6 on the master channel says 15 members.
    bool zone = false;
    for (const smf::Event& e : f.tracks[1].events)
        zone |= e.bytes == std::vector<uint8_t>{0xB0, 6, 15};
    CHECK(zone);
    // C4's bend rises to +12 semitones of a ±48 range.
    int ch60 = notes[0].pitch == 60 ? notes[0].channel : notes[1].channel;
    int lastBend = 0;
    for (const smf::Event& e : f.tracks[1].events)
        if (e.status() == (0xE0 | ch60))
            lastBend = (e.bytes[1] | e.bytes[2] << 7) - 8192;
    CHECK(lastBend == doctest::Approx(2048).epsilon(0.1)); // the last sample is just before the note ends
}

TEST_CASE("export: flattened expressions use the track channel") {
    Song song;
    TrackId lead = addTrack(song);
    Note a = makeNote(song, 0, kBeat, 60);
    a.timbreExpr = {{0, 0.5f, 0}};
    a.pressureExpr = {{0, 1, 0}};
    Clip& c = addClip(song, kBar, {a});
    place(song, lead, c.id, 0, kBar);
    Options o;
    o.expressions = ExpressionMode::Flatten;
    smf::File f = roundTrip(renderSong(song, o, Context()));
    CHECK(controllers(f.tracks[1], 74) == std::vector<std::pair<Tick, int>>{{0, 64}});
    bool poly = false;
    for (const smf::Event& e : f.tracks[1].events)
        poly |= e.bytes == std::vector<uint8_t>{0xA0, 60, 127};
    CHECK(poly);
}

TEST_CASE("export: a clip on its own") {
    Song song;
    TrackId lead = addTrack(song);
    Clip& c = addClip(song, kBar, {makeNote(song, kBeat, kBeat, 60)});
    place(song, lead, c.id, 8 * kBar, kBar);
    smf::File f = roundTrip(renderClip(song, Options(), Context(), lead, c.id));
    REQUIRE(f.tracks.size() == 2);
    auto notes = notesOf(f.tracks[1]);
    REQUIRE(notes.size() == 1);
    CHECK(notes[0].on == kBeat);
}

TEST_CASE("export: options round-trip through JSON") {
    Options o;
    o.timing = Timing::Played;
    o.operators = OperatorMode::Rendered;
    o.automation = AutomationMode::Cc14;
    o.expressions = ExpressionMode::Mpe;
    o.range = RangeMode::Loop;
    o.split = Split::PerTrack;
    o.includeMuted = true;
    o.laneCc[3] = 74;
    o.channels[7] = 3;
    json_t* j = o.toJson();
    Options back = Options::fromJson(j);
    json_decref(j);
    CHECK(back == o);
}

TEST_CASE("export: a big song renders quickly") {
    Song song;
    std::vector<Note> notes;
    for (int i = 0; i < 16; i++)
        notes.push_back(makeNote(song, Tick(i) * kBeat / 4, kBeat / 8, 48 + i));
    Clip& c = addClip(song, kBar, notes);
    for (int t = 0; t < 64; t++) {
        TrackId id = addTrack(song);
        for (int bar = 0; bar < 200; bar += 4)
            place(song, id, c.id, Tick(bar) * kBar, 4 * kBar);
    }
    auto start = std::chrono::steady_clock::now();
    smf::File f = renderSong(song, Options(), Context());
    std::vector<uint8_t> bytes = smf::write(f);
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    MESSAGE("64 tracks x 200 bars: " << ms << " ms, " << bytes.size() << " bytes");
    CHECK(f.tracks.size() == 65);
#if defined(NDEBUG) && !defined(__SANITIZE_ADDRESS__)
    CHECK(ms < 200); // DESIGN.md §17
#else
    CHECK(ms < 30000); // unoptimized, with sanitizers: only a guard against something quadratic
#endif
}

namespace {

Device& addDeviceOn(Song& song, TrackId track, DeviceType type, std::initializer_list<std::pair<int, float>> values) {
    Device d = devices::makeDevice(type, song.newId<DeviceId>());
    for (const auto& [k, v] : values)
        d.params[size_t(k)] = devices::normalize(devices::deviceInfo(type).params[size_t(k)], v);
    Track* t = song.findTrack(track);
    t->devices.push_back(d);
    return t->devices.back();
}

int shapeNamed(const char* name) {
    const std::vector<ChordShapeInfo>& shapes = chordShapes();
    for (size_t i = 0; i < shapes.size(); i++)
        if (std::string(shapes[i].name) == name)
            return int(i);
    return 0;
}

} // namespace

TEST_CASE("export: device chains are applied, or bypassed on request") {
    Song song;
    TrackId lead = addTrack(song);
    Clip& c = addClip(song, kBar, {makeNote(song, 0, kBeat, 60)});
    place(song, lead, c.id, 0, kBar);
    addDeviceOn(song, lead, DeviceType::Chord,
                {{chain::knob::kChord, float(shapeNamed("Major"))}, {chain::knob::kInKey, 0}});
    Options o;
    smf::File f = roundTrip(renderSong(song, o, Context()));
    std::vector<NoteSeen> notes = notesOf(f.tracks[1]);
    REQUIRE(notes.size() == 3);
    std::vector<int> pitches;
    for (const NoteSeen& n : notes) {
        pitches.push_back(n.pitch);
        CHECK(n.on == 0);
        CHECK(n.off == kBeat);
    }
    std::sort(pitches.begin(), pitches.end());
    CHECK(pitches == std::vector<int>{60, 64, 67});

    o.devices = DeviceMode::Bypassed;
    CHECK(notesOf(roundTrip(renderSong(song, o, Context())).tracks[1]).size() == 1);

    // Remembered with the other choices.
    json::Ref j(o.toJson());
    CHECK(Options::fromJson(j.get()).devices == DeviceMode::Bypassed);
}

TEST_CASE("export: knob lanes turn the devices as they do in playback") {
    Song song;
    TrackId lead = addTrack(song);
    Clip& c = addClip(song, kBar,
                      {makeNote(song, 0, kBeat / 2, 60), makeNote(song, kBeat, kBeat / 2, 60),
                       makeNote(song, 2 * kBeat, kBeat / 2, 60), makeNote(song, 3 * kBeat, kBeat / 2, 60)});
    place(song, lead, c.id, 0, kBar);
    Device& d = addDeviceOn(song, lead, DeviceType::Transpose, {});
    const devices::ParamInfo& info = devices::deviceInfo(DeviceType::Transpose).params[chain::knob::kSemitones];
    ParamRef ref{ParamRef::Kind::Device, d.id.value, chain::knob::kSemitones};
    auto lane = [&](float semitones, Tick at) {
        AutomationEnvelope l =
            makeKnobLane(song.newId<AutomationId>(), ref, "Semitones", devices::normalize(info, 0), true, 0);
        AutoPoint p;
        p.t = at;
        p.value = devices::normalize(info, semitones);
        p.curve = CurveType::Hold;
        l.points.push_back(p);
        return l;
    };
    auto pitches = [&] {
        std::vector<int> out;
        for (const NoteSeen& n : notesOf(roundTrip(renderSong(song, Options(), Context())).tracks[1]))
            out.push_back(n.pitch);
        return out;
    };
    song.findTrack(lead)->automation.push_back(lane(5, 2 * kBeat)); // +5 from beat 3 on
    CHECK(pitches() == std::vector<int>{60, 60, 65, 65});

    // A clip lane overrides the track's from beat 4.
    song.findClip(c.id)->automation.push_back(lane(-12, 3 * kBeat));
    CHECK(pitches() == std::vector<int>{60, 60, 60, 48});

    // Knob lanes are not CC lanes.
    Options cc;
    cc.automation = AutomationMode::Cc;
    smf::File f = roundTrip(renderSong(song, cc, Context()));
    bool anyCc = false;
    for (const smf::Event& e : f.tracks[1].events)
        anyCc |= (e.status() & 0xF0) == 0xB0;
    CHECK_FALSE(anyCc);
}

TEST_CASE("export: an arpeggiator's steps become notes, and echoes stop at the end of the range") {
    Song song;
    TrackId lead = addTrack(song);
    Clip& c =
        addClip(song, kBar, {makeNote(song, 0, kBar, 60), makeNote(song, 0, kBar, 64), makeNote(song, 0, kBar, 67)});
    place(song, lead, c.id, 0, kBar);
    addDeviceOn(song, lead, DeviceType::Arpeggiator, {{chain::knob::kRate, 6}, {chain::knob::kGate, 50}}); // 1/8
    std::vector<NoteSeen> notes = notesOf(roundTrip(renderSong(song, Options(), Context())).tracks[1]);
    REQUIRE(notes.size() == 8);
    std::vector<int> pitches;
    for (size_t i = 0; i < notes.size(); i++) {
        CHECK(notes[i].on == Tick(i) * kBeat / 2);
        CHECK(notes[i].off - notes[i].on == kBeat / 4);
        pitches.push_back(notes[i].pitch);
    }
    CHECK(pitches == std::vector<int>{60, 64, 67, 60, 64, 67, 60, 64});

    Song echo;
    TrackId t = addTrack(echo);
    Clip& one = addClip(echo, kBar, {makeNote(echo, 3 * kBeat, kBeat / 4, 60)});
    place(echo, t, one.id, 0, kBar);
    addDeviceOn(echo, t, DeviceType::Echo,
                {{chain::knob::kTime, 5}, {chain::knob::kRepeats, 8}, {chain::knob::kDecay, 10}});
    // The song ends at the bar: repeats after it aren't written.
    std::vector<NoteSeen> echoes = notesOf(roundTrip(renderSong(echo, Options(), Context())).tracks[1]);
    REQUIRE(echoes.size() == 1);
    CHECK(echoes[0].on == 3 * kBeat);
}

// ---- DAWproject -----------------------------------------------------------------------------------------------

namespace {

size_t count(const std::string& text, const std::string& what) {
    size_t n = 0;
    for (size_t at = text.find(what); at != std::string::npos; at = text.find(what, at + what.size()))
        n++;
    return n;
}

/// A song with what DAWproject carries: two tracks (one muted), an arranger clip, a launcher clip, a marker.
Song dawSong() {
    Song song;
    TrackId lead = addTrack(song, TrackType::Instrument, "Lead & Bass");
    TrackId drums = addTrack(song, TrackType::Drum, "Drums");
    song.findTrack(lead)->color = 0xFF8A3D;
    song.findTrack(drums)->mute = true;
    Clip& a = addClip(song, kBar, {makeNote(song, 0, kBeat, 60), makeNote(song, 2 * kBeat, kBeat, 64)});
    a.name = "Verse";
    place(song, lead, a.id, 0, 2 * kBar);
    Clip& b = addClip(song, kBar, {makeNote(song, 0, kBeat / 4, 36)});
    b.loopMode = LoopMode::Loop;
    song.scenes.resize(2);
    song.findTrack(lead)->slots.resize(2);
    song.findTrack(lead)->slots[1].clip = b.id;
    Marker m;
    m.id = song.newId<MarkerId>();
    m.position = kBar;
    m.name = "Chorus";
    song.markers.push_back(m);
    return song;
}

} // namespace

TEST_CASE("zip: stored archives read back, with the standard CRC") {
    CHECK(zip::crc32("123456789") == 0xCBF43926u);
    std::string archive = zip::store({{"project.xml", "<Project/>"}, {"metadata.xml", ""}});
    CHECK(archive.compare(0, 4, "PK\x03\x04") == 0);
    std::map<std::string, std::string> files = zip::read(archive);
    REQUIRE(files.size() == 2);
    CHECK(files["project.xml"] == "<Project/>");
    CHECK(files["metadata.xml"].empty());
    archive[30 + 11 + 2] ^= 1; // inside the first file's data, after its header and name
    CHECK(zip::read(archive).empty());
}

TEST_CASE("dawproject: tracks, clips, scenes and markers") {
    Song song = dawSong();
    Context ctx{128, "My song"};
    std::string xml = dawproject::projectXml(song, Options(), ctx, "2.0.0");
    if (const char* out = std::getenv("TESSERA_DAWPROJECT_OUT")) {
        Options all;
        all.includeMuted = true;
        all.automation = AutomationMode::Cc;
        std::ofstream(out) << dawproject::projectXml(song, all, ctx, "2.0.0");
    }

    CHECK(xml.find("<Application name=\"Tessera\" version=\"2.0.0\"/>") != std::string::npos);
    CHECK(xml.find("unit=\"bpm\" value=\"128.0\"") != std::string::npos);
    CHECK(xml.find("numerator=\"4\" denominator=\"4\"") != std::string::npos);
    // The muted drum track stays out, as in MIDI export; names are escaped.
    CHECK(count(xml, "<Track ") == 1);
    CHECK(xml.find("name=\"Lead &amp; Bass\" color=\"#ff8a3d\"") != std::string::npos);
    // The arranger clip: two bars, its notes in beats.
    CHECK(xml.find("<Clip time=\"0.0\" duration=\"8.0\" playStart=\"0.0\" name=\"Verse\"") != std::string::npos);
    CHECK(xml.find("<Note time=\"2.0\" duration=\"1.0\" channel=\"0\" key=\"64\" vel=\"0.8\" rel=\"0.5\"/>") !=
          std::string::npos);
    // Two bars of a looping one-bar clip: its notes twice; one more in the launcher clip.
    CHECK(count(xml, "<Note ") == 5);
    // The launcher clip in scene 2, looping.
    CHECK(count(xml, "<Scene ") == 2);
    CHECK(count(xml, "<ClipSlot ") == 1);
    CHECK(xml.find("loopStart=\"0.0\" loopEnd=\"4.0\"") != std::string::npos);
    CHECK(xml.find("<Marker time=\"4.0\" name=\"Chorus\"/>") != std::string::npos);

    SUBCASE("muted tracks on request, with their mute") {
        Options o;
        o.includeMuted = true;
        std::string all = dawproject::projectXml(song, o, ctx, "2.0.0");
        CHECK(count(all, "<Track ") == 2);
        CHECK(all.find("name=\"Mute\" value=\"true\"") != std::string::npos);
    }
    SUBCASE("the zip holds both files") {
        std::map<std::string, std::string> files = zip::read(dawproject::build(song, Options(), ctx, "2.0.0"));
        CHECK(files["project.xml"] == xml);
        CHECK(files["metadata.xml"].find("<Title>My song</Title>") != std::string::npos);
    }
}

TEST_CASE("dawproject: notes follow the export choices, and AUTO lanes become controller curves") {
    Song song;
    TrackId lead = addTrack(song);
    Clip& c = addClip(song, kBar, {makeNote(song, 0, kBeat, 60)});
    place(song, lead, c.id, 0, kBar);
    addDeviceOn(song, lead, DeviceType::Chord,
                {{chain::knob::kChord, float(shapeNamed("Major"))}, {chain::knob::kInKey, 0}});
    AutomationEnvelope lane = makeLane(song.newId<AutomationId>(), 2, 0);
    AutoPoint p0, p1;
    p0.t = 0;
    p0.value = 0;
    p1.t = kBar;
    p1.value = 1;
    lane.points = {p0, p1};
    song.findTrack(lead)->automation.push_back(lane);

    Options o;
    std::string applied = dawproject::projectXml(song, o, Context(), "2.0.0");
    CHECK(count(applied, "<Note ") == 3);
    CHECK(count(applied, "<Points ") == 0); // automation is off by default
    o.devices = DeviceMode::Bypassed;
    o.automation = AutomationMode::Cc;
    std::string bypassed = dawproject::projectXml(song, o, Context(), "2.0.0");
    if (const char* out = std::getenv("TESSERA_DAWPROJECT_AUTO_OUT"))
        std::ofstream(out) << bypassed;
    CHECK(count(bypassed, "<Note ") == 1);
    CHECK(bypassed.find("<Target expression=\"channelController\" channel=\"0\" controller=\"11\"/>") !=
          std::string::npos);
    // A straight ramp thins to its two ends.
    CHECK(count(bypassed, "<RealPoint ") == 2);
}

TEST_CASE("export and import: MPE curves come back on their notes, as one part") {
    Song song;
    TrackId lead = addTrack(song);
    Note a = makeNote(song, 0, kBeat, 60);
    a.pitchExpr = {{0, 0, 0}, {1, 12, 0}};
    Note b = makeNote(song, 0, kBeat, 64);
    b.pressureExpr = {{0, 0, 0}, {1, 1, 0}};
    Clip& c = addClip(song, kBar, {a, b});
    place(song, lead, c.id, 0, kBar);
    Options o;
    o.expressions = ExpressionMode::Mpe;
    midiImport::Imported in;
    REQUIRE(midiImport::read(smf::write(renderSong(song, o, Context())), in));
    REQUIRE(in.parts.size() == 1);
    const auto& notes = in.parts[0].notes;
    REQUIRE(notes.size() == 2);
    const Note& up = notes[0].pitch == 60 ? notes[0] : notes[1];
    const Note& pressed = notes[0].pitch == 64 ? notes[0] : notes[1];
    REQUIRE(!up.pitchExpr.empty());
    CHECK(up.pitchExpr.back().value == doctest::Approx(12).epsilon(0.1)); // the last sample is just before the end
    CHECK(up.pressureExpr.empty());
    REQUIRE(!pressed.pressureExpr.empty());
    CHECK(pressed.pressureExpr.back().value == doctest::Approx(1).epsilon(0.05));
    CHECK(pressed.pitchExpr.empty());
}

TEST_CASE("export and import: poly aftertouch comes back as each note's pressure") {
    Song song;
    TrackId lead = addTrack(song);
    Note a = makeNote(song, 0, kBeat, 60);
    a.pressureExpr = {{0, 0, 0}, {1, 1, 0}};
    Clip& c = addClip(song, kBar, {a, makeNote(song, 0, kBeat, 67)});
    place(song, lead, c.id, 0, kBar);
    Options o;
    o.expressions = ExpressionMode::Flatten;
    midiImport::Imported in;
    REQUIRE(midiImport::read(smf::write(renderSong(song, o, Context())), in));
    REQUIRE(in.parts.size() == 1);
    const auto& notes = in.parts[0].notes;
    REQUIRE(notes.size() == 2);
    const Note& pressed = notes[0].pitch == 60 ? notes[0] : notes[1];
    const Note& other = notes[0].pitch == 60 ? notes[1] : notes[0];
    REQUIRE(pressed.pressureExpr.size() >= 2);
    CHECK(pressed.pressureExpr.back().value == doctest::Approx(1).epsilon(0.05));
    CHECK(other.pressureExpr.empty());
}

TEST_CASE("import: channel controls become clip lanes") {
    smf::File file;
    file.ppq = 480;
    smf::Track t;
    t.events.push_back(smf::noteOn(0, 0, 60, 100));
    t.events.push_back(smf::controlChange(0, 0, 1, 0));
    t.events.push_back(smf::controlChange(240, 0, 1, 127));
    t.events.push_back(smf::channelPressure(240, 0, 64));
    t.events.push_back(smf::noteOff(480, 0, 60, 64));
    file.tracks.push_back(t);
    midiImport::Imported in;
    REQUIRE(midiImport::read(smf::write(file), in));
    REQUIRE(in.parts.size() == 1);
    REQUIRE(in.parts[0].lanes.size() == 2);
    Clip clip = midiImport::clipOf(in, 0, TimeSig(), [n = uint64_t(1)]() mutable { return n++; }, "x");
    REQUIRE(clip.automation.size() == 2);
    CHECK(clip.automation[0].name == "Mod wheel (CC 1)");
    CHECK(clip.automation[0].outputIndex == 0);
    CHECK(clip.automation[1].drive == LaneDrive::Pressure);
}
