#pragma once

#include "../../model/DeviceCommands.hpp"
#include "../../model/DeviceLookup.hpp"
#include "../editing/DeviceActions.hpp"
#include "../interaction/DeviceTags.hpp"
#include "../interaction/InteractiveView.hpp"
#include "../interaction/Shortcuts.hpp"
#include "../widgets/HoverTooltip.hpp"

#include <array>
#include <map>
#include <memory>
#include <optional>
#include <vector>

namespace tessera {

class TileLayer;
class CachedLayer;

/// The selected track's devices, in a strip of fixed height above its notes (docs/DEVICES.md §1.1): on the left the
/// song's modulator section, the TRACK | CLIP switch and the track's (or clip's) section, then the device cards left to
/// right in the order notes flow, each with its own modulator section on its left edge (docs/DECISIONS.md §163). Each
/// card shows what its device does as one to three small pictures, with its settings as tags on them that you drag,
/// scroll, or click for a panel. A playhead's own devices sit in a frame in its colour after the Playheads card.
/// Closed, a thin bar names the chain.
class DeviceStrip : public InteractiveView {
  public:
    explicit DeviceStrip(EditorContext& ctx);

    /// A card's height (header, pictures, padding), and the room above and below the row for a playhead frame.
    static constexpr float kCardHeight = 198;
    static constexpr float kTop = 10;
    /// The open strip's height: one row of cards, which scrolls sideways when they don't fit.
    static constexpr float kOpenHeight = kCardHeight + 2 * kTop;
    /// The notes need this much under the strip; with less room the open strip takes the whole pane (devices only).
    static constexpr float kMinNotes = 110;
    /// Card indices from here on are devices in the shown playhead's branch (index - kBranchBase).
    static constexpr int kBranchBase = 1000;
    static constexpr float kClosedHeight = 24;
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
    /// Where the open modulator's settings sit, right of its section (view coordinates), or nothing.
    std::optional<rack::math::Rect> settingsRect() const;
    /// Where the scrolling chain starts: what lies left of it stays put.
    float chainStart() const { return chainLeft; }

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
            TagBadge,     // the "A" on an automated tag: shows its lane
            Insert,       // the + on a link: adds a device there
            AddTile,      // "+ Add device" at the chain's end
            Tile,         // a picture: a click selects its device
            Tag,          // a setting on a picture
            Switch,       // an on/off setting on a picture
            PlayheadChip, // one playhead of a Playheads device: click to show its settings
            AddPlayhead,
            Frame,       // the shown playhead's own devices, in its colour; only drawn
            FramePlus,   // the round + at the frame's end
            Slot,        // the empty frame: a dashed slot that adds the playhead's first own device
            MotionNotes, // a Motion card's "6 notes": selects them in the piano roll
            MotionJoin,  // a Motion card's "+ Selected": the selected notes join it
            MotionLeave, // a Motion card's "− Selected": the selected notes leave it
            SourceCard,  // where the shown clip's notes enter: which chain they go through (only drawn)
            SourceTrack, // its "Track" half: the clip plays through the track's devices
            SourceOwn,   // its "Own" half: the clip plays through its own (the first time a copy of the track's)
            ChainStub,   // the chain not shown, folded at the end: a click opens it or switches to it
            ModSection,  // a modulator section (song, track or clip, a device's): only drawn, its parts take clicks
            ModFold,     // a section's fold arrow: open it, or fold it to a rail
            Settings,    // the room for the open modulator's settings, right of its section
            DiceRoll,    // a random device's dice: rolls another number and keeps it
            DiceFixed,   // its switch's number: the same every time (double-click to type one)
            DiceNew,     // its switch's New: new every play and loop pass
            PatternGrid,   // an Arpeggiator on Pattern: its line's note rows and value rows (Vel, Gate, Oct, Semi)
            PatternColumn, // its settings beside the grid (Pattern, Rate, Swing, Hold), one under another
            PatternReset   // "Reset" in its header: a plain rising line again
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
        bool fixed = false;  // in the part left of the chain, which never scrolls
        uint64_t section = 0; // modulator parts: the section they belong to (sectionKey)
    };

    // What running gestures show before they are committed.
    std::optional<std::pair<ParamRef, float>> knobPreview;   // a tag being dragged or previewed (normalized)
    std::optional<std::pair<ParamRef, float>> amountPreview; // the mapped modulator's amount on a tag
    std::optional<std::pair<uint64_t, int>> directionPreview; // a playhead's Direction tag (its 0..7 value)
    std::optional<std::pair<uint64_t, uint16_t>> channelPreview; // a Note Filter's channels being typed
    int dropIndex = -1;                                          // where a dragged card would land
    /// A modulator being dragged to another section: which, where the pointer is, whether Ctrl copies it, and the
    /// section under the pointer (its key, 0 for none) with how it would land there.
    struct ModulatorDragShown {
        uint64_t id = 0;
        rack::math::Vec pos;
        bool copy = false;
        uint64_t section = 0;
        ModulatorLanding landing;
    };
    std::optional<ModulatorDragShown> modulatorDrag;
    /// Ends a modulator drag at the section under the pointer: moves it there, or copies it with `copy`.
    void dropModulator(const ModulatorDragShown& drag);
    /// The section whose box holds `pos`, as its key; 0 when none.
    uint64_t sectionUnder(rack::math::Vec pos) const;
    /// Where the modulators of the section with key `key` live, if it was laid out this frame.
    std::optional<ModulatorPlace> placeOfSection(uint64_t key) const {
        auto it = sectionPlaces.find(key);
        return it != sectionPlaces.end() ? std::optional<ModulatorPlace>(it->second) : std::nullopt;
    }
    /// Draws a modulator drag: the row left behind, the section under the pointer and what lands there, the row.
    void drawModulatorDrag(NVGcontext* vg, const ModulatorDragShown& drag);
    /// A Pattern drag on the Arpeggiator `patternPreviewDevice`: the step settings it sets (knob -> display value)
    /// and a row length it sets (row -1: the notes; 0..3: Vel, Gate, Oct, Semi).
    std::map<int, float> stepPreview;
    std::optional<std::pair<int, int>> lengthPreview;
    /// A note row being painted: the Arpeggiator's ID and its line as the drag would leave it.
    std::optional<std::pair<uint64_t, std::vector<uint8_t>>> lineDraft;
    /// Sets an Arpeggiator's Pattern line as one undo step named `name`; the dice switch then reads Yours.
    void commitLine(DeviceId device, const std::vector<uint8_t>& line, const std::string& name);
    uint64_t patternPreviewDevice = 0;
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
    /// Sets a tag back to its default (a channel set to any channel) as one undo step: what a double-click does.
    void resetTag(const TagRef& t);
    /// An Arpeggiator setting as it shows (display units): a running drag's value, else what plays now (a lane or a
    /// modulator may be moving it), else the saved one.
    float patternValue(DeviceId device, int knob);
    /// Sets some Pattern settings (display values by knob) as one undo step. A device still on the line made from
    /// the held notes gets its own line first, written by its dice, so what is edited is what plays.
    void commitPattern(DeviceId device, const std::vector<std::pair<int, float>>& settings, const std::string& name);
    /// A Pattern row's length (row -1: the notes, 0..3 a value row), 1..16 steps, as one undo step.
    void setPatternLength(DeviceId device, int row, int length);
    /// "Reset": notes 1 2 3 4 over 16 steps, every value row back to its default and 16 steps long.
    void resetPattern(DeviceId device);

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
    /// The devices of the chain shown: the shown clip's own, or the track's (deviceActions::shownChain).
    const std::vector<Device>& chain();
    /// The shown clip's colour, which its own devices wear.
    NVGcolor clipTint();
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
    /// The chain under the pictures (frames, links, cards, the source card) and the still tags over them: each
    /// painted into its own cached layer, which repaints only when its key from stripKeys() changes. Live tags are
    /// drawn over them every frame instead.
    void paintChain(NVGcontext* vg);
    void paintTags(NVGcontext* vg);
    /// The keys of the chain and tag layers: everything they draw from (the song, the layout, focus, previews, and
    /// what is hovered in each). Also marks which tags are live.
    std::pair<uint64_t, uint64_t> stripKeys();
    /// Whether a tag part shows a setting a modulator or a lane moves, so its value changes while playing.
    bool isLive(const Part& p) const;
    std::vector<bool> tagLive; // per tag in `tagRefs`, from stripKeys()
    /// One modulator section to lay out: where its modulators live, which they are, and what its header says.
    struct SectionSpec {
        ModulatorPlace place;
        std::vector<const Modulator*> mods;
        std::string label;
        std::string railLabel;
        uint64_t key = 0;
    };
    /// Lays out a section at `x` from `top`, `height` tall: open, its header (fold arrow, label, +) over a row per
    /// modulator; folded, a rail with a dot per modulator and a small upright fader per Macro. Returns its width.
    float layoutSection(const SectionSpec& spec, float x, float top, float height, float width, bool open, bool fixed,
                        int device);
    /// A folded song, track or clip section (docs/DECISIONS.md §167): its fold arrow and icon at the top, its word
    /// under them only when it fits whole, then a dot per modulator, or rows of small dots when the rail is short,
    /// and + at the bottom when there is room. Clears its label in `sectionLabels` when the word doesn't fit.
    void layoutIconRail(const SectionSpec& spec, size_t label, float x, float top, float height, int device);
    /// Words show on every folded song or track rail and in the switch, or on none, so the bars always match: clears
    /// the rails' words unless all of them, and the switch's, fit a strip part `height` tall.
    void fitRailWords(bool withSwitch, float height);
    bool switchWords = true; // from fitRailWords: the switch shows TRACK and CLIP under its icons
    /// Where a section's new modulators go: its level, and for a device section the device.
    ModulatorPlace sectionPlace(const Part& p);
    /// The sections in the part that stays put (`fixed`), or the cards' sections, over what is drawn under them.
    void drawSections(NVGcontext* vg, bool fixed);
    /// The upright TRACK | CLIP switch: which devices the shown clip plays through.
    void drawSwitch(NVGcontext* vg);

    // ---- Selecting, copying and pasting (docs/DECISIONS.md §163) ----
    /// What a click on a card (Card), a modulator row (ModulatorRow) or a whole section (ModSection, its key in
    /// `section`) does to the selection: alone, Ctrl adds or removes one, Shift takes the run from the last one, as
    /// with tracks and clips. Song modulators never share a selection with the rest.
    void selectInStrip(Part::Kind kind, uint64_t id, uint64_t section, Modifiers mods);
    /// Whether a part is selected: its card, its row, or its whole section.
    bool partSelected(const Part& p) const;
    /// The selection as copies, with a label that names it ("Arpeggiator + Env 1").
    struct Copied {
        std::vector<Device> devices;
        std::vector<Modulator> modulators, songModulators;
        std::string label;
        bool empty() const { return devices.empty() && modulators.empty() && songModulators.empty(); }
    };
    Copied gatherSelection();
    /// Puts the selection on the strip's clipboard; says so unless `quiet`.
    void copySelection(bool quiet = false);
    /// Copies the selection and removes it, as one undo step (Ctrl+X while the strip has the focus).
    void cutSelection();
    /// Selects every card of the shown chain (Ctrl+A while the strip has the focus).
    void selectAllInStrip();
    /// Pastes `items` into the shown chain, its devices from `index` on (-1 = the end), as one undo step; the
    /// message starts with `verb` ("Pasted", "Duplicated"). With `into`, every modulator goes into that section.
    void pasteItems(const Copied& items, int index, const char* verb = "Pasted",
                    std::optional<ModulatorPlace> into = std::nullopt);
    /// The section a paste puts modulators into, from what was right-clicked or is selected (docs/DECISIONS.md
    /// §182): a section, a modulator row's section, or a card's own section while only modulators are copied.
    /// None: the usual places (the track's or clip's section, and the song's for song modulators).
    std::optional<ModulatorPlace> pasteTarget(const Part* clicked);
    /// "the song", "the track", "the clip" or the device's name, for the Paste item.
    std::string placeName(const ModulatorPlace& place);
    /// Removes the selection as one undo step; the message starts with `verb` ("Deleted", "Cut").
    void deleteSelection(const char* verb = "Deleted");
    /// Where pasted devices go after card `cardIndex` (-1: after the last selected card, else at the end).
    int pasteIndexAfter(int cardIndex);
    /// An Arpeggiator on Pattern: its settings in a slim column, then the line's grid; returns where the grid ends.
    float layoutPattern(const Device& d, int index, float x, float top, NVGcolor tint);
    /// How many held-note rows an Arpeggiator's Pattern grid shows (4 to 8, docs/DECISIONS.md §171): the most notes
    /// its clips send it at once, the most it has held while playing this session, and the highest row its line uses.
    int patternNotes(const Device& d, int index);
    std::map<uint64_t, int> arpHeard;                           // per Arpeggiator: the most notes it held
    std::map<uint64_t, std::pair<uint64_t, int>> arpNotesCounted; // per Arpeggiator: song revision + 1, notes in
    /// A click in the grid: on the Move row's name it cycles the row's unit (semitones, key steps, chord notes).
    void editPatternGrid(const Part& grid, rack::math::Vec pos);
    void drawPatternGrid(NVGcontext* vg, const Part& grid);
    /// Copy and Paste for a right-click menu, and with `full` Duplicate and Delete too (a card's or a modulator's
    /// own menu has those already); pasted devices land at `pasteIndex`, modulators in `into` when set.
    void appendEditItems(rack::ui::Menu* menu, int pasteIndex, bool full = true,
                         std::optional<ModulatorPlace> into = std::nullopt);
    void drawCard(NVGcontext* vg, const Part& card);
    void drawPlayheadChip(NVGcontext* vg, const Device& d, const Part& part, bool hot);
    /// Shows the tooltip for the part under the mouse: a playhead chip's dot or name.
    void updateTip();
    void drawTag(NVGcontext* vg, const Part& part, bool hot);
    /// The folded block of the chain not shown.
    void drawStub(NVGcontext* vg, const Part& part, bool hot);
    void sampleScopes();
    /// The tint of the card with index `index`: the accent, a Motion's colour, or a playhead's.
    NVGcolor tintOf(const Device& d, int index);

    /// Shows the pictures laid out this frame.
    void showTiles();

    std::vector<Part> parts;
    std::vector<TagRef> tagRefs; // the tags of this frame, which parts point into
    /// A picture laid out this frame, shown by showTiles once the whole row is laid out.
    struct PendingTile {
        int device, tile;
        uint64_t key, hash;
        DeviceType type;
        tags::Values values;
        NVGcolor tint;
        float width, alpha;
    };
    std::vector<PendingTile> pending;

  private:
    int hovered = -1;
    HoverTooltip tip{""};
    std::string tipText; // what the tooltip says now; empty when none shows
    float scrollX = 0, contentWidth = 0;
    std::map<uint64_t, float> sectionScroll; // per section key: how far its rows are scrolled
    std::vector<std::string> sectionLabels;  // this frame's section labels, which ModSection parts point into
    std::map<uint64_t, ModulatorPlace> sectionPlaces; // this frame's sections by key: where their modulators live
    float chainLeft = 0; // where the scrolling chain starts (the modulator list stays put)
    // Each modulator's last outputs (0..1 for drawing), for its row's little graph.
    std::map<uint64_t, std::array<float, 40>> scopes;
    int scopeHead = 0;
    TrackId shownTrack;
    ClipId shownClip; // another clip shown goes back to its own chain (EditorContext::trackChainOpened)
    TileLayer* tileLayer = nullptr;
    CachedLayer* chainLayer = nullptr;
    CachedLayer* tagLayer = nullptr;
    std::map<uint64_t, float> widestTag;           // per setting: its tag at its widest label
    std::map<uint64_t, tags::ValueList> lists;     // per setting: what its tag steps through
    std::optional<TagRef> panelTag;                // the tag whose panel is open
    /// Panels hold this weakly, so a panel left open after the editor closes calls nothing.
    std::shared_ptr<DeviceStrip*> self;
};

} // namespace tessera
