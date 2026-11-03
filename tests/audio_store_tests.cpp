#include "doctest.h"

#include "../src/audio/AudioStore.hpp"
#include "../src/audio/TimeStretcher.hpp"
#include "../src/model/AudioEdits.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>
#include <thread>

using namespace tessera;
namespace fs = std::filesystem;

namespace {

/// The path of a file in tests/audio.
std::string testFile(const std::string& name) {
    std::string here = __FILE__;
    return here.substr(0, here.find_last_of('/') + 1) + "audio/" + name;
}

/// A fresh folder standing in for a module's patch storage, removed at the end of the test.
struct TempFolder {
    fs::path path;
    TempFolder() {
        std::random_device rd;
        path = fs::temp_directory_path() / ("tessera-store-" + std::to_string(rd()));
        fs::create_directories(path);
    }
    ~TempFolder() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

/// Imports `name` into `store` and puts it on a new audio track of `song`, as a drop does.
AudioSourceId importInto(AudioStore& store, Song& song, const std::string& name) {
    AudioImport imp = store.importFile(testFile(name), 120, {4, 4});
    REQUIRE(imp.ok);
    ImportedAudio f;
    f.source = imp.source;
    f.tempo = imp.guess.bpm > 0 ? imp.guess.bpm : 120;
    importAudio(song, {f}, {}, [&] { return song.nextId++; })->apply(song);
    return song.audioSources.back().id;
}

/// Waits (at most a few seconds) until `done` holds.
template <typename F>
bool eventually(F done) {
    for (int i = 0; i < 500; i++) {
        if (done())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

} // namespace

TEST_CASE("a dropped file is copied into patch storage under its hash, and looked at") {
    TempFolder dir;
    AudioStore store;
    store.setFolder(dir.path.string());
    AudioImport imp = store.importFile(testFile("click120.wav"), 100, {4, 4});
    REQUIRE(imp.ok);
    CHECK(imp.source.file.rfind("audio/", 0) == 0);
    CHECK(imp.source.file.find("-click120.wav") != std::string::npos);
    CHECK(fs::exists(dir.path / imp.source.file));
    CHECK(imp.source.contentHash == hashFile(testFile("click120.wav")));
    CHECK(imp.source.sampleRate == 22050);
    CHECK(imp.source.frames == 4 * 22050);
    CHECK(imp.guess.bpm == doctest::Approx(120));
    CHECK(imp.source.tempo == doctest::Approx(120));
    CHECK(storageName("a/b.wav", 0x1234abcd5678ull) == "audio/abcd5678-a_b.wav");

    AudioImport bad = store.importFile(testFile("notaudio.wav"), 120, {4, 4});
    CHECK_FALSE(bad.ok);
    CHECK(bad.error == DecodeError::NotAudio);
    CHECK(store.importFile(testFile("nothing-here.wav"), 120, {4, 4}).error == DecodeError::CantOpen);
}

TEST_CASE("an imported source becomes ready at the engine rate, with its waveform") {
    TempFolder dir;
    AudioStore store;
    store.setFolder(dir.path.string());
    store.setEngineRate(48000);
    Song song = makeDefaultSong();
    AudioSourceId id = importInto(store, song, "click120.wav");
    uint64_t before = store.changes();
    store.sync(song);
    store.waitIdle();
    CHECK(store.changes() > before);
    SourceStatus st = store.status(id);
    CHECK(st.state == SourceState::Ready);
    REQUIRE(st.peaks);
    CHECK(st.peaks->frames() == 4 * 22050);
    CHECK(st.sampleRate == 22050);
    auto buf = store.prepared(id);
    REQUIRE(buf);
    CHECK(buf->rate == 48000);
    CHECK(buf->frames == 4 * 48000);
    CHECK(store.memoryBytes() >= size_t(4 * 48000 * sizeof(float)));
    CHECK(store.idle());
}

TEST_CASE("a reversed copy is made on first asking and dropped when no reversed clip plays it") {
    TempFolder dir;
    AudioStore store;
    store.setFolder(dir.path.string());
    store.setEngineRate(48000);
    Song song = makeDefaultSong();
    AudioSourceId id = importInto(store, song, "click120.wav");
    store.sync(song);
    store.waitIdle();
    auto forwards = store.prepared(id);
    REQUIRE(forwards);
    CHECK_FALSE(store.prepared(id, true)); // asked for: made on the loader thread
    store.waitIdle();
    auto back = store.prepared(id, true);
    REQUIRE(back);
    REQUIRE(back->frames == forwards->frames);
    CHECK(back->channel(0)[0] == forwards->channel(0)[size_t(forwards->frames - 1)]);
    CHECK(back->channel(0)[size_t(back->frames - 1)] == forwards->channel(0)[0]);
    size_t both = store.memoryBytes();

    // No clip plays it reversed: the next sync lets the copy go.
    store.sync(song);
    CHECK(store.memoryBytes() < both);
}

TEST_CASE("a source whose file isn't in patch storage is missing; the store forgets sources the song drops") {
    TempFolder dir;
    AudioStore store;
    store.setFolder(dir.path.string());
    Song song = makeDefaultSong();
    AudioSource s;
    s.id = song.newId<AudioSourceId>();
    s.name = "gone.wav";
    s.file = "audio/00000000-gone.wav";
    s.sampleRate = 48000;
    s.frames = 100;
    song.audioSources.push_back(s);
    store.sync(song);
    store.waitIdle();
    CHECK(store.status(s.id).state == SourceState::Missing);
    CHECK_FALSE(store.prepared(s.id));

    song.audioSources.clear();
    store.sync(song);
    CHECK(store.status(s.id).state == SourceState::Missing);
}

TEST_CASE("renders are made when wanted, and the nearest ready one stands in") {
    TempFolder dir;
    AudioStore store;
    store.setFolder(dir.path.string());
    store.setEngineRate(48000);
    Song song = makeDefaultSong();
    AudioSourceId id = importInto(store, song, "click120.wav");
    store.sync(song);
    store.waitIdle();

    RenderKey key{id, 1.25, 0, SoundType::Drums};
    RenderKey found;
    CHECK_FALSE(store.render(key, found));
    store.want({key});
    store.waitIdle();
    CHECK(store.renderStatus(key).state == RenderState::Ready);
    auto r = store.render(key, found);
    REQUIRE(r);
    CHECK(found == key);
    CHECK(r->frames == stretchedFrames(4 * 48000, 1.25));

    // Another tempo: until its render is ready, the 1.25 one is the nearest.
    RenderKey other{id, 1.3, 0, SoundType::Drums};
    auto near = store.render(other, found);
    REQUIRE(near);
    CHECK(found.factor == 1.25);
    // A different pitch doesn't stand in.
    CHECK_FALSE(store.render(RenderKey{id, 1.25, 2, SoundType::Drums}, found));
}

TEST_CASE("renders no longer wanted before they start are dropped") {
    TempFolder dir;
    AudioStore store;
    store.setFolder(dir.path.string());
    Song song = makeDefaultSong();
    AudioSourceId id = importInto(store, song, "click128.flac");
    store.sync(song);
    std::vector<RenderKey> many;
    for (int i = 0; i < 20; i++)
        many.push_back(RenderKey{id, 1 + 0.01 * i, 0, SoundType::FullMix});
    store.want(many);
    store.want({many.back()});
    store.waitIdle();
    CHECK(store.renderStatus(many.back()).state == RenderState::Ready);
    int made = 0;
    for (const RenderKey& k : many)
        made += store.renderStatus(k).state == RenderState::Ready;
    CHECK(made < 20);
}

TEST_CASE("a new sample rate prepares every source again; the old buffer plays on meanwhile") {
    TempFolder dir;
    AudioStore store;
    store.setFolder(dir.path.string());
    store.setEngineRate(48000);
    Song song = makeDefaultSong();
    AudioSourceId id = importInto(store, song, "sine1k.aiff");
    store.sync(song);
    store.waitIdle();
    REQUIRE(store.prepared(id));
    store.setEngineRate(96000);
    CHECK(store.prepared(id)); // still the 48 kHz one, until the new one is ready
    store.waitIdle();
    auto buf = store.prepared(id);
    REQUIRE(buf);
    CHECK(buf->rate == 96000);
    CHECK(buf->frames == 48000); // half a second at 96 kHz
    CHECK(store.status(id).state == SourceState::Ready);
}

TEST_CASE("the store feeds the snapshot builder: a clip plays its prepared buffer at the engine rate") {
    TempFolder dir;
    AudioStore store;
    store.setFolder(dir.path.string());
    store.setEngineRate(44100);
    Song song = makeDefaultSong();
    importInto(store, song, "click120.wav");
    store.sync(song);
    store.waitIdle();
    AudioBuild b = buildAudioSnapshot(song, store, 44100, 120);
    REQUIRE(b.snapshot->clips.size() == 1);
    const AudioClipPlay& c = b.snapshot->clips[0];
    REQUIRE(c.prepared);
    CHECK(c.prepared->rate == 44100);
    // One tick at 120 BPM is 1/1920 s: 44100/1920 frames at the engine rate.
    CHECK(c.framesPerTick == doctest::Approx(44100.0 / 1920));
    CHECK(b.wanted.empty());
}

TEST_CASE("a pasted Tessera copies its audio files from the original's patch storage") {
    TempFolder original, pasted;
    AudioStore store;
    store.setFolder(original.path.string());
    Song song = makeDefaultSong();
    importInto(store, song, "kick.wav");
    importInto(store, song, "click120.wav");
    CHECK(copyMissingFiles(song, original.path.string(), pasted.path.string()) == 2);
    for (const AudioSource& s : song.audioSources)
        CHECK(fs::exists(pasted.path / s.file));
    CHECK(copyMissingFiles(song, original.path.string(), pasted.path.string()) == 0);
    // A file in the original folder with other content (the original changed since) is not taken.
    TempFolder other;
    fs::create_directories(other.path / "audio");
    { std::ofstream(other.path / song.audioSources[0].file) << "not the same"; }
    TempFolder third;
    CHECK(copyMissingFiles(song, other.path.string(), third.path.string()) == 0);
}

TEST_CASE("loading a patch removes the files no source uses, and only those") {
    TempFolder dir;
    AudioStore store;
    store.setFolder(dir.path.string());
    Song song = makeDefaultSong();
    importInto(store, song, "kick.wav");
    importInto(store, song, "click120.wav");
    std::string dropped = song.audioSources[1].file;
    song.audioSources.pop_back();
    CHECK(removeUnusedFiles(song, dir.path.string()) == 1);
    CHECK_FALSE(fs::exists(dir.path / dropped));
    CHECK(fs::exists(dir.path / song.audioSources[0].file));
    CHECK(removeUnusedFiles(song, (dir.path / "nowhere").string()) == 0);
}

TEST_CASE("a project file's audio folder takes the files out and brings them back") {
    TempFolder storage, project, other;
    AudioStore store;
    store.setFolder(storage.path.string());
    Song song = makeDefaultSong();
    importInto(store, song, "kick.wav");
    CHECK(exportFiles(song, storage.path.string(), project.path.string()) == 1);
    CHECK(exportFiles(song, storage.path.string(), project.path.string()) == 0);
    CHECK(importFiles(song, project.path.string(), other.path.string()) == 1);
    CHECK(fs::exists(other.path / song.audioSources[0].file));
    CHECK(importFiles(song, project.path.string(), other.path.string()) == 0);
}
