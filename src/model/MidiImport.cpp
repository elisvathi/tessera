#include "MidiImport.hpp"

#include "DrumKit.hpp"
#include "MidiFile.hpp"
#include "TrackColors.hpp"

#include <algorithm>
#include <cmath>
#include <deque>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>

namespace tessera::midiImport {

namespace {

/// A note waiting for its note-off, and what its key played meanwhile (in file ticks).
struct OpenNote {
    Tick start = 0;
    float velocity = 0;
    TimedValues pitch, timbre, pressure;
};

/// The notes of one part of one file track while it is read: open notes wait for their note-off.
struct Building {
    std::vector<Note> notes;
    std::map<int, std::deque<OpenNote>> open; // by channel * 128 + pitch, oldest first
    std::vector<std::pair<MidiControl, TimedValues>> controls;
};

/// The note an open key becomes once it ends at `end` (file ticks), with its curves.
Note finishNote(const OpenNote& o, int pitch, Tick end, float release, const std::function<Tick(Tick)>& ticks) {
    Note n;
    n.start = ticks(o.start);
    n.length = std::max<Tick>(1, ticks(end) - n.start);
    n.pitch = int8_t(pitch);
    n.velocity = o.velocity;
    n.releaseVelocity = release;
    double length = double(std::max<Tick>(1, end - o.start));
    n.pitchExpr = noteCurve(NoteCurve::Pitch, o.pitch, double(o.start), length);
    n.timbreExpr = noteCurve(NoteCurve::Timbre, o.timbre, double(o.start), length);
    n.pressureExpr = noteCurve(NoteCurve::Pressure, o.pressure, double(o.start), length);
    return n;
}

/// The control a channel value belongs to.
MidiControl controlOf(const MidiEvent& e) {
    switch (e.control) {
    case ChannelControl::PitchBend:
        return {MidiControl::Kind::PitchBend, 0};
    case ChannelControl::Pressure:
        return {MidiControl::Kind::Pressure, 0};
    case ChannelControl::Cc:
    default:
        return {MidiControl::Kind::Cc, e.cc};
    }
}

/// A drum lane for each pitch the part uses: General MIDI names where the kit knows the pitch, in the kit's order,
/// then the others by pitch. At most kMaxDrumLanes; notes past that have no lane and stay silent.
std::vector<DrumLane> lanesFor(const Part& part) {
    std::vector<int> used;
    for (const Note& n : part.notes)
        if (std::find(used.begin(), used.end(), int(n.pitch)) == used.end())
            used.push_back(n.pitch);
    std::vector<DrumLane> lanes;
    for (const DrumLane& known : drumKit(DrumKitPreset::Gm16)) {
        auto it = std::find(used.begin(), used.end(), int(known.pitch));
        if (it != used.end()) {
            lanes.push_back(known);
            used.erase(it);
        }
    }
    std::sort(used.begin(), used.end());
    for (int pitch : used) {
        DrumLane lane;
        lane.name = "Note " + std::to_string(pitch);
        lane.pitch = int8_t(pitch);
        lanes.push_back(lane);
    }
    if (lanes.size() > size_t(kMaxDrumLanes))
        lanes.resize(size_t(kMaxDrumLanes));
    for (size_t i = 0; i < lanes.size(); i++)
        lanes[i].outputIndex = int(i);
    return lanes;
}

} // namespace

bool read(const std::vector<uint8_t>& bytes, Imported& out) {
    smf::File file;
    // A negative division (top bit set) counts SMPTE frames, which have no beats to put on Tessera's grid.
    if (!smf::read(bytes, file) || file.ppq <= 0 || (file.ppq & 0x8000))
        return false;
    out = Imported();
    std::function<Tick(Tick)> ticks = [&](Tick t) {
        return Tick(std::llround(double(t) * double(kPpq) / double(file.ppq)));
    };
    Tick tempoAt = 0, meterAt = 0;
    bool mpeSeen = false; // a file announces MPE once; the tracks after it are read as MPE too

    for (size_t k = 0; k < file.tracks.size(); k++) {
        std::string trackName;
        std::map<int, Building> parts; // by part channel, in channel order
        MidiReader reader;
        reader.setMpe(mpeSeen);
        Tick last = 0;
        for (const smf::Event& e : file.tracks[k].events) {
            last = std::max(last, e.tick);
            if (e.isMeta()) {
                uint8_t type = e.bytes.size() > 1 ? e.bytes[1] : 0;
                const uint8_t* data = e.bytes.data() + 2;
                size_t size = e.bytes.size() >= 2 ? e.bytes.size() - 2 : 0;
                if (type == smf::kTrackName && trackName.empty())
                    trackName.assign(reinterpret_cast<const char*>(data), size);
                // The first tempo and time signature in time order, whichever track holds them.
                if (type == smf::kTempo && size >= 3 && (!out.tempo || e.tick < tempoAt)) {
                    uint32_t micros = uint32_t(data[0]) << 16 | uint32_t(data[1]) << 8 | data[2];
                    if (micros > 0) {
                        out.tempo = 60e6 / double(micros);
                        tempoAt = e.tick;
                    }
                }
                if (type == smf::kTimeSignature && size >= 2 && (!out.timeSig || e.tick < meterAt) && data[0] > 0 &&
                    data[1] <= 5) {
                    out.timeSig = TimeSig{int(data[0]), 1 << data[1]};
                    meterAt = e.tick;
                }
                continue;
            }
            std::array<MidiEvent, MidiReader::kMaxEvents> events;
            int count = reader.read(e.bytes.data(), int(e.bytes.size()), events);
            for (int i = 0; i < count; i++) {
                const MidiEvent& m = events[size_t(i)];
                Building& b = parts[m.part];
                int key = m.channel * 128 + (m.pitch & 127);
                switch (m.kind) {
                case MidiEvent::Kind::NoteOn:
                    b.open[key].push_back(OpenNote{e.tick, m.value, {}, {}, {}});
                    break;
                case MidiEvent::Kind::NoteOff: {
                    auto& starts = b.open[key];
                    if (starts.empty())
                        break; // a note-off without its note-on
                    b.notes.push_back(finishNote(starts.front(), m.pitch, e.tick, m.value, ticks));
                    starts.pop_front();
                    break;
                }
                case MidiEvent::Kind::NoteValue:
                    for (auto& [k, starts] : b.open) {
                        // One key (poly aftertouch), or every key on the channel (MPE).
                        if (starts.empty() || (m.pitch >= 0 ? k != key : k / 128 != m.channel))
                            continue;
                        OpenNote& o = starts.back();
                        TimedValues& to = m.curve == NoteCurve::Pitch    ? o.pitch
                                          : m.curve == NoteCurve::Timbre ? o.timbre
                                                                         : o.pressure;
                        to.push_back({double(e.tick), m.value});
                    }
                    break;
                case MidiEvent::Kind::ChannelValue: {
                    MidiControl control = controlOf(m);
                    float value = control.kind == MidiControl::Kind::PitchBend ? (m.value + 1) / 2 : m.value;
                    auto it = std::find_if(b.controls.begin(), b.controls.end(),
                                           [&](const auto& c) { return c.first == control; });
                    if (it == b.controls.end())
                        it = b.controls.insert(b.controls.end(), {control, TimedValues{}});
                    it->second.push_back({double(ticks(e.tick)), value});
                    break;
                }
                }
            }
        }
        mpeSeen |= reader.mpe();
        // Notes still held when the track ends stop there.
        for (auto& [part, b] : parts)
            for (auto& [key, starts] : b.open)
                for (const OpenNote& o : starts)
                    b.notes.push_back(finishNote(o, key % 128, last, 0.5f, ticks));

        int withNotes =
            int(std::count_if(parts.begin(), parts.end(), [](const auto& c) { return !c.second.notes.empty(); }));
        for (auto& [channel, b] : parts) {
            if (b.notes.empty())
                continue;
            Part part;
            part.channel = channel;
            part.drum = channel == 9;
            std::string base = trackName.empty() ? "MIDI " + std::to_string(out.parts.size() + 1) : trackName;
            part.name = withNotes > 1 ? base + " (ch " + std::to_string(channel + 1) + ")" : base;
            part.notes = std::move(b.notes);
            for (const auto& [control, values] : b.controls) {
                TakeLane lane{control, std::nullopt, lanePoints(control, values)};
                if (!lane.points.empty())
                    part.lanes.push_back(std::move(lane));
            }
            std::sort(part.notes.begin(), part.notes.end(), [](const Note& a, const Note& b) {
                return a.start != b.start ? a.start < b.start : a.pitch < b.pitch;
            });
            out.parts.push_back(std::move(part));
        }
    }
    return true;
}

bool readFile(const std::string& path, Imported& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return read(bytes, out);
}

size_t noteCount(const Imported& in, int part) {
    size_t n = 0;
    for (size_t i = 0; i < in.parts.size(); i++)
        if (part < 0 || int(i) == part)
            n += in.parts[i].notes.size();
    return n;
}

Clip clipOf(const Imported& in, int part, TimeSig timeSig, const NextId& nextId, const std::string& name) {
    Clip clip;
    clip.id = ClipId(nextId());
    clip.name = name;
    Tick end = 0;
    for (size_t i = 0; i < in.parts.size(); i++) {
        if (part >= 0 && int(i) != part)
            continue;
        for (const Note& n : in.parts[i].notes) {
            Note copy = n;
            copy.id = NoteId(nextId());
            clip.notes.push_back(copy);
            end = std::max(end, n.end());
        }
    }
    std::sort(clip.notes.begin(), clip.notes.end(),
              [](const Note& a, const Note& b) { return a.start != b.start ? a.start < b.start : a.pitch < b.pitch; });
    int outputs = 0;
    for (size_t i = 0; i < in.parts.size(); i++) {
        if (part >= 0 && int(i) != part)
            continue;
        for (const TakeLane& tl : in.parts[i].lanes) {
            auto it = std::find_if(clip.automation.begin(), clip.automation.end(),
                                   [&](const AutomationEnvelope& l) { return l.midi == tl.control; });
            if (it == clip.automation.end()) {
                bool cc = tl.control.kind == MidiControl::Kind::Cc;
                if (cc && outputs >= kMaxAutomationLanes)
                    continue;
                clip.automation.push_back(makeMidiLane(AutomationId(nextId()), tl.control, cc ? outputs++ : 0, 0));
                it = clip.automation.end() - 1;
            }
            it->points.insert(it->points.end(), tl.points.begin(), tl.points.end());
            std::stable_sort(it->points.begin(), it->points.end(),
                             [](const AutoPoint& a, const AutoPoint& b) { return a.t < b.t; });
            for (const AutoPoint& p : tl.points)
                end = std::max(end, p.t);
        }
    }
    // Whole bars, so the loop comes round on the downbeat.
    Tick bar = std::max<Tick>(1, ticksPerBar(timeSig));
    clip.length = std::max(bar, (end + bar - 1) / bar * bar);
    clip.loopStart = 0;
    clip.loopEnd = clip.length;
    clip.loopMode = LoopMode::Loop;
    return clip;
}

CommandPtr importTracks(const Song& song, const Imported& in, const NextId& nextId, const std::string& fileName) {
    int room = kMaxTracks - int(song.tracks.size());
    if (in.parts.empty() || room <= 0)
        return nullptr;
    auto compound = std::make_unique<CompoundCommand>("Import " + fileName);
    TimeSig timeSig = in.timeSig.value_or(song.timeSig);
    if (in.timeSig && *in.timeSig != song.timeSig)
        compound->add(
            std::make_unique<SetSongField<TimeSig>>("Set time signature", &Song::timeSig, song.timeSig, *in.timeSig));
    int index = int(song.tracks.size());
    int count = std::min(room, int(in.parts.size()));
    for (int k = 0; k < count; k++) {
        const Part& part = in.parts[size_t(k)];
        Track track;
        track.id = TrackId(nextId());
        track.name = part.name;
        track.type = part.drum ? TrackType::Drum : TrackType::Instrument;
        track.color = trackColorForIndex(index + k);
        track.slots.resize(song.scenes.size()); // one slot per scene, as every track has
        if (part.drum)
            track.drumLanes = lanesFor(part);
        Clip clip = clipOf(in, k, timeSig, nextId, part.name);
        clip.color = track.color;
        if (!track.slots.empty())
            track.slots[0].clip = clip.id;
        ArrangerClip placed;
        placed.id = ArrangementId(nextId());
        placed.clip = clip.id;
        placed.position = 0;
        placed.length = clip.length;
        track.arrangement.push_back(placed);
        compound->add(std::make_unique<AddTrack>("Import " + fileName, index + k, std::move(track),
                                                 std::vector<Clip>{std::move(clip)}));
    }
    return compound;
}

} // namespace tessera::midiImport
