#pragma once

#include "Commands.hpp"
#include "Recording.hpp"
#include "Song.hpp"
#include "TrackCommands.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

/// Standard MIDI File import (DESIGN.md §1, planned for v1.1): a file's notes as Tessera clips and tracks. Pure, so
/// the tests check it without Rack; the editor reads the file and performs the commands.
namespace tessera::midiImport {

/// The notes of one MIDI track on one channel (a type 0 file is split by channel; an MPE zone is one part), at
/// Tessera's 960 PPQ, with what was played on them (poly aftertouch, MPE) and the controls the channel moved.
struct Part {
    std::string name;
    int channel = 0;             // 0..15
    bool drum = false;           // channel 10
    std::vector<Note> notes;     // sorted by start, then pitch; IDs not set yet
    std::vector<TakeLane> lanes; // pitch bend, channel pressure and CCs, in the order they first moved
};

struct Imported {
    std::vector<Part> parts;        // only the ones with notes, in file order
    std::optional<double> tempo;    // the first tempo in the file, in BPM
    std::optional<TimeSig> timeSig; // the first time signature
};

/// Reads a file's notes. False when the bytes are not a Standard MIDI File Tessera can place on a beat grid (SMPTE
/// timed files are refused). A file without notes reads fine, with no parts.
bool read(const std::vector<uint8_t>& bytes, Imported& out);
bool readFile(const std::string& path, Imported& out);

/// All the parts' notes and lanes (or only part `part`'s) as one looping clip of whole bars, starting where the file
/// starts. Lanes for the same control are merged; CCs past the 8 AUTO channels are left out.
Clip clipOf(const Imported& in, int part, TimeSig timeSig, const NextId& nextId, const std::string& name);

/// The file as new tracks after the last one: one per part (channel 10 becomes a drum track with a lane for each
/// note it uses), each with its clip on the timeline at bar 1 and in its first launcher slot, and the file's time
/// signature. One undo step. Null when there are no notes or no room for another track; tracks past kMaxTracks are
/// left out.
CommandPtr importTracks(const Song& song, const Imported& in, const NextId& nextId, const std::string& fileName);

/// How many notes a part, or all of them (part -1), holds.
size_t noteCount(const Imported& in, int part = -1);

} // namespace tessera::midiImport
