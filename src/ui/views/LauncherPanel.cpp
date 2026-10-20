#include "LauncherPanel.hpp"

#include "../../model/LauncherCommands.hpp"
#include "../../module/TesseraModule.hpp"
#include "../Theme.hpp"
#include "../editing/ClipActions.hpp"
#include "../widgets/Draw.hpp"
#include "../widgets/MenuTextField.hpp"
#include "Arranger.hpp"
#include "ArrangerLanes.hpp"
#include "SlotDraw.hpp"

#include <cmath>

namespace tessera {

using rack::math::Rect;
using rack::math::Vec;

namespace {

constexpr float kMinWidth = LauncherPanel::kSlotWidth + LauncherPanel::kEdge;
constexpr float kInset = 2;
constexpr float kQuietAlpha = 0.55f; // slots that don't play while the timeline does

/// A name field in its own menu, for the scene menu's "Rename".
void openRename(const std::string& title, const std::string& current, std::function<void(const std::string&)> done) {
    rack::ui::Menu* menu = rack::createMenu();
    menu->addChild(rack::createMenuLabel(title));
    auto* field = new MenuTextField(current, std::move(done));
    menu->addChild(field);
    APP->event->setSelectedWidget(field);
}

const Clip* slotClip(const Song& song, const Track& t, int scene) {
    if (scene < 0 || scene >= int(t.slots.size()) || !t.slots[size_t(scene)].clip)
        return nullptr;
    return song.findClip(*t.slots[size_t(scene)].clip);
}

} // namespace

// ---- Geometry -----------------------------------------------------------------------------------------------

int LauncherPanel::sceneCount() {
    TesseraModule* m = ctx.module();
    return m ? int(m->song().scenes.size()) : 0;
}

Rect LauncherPanel::cellRect(int row, int scene) {
    float top = rowsTop + float(owner.tracks.trackTop(row));
    float h = std::min(float(owner.tracks.trackHeight(row)) - 2 * kInset, kMaxSlotHeight);
    return Rect(columnX(scene) + kInset, top + kInset, kSlotWidth - 2 * kInset, std::max(4.f, h));
}

LauncherPanel::Hit LauncherPanel::hitTest(Vec pos) {
    Hit hit;
    if (pos.x >= box.size.x - kEdge) {
        hit.part = Hit::Part::Edge;
        return hit;
    }
    int scene = int(std::floor((pos.x + scrollX) / kSlotWidth));
    if (pos.y < rowsTop) {
        if (pos.y < headerTop)
            return hit;
        if (scene == sceneCount() && pos.x - columnX(scene) < kAddSceneWidth && scene < kMaxScenes) {
            hit.part = Hit::Part::AddScene;
            return hit;
        }
        if (scene < 0 || scene >= sceneCount())
            return hit;
        hit.scene = scene;
        Rect r = sceneHeaderRect(scene);
        hit.part = pos.x - r.pos.x < r.size.y ? Hit::Part::SceneLaunch : Hit::Part::SceneName;
        return hit;
    }
    if (scene < 0 || scene >= sceneCount())
        return hit;
    hit.scene = scene;
    int row = owner.tracks.trackAt(pos.y - rowsTop);
    if (row < 0 || row >= int(owner.rows.size()) || owner.rows[size_t(row)].kind != TrackRow::Kind::Track)
        return hit;
    Rect cell = cellRect(row, scene);
    if (pos.y > cell.getBottom())
        return hit;
    hit.row = row;
    hit.track = owner.rows[size_t(row)].track;
    hit.part = slotDraw::launchRect(cell).contains(pos) ? Hit::Part::Launch : Hit::Part::Body;
    return hit;
}

// ---- Dragging -----------------------------------------------------------------------------------------------

/// Drags a slot's clip: onto the timeline it is placed there (one undo step, as the arranger previews it); onto
/// another slot it moves there, or is copied with Ctrl/Cmd or Alt.
class SlotDragGesture : public DragGesture {
  public:
    SlotDragGesture(LauncherPanel& panel, TrackId track, int scene, ClipId clip, Vec press)
        : panel(panel), lanes(*panel.owner.lanesView()), track(track), scene(scene), clip(clip), press(press) {}

    void update(Vec pos, Modifiers mods) override {
        current = pos;
        copy = copy || mods.ctrl || mods.alt; // stays a copy once asked for, like note and clip drags
        target = LauncherPanel::Hit();
        onTimeline = false;
        Vec lanePos = pos.plus(panel.box.pos).minus(lanes.box.pos);
        if (lanePos.x >= 0 && lanePos.y >= 0 && lanePos.x < lanes.box.size.x && lanePos.y < lanes.box.size.y) {
            placeOnTimeline(lanePos, mods);
            return;
        }
        lanes.clearPreview();
        if (pos.x >= 0 && pos.x < panel.box.size.x) {
            LauncherPanel::Hit h = panel.hitTest(pos);
            if (h.part == LauncherPanel::Hit::Part::Launch || h.part == LauncherPanel::Hit::Part::Body)
                target = h;
        }
    }
    void commit() override {
        TesseraModule* m = panel.ctx.module();
        if (onTimeline) {
            lanes.clearPreview();
            if (m)
                lanes.commit("Place clip", before, result, {placed});
            return;
        }
        if (!m || !target.track.valid() || (target.track == track && target.scene == scene))
            return;
        clipActions::move(panel.ctx, track, scene, target.track, target.scene, copy);
    }
    void cancel() override { lanes.clearPreview(); }
    std::string readout() const override {
        if (onTimeline)
            return "Place at bar " +
                   std::to_string(int(at / ticksPerBar(lanes.context().module()->song().timeSig)) + 1);
        if (target.track.valid())
            return copy ? "Copy to slot" : "Move to slot";
        return "";
    }
    CursorShape cursor() const override { return CursorShape::Move; }
    void drawOverlay(NVGcontext* vg) override {
        if (!target.track.valid())
            return;
        const theme::Palette& c = theme::colors();
        draw::roundedRectStroke(vg, panel.cellRect(target.row, target.scene), theme::metrics::kRadiusItem, c.accent, 2);
    }

  private:
    void placeOnTimeline(Vec lanePos, Modifiers mods) {
        int row = lanes.rowAt(lanePos.y);
        TrackId to = lanes.trackOfRow(row);
        const Song& song = lanes.context().module()->song();
        const Clip* c = song.findClip(clip);
        if (!to.valid() || !c) {
            lanes.clearPreview();
            return;
        }
        if (!placed.valid())
            placed = lanes.newInstanceId();
        before = lanes.committed();
        ClipSpan span = clipSpan(*c);
        at = lanes.snapTick(lanes.time().xToTick(lanePos.x), mods.alt, {});
        arrange::Placed p{to, ArrangerClip{}};
        p.clip.id = placed;
        p.clip.clip = clip;
        p.clip.position = std::max<Tick>(0, at);
        p.clip.length = std::max<Tick>(1, span.length());
        p.clip.offset = span.start;
        arrange::Layout after = before;
        after.push_back(p);
        result = lanes.settle(after, {placed}, [this] { return lanes.newInstanceId(); });
        lanes.setPreview(result);
        onTimeline = true;
    }

    LauncherPanel& panel;
    ArrangerLanes& lanes;
    TrackId track;
    int scene;
    ClipId clip;
    Vec press, current;
    bool copy = false, onTimeline = false;
    LauncherPanel::Hit target;
    ArrangementId placed;
    Tick at = 0;
    arrange::Layout before, result;
};

/// The panel's right edge: drag to show more or fewer scenes.
class PanelResizeGesture : public DragGesture {
  public:
    PanelResizeGesture(LauncherPanel& panel, Vec press) : panel(panel), press(press), start(panel.box.size.x) {
        panel.ctx.view().clipsWide = false;
    }
    void update(Vec pos, Modifiers) override {
        float most = panel.contentWidth();
        panel.ctx.view().launcherPanelWidth = std::clamp(start + pos.x - press.x, kMinWidth, std::max(kMinWidth, most));
    }
    CursorShape cursor() const override { return CursorShape::ResizeH; }

  private:
    LauncherPanel& panel;
    Vec press;
    float start;
};

std::unique_ptr<DragGesture> LauncherPanel::startGesture(Vec pos, int button, Modifiers mods) {
    if (button != GLFW_MOUSE_BUTTON_LEFT || !ctx.module())
        return nullptr;
    Hit h = hitTest(pos);
    if (h.part == Hit::Part::Edge)
        return std::make_unique<PanelResizeGesture>(*this, pos);
    if (h.part != Hit::Part::Body)
        return nullptr; // the launch button is a click
    const Track* t = ctx.module()->song().findTrack(h.track);
    const Clip* clip = t ? slotClip(ctx.module()->song(), *t, h.scene) : nullptr;
    if (!clip)
        return nullptr;
    // The press selects, as in the arranger: a click without a drag ends the gesture, so click() never runs. A slot's
    // clip has no timeline length, so the timeline's selection goes (the piano roll would show its length).
    clipActions::select(ctx, h.track, clip->id);
    ctx.state().clipSelection.clear();
    return std::make_unique<SlotDragGesture>(*this, h.track, h.scene, clip->id, pos);
}

// ---- Clicks -------------------------------------------------------------------------------------------------

void LauncherPanel::click(Vec pos, int button, Modifiers mods) {
    TesseraModule* m = ctx.module();
    if (!m)
        return;
    Hit h = hitTest(pos);
    const Song& song = m->song();
    EditorContext* c = &ctx;
    if (h.part == Hit::Part::AddScene) {
        if (button == GLFW_MOUSE_BUTTON_LEFT)
            clipActions::insertScene(ctx, sceneCount());
        return;
    }
    if (h.part == Hit::Part::SceneLaunch || h.part == Hit::Part::SceneName) {
        if (button == GLFW_MOUSE_BUTTON_RIGHT) {
            rack::ui::Menu* menu = rack::createMenu();
            int scene = h.scene;
            clipActions::appendSceneMenu(menu, ctx, scene, [c, scene] {
                TesseraModule* mod = c->module();
                if (!mod || scene >= int(mod->song().scenes.size()))
                    return;
                openRename("Scene name", mod->song().scenes[size_t(scene)].name, [c, scene](const std::string& name) {
                    TesseraModule* m2 = c->module();
                    if (!m2 || scene >= int(m2->song().scenes.size()))
                        return;
                    Scene after = m2->song().scenes[size_t(scene)];
                    after.name = name;
                    m2->perform(setScene(m2->song(), scene, after, "Rename scene"));
                });
            });
        } else if (h.part == Hit::Part::SceneLaunch) {
            m->launchScene(h.scene);
        }
        return;
    }
    if (!h.track.valid())
        return;
    const Track* t = song.findTrack(h.track);
    const Clip* clip = t ? slotClip(song, *t, h.scene) : nullptr;
    if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        rack::ui::Menu* menu = rack::createMenu();
        if (clip)
            clipActions::appendClipMenu(menu, ctx, h.track, h.scene);
        else
            clipActions::appendEmptySlotMenu(menu, ctx, h.track, h.scene);
        return;
    }
    if (button != GLFW_MOUSE_BUTTON_LEFT)
        return;
    if (h.part == Hit::Part::Launch) {
        if (clip)
            m->launchSlot(h.track, h.scene);
        else
            m->stopTrack(h.track, h.scene);
        return;
    }
    if (clip)
        clipActions::select(*c, h.track, clip->id);
    else
        ctx.state().selectedTrack = h.track;
}

void LauncherPanel::doubleClick(Vec pos, Modifiers mods) {
    TesseraModule* m = ctx.module();
    Hit h = hitTest(pos);
    if (!m || !h.track.valid())
        return;
    const Track* t = m->song().findTrack(h.track);
    if (t && !slotClip(m->song(), *t, h.scene))
        clipActions::create(ctx, h.track, h.scene);
}

void LauncherPanel::wheel(Vec pos, Vec delta, Modifiers mods) {
    ctx.touchNavigation();
    float most = std::max(0.f, contentWidth() - box.size.x);
    if (mods.shift || delta.x != 0) {
        scrollX = std::clamp(scrollX - (mods.shift ? delta.y : 0) - delta.x, 0.f, most);
        if (!mods.shift)
            owner.lanesView()->setVerticalScroll(owner.lanesView()->verticalScroll() - delta.y);
        return;
    }
    owner.lanesView()->setVerticalScroll(owner.lanesView()->verticalScroll() - delta.y);
}

CursorShape LauncherPanel::hoverCursor(Vec pos, Modifiers mods) {
    hover = hitTest(pos);
    switch (hover.part) {
    case Hit::Part::Edge:
        return CursorShape::ResizeH;
    case Hit::Part::Launch:
    case Hit::Part::SceneLaunch:
    case Hit::Part::AddScene:
        return CursorShape::Hand;
    default:
        return CursorShape::Arrow;
    }
}

void LauncherPanel::onLeave(const LeaveEvent& e) {
    hover = Hit();
    InteractiveView::onLeave(e);
}

// ---- Drawing ------------------------------------------------------------------------------------------------

Rect LauncherPanel::sceneHeaderRect(int scene) {
    return Rect(columnX(scene) + kInset, headerTop + kInset, kSlotWidth - 2 * kInset,
                std::max(8.f, rowsTop - headerTop - 3 * kInset));
}

void LauncherPanel::drawSceneHeaders(NVGcontext* vg) {
    const theme::Palette& c = theme::colors();
    const Song& song = ctx.module()->song();
    draw::TextStyle name{theme::fonts::uiStrong(), theme::metrics::kTextRuler, c.text1,
                         NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE};
    for (int s = 0; s < sceneCount(); s++) {
        float x = columnX(s);
        if (x + kSlotWidth < 0 || x > box.size.x)
            continue;
        Rect r = sceneHeaderRect(s);
        bool hot = hover.scene == s && (hover.part == Hit::Part::SceneLaunch || hover.part == Hit::Part::SceneName);
        draw::roundedRect(vg, r, theme::metrics::kRadiusItem, hot ? c.bg2 : c.bg1);
        const Scene& scene = song.scenes[size_t(s)];
        if (scene.color)
            draw::roundedRect(vg, Rect(r.pos.x, r.getBottom() - 2, r.size.x, 2), 1, theme::track(scene.color));
        float tri = r.size.y;
        slotDraw::drawTriangle(vg, Vec(r.pos.x + tri / 2, r.getCenter().y), 7,
                               hot && hover.part == Hit::Part::SceneLaunch ? c.text0 : c.text1);
        std::string label = scene.name.empty() ? std::to_string(s + 1) : scene.name;
        draw::text(vg, name, r.pos.x + tri, r.getCenter().y, draw::ellipsize(vg, name, label, r.size.x - tri - 4));
    }
    if (sceneCount() < kMaxScenes) {
        Rect r = sceneHeaderRect(sceneCount());
        r.size.x = kAddSceneWidth - 2 * kInset;
        bool hot = hover.part == Hit::Part::AddScene;
        draw::roundedRect(vg, r, theme::metrics::kRadiusItem, hot ? c.bg2 : c.bg1);
        draw::TextStyle plus{theme::fonts::uiStrong(), theme::metrics::kTextHeader, hot ? c.text0 : c.text2,
                             NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE};
        draw::text(vg, plus, r.getCenter().x, r.getCenter().y, "+");
    }
}

void LauncherPanel::drawView(const DrawArgs& args) {
    NVGcontext* vg = args.vg;
    const theme::Palette& c = theme::colors();
    nvgBeginPath(vg);
    nvgRect(vg, 0, 0, box.size.x, box.size.y);
    NVGcolor background = nvgLerpRGBA(c.bg0, c.bg1, 0.5f);
    nvgFillColor(vg, background);
    nvgFill(vg);
    TesseraModule* m = ctx.module();
    if (!m)
        return;
    bool quiet = m->playMode() == PlayMode::Arranger;
    nvgSave(vg);
    nvgScissor(vg, 0, 0, box.size.x - kEdge, box.size.y);
    drawSceneHeaders(vg);
    nvgIntersectScissor(vg, 0, rowsTop, box.size.x, box.size.y - rowsTop);
    for (size_t row = 0; row < owner.rows.size() && row < owner.tracks.heights().size(); row++) {
        float top = rowsTop + float(owner.tracks.trackTop(int(row)));
        float h = float(owner.tracks.trackHeight(int(row)));
        if (top > box.size.y || top + h < rowsTop)
            continue;
        nvgBeginPath(vg);
        nvgRect(vg, 0, top + h - 1, box.size.x, 1);
        nvgFillColor(vg, c.line0);
        nvgFill(vg);
        const TrackRow& r = owner.rows[row];
        if (r.kind != TrackRow::Kind::Track)
            continue;
        LauncherStatus status = m->launcherStatus(r.track);
        for (int s = 0; s < sceneCount(); s++) {
            float x = columnX(s);
            if (x + kSlotWidth < 0 || x > box.size.x)
                continue;
            bool hovered = hover.row == int(row) && hover.scene == s;
            Rect cell = cellRect(int(row), s);
            slotDraw::drawSlot(vg, ctx, cell, r.track, s, status, hovered, hovered && hover.part == Hit::Part::Launch);
            // While the timeline plays the clips step back, but a clip that took its track over stays lit.
            if (quiet && status.playing != s && status.queued != s) {
                nvgBeginPath(vg);
                nvgRect(vg, cell.pos.x - kInset, cell.pos.y - kInset, cell.size.x + 2 * kInset,
                        cell.size.y + 2 * kInset);
                nvgFillColor(vg, nvgTransRGBAf(background, kQuietAlpha));
                nvgFill(vg);
            }
        }
    }
    nvgRestore(vg);
    // The resize edge, and a line where the timeline starts.
    nvgBeginPath(vg);
    nvgRect(vg, box.size.x - 1, 0, 1, box.size.y);
    nvgFillColor(vg, hover.part == Hit::Part::Edge ? c.accent : c.line1);
    nvgFill(vg);
}

} // namespace tessera
