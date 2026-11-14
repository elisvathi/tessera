#pragma once

#include "Types.hpp"

#include <string>
#include <vector>

/// One scale of note lengths for every setting that picks a length or a synced rate (docs/DECISIONS.md §156): the
/// straight values from 1/64 to 64 bars, with the triplet (T, 2/3 of the note) and dotted (D, 3/2 of it) values from
/// 1/32 to 1/2 between them. Each setting offers the stretch of it that makes sense, always short to long, so a curve
/// on a setting moves through its lengths in order and the same length has the same name everywhere.
namespace tessera::lengths {

/// One length: its name ("1/8 T", "2 bars") and how long it is, in whole notes, or in bars when `bars` is set.
struct Length {
    std::string name;
    double wholes = 0;
    bool bars = false;
};

/// The lengths on the scale from `shortest` to `longest` (both names on it), short to long.
std::vector<Length> range(const std::string& shortest, const std::string& longest);
/// The names of `list`, in its order.
std::vector<std::string> names(const std::vector<Length>& list);
/// A length in song ticks in time signature `ts` (a bar follows the signature, a note value doesn't). Off is 0.
double ticks(const Length& l, TimeSig ts);
/// `list[index]` in ticks, the index clamped to the list.
double ticksAt(const std::vector<Length>& list, int index, TimeSig ts);

/// The stretches the settings use.
/// Note devices' lengths and rates (Note Length, Arpeggiator, Note Repeat, Echo): 1/64 to 8 bars.
const std::vector<Length>& noteValues();
/// Modulators' synced rates (LFO, Steps, Random), which may also move slowly: 1/64 to 32 bars.
const std::vector<Length>& slowValues();

} // namespace tessera::lengths
