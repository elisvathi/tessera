#include "doctest.h"

#include "../src/model/DawImport.hpp"
#include "../src/model/DawProject.hpp"
#include "../src/model/Zip.hpp"

#include <cstdlib>
#include <fstream>
#include <iterator>

using namespace tessera;

namespace {

/// A project in the shape Bitwig Studio writes: a group holding a note track, an audio track with a warped loop, an
/// effect track the note track sends to, the master; clips on the timeline and in two scenes; lanes.
const char* const kProject = R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Project version="1.0">
  <Application name="Bitwig Studio" version="5.1.6"/>
  <Transport>
    <Tempo max="666.0" min="20.0" unit="bpm" value="150.0" id="id0" name="Tempo"/>
    <TimeSignature denominator="4" numerator="3" id="id1"/>
  </Transport>
  <Structure>
    <Track contentType="tracks" loaded="true" id="g1" name="Band" color="#102030">
      <Channel audioChannels="2" role="master" id="gc1">
        <Volume max="2.0" min="0.0" unit="linear" value="0.5" id="gv1" name="Volume"/>
      </Channel>
      <Track contentType="notes" loaded="true" id="id2" name="Keys" color="#a2eabf">
        <Channel audioChannels="2" destination="gc1" role="regular" solo="true" id="id3">
          <Devices>
            <ClapPlugin deviceID="x" deviceName="Diva" deviceRole="instrument" loaded="true" id="id7" name="Diva"/>
          </Devices>
          <Mute value="false" id="id6" name="Mute"/>
          <Pan max="1.0" min="0.0" unit="normalized" value="0.25" id="id5" name="Pan"/>
          <Sends>
            <Send destination="id15" type="pre" id="id24">
              <Volume max="1.0" min="0.0" unit="linear" value="0.5" id="id25" name="Send"/>
            </Send>
          </Sends>
          <Volume max="2.0" min="0.0" unit="linear" value="1.0" id="id4" name="Volume"/>
        </Channel>
      </Track>
    </Track>
    <Track contentType="audio" loaded="true" id="id9" name="Guitar">
      <Channel audioChannels="2" destination="id20" role="regular" id="id10">
        <Mute value="true" id="id13" name="Mute"/>
        <Sends>
          <Send destination="id15" type="post" id="id26">
            <Volume max="1.0" min="0.0" unit="linear" value="0.0" id="id27" name="Send"/>
          </Send>
        </Sends>
        <Volume max="2.0" min="0.0" unit="linear" value="0.316228" id="id11" name="Volume"/>
      </Channel>
    </Track>
    <Track contentType="audio" loaded="true" id="id14" name="FX 1">
      <Channel audioChannels="2" destination="id20" role="effect" id="id15">
        <Volume max="2.0" min="0.0" unit="linear" value="1.0" id="id16" name="Volume"/>
      </Channel>
    </Track>
    <Track contentType="audio notes" loaded="true" id="id19" name="Master">
      <Channel audioChannels="2" role="master" id="id20">
        <Volume max="2.0" min="0.0" unit="linear" value="2.0" id="id21" name="Volume"/>
      </Channel>
    </Track>
  </Structure>
  <Arrangement id="id30">
    <Markers><Marker time="12.0" name="Chorus" color="#ff0000"/><Marker time="0.0" name="Intro"/></Markers>
    <Lanes timeUnit="beats" id="id31">
      <Lanes track="id2" id="id32">
        <Clips id="id33">
          <Clip time="4.0" duration="8.0" playStart="1.0" loopStart="0.0" loopEnd="4.0" name="Riff" color="#00ff00">
            <Lanes>
              <Notes id="id34">
                <Note time="0.0" duration="1.03125" channel="1" key="60" vel="0.5" rel="0.25"/>
                <Note time="1.0" duration="1.0" channel="1" key="64" vel="0.5" rel="0.25">
                  <Lanes>
                    <Points unit="semitones"><Target expression="transpose"/>
                      <RealPoint time="0.0" value="0.0" interpolation="linear"/>
                      <RealPoint time="1.0" value="2.0" interpolation="linear"/></Points>
                    <Points unit="linear"><Target expression="pressure"/><RealPoint time="0.0" value="0.75"/></Points>
                    <Points unit="linear"><Target expression="formant"/><RealPoint time="0.0" value="0.5"/></Points>
                  </Lanes>
                </Note>
                <Note time="3.0" duration="0.5" channel="0" key="67" vel="1.0" rel="0.5">
                  <Points unit="linear"><Target expression="timbre"/><RealPoint time="0.5" value="0.4"/></Points>
                </Note>
              </Notes>
              <Points unit="normalized"><Target expression="channelController" channel="0" controller="74"/>
                <RealPoint time="0.0" value="0.25" interpolation="linear"/><RealPoint time="2.0" value="1.0"/></Points>
            </Lanes>
          </Clip>
        </Clips>
        <Points unit="linear"><Target parameter="id4"/>
          <RealPoint time="0.0" value="1.0" interpolation="linear"/><RealPoint time="8.0" value="0.5"/></Points>
        <Points unit="normalized"><Target expression="pitchBend" channel="0"/><RealPoint time="2.0" value="0.75"/></Points>
        <Points unit="normalized"><Target parameter="id7"/><RealPoint time="0.0" value="0.3"/></Points>
      </Lanes>
      <Lanes track="id9" id="id35">
        <Clips id="id36">
          <Clip time="0.0" duration="48.0" playStart="0.0" loopStart="0.0" loopEnd="48.0" fadeTimeUnit="beats"
                fadeInTime="0.5" fadeOutTime="1.0" name="Loop">
            <Clips id="id37">
              <Clip time="0.0" duration="48.0" contentTimeUnit="beats" playStart="0.0">
                <Warps contentTimeUnit="seconds" timeUnit="beats" id="id39">
                  <Audio algorithm="stretch" channels="1" sampleRate="44100" duration="19.2" id="id38">
                    <File path="audio/guitar.wav"/>
                  </Audio>
                  <Warp time="0.0" contentTime="0.0"/>
                  <Warp time="48.0" contentTime="19.2"/>
                </Warps>
              </Clip>
            </Clips>
          </Clip>
        </Clips>
      </Lanes>
      <Lanes track="id19" id="id42">
        <Points unit="linear"><Target parameter="id21"/><RealPoint time="0.0" value="1.0"/></Points>
      </Lanes>
    </Lanes>
    <TempoAutomation target="id0" timeUnit="beats">
      <RealPoint time="0.0" value="150.0"/><RealPoint time="16.0" value="160.0"/>
    </TempoAutomation>
  </Arrangement>
  <Scenes>
    <Scene id="id45" name="Verse">
      <Lanes id="id46">
        <ClipSlot hasStop="true" track="id2" id="id47">
          <Clip time="0.0" duration="4.0" playStart="0.0" loopStart="0.0" loopEnd="4.0" name="Slot clip">
            <Notes><Note time="0.0" duration="1.0" channel="0" key="48" vel="0.8" rel="0.5"/></Notes>
          </Clip>
        </ClipSlot>
        <ClipSlot hasStop="false" track="id9" id="id48"/>
      </Lanes>
    </Scene>
    <Scene id="id51" name="Chorus"><Lanes id="id52"/></Scene>
  </Scenes>
</Project>)";

/// The track named `name` in `song`.
const Track& trackNamed(const Song& song, const std::string& name) {
    for (const Track& t : song.tracks)
        if (t.name == name)
            return t;
    FAIL("no track " << name);
    return song.tracks.front();
}

/// Reads kProject, failing the test when it doesn't read.
dawImport::Imported readTestProject() {
    dawImport::Imported out;
    std::string error;
    REQUIRE_MESSAGE(dawImport::readProject(kProject, "<MetaData><Title>Demo</Title></MetaData>", 5000, out, &error),
                    error);
    return out;
}

} // namespace

TEST_CASE("dawproject import: tracks, groups, the mixer, sends and the master") {
    dawImport::Imported in = readTestProject();
    const Song& song = in.song;
    CHECK(in.title == "Demo");
    CHECK(in.tempo == 150);
    CHECK(song.timeSig == TimeSig{3, 4});
    REQUIRE(song.tracks.size() == 3);
    const Track& keys = trackNamed(song, "Keys");
    const Track& guitar = trackNamed(song, "Guitar");
    const Track& fx = trackNamed(song, "FX 1");
    CHECK(keys.type == TrackType::Instrument);
    CHECK(guitar.type == TrackType::Audio);
    CHECK(fx.type == TrackType::Audio);
    CHECK(keys.color == 0xA2EABF);
    CHECK(keys.solo);
    CHECK(guitar.mute);
    CHECK(keys.channel.pan == doctest::Approx(-0.5));
    CHECK(guitar.channel.volumeDb == doctest::Approx(-10).epsilon(0.01));
    CHECK(song.master.volumeDb == doctest::Approx(6).epsilon(0.01));
    REQUIRE(song.groups.size() == 1);
    CHECK(song.groups[0].name == "Band");
    CHECK(song.groups[0].tracks == std::vector<TrackId>{keys.id});
    CHECK(song.groups[0].channel.volumeDb == doctest::Approx(-6.02).epsilon(0.01));
    // Keys sends to FX 1 before its fader; Guitar's silent send is left out.
    REQUIRE(keys.sends.size() == 1);
    CHECK(keys.sends[0].to == fx.id);
    CHECK(keys.sends[0].preFader);
    CHECK(keys.sends[0].db == doctest::Approx(-6.02).epsilon(0.01));
    CHECK(guitar.sends.empty());
    CHECK(song.tracks.front().id.value >= 5000);
}

TEST_CASE("dawproject import: note clips with their curves, lanes and loop, on the timeline") {
    dawImport::Imported in = readTestProject();
    const Song& song = in.song;
    const Track& keys = trackNamed(song, "Keys");
    REQUIRE(keys.arrangement.size() == 1);
    const ArrangerClip& a = keys.arrangement[0];
    CHECK(a.position == 4 * kPpq);
    CHECK(a.length == 8 * kPpq);
    CHECK(a.offset == kPpq);
    const Clip* clip = song.findClip(a.clip);
    REQUIRE(clip);
    CHECK(clip->name == "Riff");
    CHECK(clip->ownColor == 0x00FF00);
    CHECK(clip->loopMode == LoopMode::Loop);
    CHECK(clip->loopEnd == 4 * kPpq);
    REQUIRE(clip->notes.size() == 3);
    const Note& first = clip->notes[0];
    CHECK(first.channel == 2);
    CHECK(first.velocity == 0.5f);
    CHECK(first.releaseVelocity == 0.25f);
    // It overlapped the next a 1/128 on one line: the export's tie comes back as one.
    CHECK(first.end() == kPpq);
    CHECK(clip->notes[1].tied);
    REQUIRE(clip->notes[1].pitchExpr.size() == 2);
    CHECK(clip->notes[1].pitchExpr[1].value == 2);
    CHECK(clip->notes[1].pressureExpr.front().value == 0.75f);
    REQUIRE(!clip->notes[2].timbreExpr.empty());
    CHECK(clip->notes[2].timbreExpr.back().pos == doctest::Approx(1));
    // A CC lane in the clip on its first AUTO channel.
    REQUIRE(clip->automation.size() == 1);
    CHECK(clip->automation[0].midi == MidiControl{MidiControl::Kind::Cc, 74});
    CHECK(clip->automation[0].points.size() == 2);
    // The track's volume and pitch bend lanes; the plugin knob's lane is left out.
    bool volume = false, bend = false;
    for (const AutomationEnvelope& l : keys.automation) {
        volume |= l.drive == LaneDrive::Volume && l.points.size() == 2 && l.points[0].value == doctest::Approx(0.75f);
        bend |= l.midi.kind == MidiControl::Kind::PitchBend && l.points[0].value == 0.75f;
    }
    CHECK(volume);
    CHECK(bend);
    REQUIRE(song.master.automation.size() == 1);
    CHECK(song.master.automation[0].drive == LaneDrive::Volume);
}

TEST_CASE("dawproject import: a clip that plays another clip's content by reference keeps its notes") {
    const char* project = R"(<?xml version="1.0" encoding="UTF-8"?>
<Project version="1.0">
  <Transport><Tempo unit="bpm" value="120.0" id="id0"/></Transport>
  <Structure>
    <Track contentType="notes" id="t1" name="Keys"><Channel role="regular" id="c1"/></Track>
  </Structure>
  <Arrangement id="a">
    <Lanes timeUnit="beats" id="l0">
      <Lanes track="t1" id="l1">
        <Clips id="cs">
          <Clip time="0.0" duration="4.0" playStart="0.0" name="A">
            <Notes id="n1"><Note time="0.0" duration="1.0" key="60" vel="0.8" channel="0"/></Notes>
          </Clip>
          <Clip time="4.0" duration="4.0" playStart="0.0" name="A" reference="n1"/>
          <Clip time="8.0" duration="4.0" playStart="0.0" name="Loop" reference="cs"/>
        </Clips>
      </Lanes>
    </Lanes>
  </Arrangement>
</Project>)";
    // The last clip names the timeline it sits in: reading it must not go round forever.
    std::string text = project;
    dawImport::Imported in;
    std::string error;
    REQUIRE_MESSAGE(dawImport::readProject(text, "", 1, in, &error), error);
    const Track& keys = trackNamed(in.song, "Keys");
    REQUIRE(keys.arrangement.size() >= 2);
    const Clip* copy = in.song.findClip(keys.arrangement[1].clip);
    REQUIRE(copy);
    CHECK(keys.arrangement[1].position == 4 * kPpq);
    REQUIRE(copy->notes.size() == 1);
    CHECK(copy->notes[0].pitch == 60);
}

TEST_CASE("dawproject import: an audio clip plays its file at the tempo its warps say") {
    dawImport::Imported in = readTestProject();
    const Song& song = in.song;
    const Track& guitar = trackNamed(song, "Guitar");
    REQUIRE(guitar.arrangement.size() == 1);
    const ArrangerClip& a = guitar.arrangement[0];
    CHECK(a.length == 48 * kPpq);
    CHECK(a.fadeIn.length == kPpq / 2);
    CHECK(a.fadeOut.length == kPpq);
    const Clip* clip = song.findClip(a.clip);
    REQUIRE(clip);
    REQUIRE(clip->audio);
    CHECK(clip->audio->sourceTempo == doctest::Approx(150)); // 48 beats in 19.2 s
    CHECK(clip->audio->follow == TempoFollow::Stretch);
    CHECK(clip->audio->warps.empty());
    REQUIRE(in.audio.size() == 1);
    CHECK(in.audio[0].path == "audio/guitar.wav");
    CHECK(in.audio[0].source == clip->audio->source);
    const AudioSource* src = song.findSource(clip->audio->source);
    REQUIRE(src);
    CHECK(src->name == "guitar.wav");
    CHECK(src->frames == 19.2 * 44100);
}

TEST_CASE("dawproject import: scenes, slots, markers, and a report of what didn't come across") {
    dawImport::Imported in = readTestProject();
    const Song& song = in.song;
    REQUIRE(song.scenes.size() == 2);
    CHECK(song.scenes[0].name == "Verse");
    const Track& keys = trackNamed(song, "Keys");
    const Track& guitar = trackNamed(song, "Guitar");
    REQUIRE(keys.slots.size() == 2);
    REQUIRE(keys.slots[0].clip);
    CHECK(song.findClip(*keys.slots[0].clip)->notes.size() == 1);
    CHECK_FALSE(guitar.slots[0].stop);
    REQUIRE(song.markers.size() == 2);
    CHECK(song.markers[0].name == "Intro");
    CHECK(song.markers[1].position == 12 * kPpq);
    std::string report;
    for (const std::string& l : in.leftOut)
        report += l + "\n";
    CHECK(report.find("Keys: Diva") != std::string::npos);
    CHECK(report.find("1 lane moving an instrument or effect knob") != std::string::npos);
    CHECK(report.find("1 note curve of gain, pan or formant") != std::string::npos);
    CHECK(report.find("Tempo changes") != std::string::npos);
}

TEST_CASE("dawproject import: times in seconds count at the project tempo") {
    const char* project = R"(<Project version="1.0"><Transport><Tempo value="120.0" id="t"/></Transport>
      <Structure><Track contentType="notes" id="a" name="A"><Channel id="c" role="regular"/></Track></Structure>
      <Arrangement><Lanes timeUnit="seconds"><Lanes track="a"><Clips>
        <Clip time="2.0" duration="1.0"><Notes><Note time="0.5" duration="0.25" channel="0" key="60" vel="1"/></Notes></Clip>
      </Clips></Lanes></Lanes></Arrangement></Project>)";
    dawImport::Imported in;
    REQUIRE(dawImport::readProject(project, "", 1, in));
    const Track& t = in.song.tracks[0];
    REQUIRE(t.arrangement.size() == 1);
    CHECK(t.arrangement[0].position == 4 * kPpq);
    const Clip* clip = in.song.findClip(t.arrangement[0].clip);
    REQUIRE(clip);
    CHECK(clip->notes[0].start == kPpq);
    CHECK(clip->notes[0].length == kPpq / 2);
}

TEST_CASE("dawproject import: something that isn't a project says so") {
    dawImport::Imported in;
    std::string error;
    CHECK_FALSE(dawImport::readProject("<Song/>", "", 1, in, &error));
    CHECK(error == "not a DAWproject");
    CHECK_FALSE(dawImport::readProject("<Project", "", 1, in, &error));
    CHECK(error.find("damaged") != std::string::npos);
    CHECK_FALSE(dawImport::readProject("<Project version=\"1.0\"/>", "", 1, in, &error));
    CHECK(error.find("no tracks") != std::string::npos);
}

TEST_CASE("dawproject import: a real file from TESSERA_DAWPROJECT_IN, when set, reads and is summed up") {
    const char* path = std::getenv("TESSERA_DAWPROJECT_IN");
    if (!path)
        return;
    std::ifstream in(path, std::ios::binary);
    std::string archive((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::map<std::string, std::string> files = zip::read(archive);
    REQUIRE(files.count("project.xml"));
    dawImport::Imported out;
    std::string error;
    REQUIRE_MESSAGE(dawImport::readProject(files["project.xml"], files["metadata.xml"], 1000, out, &error), error);
    MESSAGE("title " << out.title << ", " << out.tempo << " BPM, " << out.song.tracks.size() << " tracks, "
                     << out.clips << " clips, " << out.notes << " notes, " << out.audio.size() << " audio files, "
                     << out.song.scenes.size() << " scenes, " << out.song.markers.size() << " markers");
    for (const Track& t : out.song.tracks)
        MESSAGE("  " << t.name << std::string(t.type == TrackType::Audio ? " (audio)" : "") << " vol " << t.channel.volumeDb
                     << " dB pan " << t.channel.pan << " sends " << t.sends.size() << " timeline "
                     << t.arrangement.size() << " lanes " << t.automation.size());
    for (const auto& [id, c] : out.song.clips)
        if (c.audio)
            MESSAGE("  audio clip " << c.name << " tempo " << c.audio->sourceTempo << " start " << c.audio->sourceStart
                                    << " warps " << c.audio->warps.size() << " length " << c.length);
    for (const std::string& l : out.leftOut)
        MESSAGE("  left out: " << l);
    for (const dawImport::AudioFile& f : out.audio)
        CHECK_MESSAGE(files.count(f.path), f.path);
}

TEST_CASE("dawproject import: what Tessera exports comes back") {
    Song song;
    song.scenes.resize(2);
    Track lead;
    lead.id = song.newId<TrackId>();
    lead.name = "Lead";
    lead.color = 0x9B7FE2;
    lead.slots.resize(2);
    Clip clip;
    clip.id = song.newId<ClipId>();
    clip.name = "Hook";
    clip.length = clip.loopEnd = 4 * kPpq;
    Note a;
    a.id = song.newId<NoteId>();
    a.length = kPpq;
    a.pitch = 60;
    a.velocity = 0.5f;
    a.timbreExpr = {{0, 0.25f, 0}, {1, 0.75f, 0}};
    Note b = a;
    b.id = song.newId<NoteId>();
    b.start = kPpq;
    b.pitch = 64;
    b.timbreExpr.clear();
    b.tied = true;
    Note c = a;
    c.id = song.newId<NoteId>();
    c.start = 3 * kPpq;
    c.pitch = 67;
    c.timbreExpr.clear();
    c.pitchExpr = {{0, 0, 0}, {1, -2, 0}};
    clip.notes = {a, b, c};
    ArrangerClip at;
    at.id = song.newId<ArrangementId>();
    at.clip = clip.id;
    at.position = 4 * kPpq;
    at.length = 4 * kPpq;
    lead.arrangement.push_back(at);
    lead.slots[1].clip = clip.id;
    song.clips.emplace(clip.id, clip);
    song.tracks.push_back(lead);
    song.markers.push_back({song.newId<MarkerId>(), 4 * kPpq, "Verse", 0});
    song.master.id = song.newId<TrackId>();

    std::string xml = dawproject::projectXml(song, render::Options(), render::Context{98, "Back"}, "2.0.0");
    dawImport::Imported in;
    std::string error;
    REQUIRE_MESSAGE(dawImport::readProject(xml, dawproject::metadataXml(render::Context{98, "Back"}), 100, in, &error),
                    error);
    CHECK(in.tempo == 98);
    CHECK(in.title == "Back");
    REQUIRE(in.song.tracks.size() == 1);
    const Track& t = in.song.tracks[0];
    CHECK(t.name == "Lead");
    CHECK(t.color == 0x9B7FE2);
    REQUIRE(t.arrangement.size() == 1);
    CHECK(t.arrangement[0].position == 4 * kPpq);
    const Clip* back = in.song.findClip(t.arrangement[0].clip);
    REQUIRE(back);
    REQUIRE(back->notes.size() == 3);
    CHECK(back->notes[0].end() == kPpq); // the export's overlap trimmed again
    CHECK(back->notes[1].tied);
    // The curve comes back to within a tick (the export plays a tied note a tick long).
    REQUIRE(back->notes[0].timbreExpr.size() >= 2);
    CHECK(back->notes[0].timbreExpr.front().value == doctest::Approx(0.25f));
    CHECK(back->notes[0].timbreExpr.back().value == doctest::Approx(0.75f).epsilon(0.01));
    REQUIRE(back->notes[2].pitchExpr.size() == 2);
    CHECK(back->notes[2].pitchExpr[1].value == -2);
    CHECK(back->notes[0].velocity == 0.5f);
    REQUIRE(in.song.scenes.size() == 2);
    REQUIRE(t.slots[1].clip);
    CHECK(in.song.findClip(*t.slots[1].clip)->notes.size() == 3);
    REQUIRE(in.song.markers.size() == 1);
    CHECK(in.song.markers[0].name == "Verse");
    CHECK(in.leftOut.empty());
}
