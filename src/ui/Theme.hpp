#pragma once

#include <rack.hpp>

#include <string>

#include <cstdint>
#include <memory>

/// Every color, size and font used by Tessera's UI. Views never hard-code these values.
namespace tessera::theme {

struct Palette {
    NVGcolor bg0;    // canvas: grids, timeline, slots
    NVGcolor bg1;    // surface: panels, headers, toolbars
    NVGcolor bg2;    // raised controls, hover
    NVGcolor line0;  // sub-grid lines
    NVGcolor line1;  // beat lines
    NVGcolor line2;  // bar lines
    NVGcolor text0;  // primary text
    NVGcolor text1;  // secondary text
    NVGcolor text2;  // disabled, hints
    NVGcolor accent; // focus, selection, primary actions
    NVGcolor record;
    NVGcolor play;
    NVGcolor queue;
    NVGcolor select; // selection outlines and rubber bands
    NVGcolor ground; // behind the panes
    NVGcolor ink;    // text and marks on an accent or state fill
    NVGcolor onTile; // text and marks on a track-coloured note, clip or slot
    bool dark;
};

/// The editor's colour schemes. Follow Rack uses Plaster with light panels and Slate with dark ones.
enum class Scheme {
    FollowRack,
    Plaster,
    Sand,
    Slate,
    Gruvbox,
    Nord,
    Catppuccin,
    OneDark,
    Monokai,
    Carbon,
    TokyoNight,
    RosePine,
    Kanagawa
};
constexpr Scheme kSchemes[] = {Scheme::FollowRack, Scheme::Plaster, Scheme::Sand,       Scheme::Slate,
                               Scheme::Carbon,     Scheme::Gruvbox, Scheme::Nord,       Scheme::Catppuccin,
                               Scheme::OneDark,    Scheme::Monokai, Scheme::TokyoNight, Scheme::RosePine,
                               Scheme::Kanagawa};

Scheme scheme();
void setScheme(Scheme s);
const char* schemeName(Scheme s);
/// The name the plugin settings store, so adding or removing a scheme never changes what a saved choice means.
const char* schemeId(Scheme s);
/// The scheme saved as `id`; Follow Rack for an unknown one.
Scheme schemeFromId(const std::string& id);

/// The editor's palette, from the chosen scheme.
const Palette& colors();
/// Module panels: the chosen scheme, so with Follow Rack they follow Rack's "prefer dark panels" setting like
/// every other module, and with a scheme picked they wear it.
const Palette& panelColors();
/// Module displays: a dark screen in every scheme.
const Palette& screenColors();
/// Changes whenever any palette above resolves differently, so cached drawings know to redraw.
uint64_t generation();

/// The piano roll's keys: ivory and ebony, a little softer on dark schemes so they don't glare.
struct KeyColors {
    NVGcolor ivory;      // white key top
    NVGcolor ivoryShade; // white key back end and front lip
    NVGcolor gap;        // between white keys
    NVGcolor ebony;      // black key top
    NVGcolor ebonyDeep;  // black key back end
    NVGcolor gloss;      // black key front edge
    NVGcolor label;      // C names on white keys
    NVGcolor labelSoft;  // other names on white keys
};
const KeyColors& keyColors();

/// The device strip's cards and pictures (docs/DEVICES.md §1.1), made from the editor's palette so they follow every
/// scheme. A picture draws notes going in as `ghost` outlines and notes coming out in its card's colour.
struct DeviceColors {
    NVGcolor ground;    // behind the cards
    NVGcolor card;      // a card
    NVGcolor cardEdge;  // its outline
    NVGcolor screen;    // a picture's background
    NVGcolor shade;     // the inner shadow at a picture's top
    NVGcolor tag;       // a tag's pill
    NVGcolor tagHover;  // a hovered tag
    NVGcolor tagEdge;   // its outline
    NVGcolor ghost;     // notes going in
    NVGcolor ghostDash; // notes left out (drawn dashed)
    NVGcolor base;      // the time line under a picture
    NVGcolor tick;      // small marks on it
    NVGcolor guide;     // grey arrows, crosses and stems
    NVGcolor key;       // a piano key that is out of the key
    NVGcolor keyBlack;
    NVGcolor caption;
    NVGcolor link;      // the line from card to card
    NVGcolor stepUp;    // a Pattern step moved up (octaves, semitones)
    NVGcolor stepDown;  // and down
    float glow;         // how strongly coloured shapes glow (less on light schemes)
};
const DeviceColors& deviceColors();

/// "Colour scheme" items: one check item per scheme.
void appendSchemeMenu(rack::ui::Menu* menu);

/// A track or clip colour as stored in the song, tuned for the editor's scheme: deeper on light ones.
NVGcolor track(uint32_t color, float alpha = 1.f);
/// The master's colour where a track would show its own: its lane, its Track Out screen.
constexpr uint32_t kMasterColor = 0xC9CED6;

NVGcolor rgb(uint32_t hex, float alpha = 1.f);

/// `base` sunk a little: darker rows and lanes. `strength` is for dark schemes; light ones need far less.
NVGcolor recessed(const Palette& p, NVGcolor base, float strength);

/// Each modulator's colour: its rows, its rings on the knobs it moves, its mapping highlight. Picked from a set
/// that stays clear of the accent and of each other, by modulator ID so it never changes.
NVGcolor modulatorColor(uint64_t modulatorId);
/// A take's colour in the audio editor, by its place among the clip's takes (-1 is the clip's own).
inline NVGcolor takeColor(int take) { return modulatorColor(uint64_t(take + 2)); }
/// A playhead's colour, by its colour index (docs/design/PLAYHEADS.md §3.2): its chip, its line over the notes.
NVGcolor playheadColor(int index);

/// The "Own" mark of a clip with its own devices: light on a dark backing in every scheme, so it reads on any clip
/// colour. Lit: a light pill with dark ink; unlit: a dark pill ringed in light.
struct OwnMarkColors {
    NVGcolor lit;
    NVGcolor litInk;
    NVGcolor unlit;
    NVGcolor unlitInk;
    NVGcolor edge;
};
const OwnMarkColors& ownMarkColors();

namespace fonts {
std::shared_ptr<rack::window::Font> ui();
std::shared_ptr<rack::window::Font> uiStrong();
std::shared_ptr<rack::window::Font> mono();
} // namespace fonts

/// Sizes in px at 100% scale.
namespace metrics {
constexpr float kSpacing = 4;
constexpr float kRadiusControl = 4;
constexpr float kRadiusItem = 3; // notes and clips
// Ties in the piano roll (docs/design/TIES_VOICES.md): the joint dot, glide handles, the line and a same-pitch arc
constexpr float kTieDot = 3.5f;
constexpr float kTieHandle = 3;
constexpr float kTieLine = 2;
constexpr float kTieArc = 6;
// Clip control in the launcher (DECISIONS §179): the grid controller's window, and a learned slot's dot
constexpr float kControlWindowLine = 1.5f;
constexpr float kLearnedMark = 2.5f;
// A key held on a live input: its outline on the keyboard and drum lanes, and the faint band along its grid row
constexpr float kInputMarkWidth = 1.5f;
constexpr float kInputRowAlpha = 0.08f;

constexpr float kTextUi = 11;
constexpr float kTextHeader = 12;
constexpr float kTextRuler = 10;

// Editor layout
constexpr float kTransportHeight = 40;
constexpr float kMarkerRow = 14;
constexpr float kLoopStrip = 12;
constexpr float kPlayheadMarkWidth = 12; // a clip tile's stripe per playhead, when playheads play it
constexpr float kPlayheadMarkHeight = 3;
// An audio clip's gain is a line across its timeline body, from this tall: drawn when off 0 dB, faint on hover at 0 dB
// (just under the title). The mouse catches it this far above or below.
constexpr float kLevelLineMinBody = 10;
constexpr float kLevelLineHit = 3;
constexpr float kOwnMarkWidth = 16; // the device-card mark of a clip with its own devices
constexpr float kOwnMarkHeight = 12;
constexpr float kOwnMarkSmallWidth = 12; // its small form, on a slot too narrow for the card beside the name
constexpr float kOwnMarkSmallHeight = 9;
constexpr float kOwnMarkCardRoom = 100; // the slot body from which the card fits beside the name
// A clip's next action on its slot (DECISIONS §184): a small tag with the action's icon and its count ("2/4" while it
// plays) while the name keeps kNextMarkNameRoom; on a narrower slot the icon alone, bare, while the name keeps
// kNextMarkBareNameRoom; then nothing.
constexpr float kNextMarkIcon = 9;
constexpr float kNextMarkBareNameRoom = 18;
constexpr float kNextMarkPad = 3;
constexpr float kNextMarkHeight = 14;
constexpr float kNextMarkNameRoom = 36;
constexpr float kNextMarkStroke = 1.3f;
constexpr float kNextMarkFill = 0.14f;        // the tag's tint over the clip colour
constexpr float kNextMarkFillPlaying = 0.24f; // stronger while the clip counts
constexpr float kNextMarkHeldAlpha = 0.45f;   // its icon and count while next actions are held
// A track's "back to timeline" button in the clips panel (DECISIONS §185): its icon, faint while the track already
// plays the timeline.
constexpr float kBackIcon = 11;
constexpr float kBackIdleAlpha = 0.35f;
constexpr float kPartlyOutline = 1.3f; // the panel's TIMELINE while some tracks play launched clips instead
// The clips panel is a grid (DECISIONS §188): a slot fills its track's row, cells kSlotGap apart. Its launch button
// is a strip down its whole left side (§191), square in a short slot and at most kSlotLaunchWidth wide. From
// kSlotNameRowFrom tall the name gets a row kSlotNameRow high and the notes fill the room under it; shorter slots
// keep everything on one line.
constexpr float kSlotGap = 1;
constexpr float kSlotLaunchWidth = 32;
constexpr float kSlotNameRow = 22;
constexpr float kSlotNameRowFrom = 52; // a usual (32 px) track's slots stay one line
// A clip its playheads play draws one band per playhead instead of its notes, a block wherever the playhead is over a
// note, sampled every kPlayheadPathStep pixels: with lines between the bands from kPlayheadBand tall each, thin bands
// down to kPlayheadThinBand, and below that every playhead's blocks laid over the whole clip.
constexpr float kPlayheadBand = 7;
constexpr float kPlayheadThinBand = 3;
constexpr float kPlayheadPathStep = 2;
constexpr float kSegmentGrip = 6; // the solid grips at the ends of the selected playhead's segment
constexpr float kLevelLineMinHeight = 40; // shorter rows have no room for the second line
constexpr float kSegmentRow = 16; // the piano roll ruler's row of playhead segments, when the track has playheads
constexpr float kBranchCap = 3;   // the coloured cap on a device in a playhead's own chain
constexpr float kTimeRuler = 20;
constexpr float kOverview = 8;
// The in-panel editor's thinner ruler strips: a module is only 380 px tall.
constexpr float kMarkerRowPanel = 11;
constexpr float kLoopStripPanel = 9;
constexpr float kTimeRulerPanel = 14;
constexpr float kOverviewPanel = 4;
constexpr float kSplitter = 6; // hit area; the drawn line is 1 px
constexpr float kFoldedPaneBar = 22; // what a pane folds to while the other is maximised: its switch (DECISIONS §190)
constexpr float kEdgeHit = 6;  // resize hot zone at item edges
constexpr float kKeyboardWidth = 56;
// A drum track's lane headers stand in for the keys: name, output, mute and solo need the room.
constexpr float kDrumHeaderWidth = 168;
constexpr float kDrumHeaderWidthPanel = 124;
constexpr float kDrumMinRow = 22; // lanes open at least this tall, so the names stay readable
constexpr float kDrumMinRowPanel = 16;
constexpr float kDrumCellInset = 1.5f; // the gap around each step cell, so steps read one by one
constexpr float kDrumMinCell = 5;      // narrower steps draw as beat bands instead of cells
constexpr float kInspectorWidth = 240;
constexpr float kPaneHeader = 26;
constexpr float kReadoutOffset = 16; // readout pill distance from the cursor

// Module panels (panel px; 1 mm ≈ 2.95 px)
constexpr float kPanelLabel = 7.5f;
constexpr float kPanelLabelSpacing = 0.6f; // letter spacing for uppercase labels
constexpr float kPanelTitle = 12;
constexpr float kPanelDisplayRadius = 3;
constexpr float kPanelPlateRadius = 4;
constexpr float kPanelGroupTitlePad = 3;       // room on each side of a group box's title, cut into its top edge
constexpr float kPanelLabelAboveJackMm = 6.4f; // label baseline offset above a jack's center
constexpr float kDisplaySmall = 9;
constexpr float kDisplayLarge = 22;
constexpr float kStatusBar = 3; // colored strip at the top of an output display

// The device strip (docs/DEVICES.md §1.1): cards of pictures, each picture with its tags on it.
constexpr float kDeviceTile = 150;        // a picture's height (the Arpeggiator's Pattern grid needs it)
constexpr float kDeviceTileRadius = 9;
constexpr float kDeviceTileGap = 8;       // between the pictures of a card
constexpr float kDeviceCardHeader = 30;
constexpr float kDeviceCardRadius = 12;
constexpr float kDeviceCardTop = 6;       // room above the header
constexpr float kDeviceCardPad = 12;      // left, right and below the pictures
constexpr float kDeviceTag = 22;          // a tag's height
constexpr float kDeviceTagInset = 9;      // tags from a picture's edges
constexpr float kDeviceTagGap = 6;
constexpr float kDeviceTagText = 12;
constexpr float kDeviceCaptionText = 10.5f;
constexpr float kDeviceLink = 26;         // the gap between cards, with its line
constexpr float kDeviceIo = 34;           // "in" and "out"
constexpr float kDeviceSpine = 26;        // a playhead frame's side, with "<Name> only" written down it
constexpr float kDeviceSlot = 46;         // an empty playhead frame
constexpr float kDeviceFrameOut = 7;      // how far a playhead frame reaches past its cards
constexpr float kDevicePlus = 24;         // the round + at a frame's end
constexpr float kDeviceGlow = 5;          // a coloured shape's glow

// Audio clips (docs/design/AUDIO_MIXER.md §6.1)
constexpr float kAudioHandle = 7;          // fade handles and curve dots, as drawn
constexpr float kAudioHandleHit = 12;      // their hit area
constexpr float kAudioHandleMinBody = 16;  // bodies shorter than this hide the handles (the inspector has them)
constexpr float kAudioHandleMinWidth = 40; // and so do narrower clips
constexpr float kAudioHandleTopZone = 10;  // the top of the body belongs to the fade handles, not the edges
constexpr float kWaveformAlpha = 0.85f;    // the waveform over a clip body
constexpr float kHatchSpacing = 6;         // the stripes of a clip whose file is missing
} // namespace metrics

} // namespace tessera::theme
