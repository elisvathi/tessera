#pragma once

#include "../../model/DeviceLookup.hpp"
#include "../editing/DeviceActions.hpp"
#include "../interaction/DeviceTags.hpp"
#include "../interaction/InteractiveView.hpp"
#include "../interaction/Shortcuts.hpp"

#include <array>
#include <map>
#include <memory>
#include <optional>
#include <vector>

namespace tessera {

class TileLayer;

/// The selected track's devices, in a strip of fixed height above its notes (docs/DEVICES.md §1.1): the modulator
/// list on the left, then the device cards left to right in the order notes flow. Each card shows what its device
/// does as one to three small pictures, with its settings as tags on them that you drag, scroll, or click for a
/// panel. A playhead's own devices sit in a frame in its colour after the Playheads card. Closed, a thin bar names
/// the chain.
class DeviceStrip : public InteractiveView {
  public:
    explicit DeviceStrip(EditorContext& ctx);

    /// A card's height (header, pictures, padding), the room above and below a row for a playhead frame, and the
    /// gap between rows when the strip shows several.
    static constexpr float kCardHeight = 148;
    static constexpr float kTop = 10;
    static constexpr float kRowGap = 20;
    /// One row of cards.
    static constexpr float kOpenHeight = kCardHeight + 2 * kTop;
    /// The notes need this much under the strip; with less room the open strip takes the whole pane (devices only).
    static constexpr float kMinNotes = 110;
    /// The strip's height for `rows` rows of cards.
    static float heightFor(int rows) { return float(rows) * (kCardHeight + kRowGap) - kRowGap + 2 * kTop; }
    /// Card indices from here on are devices in the shown playhead's branch (index - kBranchBase).
    static constexpr int kBranchBase = 1000;
    static constexpr float kClosedHeight = 24;
    static constexpr float kListRight = 222; // the right edge of the modulator list
    /// Below this nothing useful fits: an open strip then shows as a bar that says so.
    static constexpr float kMinOpenHeight = 60;
    /// Room under the strip inside the pane, set by the pane: the mapping message floats there when it fits.
    float roomBelow = 0;
    /// Open, but the pane is too short for it.
    bool squeezed() { return deviceActions::stripOpen(ctx) && box.size.y < kMinOpenHeight; }
    /// Open, the closed bar (when the track has devices or modulators), or nothing. Open on the whole pane (devices
    /// only), it asks for more than any pane has.
    float wantedHeight();
    /// The room the pane has for the strip, set by the pane: dragging the strip's bottom edge near the pane's bottom
    /// gives it the whole pane.
    float paneRoom = 0;
    /// Delete and Duplicate act on the selected card; the arrow keys step the tag last touched. True when handled.
    bool handleAction(Action action);
    /// The bottom of the modulator list, where a modulator's popover opens (view coordinates).
    rack::math::Vec popoverAnchor() const { return rack::math::Vec(8, box.size.y - 4); }

    /// Which setting a tag stands for: a device knob or a playhead setting, the device type whose pictures it sits on,
    /// and its spec (family, prefix, step). A playhead's Direction tag stands for its direction and step order.
    struct TagRef {
        ParamRef ref;
        DeviceType type = DeviceType::Transpose;
        tags::TagSpec spec{0, tags::Family::Plain};
        NVGcolor tint; // its card's colour
        bool operator==(const TagRef& o) const { return ref == o.ref && spec.family == o.spec.family; }
    };

    /// One clickable or draggable part, rebuilt every frame.
    struct Part {
        enum class Kind {
            ClosedBar,
            AddModulator,
            ModulatorRow,
            ModulatorMap,
            Card,
            CardHeader,
            Power,
            Menu,
            Dot,
            ResizeEdge,  // the strip's bottom edge: drag it for more or fewer rows, or the whole pane
            TagBadge,    // the "A" on an automated tag: shows its lane
            Insert,      // the + on a link: adds a device there
            AddTile,     // "+ Add device" at the chain's end
            Tile,        // a picture: a click selects its device
            Tag,         // a setting on a picture
            Switch,      // an on/off setting on a picture
            PlayheadChip, // one playhead of a Playheads device: click to show its settings
            AddPlayhead,
            PlayheadOn,  // the shown playhead's On switch, next to the chips
            Frame,       // the shown playhead's own devices, in its colour; only drawn
            FramePlus,   // the round + at the frame's end
            Slot,        // the empty frame: a dashed slot that adds the playhead's first own device
            MotionNotes, // a Motion card's "6 notes": selects them in the piano roll
            MotionJoin,  // a Motion card's "+ Selected": the selected notes join it
            MotionLeave  // a Motion card's "− Selected": the selected notes leave it
        };
        Kind kind;
        rack::math::Rect rect;
        int device = -1; // index in the chain; from kBranchBase on, in the shown playhead's branch
        int param = -1;  // PlayheadChip: the playhead's index
        uint64_t id = 0; // device, modulator or playhead ID
        int tag = -1;    // Tag, Switch: its place in `tagRefs`
        int tile = -1;   // Tile: which picture of its card
        bool first = false;  // Tag: the first on its picture, drawn tinted
        bool dim = false;    // Tag: does nothing now (Times while Piece is Off)
    };

    // What running gestures show before they are committed.
    std::optional<std::pair<ParamRef, float>> knobPreview;   // a tag being dragged or previewed (normalized)
    std::optional<std::pair<ParamRef, float>> amountPreview; // the mapped modulator's amount on a tag
    std::optional<std::pair<uint64_t, int>> directionPreview; // a playhead's Direction tag (its 0..7 value)
    std::optional<std::pair<uint64_t, uint16_t>> channelPreview; // a Note Filter's channels being typed
    int dropIndex = -1;                                          // where a dragged card would land
    /// The tag a press, drag or panel last touched: the arrow keys step it.
    std::optional<TagRef> focusedTag;

    void step() override;

    /// A tag's values, from its setting's list (choice order, number steps), made once per setting.
    const tags::ValueList& listOf(const TagRef& t);
    /// A tag's value as it shows now: its place in listOf(t).
    int indexOf(const TagRef& t);
    /// Sets a tag to its list's `index` as one undo step.
    void setIndex(const TagRef& t, int index);
    /// Opens a tag's panel under the mouse.
    void openPanel(const TagRef& t);

  protected:
    std::unique_ptr<DragGesture> startGesture(rack::math::Vec pos, int button, Modifiers mods) override;
    void click(rack::math::Vec pos, int button, Modifiers mods) override;
    void doubleClick(rack::math::Vec pos, Modifiers mods) override;
    void wheel(rack::math::Vec pos, rack::math::Vec delta, Modifiers mods) override;
    CursorShape hoverCursor(rack::math::Vec pos, Modifiers mods) override;
    void drawView(const DrawArgs& args) override;
    void drawForeground(const DrawArgs& args) override;

  private:
    const Track* track();
    void layoutParts();
    /// A device's card at `x` with its top at `top`, in `tint`. Returns its right edge.
    float layoutCard(const Device& d, int index, float x, float top, NVGcolor tint);
    /// The tiles of a card, with their tags, from `x`; `tagOwner` is what the tags' settings belong to.
    float layoutTiles(const Device& d, int index, float x, float top, ParamRef::Kind kind, uint64_t owner,
                      const tags::Values& values, NVGcolor tint, float minWidth);
    /// How wide picture `spec` must be for its tags at their widest.
    float tileWidth(DeviceType type, const tags::TileSpec& spec, ParamRef::Kind kind, uint64_t owner);
    /// The device a card index stands for: in the chain, or (from kBranchBase) in the shown playhead's branch.
    const Device* cardDevice(int index);
    const Part* partAt(rack::math::Vec pos) const;
    /// How many notes, in every clip, move with the Motion device `id`.
    int motionNoteCount(DeviceId id);
    std::map<uint64_t, int> motionCounts;
    uint64_t motionCountRevision = ~uint64_t(0);
    /// The shown playhead of the track's Playheads device (wherever it sits): whose own devices the strip shows.
    const Playhead* branchPlayhead();
    const Playhead* shownPlayhead(const Device& d) const;
    /// A device's values (or for the Playheads card its shown playhead's), as they play now and as gestures preview
    /// them, snapped to the tags' steps so a picture redraws only when a tag would read differently.
    tags::Values valuesOf(const Device& d, const Playhead* playhead);
    /// A setting's value as it shows now (normalized): previewed, moved by a modulator or a curve, set by a lane, or
    /// as stored.
    float shownValue(const ParamRef& ref);
    /// A Note Filter's channel set (bit n = channel n + 1; 0 = any) and setting it as one undo step.
    uint16_t channelsOf(const Device& d) const;
    void setChannels(DeviceId id, uint16_t mask);
    /// The colour of the first modulator moving `ref`, if any moves it.
    std::optional<NVGcolor> modulatorColorOn(const ParamRef& ref);
    /// How far something moves a setting, for the depth bar under its tag: a lane's range (its lowest to highest
    /// point) or a modulation's reach from where the setting is set, normalized, in the lane's or modulator's colour.
    struct Span {
        float from, to;
        NVGcolor color;
        std::string who; // the lane's or modulator's name, for the hover note
    };
    std::vector<Span> spansOn(const ParamRef& ref);
    /// While mapping: whether the mapped modulator may reach this setting.
    bool reachableNow(const ParamRef& ref);

    void drawClosed(NVGcontext* vg);
    void drawModulators(NVGcontext* vg);
    void drawCard(NVGcontext* vg, const Part& card);
    void drawPlayheadChip(NVGcontext* vg, const Device& d, const Part& part, bool hot);
    void drawTag(NVGcontext* vg, const Part& part, bool hot);
    void sampleScopes();
    /// How far the modulator list can scroll: 0 while every row fits.
    float modScrollMax();
    /// The tint of the card with index `index`: the accent, a Motion's colour, or a playhead's.
    NVGcolor tintOf(const Device& d, int index);

    /// Wraps the cards onto as many rows as the strip has room for (one row scrolls sideways instead).
    void flowRows(const std::vector<std::pair<size_t, size_t>>& units);
    /// Shows the pictures laid out this frame, where flowRows put them.
    void showTiles();

    std::vector<Part> parts;
    std::vector<TagRef> tagRefs; // the tags of this frame, which parts point into
    /// A picture laid out this frame, shown by showTiles once the rows are flowed.
    struct PendingTile {
        int device, tile;
        uint64_t key, hash;
        DeviceType type;
        tags::Values values;
        NVGcolor tint;
        float width, alpha;
    };
    std::vector<PendingTile> pending;
    float scrollY = 0, contentHeight = 0;
  public:
    /// How many rows the cards need when wrapped at the strip's width: the strip never grows past them.
    int rowsNeeded = 1;
    /// Whether dragging the bottom edge can change anything: the cards need more rows and the pane has room for two
    /// with the notes under them.
    bool resizable() const { return rowsNeeded > 1 && paneRoom - heightFor(2) >= kMinNotes; }

  private:
    int hovered = -1;
    float scrollX = 0, contentWidth = 0;
    float modScroll = 0;
    float chainLeft = 0; // where the scrolling chain starts (the modulator list stays put)
    // Each modulator's last outputs (0..1 for drawing), for its row's little graph.
    std::map<uint64_t, std::array<float, 40>> scopes;
    int scopeHead = 0;
    TrackId shownTrack;
    TileLayer* tileLayer = nullptr;
    std::map<uint64_t, float> widestTag;           // per setting: its tag at its widest label
    std::map<uint64_t, tags::ValueList> lists;     // per setting: what its tag steps through
    std::optional<TagRef> panelTag;                // the tag whose panel is open
    /// Panels hold this weakly, so a panel left open after the editor closes calls nothing.
    std::shared_ptr<DeviceStrip*> self;
};

} // namespace tessera
