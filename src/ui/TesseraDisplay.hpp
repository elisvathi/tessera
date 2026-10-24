#pragma once

#include "../model/Types.hpp"

#include <rack.hpp>

#include <string>
#include <vector>

namespace tessera {

class TesseraModule;
struct Song;

/// The panel screen: a preview of the music. The top line names the instance and what plays; below it the song
/// (a small timeline while the timeline plays, the clip grid while clips play) and the selected track's notes as a
/// tiny piano roll whose keys light under the playhead; the bottom line has the position, the selected clip and
/// the tempo. Works without a module too (module browser preview), showing a fresh song.
class TesseraDisplay : public rack::widget::Widget {
  public:
    explicit TesseraDisplay(TesseraModule* module) : module(module) {}

    void draw(const DrawArgs& args) override;
    void drawLayer(const DrawArgs& args, int layer) override;

  private:
    struct MiniNote {
        Tick start, end;
        int pitch;
    };
    /// The timeline, flattened once per song change.
    struct Lane {
        uint32_t color;
        bool audible;
        std::vector<std::pair<Tick, Tick>> clips;
        std::vector<MiniNote> notes;
        int low, high; // pitch range, for placing the note dashes
    };
    /// The selected clip's notes, in clip ticks.
    struct Roll {
        ClipId clip;
        TrackId track;
        uint32_t color = 0;
        std::string title;
        Tick start = 0, end = 0; // the part that plays: in a lane cycle, the page of it the playhead is on
        int64_t page = 0;        // which clip-length page of a lane cycle is shown
        int low = 60, high = 72;
        std::vector<MiniNote> notes;
    };

    const Song& song() const;
    void refresh();
    double clipPlayhead() const;

    void drawHeader(NVGcontext* vg, rack::math::Rect r);
    void drawTimeline(NVGcontext* vg, rack::math::Rect r);
    void drawClipGrid(NVGcontext* vg, rack::math::Rect r);
    void drawRoll(NVGcontext* vg, rack::math::Rect r);
    void drawFooter(NVGcontext* vg, rack::math::Rect r);

    TesseraModule* module;
    uint64_t revision = ~uint64_t(0);
    std::vector<Lane> lanes;
    Tick songEnd = 0;
    Roll roll;
};

} // namespace tessera
