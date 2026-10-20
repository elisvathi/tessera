#include "DawProject.hpp"

#include "Arrangement.hpp"
#include "AutomationEdits.hpp"
#include "Zip.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>

namespace tessera::dawproject {

using render::ExportNote;
using render::Options;

namespace {

/// How far a thinned automation curve may stray from the sampled one (lane values).
constexpr float kCurveTolerance = 0.002f;

/// Numbers with a dot whatever the locale (printf would follow it), up to 6 decimals.
std::string num(double v) {
    long long scaled = std::llround(v * 1e6);
    bool negative = scaled < 0;
    if (negative)
        scaled = -scaled;
    std::string s = (negative ? "-" : "") + std::to_string(scaled / 1000000);
    long long fraction = scaled % 1000000;
    if (fraction == 0)
        return s + ".0";
    char digits[8];
    std::snprintf(digits, sizeof(digits), "%06lld", fraction);
    std::string f = digits;
    while (f.back() == '0')
        f.pop_back();
    return s + "." + f;
}

std::string beats(double ticks) { return num(ticks / double(kPpq)); }

std::string color(uint32_t rgb) {
    char text[8];
    std::snprintf(text, sizeof(text), "#%06x", unsigned(rgb & 0xFFFFFF));
    return text;
}

std::string escape(const std::string& s) {
    std::string out;
    for (char c : s) {
        switch (c) {
        case '&':
            out += "&amp;";
            break;
        case '<':
            out += "&lt;";
            break;
        case '>':
            out += "&gt;";
            break;
        case '"':
            out += "&quot;";
            break;
        default:
            // Control characters aren't allowed in XML 1.0, except tab, newline and return.
            if (uint8_t(c) >= 0x20 || c == '\t' || c == '\n' || c == '\r')
                out += c;
        }
    }
    return out;
}

using Attrs = std::vector<std::pair<const char*, std::string>>;

/// Indented XML, element by element.
class Xml {
  public:
    void open(const char* tag, const Attrs& attrs = {}) {
        start(tag, attrs);
        text += ">\n";
        depth++;
    }
    void leaf(const char* tag, const Attrs& attrs = {}) {
        start(tag, attrs);
        text += "/>\n";
    }
    void close(const char* tag) {
        depth--;
        indent();
        text += std::string("</") + tag + ">\n";
    }
    /// A unique ID for the next element that needs one.
    std::string id() { return "id" + std::to_string(next++); }

    std::string text = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n";

  private:
    void indent() { text.append(size_t(depth) * 2, ' '); }
    void start(const char* tag, const Attrs& attrs) {
        indent();
        text += std::string("<") + tag;
        for (const auto& [name, value] : attrs)
            text += std::string(" ") + name + "=\"" + escape(value) + "\"";
    }
    int depth = 0;
    int next = 0;
};

/// `channel` is the track's (0-based); with `noteChannels` each note goes on its own instead.
void notesElement(Xml& x, const std::vector<ExportNote>& notes, double origin, int channel, bool noteChannels) {
    x.open("Notes", {{"id", x.id()}});
    for (const ExportNote& n : notes)
        x.leaf("Note", {{"time", beats(n.start - origin)},
                        {"duration", beats(std::max(1.0, n.end - n.start))},
                        {"channel", std::to_string(noteChannels ? std::clamp(int(n.channel), 1, 16) - 1 : channel)},
                        {"key", std::to_string(std::clamp(n.pitch, 0, 127))},
                        {"vel", num(std::clamp(n.velocity, 0.f, 1.f))},
                        {"rel", num(std::clamp(n.release, 0.f, 1.f))}});
    x.close("Notes");
}

/// The clips of one track's arrangement: one per arranger instance. A note belongs to the instance it starts in
/// (or, for echoes and arpeggios that run past an instance, the one before it), and a clip grows to hold its
/// notes, up to the next instance.
void arrangementClips(Xml& x, const Song& song, const Track& t, const Options& o, const render::Context& ctx,
                      int channel) {
    std::vector<const ArrangerClip*> instances;
    for (const ArrangerClip& a : t.arrangement)
        if (!a.muted || o.includeMuted)
            instances.push_back(&a);
    if (instances.empty())
        return;
    std::stable_sort(instances.begin(), instances.end(),
                     [](const ArrangerClip* a, const ArrangerClip* b) { return a->position < b->position; });
    std::vector<std::vector<ExportNote>> notes(instances.size());
    for (const ExportNote& n : render::trackNotes(song, t.id, o, ctx)) {
        int owner = -1;
        for (size_t i = 0; i < instances.size(); i++) {
            if (double(instances[i]->position) > n.start)
                break;
            owner = int(i);
            if (n.start < double(instanceEnd(*instances[i])))
                break;
        }
        if (owner >= 0)
            notes[size_t(owner)].push_back(n);
    }
    x.open("Clips", {{"id", x.id()}});
    for (size_t i = 0; i < instances.size(); i++) {
        const ArrangerClip& a = *instances[i];
        const Clip* clip = song.findClip(a.clip);
        double start = double(a.position);
        double end = double(instanceEnd(a));
        for (const ExportNote& n : notes[i])
            end = std::max(end, n.end);
        if (i + 1 < instances.size() && double(instances[i + 1]->position) > double(instanceEnd(a)))
            end = std::min(end, double(instances[i + 1]->position));
        end = std::max(end, double(instanceEnd(a)));
        Attrs attrs{{"time", beats(start)}, {"duration", beats(end - start)}, {"playStart", "0.0"}};
        if (a.muted)
            attrs.push_back({"enable", "false"});
        if (clip) {
            attrs.push_back({"name", clip->name.empty() ? "Clip" : clip->name});
            attrs.push_back({"color", color(clip->color ? clip->color : t.color)});
        }
        x.open("Clip", attrs);
        notesElement(x, notes[i], start, channel, o.noteChannels);
        x.close("Clip");
    }
    x.close("Clips");
}

/// AUTO lanes as controller automation, sampled as in MIDI export and thinned.
void automationPoints(Xml& x, const Song& song, const Track& t, const Options& o, int channel) {
    if (o.automation == render::AutomationMode::Off)
        return;
    Tick end = render::rangeOf(song, Options()).second;
    for (int output = 0; output < render::kLanes; output++) {
        std::vector<std::pair<Tick, float>> samples = render::laneSamples(song, t.id, output, o, 0, end);
        if (samples.empty())
            continue;
        autoEdits::Points points;
        for (const auto& [tick, v] : samples) {
            AutoPoint p;
            p.t = tick;
            p.value = v;
            points.push_back(p);
        }
        points = autoEdits::simplify(points, kCurveTolerance);
        x.open("Points", {{"id", x.id()}, {"unit", "normalized"}});
        x.leaf("Target", {{"expression", "channelController"},
                          {"channel", std::to_string(channel)},
                          {"controller", std::to_string(std::clamp(o.laneCc[size_t(output)], 0, 119))}});
        for (const AutoPoint& p : points)
            x.leaf("RealPoint", {{"time", beats(double(p.t))}, {"value", num(p.value)}, {"interpolation", "linear"}});
        x.close("Points");
    }
}

} // namespace

std::string projectXml(const Song& song, const Options& o, const render::Context& ctx, const std::string& version) {
    Xml x;
    x.open("Project", {{"version", "1.0"}});
    x.leaf("Application", {{"name", "Tessera"}, {"version", version}});
    x.open("Transport");
    x.leaf("Tempo", {{"id", x.id()},
                     {"name", "Tempo"},
                     {"unit", "bpm"},
                     {"value", num(ctx.tempo)},
                     {"min", "20.0"},
                     {"max", "300.0"}});
    x.leaf("TimeSignature", {{"id", x.id()},
                             {"numerator", std::to_string(song.timeSig.num)},
                             {"denominator", std::to_string(song.timeSig.den)}});
    x.close("Transport");

    std::vector<const Track*> tracks;
    for (const Track& t : song.tracks)
        if (!t.mute || o.includeMuted)
            tracks.push_back(&t);
    std::map<uint64_t, std::string> trackIds;
    x.open("Structure");
    for (const Track* t : tracks) {
        std::string id = x.id();
        trackIds[t->id.value] = id;
        x.open(
            "Track",
            {{"id", id}, {"name", t->name}, {"color", color(t->color)}, {"contentType", "notes"}, {"loaded", "true"}});
        x.open("Channel", {{"id", x.id()}, {"role", "regular"}, {"solo", t->solo ? "true" : "false"}});
        x.leaf("Mute", {{"id", x.id()}, {"name", "Mute"}, {"value", t->mute ? "true" : "false"}});
        x.close("Channel");
        x.close("Track");
    }
    x.close("Structure");

    x.open("Arrangement", {{"id", x.id()}});
    x.open("Lanes", {{"id", x.id()}, {"timeUnit", "beats"}});
    for (const Track* t : tracks) {
        int channel = render::channelOf(song, o, t->id) - 1;
        x.open("Lanes", {{"id", x.id()}, {"track", trackIds[t->id.value]}});
        arrangementClips(x, song, *t, o, ctx, channel);
        automationPoints(x, song, *t, o, channel);
        x.close("Lanes");
    }
    x.close("Lanes");
    if (!song.markers.empty()) {
        x.open("Markers", {{"id", x.id()}});
        for (const Marker& m : song.markers) {
            Attrs attrs{{"time", beats(double(m.position))}, {"name", m.name.empty() ? "Marker" : m.name}};
            if (m.color)
                attrs.push_back({"color", color(m.color)});
            x.leaf("Marker", attrs);
        }
        x.close("Markers");
    }
    x.close("Arrangement");

    x.open("Scenes");
    for (size_t s = 0; s < song.scenes.size(); s++) {
        const Scene& scene = song.scenes[s];
        Attrs attrs{{"id", x.id()}, {"name", scene.name.empty() ? "Scene " + std::to_string(s + 1) : scene.name}};
        if (scene.color)
            attrs.push_back({"color", color(scene.color)});
        x.open("Scene", attrs);
        x.open("Lanes", {{"id", x.id()}, {"timeUnit", "beats"}});
        for (const Track* t : tracks) {
            const Clip* clip = s < t->slots.size() && t->slots[s].clip ? song.findClip(*t->slots[s].clip) : nullptr;
            if (!clip)
                continue;
            ClipSpan span = clipSpan(*clip);
            int channel = render::channelOf(song, o, t->id) - 1;
            x.open("ClipSlot", {{"id", x.id()}, {"track", trackIds[t->id.value]}, {"hasStop", "true"}});
            Attrs clipAttrs{{"time", "0.0"},
                            {"duration", beats(double(span.end))},
                            {"playStart", "0.0"},
                            {"name", clip->name.empty() ? "Clip" : clip->name},
                            {"color", color(clip->color ? clip->color : t->color)}};
            if (span.looping) {
                clipAttrs.push_back({"loopStart", beats(double(span.start))});
                clipAttrs.push_back({"loopEnd", beats(double(span.end))});
            }
            x.open("Clip", clipAttrs);
            notesElement(x, render::clipNotes(song, t->id, clip->id, o, ctx), 0, channel, o.noteChannels);
            x.close("Clip");
            x.close("ClipSlot");
        }
        x.close("Lanes");
        x.close("Scene");
    }
    x.close("Scenes");
    x.close("Project");
    return x.text;
}

std::string metadataXml(const render::Context& ctx) {
    std::string title = ctx.title.empty() ? "Tessera" : ctx.title;
    return "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<MetaData>\n  <Title>" + escape(title) +
           "</Title>\n</MetaData>\n";
}

std::string build(const Song& song, const Options& o, const render::Context& ctx, const std::string& version) {
    return zip::store({{"project.xml", projectXml(song, o, ctx, version)}, {"metadata.xml", metadataXml(ctx)}});
}

} // namespace tessera::dawproject
