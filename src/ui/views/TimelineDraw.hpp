#pragma once

#include "../interaction/Axes.hpp"

#include <rack.hpp>

#include <string>

/// Drawing shared by every view that shows time: grid lines, playhead, position labels.
namespace tessera::timeline {

/// Vertical grid lines over `height`, in three strengths (bar, beat, subdivision). With a `snap` step the lines
/// show that step, so they match where edits land; when its lines would be too dense the zoom picks them.
void drawGrid(NVGcontext* vg, const TimeAxis& axis, TimeSig ts, float height, Tick snap = 0);

/// The playhead line, plus a small triangle cap when `cap` is set (rulers).
void drawPlayhead(NVGcontext* vg, float x, float height, bool cap = false);

/// "3.2" style position labels: bar.beat, with .16th added when the label step is finer than a beat.
std::string positionLabel(Tick tick, Tick labelStep, TimeSig ts);

/// "3.2.1" bar.beat.16th, 1-based, for readouts.
std::string positionText(double tick, TimeSig ts);

/// A short arrow at the left or right edge that shows where an off-screen playhead is.
void drawOffscreenArrow(NVGcontext* vg, bool right, float width, float y);

} // namespace tessera::timeline
