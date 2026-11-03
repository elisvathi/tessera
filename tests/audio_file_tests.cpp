#include "doctest.h"

#include "../src/audio/AudioDecode.hpp"
#include "../src/model/AudioAnalysis.hpp"

#include <cmath>
#include <string>

using namespace tessera;

namespace {

/// The path of a file in tests/audio, wherever the tests run from.
std::string testFile(const std::string& name) {
    std::string here = __FILE__;
    return here.substr(0, here.find_last_of('/') + 1) + "audio/" + name;
}

/// Decodes a test file, failing the test if it can't.
DecodedAudio decoded(const std::string& name) {
    DecodedAudio out;
    REQUIRE(decodeAudio(testFile(name), out) == DecodeError::None);
    return out;
}

} // namespace

TEST_CASE("WAV, AIFF, FLAC and MP3 files decode to float samples") {
    DecodedAudio wav = decoded("click120.wav");
    CHECK(wav.sampleRate == 22050);
    CHECK(wav.fileChannels == 1);
    CHECK(wav.channels.size() == 1);
    CHECK(wav.frames() == 4 * 22050);

    DecodedAudio aiff = decoded("sine1k.aiff");
    CHECK(aiff.sampleRate == 44100);
    CHECK(aiff.frames() == 22050);
    float peak = 0;
    for (float x : aiff.channels[0])
        peak = std::max(peak, std::abs(x));
    CHECK(peak == doctest::Approx(0.5).epsilon(0.01));

    DecodedAudio flac = decoded("click128.flac");
    CHECK(flac.frames() == std::llround(10.3 * 22050));

    DecodedAudio mp3 = decoded("sweep.mp3");
    CHECK(mp3.channels.size() == 2);
    CHECK(mp3.frames() > 0);
    CHECK(mp3.sampleRate > 0);
}

TEST_CASE("a file with six channels is mixed down to stereo, each side the average of its channels") {
    DecodedAudio six = decoded("six.wav");
    CHECK(six.fileChannels == 6);
    REQUIRE(six.channels.size() == 2);
    // Channels hold 0.1 … 0.6: the left gets 1, 3, 5 (0.1, 0.3, 0.5), the right 2, 4, 6.
    CHECK(six.channels[0][10] == doctest::Approx(0.3).epsilon(0.001));
    CHECK(six.channels[1][10] == doctest::Approx(0.4).epsilon(0.001));
}

TEST_CASE("files that aren't audio, or aren't there, say so") {
    DecodedAudio out;
    CHECK(decodeAudio(testFile("notaudio.wav"), out) == DecodeError::NotAudio);
    CHECK(decodeAudio(testFile("missing.wav"), out) == DecodeError::CantOpen);
    AudioFileInfo info;
    CHECK_FALSE(probeAudio(testFile("notaudio.wav"), info));
}

TEST_CASE("the header tells the length and the tempo chunk before decoding") {
    AudioFileInfo info;
    REQUIRE(probeAudio(testFile("acid.wav"), info));
    CHECK(info.tempo == doctest::Approx(140));
    CHECK(info.sampleRate == 8000);
    REQUIRE(probeAudio(testFile("click128.flac"), info));
    CHECK(info.frames == std::llround(10.3 * 22050));
    CHECK(info.tempo == 0);
}

TEST_CASE("a decode can be cancelled") {
    DecodedAudio out;
    int calls = 0;
    CHECK(decodeAudio(testFile("click128.flac"), out, [&](double) { return ++calls < 2; }) == DecodeError::Cancelled);
    CHECK(out.frames() == 0);
}

TEST_CASE("the same bytes hash the same, different files differently") {
    uint64_t a = hashFile(testFile("click120.wav"));
    CHECK(a != 0);
    CHECK(a == hashFile(testFile("click120.wav")));
    CHECK(a != hashFile(testFile("kick.wav")));
    CHECK(hashFile(testFile("missing.wav")) == 0);
}

TEST_CASE("audio file names are recognised by extension") {
    CHECK(looksLikeAudio("x/Loop.WAV"));
    CHECK(looksLikeAudio("a.aif"));
    CHECK(looksLikeAudio("a.flac"));
    CHECK(looksLikeAudio("a.mp3"));
    CHECK_FALSE(looksLikeAudio("song.mid"));
    CHECK_FALSE(looksLikeAudio("folder.wav/readme"));
}

TEST_CASE("tempos in file names are found, with or without a space or separator") {
    CHECK(tempoFromName("drums_92bpm.wav") == 92);
    CHECK(tempoFromName("Loop 120 BPM.aif") == 120);
    CHECK(tempoFromName("break-174.5BPM.flac") == 174.5);
    CHECK(tempoFromName("BPM 128 house.wav") == 128);
    CHECK(tempoFromName("kick 01.wav") == 0);
    CHECK(tempoFromName("9 bpm.wav") == 0);
}

TEST_CASE("a loop's length gives a whole number of beats nearest the song tempo") {
    int beats = 0;
    CHECK(tempoFromLength(4.0, 120, {4, 4}, &beats) == doctest::Approx(120));
    CHECK(beats == 8);
    // 8 beats at 92 BPM; 16 beats would be 184 BPM, too fast.
    CHECK(tempoFromLength(8 * 60.0 / 92, 120, {4, 4}, &beats) == doctest::Approx(92));
    // 3 seconds fits 4 beats at 80 or 8 at 160: the song tempo picks.
    CHECK(tempoFromLength(3.0, 90, {4, 4}) == doctest::Approx(80));
    CHECK(tempoFromLength(3.0, 150, {4, 4}) == doctest::Approx(160));
    // In 3/4, loops come in threes: 6 beats over 3 s is 120 BPM.
    CHECK(tempoFromLength(3.0, 120, {3, 4}, &beats) == doctest::Approx(120));
    CHECK(beats == 6);
    CHECK(tempoFromLength(0.1, 120, {4, 4}) == 0);
}

TEST_CASE("the quick guess takes the file's tempo, then its name, then its length") {
    CHECK(quickGuess("x.wav", 140, 2, 120, {4, 4}).source == TempoSource::File);
    AudioGuess named = quickGuess("drums_92bpm.wav", 0, 2, 120, {4, 4});
    CHECK(named.source == TempoSource::Name);
    CHECK(named.bpm == 92);
    AudioGuess length = quickGuess("loop.wav", 0, 4, 120, {4, 4});
    CHECK(length.source == TempoSource::Length);
    CHECK(length.bpm == doctest::Approx(120));
    CHECK(length.loop);
    AudioGuess song = quickGuess("song.wav", 0, 200, 120, {4, 4});
    CHECK(song.source == TempoSource::None);
    CHECK_FALSE(song.loop);
}

TEST_CASE("a 120 BPM click track guesses 120.0, and a 128 BPM one that isn't a whole number of beats 128.0") {
    DecodedAudio click = decoded("click120.wav");
    AudioGuess g = fullGuess(quickGuess("click120.wav", 0, 4, 100, {4, 4}), click.channels, click.sampleRate, 100);
    CHECK(g.bpm == doctest::Approx(120.0).epsilon(1e-9));
    CHECK(g.sound == SoundType::Drums);
    CHECK_FALSE(g.oneShot);

    DecodedAudio odd = decoded("click128.flac");
    AudioGuess h = fullGuess(quickGuess("clicks.flac", 0, 10.3, 120, {4, 4}), odd.channels, odd.sampleRate, 120);
    CHECK(h.source == TempoSource::Onsets);
    CHECK(h.bpm == doctest::Approx(128.0).epsilon(1e-9));
    CHECK(h.firstBeat == doctest::Approx(0).epsilon(0.01));
}

TEST_CASE("onset analysis finds the beat of a long click track on its own") {
    DecodedAudio odd = decoded("click128.flac");
    double first = -1;
    CHECK(tempoFromOnsets(onsetCurve(odd.channels, odd.sampleRate), &first) == doctest::Approx(128.0));
    CHECK(first < 0.01);
    DecodedAudio pad = decoded("pad.flac");
    CHECK(tempoFromOnsets(onsetCurve(pad.channels, pad.sampleRate)) == 0);
}

TEST_CASE("a single decaying hit is a one-shot that plays at its own speed, once") {
    DecodedAudio kick = decoded("kick.wav");
    AudioGuess g = fullGuess(quickGuess("kick.wav", 0, 0.4, 120, {4, 4}), kick.channels, kick.sampleRate, 120);
    CHECK(g.oneShot);
    CHECK_FALSE(g.loop);
    CHECK(g.bpm == 120);
}

TEST_CASE("a steady chord is a full mix, not drums") {
    DecodedAudio pad = decoded("pad.flac");
    AudioGuess g = fullGuess(quickGuess("pad.flac", 0, 6, 120, {4, 4}), pad.channels, pad.sampleRate, 120);
    CHECK(g.sound == SoundType::FullMix);
    CHECK_FALSE(g.oneShot);
    // A 6-second file with no beat keeps its length guess: 12 beats at 120.
    CHECK(g.source == TempoSource::Length);
}

TEST_CASE("peaks give the outline of the waveform at any zoom") {
    std::vector<std::vector<float>> ch(2, std::vector<float>(10000, 0.f));
    ch[0][5000] = 0.9f;
    ch[0][5001] = -0.7f;
    ch[1][123] = 0.25f;
    PeakCache peaks(ch);
    CHECK(peaks.frames() == 10000);
    CHECK(peaks.channels() == 2);
    float lo, hi;
    peaks.range(0, 0, 10000, lo, hi);
    CHECK(hi == doctest::Approx(0.9).epsilon(0.001));
    CHECK(lo == doctest::Approx(-0.7).epsilon(0.001));
    peaks.range(0, 0, 4000, lo, hi);
    CHECK(hi == 0);
    peaks.range(1, 100, 150, lo, hi);
    CHECK(hi == doctest::Approx(0.25).epsilon(0.001));
    peaks.range(0, 20000, 30000, lo, hi);
    CHECK(hi == 0);
    CHECK(peaks.bytes() > 0);
    CHECK(PeakCache().empty());
}

TEST_CASE("splits move to the nearest zero crossing") {
    std::vector<std::vector<float>> ch(1, std::vector<float>(1000));
    for (size_t i = 0; i < ch[0].size(); i++)
        ch[0][i] = std::sin(2 * 3.14159265f * float(i) / 100.f + 0.3f);
    int64_t z = nearestZeroCrossing(ch, 520, 48);
    CHECK(std::abs(ch[0][size_t(z)]) < 0.05f);
    CHECK(std::abs(z - 520) <= 48);
    std::vector<std::vector<float>> flat(1, std::vector<float>(100, 1.f));
    CHECK(nearestZeroCrossing(flat, 50, 10) == 50);
}
