#pragma once

#include "Song.hpp"

#include <jansson.h>

namespace tessera {

/// Song <-> JSON. Notes are stored as compact arrays to keep patches small:
/// [start, length, pitch, velocity, releaseVelocity, microOffset, flags, id, {extras}?]
/// where flags bit 0 = muted, bits 8-15 = color tag, and the optional extras object holds operators and
/// expression curves only when they differ from the defaults.
json_t* songToJson(const Song& song);

/// Reads a song saved with `schemaVersion`, migrating older layouts first. Never fails: missing or malformed
/// fields fall back to defaults, and the result is normalized. A non-object input gives an empty song.
Song songFromJson(const json_t* songJ, int schemaVersion);

/// Upgrades song JSON written by `fromVersion` to Song::kSchemaVersion, in place.
/// Returns false for a schema newer than this build; the caller then loads it as best it can.
bool migrateSong(json_t* songJ, int fromVersion);

json_t* clipToJson(const Clip& clip);
Clip clipFromJson(const json_t* clipJ);

/// A clip on its own, for .tessera-clip files: {"format": "tessera-clip", "schema": 1, "clip": {...}}.
json_t* clipFileToJson(const Clip& clip);
/// Nothing when the JSON isn't a Tessera clip file. The clip is normalized; give it fresh IDs before use.
std::optional<Clip> clipFileFromJson(const json_t* fileJ);

} // namespace tessera
