#pragma once

#include "Commands.hpp"

#include <cstdint>
#include <optional>
#include <set>
#include <vector>

/// Automation lanes as undoable commands, and pure edits on a lane's points (DESIGN.md §12, EDITOR_UX §6.6).
namespace tessera {

/// Where lanes live: a track (arranger time) or a clip (clip time, looping with the clip). Exactly one is set.
struct LaneOwner {
    TrackId track;
    ClipId clip;

    static LaneOwner ofTrack(TrackId id) { return {id, ClipId()}; }
    static LaneOwner ofClip(ClipId id) { return {TrackId(), id}; }
    bool valid() const { return track.valid() != clip.valid(); }
    bool operator==(const LaneOwner& o) const { return track == o.track && clip == o.clip; }
};

std::vector<AutomationEnvelope>* lanesOf(Song& song, LaneOwner owner);
const std::vector<AutomationEnvelope>* lanesOf(const Song& song, LaneOwner owner);
const AutomationEnvelope* findLane(const Song& song, LaneOwner owner, AutomationId id);

/// A lane and where it lives.
struct FoundLane {
    LaneOwner owner;
    const AutomationEnvelope* lane = nullptr;
};
/// The lanes that turn a knob: track lanes first (in track order), then clip lanes.
std::vector<FoundLane> lanesTurning(const Song& song, const ParamRef& knob);

/// Adds, removes or replaces one lane. `before` empty = add (at `index`), `after` empty = remove.
class EditLane : public Command {
  public:
    EditLane(std::string name, LaneOwner owner, int index, std::optional<AutomationEnvelope> before,
             std::optional<AutomationEnvelope> after)
        : label(std::move(name)), owner(owner), index(index), before(std::move(before)), after(std::move(after)) {}

    void apply(Song& song) override { set(song, before, after); }
    void revert(Song& song) override { set(song, after, before); }
    std::string name() const override { return label; }
    ChangeSet changes() const override {
        return owner.track.valid() ? ChangeSet::track(owner.track) : ChangeSet::clip(owner.clip);
    }

  private:
    void set(Song& song, const std::optional<AutomationEnvelope>& out,
             const std::optional<AutomationEnvelope>& in) const;
    std::string label;
    LaneOwner owner;
    int index;
    std::optional<AutomationEnvelope> before, after;
};

/// The first AUTO output channel no lane of `lanes` uses, or -1 when all 8 are taken.
int freeOutput(const std::vector<AutomationEnvelope>& lanes);
/// A new lane on `output`: 0-10 V, named after its output ("Auto 3").
AutomationEnvelope makeLane(AutomationId id, int output, uint32_t color);

/// A lane that turns a device knob or modulator setting (docs/DEVICES.md §1.3), starting at the knob's `value`
/// so adding it changes nothing until points are drawn. `steps`: the knob is a choice, so the lane holds each value
/// until the next point.
AutomationEnvelope makeKnobLane(AutomationId id, const ParamRef& target, std::string name, float value, bool steps,
                                uint32_t color);

/// Null when the owner already has as many lanes as it may: kMaxAutomationLanes AUTO lanes, kMaxKnobLanes knob
/// lanes.
CommandPtr addLane(const Song& song, LaneOwner owner, AutomationEnvelope lane);
CommandPtr removeLane(const Song& song, LaneOwner owner, AutomationId id);
/// Replaces a lane with `after` (same ID). Null when nothing changed.
CommandPtr replaceLane(const Song& song, LaneOwner owner, const AutomationEnvelope& after, const std::string& name);
/// Moves a lane to another position in its owner's list (display order only; outputs don't change).
CommandPtr moveLane(const Song& song, LaneOwner owner, AutomationId id, int toIndex);

/// Voltage range presets for the lane header (DESIGN.md §12).
struct VoltageRange {
    const char* name;
    float min, max;
    bool bipolar;
};
const std::vector<VoltageRange>& voltageRanges();

/// Pure point edits. Points are addressed by index; edits that can reorder points return where the given
/// indices ended up, so a selection follows its points.
namespace autoEdits {

using Points = std::vector<AutoPoint>;
using Selection = std::set<size_t>;

/// Sorts by time (stable), returning where `selected` moved to.
Selection sortPoints(Points& points, const Selection& selected);

/// A new point; returns its index.
size_t addPoint(Points& points, Tick t, float value, CurveType curve = CurveType::Linear);
/// A point on the existing curve, which keeps its shape (the new point takes the segment's curve).
size_t insertOnCurve(Points& points, Tick t, float rest);
Points deletePoints(const Points& points, const Selection& selected);
/// Moves points by `dt` ticks and `dv` in value; times stop at 0, values at 0..1.
Points movePoints(const Points& points, const Selection& selected, Tick dt, float dv, Selection* moved = nullptr);
Points setValue(const Points& points, const Selection& selected, float value);
Points setCurve(const Points& points, const Selection& selected, CurveType curve);

/// Ramer-Douglas-Peucker: the fewest points that stay within `tolerance` (value units) of `samples`.
Points simplify(const Points& samples, float tolerance);
/// Removes points in [from, to] and puts `drawn` there (simplified freehand drawing, a line, painted steps).
Points replaceRange(const Points& points, Tick from, Tick to, const Points& drawn);
/// Drops points that the curve would almost pass through anyway (lane menu "Thin").
Points thin(const Points& points, float tolerance);
Points quantizeTimes(const Points& points, const Selection& selected, Tick grid);
/// Flips values around the middle.
Points invert(const Points& points, const Selection& selected);
/// Mirrors the selected points in time inside their own span.
Points reverse(const Points& points, const Selection& selected);
Points randomize(const Points& points, const Selection& selected, float amount, uint32_t seed);

/// Shape stamps (EDITOR_UX §6.6).
enum class Shape { Sine, Triangle, SawUp, SawDown, Square, RandomSteps, SmoothRandom, ExpRise, ExpFall };
const char* shapeName(Shape shape);
/// One shape across [from, to): `cycles` repeats, moving between `low` and `high`, starting `phase` (0..1) into
/// the first cycle. The result replaces what was in the range.
Points stampShape(const Points& points, Tick from, Tick to, Shape shape, double cycles, float low, float high,
                  double phase, uint32_t seed);

/// Transform box: scales values toward `anchor` by `factor`, and times around `pivot` by `stretch`.
Points scaleSelection(const Points& points, const Selection& selected, float anchor, float factor, Tick pivot,
                      double stretch);
/// Transform box corners: fades the depth from `startFactor` at the selection's start to `endFactor` at its
/// end, around `anchor` (tilt).
Points tilt(const Points& points, const Selection& selected, float anchor, float startFactor, float endFactor);

} // namespace autoEdits

} // namespace tessera
