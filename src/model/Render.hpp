#pragma once

#include "MidiFile.hpp"
#include "Song.hpp"

#include <jansson.h>

#include <array>
#include <map>
#include <string>
#include <vector>

/// Song -> MIDI file (DESIGN.md §17): what plays, laid out on the timeline, as Standard MIDI File tracks.
/// Pure and testable; the export dialog only picks the options and writes the bytes.
namespace tessera::render {

enum class Timing { Written, Played };           // Played: swing and micro-timing applied
enum class OperatorMode { Written, Rendered };   // Rendered: chance, repeats, spreads... baked, pass by pass
enum class AutomationMode { Off, Cc, Cc14 };     // automation lanes as controllers (14-bit: MSB + LSB pairs)
enum class ExpressionMode { Off, Mpe, Flatten }; // per-note curves as MPE, or flattened onto the track channel
enum class RangeMode { Song, Loop, Selection };
enum class Split { OneFile, PerTrack, PerClip };
enum class DeviceMode { Applied, Bypassed }; // Applied: the notes the tracks' device chains play

constexpr int kLanes = 8;

/// The export dialog's settings, remembered per patch.
struct Options {
    Timing timing = Timing::Written;
    OperatorMode operators = OperatorMode::Written;
    AutomationMode automation = AutomationMode::Off;
    ExpressionMode expressions = ExpressionMode::Off;
    RangeMode range = RangeMode::Song;
    Split split = Split::OneFile;
    DeviceMode devices = DeviceMode::Applied;
    bool includeMuted = false;
    bool noteChannels = false; // each note on its own channel instead of its track's (not with MPE)
    Tick from = 0, to = 0;     // RangeMode::Selection
    Tick ccStep = kPpq / 16;   // how often curves are sampled (1/64)
    int bendRange = 48;        // MPE pitch bend range in semitones
    std::array<int, kLanes> laneCc{1, 2, 11, 16, 17, 18, 19, 20}; // by AUTO channel
    std::map<uint64_t, int> channels;                             // TrackId value -> MIDI channel 1..16

    json_t* toJson() const;
    static Options fromJson(const json_t* j);
    bool operator==(const Options& o) const;
};

/// What the song itself doesn't hold.
struct Context {
    double tempo = 120;
    std::string title; // the conductor track's name
};

/// The song range exported, [from, to) in song ticks. "Song" ends at the last clip on the timeline.
std::pair<Tick, Tick> rangeOf(const Song& song, const Options& o);

/// The MIDI channel (1..16) a track exports on: its own setting, else drum tracks on 10 and instrument tracks
/// 1, 2, 3... in display order, skipping 10.
int channelOf(const Song& song, const Options& o, TrackId track);

/// The arrangement (or the chosen range of it) as a format 1 file: a conductor track (title, tempo, time
/// signature, markers, the loop as cue points) and one track per exported Tessera track, in display order.
/// `tracks` empty: every track.
smf::File renderSong(const Song& song, const Options& o, const Context& ctx, const std::vector<TrackId>& tracks = {});

/// One clip, played once from its start (its loop region once if it loops), on its track's channel.
smf::File renderClip(const Song& song, const Options& o, const Context& ctx, TrackId track, ClipId clip);

/// A note as exported, in song ticks.
struct ExportNote {
    double start, end;
    int pitch;
    float velocity, release;
    uint8_t channel = 1;
};
/// Every note a track plays on the timeline with the chosen timing, operators and devices: what MIDI export writes
/// for it (the whole song; muted clips only with `includeMuted`).
std::vector<ExportNote> trackNotes(const Song& song, TrackId track, const Options& o, const Context& ctx);
/// One clip played once from its start on its track, with the same choices (as renderClip).
std::vector<ExportNote> clipNotes(const Song& song, TrackId track, ClipId clip, const Options& o, const Context& ctx);
/// An AUTO channel's value (0..1) over [from, to) as MIDI export samples it, one entry per change; empty when no
/// lane plays on the channel.
std::vector<std::pair<Tick, float>> laneSamples(const Song& song, TrackId track, int output, const Options& o,
                                                Tick from, Tick to);

} // namespace tessera::render
