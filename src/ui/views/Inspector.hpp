#pragma once

#include "../../model/NoteFields.hpp"
#include "../interaction/InteractiveView.hpp"

#include <rack.hpp>

#include <functional>

namespace tessera {

class NoteTarget;

/// The right-hand panel of the detail pane (EDITOR_UX §4.7).
///
/// NOTE: start, length, pitch, velocity, release and micro-timing as slider fields; drum hits add pitch, timbre and
/// pressure (one value each; on a shaped curve the value is where it starts, and editing moves the whole curve). Drag a
/// field (any direction) to move all values together, click to type a value or an edit ("+10", "-5%", "*0.8", "=64"),
/// double-click to reset. With several notes, clicking a field opens its chart (a histogram): how many notes sit at
/// each value, with Mean (drag: move all), Spread (drag: narrow or widen around the mean) and Chaos (drag: scatter).
/// OPERATORS: one switch per operator (chance, repeats, occurrence, recurrence). An operator that is off leaves the
/// note as written; switching it on gives a value you hear at once and shows its controls.
/// Below, facts about the track and its clip. Toggle with I.
class Inspector : public InteractiveView {
  public:
    explicit Inspector(EditorContext& ctx) : InteractiveView(ctx) {}

    /// The editor whose selection the inspector edits: the piano roll (also the drum editor).
    std::function<NoteTarget*()> target;

    void step() override;

    /// One clickable or draggable part of the notes section.
    struct Region {
        enum class Kind { Field, Chart, Mean, Spread, Chaos, Chip, Occurrence, Pass };
        Kind kind;
        fields::Field field = fields::Field::Velocity;
        int index = 0; // Chip: the operator; Occurrence: the choice; Pass: the pass
        rack::math::Rect rect;
    };

  protected:
    std::unique_ptr<DragGesture> startGesture(rack::math::Vec pos, int button, Modifiers mods) override;
    void click(rack::math::Vec pos, int button, Modifiers mods) override;
    void doubleClick(rack::math::Vec pos, Modifiers mods) override;
    void wheel(rack::math::Vec pos, rack::math::Vec delta, Modifiers mods) override;
    CursorShape hoverCursor(rack::math::Vec pos, Modifiers mods) override;
    void drawView(const DrawArgs& args) override;

  private:
    NoteTarget* editor = nullptr; // target(), refreshed each frame
    // A click on a field opens its typing box or chart only once a double-click (reset) can no longer follow.
    static constexpr double kDoubleClickWait = 0.35;
    std::function<void()> pendingClick;
    double pendingClickAt = 0;
    double doubleClickedAt = -1; // the second click of a double-click is not a click of its own
    float section(NVGcontext* vg, float y, const char* title);
    float row(NVGcontext* vg, float y, const std::string& label, const std::string& value);
    /// A track setting you can change: the value is a chip with a caret, and a click opens its menu.
    enum class Setting { Voices, Input, Recording, Audio, Send, AddSend };
    float settingRow(NVGcontext* vg, float y, const std::string& label, const std::string& value, Setting which,
                     uint64_t target = 0);
    /// Opens the menu for a setting row of the selected track (`target`: the track a Send row sends to).
    void openSetting(Setting which, uint64_t target = 0);
    /// The selected track's sends, each with its level and place, then + Send to…, and who sends to it (§7.6).
    float drawSends(NVGcontext* vg, float y, const Track& t);
    void drawRegion(NVGcontext* vg, const Region& r, bool hot);
    void drawChart(NVGcontext* vg, const Region& r);
    void drawFacts(NVGcontext* vg, float y);
    /// The clip and instance facts of an audio track: gain, tempo, sound type, the file, fades.
    void drawAudioClipFacts(NVGcontext* vg, float y, const Track& track);
    void layoutRegions();
    const Region* regionAt(rack::math::Vec pos) const;
    void openTyping(fields::Field field);
    /// The song's time signature, which note starts and lengths are written in (4/4 with no module).
    TimeSig timeSig() const;
    /// The selected track is a drum track: hits get pitch, timbre and pressure rows.
    bool drumTrack() const;
    /// A field's row name: as the field calls itself, except a drum hit's own pitch, which is its "Note".
    std::string fieldLabel(fields::Field f) const;
    /// Opens the field's chart, or closes it when it is the one open.
    void toggleChart(fields::Field field);
    /// How many of the selected notes have an operator on.
    size_t countOn(fields::Operator op);

    struct Title {
        float y;
        std::string text;
    };
    std::vector<Region> regions;
    std::vector<Title> titles;
    float recurrenceHint = -1; // y of the help line under the passes, if shown
    float notesBottom = 0;
    float scroll = 0;
    int hovered = -1;
    struct SettingRow {
        rack::math::Rect rect; // in content coordinates (before scrolling)
        Setting which;
        uint64_t target = 0;
    };
    std::vector<SettingRow> settingRows; // laid out while drawing the facts
    int hoveredSetting = -1;
    // The chart: which field it shows (one at a time), and what a Spread or Chaos drag shows while it runs.
    fields::Field chartField = fields::Field::Velocity;
    bool chartOpen = true;
    Region::Kind dragging = Region::Kind::Field;
    std::string dragText;

    friend class ChartDrag;
};

} // namespace tessera
