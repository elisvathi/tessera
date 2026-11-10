#include "AudioClipDraw.hpp"

#include "../../model/AudioRegions.hpp"

#include "../../engine/AudioSnapshotBuilder.hpp"
#include "../../module/TesseraModule.hpp"
#include "../Theme.hpp"
#include "../widgets/Draw.hpp"

#include <cmath>

namespace tessera::audioDraw {

ClipState stateOf(TesseraModule& module, const Clip& clip) {
    ClipState s;
    if (!clip.audio)
        return s;
    s.source = module.song().findSource(clip.audio->source);
    s.files = regions::filesOf(module.song(), *clip.audio);
    AudioStore* store = module.audioStore();
    if (!s.source) {
        s.status.state = SourceState::Missing;
        return s;
    }
    if (!store) {
        s.status.state = SourceState::Loading;
        return s;
    }
    s.status = store->status(clip.audio->source);
    for (const AudioTake& t : clip.audio->takes)
        s.takes.push_back(store->status(t.source));
    RenderKey key;
    double tempo = module.tempo() > 0 ? module.tempo() : 120;
    if (wantsRender(*clip.audio, tempo, key)) {
        s.render = clip.audio->warps.empty() ? store->renderStatus(key) : store->warpedRenderStatus(key);
        s.pitched = key.semitones != 0;
    }
    return s;
}

void drawWaveform(NVGcontext* vg, rack::math::Rect body, float left, float right, const ClipState& state,
                  float gainDb, NVGcolor color, const FramesAt& framesAt) {
    if (body.size.y < 3)
        return;
    if (state.status.state == SourceState::Missing || state.status.state == SourceState::Failed) {
        // Stripes: nothing to play here.
        nvgSave(vg);
        nvgIntersectScissor(vg, body.pos.x, body.pos.y, body.size.x, body.size.y);
        nvgBeginPath(vg);
        float step = theme::metrics::kHatchSpacing;
        for (float x = body.pos.x - body.size.y; x < body.getRight(); x += step) {
            nvgMoveTo(vg, x, body.getBottom());
            nvgLineTo(vg, x + body.size.y, body.pos.y);
        }
        nvgStrokeColor(vg, nvgTransRGBAf(color, 0.35f));
        nvgStrokeWidth(vg, 1);
        nvgStroke(vg);
        nvgRestore(vg);
        return;
    }
    const PeakCache* peaks = state.status.peaks.get();
    if (!peaks || peaks->empty())
        return;
    float gain = dbToGain(gainDb);
    int channels = std::min(2, peaks->channels());
    float laneH = body.size.y / float(channels);
    float x0 = std::max(left, std::floor(body.pos.x)), x1 = std::min(right, body.getRight());
    nvgBeginPath(vg);
    for (int ch = 0; ch < channels; ch++) {
        float mid = body.pos.y + laneH * (float(ch) + 0.5f);
        float half = laneH / 2 - 0.5f;
        for (float x = x0; x < x1; x += 1) {
            Column column;
            if (!framesAt(x, column))
                continue;
            float lo, hi;
            const PeakCache* source = column.peaks ? column.peaks : peaks;
            source->range(std::min(ch, source->channels() - 1), std::min(column.from, column.to),
                          std::max(column.from, column.to), lo, hi);
            float columnGain = column.extraDb != 0 ? gain * dbToGain(column.extraDb) : gain;
            lo = std::clamp(lo * columnGain, -1.f, 1.f);
            hi = std::clamp(hi * columnGain, -1.f, 1.f);
            float y0 = mid - hi * half, y1 = mid - lo * half;
            nvgRect(vg, x, y0, 1, std::max(0.75f, y1 - y0));
        }
    }
    nvgFillColor(vg, color);
    nvgFill(vg);
}

bool framesOfTicks(const Clip& clip, const ClipState& state, double u0, double u1, Column& column) {
    const AudioContent& a = *clip.audio;
    const regions::Files& files = state.files;
    if (a.regions.empty()) {
        double perTick = framesPerTick(a, files.rate);
        if (a.reverse) {
            double end = double(a.sourceStart) + double(clip.length) * perTick;
            column.from = end - u1 * perTick;
            column.to = end - u0 * perTick;
        } else {
            column.from = warpedFrame(a, u0, files.rate);
            column.to = warpedFrame(a, u1, files.rate);
        }
        return true;
    }
    const AudioRegion* r = regions::at(a.regions, u0);
    if (!r)
        return false;
    column.extraDb = r->gainDb;
    double first = regions::frameAt(a, *r, u0, files);
    double last = regions::frameAt(a, *r, std::min(u1, double(r->end())), files);
    column.from = std::min(first, last);
    column.to = std::max(first, last);
    if (r->take >= 0)
        column.peaks = state.statusOf(r->take).peaks.get();
    return r->take < 0 || column.peaks;
}

std::string dbText(float db) {
    if (db <= kSilentDb)
        return "-inf dB";
    char buf[32];
    snprintf(buf, sizeof buf, "%+.1f dB", double(db));
    return buf;
}

std::string titleMarks(const Clip& clip, const ClipState& state, float gainDb) {
    if (!clip.audio)
        return "";
    std::string out;
    if (clip.audio->follow == TempoFollow::Off) {
        // The UI fonts have no lock symbol; the words say the same.
        out = "own speed";
    } else {
        char buf[24];
        double t = clip.audio->sourceTempo;
        snprintf(buf, sizeof buf, std::abs(t - std::round(t)) < 0.05 ? "%.0f \xE2\x96\xBE" : "%.1f \xE2\x96\xBE", t);
        out = buf;
    }
    if (clip.audio->reverse)
        out = "\xE2\x97\x80 " + out;
    if (std::abs(gainDb) >= 0.05f)
        out += "  " + dbText(gainDb);
    if (state.source && state.source->fileChannels > state.source->channels)
        out += "  " + std::to_string(state.source->fileChannels) + "\xE2\x86\x92" + std::to_string(state.source->channels);
    return out;
}

std::string bigTitleMarks(const Clip& clip, const ClipState& state, float gainDb, TimeSig timeSig) {
    if (!clip.audio)
        return "";
    const AudioContent& a = *clip.audio;
    char buf[64];
    if (a.follow == TempoFollow::Off) {
        snprintf(buf, sizeof buf, "own speed \xC2\xB7 %.1f s", double(clip.length) * secondsPerTick(a.sourceTempo));
    } else {
        double bars = double(clip.length) / double(ticksPerBar(timeSig));
        double beats = double(clip.length) / double(ticksPerBeat(timeSig));
        char length[24];
        if (bars >= 1)
            snprintf(length, sizeof length, std::abs(bars - std::round(bars)) < 0.01 ? "%.0f bar%s" : "%.2g bar%s",
                     bars, bars == 1 ? "" : "s");
        else
            snprintf(length, sizeof length, "%.2g beat%s", beats, beats == 1 ? "" : "s");
        static const char* const kFollow[] = {"Stretch", "Repitch", "Off"};
        double t = a.sourceTempo;
        snprintf(buf, sizeof buf,
                 std::abs(t - std::round(t)) < 0.05 ? "%.0f BPM \xC2\xB7 %s \xC2\xB7 %s \xE2\x96\xBE"
                                                    : "%.1f BPM \xC2\xB7 %s \xC2\xB7 %s \xE2\x96\xBE",
                 t, length, kFollow[int(a.follow)]);
    }
    std::string out = buf;
    if (a.reverse)
        out = "\xE2\x97\x80 " + out;
    if (std::abs(gainDb) >= 0.05f)
        out += "  " + dbText(gainDb);
    if (state.source && state.source->fileChannels > state.source->channels)
        out +=
            "  " + std::to_string(state.source->fileChannels) + "\xE2\x86\x92" + std::to_string(state.source->channels);
    return out;
}

std::string pitchText(const AudioContent& a) {
    char buf[32];
    if (a.cents == 0)
        snprintf(buf, sizeof buf, a.semitones == 0 ? "0 st" : "%+d st", int(a.semitones));
    else
        snprintf(buf, sizeof buf, "%+d st %+.0f ct", int(a.semitones), double(a.cents));
    return buf;
}

std::string mixLaneText(const AutomationEnvelope& lane, float value) {
    if (lane.drive == LaneDrive::Volume || lane.drive == LaneDrive::Send)
        return dbText(faderDbOfPos(value));
    if (lane.drive != LaneDrive::Pan)
        return "";
    float pan = value * 2 - 1;
    if (std::abs(pan) < 0.005f)
        return "Centre";
    char text[24];
    snprintf(text, sizeof text, "%.0f%% %s", double(std::abs(pan) * 100), pan < 0 ? "L" : "R");
    return text;
}

std::string stateText(const ClipState& state) {
    switch (state.status.state) {
    case SourceState::Loading:
        return "Loading " + std::to_string(int(state.status.progress * 100)) + "%";
    case SourceState::Missing:
        return "Missing file";
    case SourceState::Failed:
        return "Can't read file";
    case SourceState::NoMemory:
        return "Not enough memory";
    case SourceState::Ready:
        break;
    }
    if (state.render.state == RenderState::Waiting || state.render.state == RenderState::Rendering)
        return "\xE2\x80\xA6 " + std::to_string(int(state.render.progress * 100)) + "%";
    if (state.render.state == RenderState::Failed)
        return "!";
    return "";
}

bool rendering(const ClipState& state) {
    return state.status.state == SourceState::Ready &&
           (state.render.state == RenderState::Waiting || state.render.state == RenderState::Rendering);
}

std::string renderingText(const ClipState& state) {
    return (state.pitched ? "Shifting pitch " : "Stretching ") + std::to_string(int(state.render.progress * 100)) + "%";
}

void drawRenderBar(NVGcontext* vg, rack::math::Rect bar, const ClipState& state, NVGcolor color) {
    // The dark backing keeps it readable over a waveform, on a clip of any colour.
    draw::roundedRect(vg, bar, 1, nvgTransRGBAf(theme::colors().bg0, 0.8f));
    draw::roundedRect(vg, bar, 1, nvgTransRGBAf(color, 0.25f));
    rack::math::Rect done = bar;
    done.size.x *= std::clamp(state.render.progress, 0.f, 1.f);
    if (done.size.x >= 1)
        draw::roundedRect(vg, done, 1, color);
}

bool stateWarns(const ClipState& state) {
    return state.status.state == SourceState::Missing || state.status.state == SourceState::Failed ||
           state.status.state == SourceState::NoMemory || state.render.state == RenderState::Failed;
}

} // namespace tessera::audioDraw
