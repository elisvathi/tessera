#pragma once

#include "Song.hpp"

#include <string>
#include <vector>

/// DAWproject import (docs/DECISIONS.md §178): an open project file from another program becomes a new song. Pure:
/// it reads the project's XML; the caller unpacks the archive and brings the audio files into the patch.
namespace tessera::dawImport {

/// An audio file the song's clips play, still inside the project (or outside it, `external`).
struct AudioFile {
    AudioSourceId source; // the placeholder source in the song, to fill in once the file is in the patch
    std::string path;     // inside the archive ("audio/loop.wav"), or a path on disk when external
    bool external = false;
    double sampleRate = 0; // as the project says; frames in the song count at this rate
};

/// What a project became.
struct Imported {
    Song song;
    double tempo = 120;
    std::string title;
    std::vector<AudioFile> audio;
    /// What didn't come across, one line each, for the report ("Plugins (rebuild them in Rack): Bass: Diva").
    std::vector<std::string> leftOut;
    int notes = 0, clips = 0;
};

/// Reads an unpacked project: its project.xml and (may be empty) metadata.xml. New IDs count up from `firstId`, so
/// nothing that pointed into the old song can match the new one. False, with `error`, when it isn't a project.
bool readProject(const std::string& projectXml, const std::string& metadataXml, uint64_t firstId, Imported& out,
                 std::string* error = nullptr);

} // namespace tessera::dawImport
