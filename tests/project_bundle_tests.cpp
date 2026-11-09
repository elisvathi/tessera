#include "doctest.h"

#include "../src/audio/AudioDecode.hpp"
#include "../src/audio/AudioStore.hpp"
#include "../src/audio/ProjectBundle.hpp"
#include "../src/model/AudioEdits.hpp"
#include "../src/model/Serialize.hpp"
#include "../src/model/Zip.hpp"

#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>

using namespace tessera;
namespace fs = std::filesystem;

namespace {

/// A fresh folder, removed at the end of the test.
struct Temp {
    fs::path path;
    Temp() {
        std::random_device rd;
        path = fs::temp_directory_path() / ("tessera-bundle-" + std::to_string(rd()));
        fs::create_directories(path);
    }
    ~Temp() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

void writeFile(const fs::path& path, const std::string& bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << bytes;
}

std::string readFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream s;
    s << in.rdbuf();
    return s.str();
}

/// A source named `name` whose file holds `bytes`, written into `folder`.
AudioSource sourceIn(const fs::path& folder, uint64_t id, const std::string& name, const std::string& bytes) {
    AudioSource s;
    s.id = AudioSourceId(id);
    s.name = name;
    s.file = "audio/" + std::to_string(id) + "-" + name;
    writeFile(folder / s.file, bytes);
    s.contentHash = hashFile((folder / s.file).string());
    return s;
}

} // namespace

TEST_CASE("a project file holds its audio and gives it back") {
    Temp dir, storage, other;
    AudioSource a = sourceIn(storage.path, 1, "vox.wav", std::string(600000, 'a'));
    AudioSource b = sourceIn(storage.path, 2, "drums.flac", "tiny");
    std::string project = (dir.path / "Song.tessera").string();
    std::atomic<float> progress{0};
    REQUIRE(bundle::write(project, "{\"format\":1}",
                          {{a.file, (storage.path / a.file).string()}, {b.file, (storage.path / b.file).string()}},
                          &progress));
    CHECK(progress.load() == doctest::Approx(1));
    CHECK(bundle::isBundle(project));

    std::string json;
    REQUIRE(bundle::readProject(project, json));
    CHECK(json == "{\"format\":1}");

    std::vector<bundle::Wanted> wanted{{a.file, a.contentHash}, {b.file, b.contentHash}};
    CHECK(bundle::extract(project, wanted, other.path.string()) == 2);
    CHECK(readFile(other.path / a.file) == std::string(600000, 'a'));
    CHECK(readFile(other.path / b.file) == "tiny");
    CHECK(bundle::extract(project, wanted, other.path.string()) == 0);
    CHECK_FALSE(fs::exists(project + ".part"));

    SUBCASE("the project keeps working after a move and a rename") {
        fs::path moved = dir.path / "elsewhere" / "Renamed.tessera";
        fs::create_directories(moved.parent_path());
        fs::rename(project, moved);
        Temp third;
        CHECK(bundle::extract(moved.string(), wanted, third.path.string()) == 2);
    }
    SUBCASE("a file that doesn't match its hash stays out") {
        Temp third;
        CHECK(bundle::extract(project, {{a.file, a.contentHash + 1}}, third.path.string()) == 0);
        CHECK_FALSE(fs::exists(third.path / a.file));
        CHECK_FALSE(fs::exists(third.path / (a.file + ".part")));
    }
    SUBCASE("a name that climbs out of the storage folder is refused") {
        std::string evil = (dir.path / "Evil.tessera").string();
        fs::path outside = dir.path / "outside.wav";
        REQUIRE(bundle::write(evil, "{}", {{"../outside.wav", (storage.path / a.file).string()}}));
        fs::path inside = dir.path / "store";
        fs::create_directories(inside);
        CHECK(bundle::extract(evil, {{"../outside.wav", 0}}, inside.string()) == 0);
        CHECK_FALSE(fs::exists(outside));
    }
}

TEST_CASE("a project file that can't be written leaves the old one whole") {
    Temp dir;
    std::string project = (dir.path / "Song.tessera").string();
    REQUIRE(bundle::writeText(project, "old"));
    CHECK_FALSE(bundle::write(project, "new", {{"audio/gone.wav", (dir.path / "gone.wav").string()}}));
    CHECK(readFile(project) == "old");
    CHECK_FALSE(fs::exists(project + ".part"));
    CHECK_FALSE(bundle::writeText((dir.path / "no" / "folder" / "x").string(), "text"));
}

TEST_CASE("a plain JSON project isn't taken for a bundle, and damage is noticed") {
    Temp dir;
    std::string plain = (dir.path / "old.tessera").string();
    writeFile(plain, "{\"format\":\"tessera\"}");
    CHECK_FALSE(bundle::isBundle(plain));
    std::string json;
    CHECK_FALSE(bundle::readProject(plain, json));

    std::string project = (dir.path / "Song.tessera").string();
    REQUIRE(bundle::write(project, "{\"format\":1}", {}));
    SUBCASE("a flipped byte in the project") {
        std::fstream f(project, std::ios::binary | std::ios::in | std::ios::out);
        f.seekp(45);
        f.put('X');
        f.close();
        CHECK_FALSE(bundle::readProject(project, json));
    }
    SUBCASE("a cut-off file") {
        fs::resize_file(project, fs::file_size(project) / 2);
        CHECK_FALSE(bundle::readProject(project, json));
    }
    SUBCASE("rubbish that starts like a zip") {
        writeFile(project, std::string("PK\3\4") + std::string(100, char(0xFF)));
        CHECK_FALSE(bundle::readProject(project, json));
    }
}

TEST_CASE("a file name from a song can't leave the storage folder") {
    CHECK(isSafeAudioFile("audio/3fa1-vox.wav"));
    CHECK_FALSE(isSafeAudioFile("../secret.wav"));
    CHECK_FALSE(isSafeAudioFile("audio/../../secret.wav"));
    CHECK_FALSE(isSafeAudioFile("/etc/passwd"));
    CHECK_FALSE(isSafeAudioFile("audio\\..\\x.wav"));
    CHECK_FALSE(isSafeAudioFile("C:/x.wav"));
    CHECK_FALSE(isSafeAudioFile("audio//x.wav"));
    CHECK(safeAudioFile("audio/3fa1-vox.wav") == "audio/3fa1-vox.wav");
    CHECK(safeAudioFile("../../secret.wav") == "audio/secret.wav");
    CHECK(safeAudioFile("..") == "audio/unnamed");
    CHECK(safeAudioFile("") == "");
}

TEST_CASE("Find looks through a folder for every missing file") {
    Temp was, folder, storage;
    AudioSource vox = sourceIn(was.path, 1, "vox.wav", "vox samples");
    AudioSource drums = sourceIn(was.path, 2, "drums.wav", "drum samples");
    AudioSource bass = sourceIn(was.path, 3, "bass.wav", "bass samples");
    AudioSource lost = sourceIn(was.path, 4, "lost.wav", "nowhere");

    writeFile(folder.path / "vox.wav", "vox samples");                      // same name, same audio
    writeFile(folder.path / "deep" / "er" / "renamed.wav", "drum samples"); // renamed, in a subfolder
    writeFile(folder.path / "bass.wav", "other bass");                      // same name, other audio
    writeFile(folder.path / "notes.txt", "drum samples");                   // not audio

    FolderSearch r = findInFolder({vox, drums, bass, lost}, folder.path.string(), storage.path.string());
    CHECK(r.found == 2);
    CHECK(readFile(storage.path / vox.file) == "vox samples");
    CHECK(readFile(storage.path / drums.file) == "drum samples");
    CHECK_FALSE(fs::exists(storage.path / bass.file));
    CHECK_FALSE(fs::exists(storage.path / lost.file));
    REQUIRE(r.lookalikes.size() == 1);
    CHECK(r.lookalikes[0].first == bass.id);
    CHECK(fs::path(r.lookalikes[0].second).filename() == "bass.wav");

    SUBCASE("files kept under their stored names, as an old project's folder has them") {
        Temp oldFolder, again;
        writeFile(oldFolder.path / fs::path(vox.file).filename(), "vox samples");
        CHECK(findInFolder({vox}, oldFolder.path.string(), again.path.string()).found == 1);
    }
    SUBCASE("a folder that isn't there finds nothing") {
        CHECK(findInFolder({vox}, (folder.path / "no").string(), storage.path.string()).found == 0);
    }
}

TEST_CASE("accepting a different recording can be undone") {
    Song song = makeDefaultSong();
    AudioSource before;
    before.id = song.newId<AudioSourceId>();
    before.file = "audio/a.wav";
    before.contentHash = 1;
    song.audioSources.push_back(before);
    AudioSource after = before;
    after.file = "audio/b.wav";
    after.contentHash = 2;
    SetAudioSource cmd(before, after);
    cmd.apply(song);
    CHECK(song.findSource(before.id)->file == "audio/b.wav");
    cmd.revert(song);
    CHECK(song.findSource(before.id)->contentHash == 1);
}
