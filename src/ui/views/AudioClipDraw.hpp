#pragma once

#include "../../audio/AudioStore.hpp"
#include "../../model/AudioRegions.hpp"
#include "../../model/Song.hpp"

#include <rack.hpp>

#include <functional>
#include <memory>
#include <string>

/// Drawing audio clips: waveforms, the tempo badge, gain and the clip states (docs/design/AUDIO_MIXER.md §6.1,
/// §6.6). Shared by the timeline and the launcher slots.
namespace tessera {

class TesseraModule;

namespace audioDraw {

/// Everything an audio clip shows about its file and its render.
struct ClipState {
    const AudioSource* source = nullptr;
    SourceStatus status;
    RenderStatus render; // the render this clip wants at the song tempo; None when it needs none
    std::vector<SourceStatus> takes; // per take, beside `status` for the clip's own
    regions::Files files;

    /// The status of take `take`'s file (-1 the clip's own).
    const SourceStatus& statusOf(int take) const {
        return take >= 0 && size_t(take) < takes.size() ? takes[size_t(take)] : status;
    }
};

/// The state of `clip` in `module` now.
ClipState stateOf(TesseraModule& module, const Clip& clip);

/// What one pixel column of a waveform shows.
struct Column {
    double from = 0, to = 0;            // source frames, lowest first
    float extraDb = 0;                  // on top of the waveform's gain
    const PeakCache* peaks = nullptr;   // another file's waveform, for a part playing another take; null for the clip's own
};

/// Fills what the column at `x` shows; false where the clip shows nothing.
using FramesAt = std::function<bool(float x, Column& column)>;

/// The waveform of `clip` over `body`: lowest and highest sample per pixel column, a stereo file as two halves,
/// scaled by the clip gain (`gainDb`, so a drag previews live). Missing files show stripes instead. Columns outside
/// [`left`, `right`) are skipped.
void drawWaveform(NVGcontext* vg, rack::math::Rect body, float left, float right, const ClipState& state,
                  float gainDb, NVGcolor color, const FramesAt& framesAt);

/// The small title-bar marks after the name: the tempo badge ("92 ▾", or "own speed" for clips that ignore the tempo),
/// the gain when it isn't 0 dB, and "6→2" for mixed-down files.
std::string titleMarks(const Clip& clip, const ClipState& state, float gainDb);

/// The big form of the title-bar marks, for a clip just dropped: "92 BPM · 2 bars · Stretch ▾", or "own speed · 1.2 s"
/// for clips that ignore the tempo, then the same gain and mix-down marks as titleMarks (§5.5).
std::string bigTitleMarks(const Clip& clip, const ClipState& state, float gainDb, TimeSig timeSig);

/// A short word for a state worth seeing ("Loading 40%", "Missing file", "40%"), or empty when it just plays.
std::string stateText(const ClipState& state);

/// Whether a state wants an amber warning mark.
bool stateWarns(const ClipState& state);

/// What clip ticks [u0, u1) play. False in a gap between parts or over a muted part, where nothing plays.
bool framesOfTicks(const Clip& clip, const ClipState& state, double u0, double u1, Column& column);

/// A Stretch clip's pitch: "0 st", "+2 st", "−3 st 25 ct".
std::string pitchText(const AudioContent& a);

/// A fader or pan lane's value (0..1) in the strip's own units: "−3.0 dB", "Centre", "40% L". Empty for other lanes.
std::string mixLaneText(const AutomationEnvelope& lane, float value);

/// "−3.0 dB", "+2.5 dB", "−inf dB".
std::string dbText(float db);

} // namespace audioDraw
} // namespace tessera
