#pragma once

#include "Render.hpp"

#include <string>

/// DAWproject export (an open format read by several major DAWs): a zip holding project.xml and metadata.xml.
/// Pure, like MIDI export, and it plays the notes through the same code, so the Timing, Note operators and Devices
/// choices mean the same in both.
namespace tessera::dawproject {

/// project.xml: one note track per Tessera track (muted ones only with `includeMuted`); the arrangement as one
/// clip per arranger instance, holding the notes that start in it; launcher clips as scenes (one pass, looping);
/// tempo, time signature and markers; AUTO lanes as MIDI CC automation when `automation` isn't Off.
std::string projectXml(const Song& song, const render::Options& o, const render::Context& ctx,
                       const std::string& version);
std::string metadataXml(const render::Context& ctx);

/// The .dawproject file's bytes.
std::string build(const Song& song, const render::Options& o, const render::Context& ctx, const std::string& version);

} // namespace tessera::dawproject
