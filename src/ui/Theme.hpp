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
enum class Scheme { FollowRack, Plaster, Sand, Slate, Gruvbox, Nord, Catppuccin, OneDark, Monokai };
constexpr Scheme kSchemes[] = {Scheme::FollowRack, Scheme::Plaster,    Scheme::Sand,    Scheme::Slate,  Scheme::Gruvbox,
                               Scheme::Nord,       Scheme::Catppuccin, Scheme::OneDark, Scheme::Monokai};

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

/// "Colour scheme" items: one check item per scheme.
void appendSchemeMenu(rack::ui::Menu* menu);

/// A track or clip colour as stored in the song, tuned for the editor's scheme: deeper on light ones.
NVGcolor track(uint32_t color, float alpha = 1.f);

NVGcolor rgb(uint32_t hex, float alpha = 1.f);

/// `base` sunk a little: darker rows and lanes. `strength` is for dark schemes; light ones need far less.
NVGcolor recessed(const Palette& p, NVGcolor base, float strength);

/// Each modulator's colour: its rows, its rings on the knobs it moves, its mapping highlight. Picked from a set
/// that stays clear of the accent and of each other, by modulator ID so it never changes.
NVGcolor modulatorColor(uint64_t modulatorId);

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

constexpr float kTextUi = 11;
constexpr float kTextHeader = 12;
constexpr float kTextRuler = 10;

// Editor layout
constexpr float kTransportHeight = 40;
constexpr float kMarkerRow = 14;
constexpr float kLoopStrip = 12;
constexpr float kTimeRuler = 20;
constexpr float kOverview = 8;
// The in-panel editor's thinner ruler strips: a module is only 380 px tall.
constexpr float kMarkerRowPanel = 11;
constexpr float kLoopStripPanel = 9;
constexpr float kTimeRulerPanel = 14;
constexpr float kOverviewPanel = 4;
constexpr float kSplitter = 6; // hit area; the drawn line is 1 px
constexpr float kEdgeHit = 6;  // resize hot zone at item edges
constexpr float kKeyboardWidth = 56;
// A drum track's lane headers stand in for the keys: name, output, mute and solo need the room.
constexpr float kDrumHeaderWidth = 168;
constexpr float kDrumHeaderWidthPanel = 124;
constexpr float kDrumMinRow = 22; // lanes open at least this tall, so the names stay readable
constexpr float kDrumMinRowPanel = 16;
constexpr float kInspectorWidth = 240;
constexpr float kPaneHeader = 26;
constexpr float kReadoutOffset = 16; // readout pill distance from the cursor

// Module panels (panel px; 1 mm ≈ 2.95 px)
constexpr float kPanelLabel = 7.5f;
constexpr float kPanelLabelSpacing = 0.6f; // letter spacing for uppercase labels
constexpr float kPanelTitle = 12;
constexpr float kPanelDisplayRadius = 3;
constexpr float kPanelPlateRadius = 4;
constexpr float kPanelLabelAboveJackMm = 6.4f; // label baseline offset above a jack's center
constexpr float kDisplaySmall = 9;
constexpr float kDisplayLarge = 22;
constexpr float kStatusBar = 3; // colored strip at the top of an output display
} // namespace metrics

} // namespace tessera::theme
