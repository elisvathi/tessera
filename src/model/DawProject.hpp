#pragma once

#include "Render.hpp"

#include <map>
#include <string>
#include <utility>
#include <vector>

/// DAWproject export (an open format read by several major DAWs): a zip holding project.xml and metadata.xml.
/// Pure, like MIDI export, and it plays the notes through the same code, so the Timing, Note operators and Devices
/// choices mean the same in both.
namespace tessera::dawproject {

/// project.xml: one note track per Tessera track (muted ones only with `includeMuted`); the arrangement as one
/// clip per arranger instance, holding the notes that start in it; launcher clips as scenes (one pass, looping);
/// tempo, time signature and markers; AUTO lanes as MIDI CC automation when `automation` isn't Off.
/// Audio tracks become audio tracks (§3.5): each timeline clip a warped audio clip pointing at its file inside the
/// project ("audio/<name>"), with its fades and loop, and the track's volume and pan.
std::string projectXml(const Song& song, const render::Options& o, const render::Context& ctx,
                       const std::string& version);
std::string metadataXml(const render::Context& ctx);

/// Where each audio source's file goes inside the project, by source: "audio/Drum loop.wav".
std::map<uint64_t, std::string> audioPaths(const Song& song);

/// The .dawproject file's bytes. `audioFiles` are the files the audio clips play: (path inside, contents).
std::string build(const Song& song, const render::Options& o, const render::Context& ctx, const std::string& version,
                  const std::vector<std::pair<std::string, std::string>>& audioFiles = {});

} // namespace tessera::dawproject
