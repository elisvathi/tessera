#include "AudioEdits.hpp"

#include "LauncherCommands.hpp"
#include "TrackColors.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <optional>
#include <set>

namespace tessera {

namespace {

/// Puts `source` into the song's list, which is sorted by ID.
void insertSource(Song& song, const AudioSource& source) {
    auto& list = song.audioSources;
    auto it = std::lower_bound(list.begin(), list.end(), source.id,
                               [](const AudioSource& s, AudioSourceId id) { return s.id < id; });
    if (it != list.end() && it->id == source.id)
        *it = source;
    else
        list.insert(it, source);
}

/// Takes the source with `id` out of the song's list.
void eraseSource(Song& song, AudioSourceId id) {
    auto& list = song.audioSources;
    list.erase(std::remove_if(list.begin(), list.end(), [&](const AudioSource& s) { return s.id == id; }),
               list.end());
}

/// "−3.0 dB" style text for command names.
std::string dbText(float db) {
    if (db <= kSilentDb)
        return "-inf dB";
    char buf[32];
    snprintf(buf, sizeof buf, "%+.1f dB", double(db));
    return buf;
}

} // namespace

void AddAudioSources::apply(Song& song) {
    for (const AudioSource& s : sources)
        insertSource(song, s);
}

void AddAudioSources::revert(Song& song) {
    for (const AudioSource& s : sources)
        eraseSource(song, s.id);
}

void RemoveAudioSources::apply(Song& song) {
    for (const AudioSource& s : sources)
        eraseSource(song, s.id);
}

void RemoveAudioSources::revert(Song& song) {
    for (const AudioSource& s : sources)
        insertSource(song, s);
}

void SetAudioContent::set(Song& song, const AudioContent& value) const {
    if (Clip* c = song.findClip(clip))
        if (c->audio)
            *c->audio = value;
}

ChangeSet SetChannels::changes() const {
    ChangeSet c;
    for (const Change& ch : list)
        c.tracks.push_back(ch.track);
    return c;
}

void SetChannels::set(Song& song, bool undo) const {
    for (const Change& ch : list)
        if (Track* t = song.findTrack(ch.track))
            t->channel = undo ? ch.before : ch.after;
}

CommandPtr editAudio(const Song& song, ClipId clip, const std::function<void(AudioContent&)>& change,
                     const std::string& name) {
    const Clip* c = song.findClip(clip);
    if (!c || !c->audio)
        return nullptr;
    AudioContent after = *c->audio;
    change(after);
    after.gainDb = std::clamp(after.gainDb, kSilentDb, kMaxClipGainDb);
    after.semitones = int8_t(std::clamp<int>(after.semitones, -24, 24));
    after.cents = std::clamp(after.cents, -50.f, 50.f);
    after.sourceStart = std::max<int64_t>(0, after.sourceStart);
    if (after == *c->audio)
        return nullptr;
    return std::make_unique<SetAudioContent>(name, clip, *c->audio, after);
}

CommandPtr setAudioWindow(const Song& song, ClipId clip, int64_t start, int64_t end) {
    const Clip* c = song.findClip(clip);
    const AudioSource* src = c ? song.sourceOf(*c) : nullptr;
    if (!c || !c->audio || !src)
        return nullptr;
    start = std::clamp<int64_t>(start, 0, std::max<int64_t>(0, src->frames - 1));
    end = std::clamp<int64_t>(end, start + 1, std::max<int64_t>(start + 1, src->frames));
    AudioContent audio = *c->audio;
    Tick shift = ticksForFrames(audio, start - audio.sourceStart, src->sampleRate);
    audio.sourceStart = start;
    Tick length = std::max<Tick>(1, ticksForFrames(audio, end - start, src->sampleRate));
    Tick loopStart = c->loopStart, loopEnd = c->loopEnd;
    if (c->loopStart == 0 && c->loopEnd >= c->length) {
        loopStart = 0;
        loopEnd = length;
    } else {
        loopStart = std::clamp<Tick>(loopStart - shift, 0, length - 1);
        loopEnd = std::clamp<Tick>(loopEnd - shift, loopStart + 1, length);
    }
    auto out = std::make_unique<CompoundCommand>("Audio window");
    out->add(setClipField(song, clip, &Clip::audio, std::optional<AudioContent>(audio), "Audio window"));
    out->add(setClipField(song, clip, &Clip::length, length, "Audio window"));
    out->add(setClipField(song, clip, &Clip::loopStart, loopStart, "Audio window"));
    out->add(setClipField(song, clip, &Clip::loopEnd, loopEnd, "Audio window"));
    if (out->empty())
        return nullptr;
    return out;
}

float clipPeak(const Clip& clip, const AudioSource& source, const PeakCache& peaks) {
    if (!clip.audio || peaks.empty())
        return 0;
    double from = double(clip.audio->sourceStart);
    double to = from + double(clip.length) * framesPerTick(*clip.audio, source.sampleRate);
    float peak = 0;
    for (int ch = 0; ch < peaks.channels(); ch++) {
        float lo = 0, hi = 0;
        peaks.range(ch, from, to, lo, hi);
        peak = std::max({peak, std::abs(lo), std::abs(hi)});
    }
    return peak;
}

CommandPtr normaliseClip(const Song& song, ClipId clip, float peak) {
    if (!(peak > 0))
        return nullptr;
    float db = std::clamp(-gainToDb(peak), kSilentDb, kMaxClipGainDb);
    // Within a hundredth of a dB is already there: a second Normalise changes nothing.
    const Clip* c = song.findClip(clip);
    if (!c || !c->audio || std::abs(c->audio->gainDb - db) < 0.01f)
        return nullptr;
    return editAudio(song, clip, [db](AudioContent& a) { a.gainDb = db; }, "Normalise");
}

CommandPtr nudgeClipGain(const Song& song, const std::vector<ClipId>& clips, float deltaDb) {
    auto out = std::make_unique<CompoundCommand>("Clip gain");
    std::set<ClipId> seen;
    for (ClipId id : clips) {
        if (!seen.insert(id).second)
            continue;
        out->add(editAudio(song, id, [&](AudioContent& a) {
            if (a.gainDb > kSilentDb)
                a.gainDb = a.gainDb + deltaDb;
        }, "Clip gain"));
    }
    return out->empty() ? nullptr : std::move(out);
}

CommandPtr setClipTempo(const Song& song, ClipId id, double bpm) {
    const Clip* c = song.findClip(id);
    if (!c || !c->audio || !std::isfinite(bpm))
        return nullptr;
    bpm = std::clamp(bpm, kMinClipTempo, kMaxClipTempo);
    double old = c->audio->sourceTempo;
    if (std::abs(bpm - old) < 1e-9)
        return nullptr;
    char label[48];
    snprintf(label, sizeof label, "Clip tempo %.1f BPM", bpm);
    auto out = std::make_unique<CompoundCommand>(label);
    out->add(editAudio(song, id, [&](AudioContent& a) { a.sourceTempo = bpm; }, label));

    // An Off clip plays at its own speed whatever its tempo says, so its length stays. A Stretch or Repitch clip
    // keeps playing the same audio: at a higher clip tempo that audio covers more ticks.
    if (c->audio->follow != TempoFollow::Off) {
        double factor = bpm / old;
        auto scaled = [factor](Tick t) { return Tick(std::llround(double(t) * factor)); };
        out->add(setClipField(song, id, &Clip::length, std::max<Tick>(1, scaled(c->length)), label));
        out->add(setClipField(song, id, &Clip::loopStart, scaled(c->loopStart), label));
        out->add(setClipField(song, id, &Clip::loopEnd, std::max<Tick>(1, scaled(c->loopEnd)), label));
        arrange::Layout before = arrange::layoutOf(song);
        arrange::Layout after = before;
        for (arrange::Placed& p : after)
            if (p.clip.clip == id) {
                p.clip.length = std::max<Tick>(1, scaled(p.clip.length));
                p.clip.offset = scaled(p.clip.offset);
                p.clip.fadeIn.length = scaled(p.clip.fadeIn.length);
                p.clip.fadeOut.length = scaled(p.clip.fadeOut.length);
            }
        out->add(arrange::replaceArrangement(song, label, before, after));
    }
    return out->empty() ? nullptr : std::move(out);
}

CommandPtr setFades(const Song& song, ArrangementId instance, Fade in, Fade out) {
    arrange::Layout before = arrange::layoutOf(song);
    arrange::Layout after = before;
    for (arrange::Placed& p : after)
        if (p.clip.id == instance) {
            in.length = std::clamp<Tick>(in.length, 0, p.clip.length);
            out.length = std::clamp<Tick>(out.length, 0, p.clip.length - in.length);
            in.curve = std::clamp(in.curve, -1.f, 1.f);
            out.curve = std::clamp(out.curve, -1.f, 1.f);
            bool inChanged = !(p.clip.fadeIn == in);
            p.clip.fadeIn = in;
            p.clip.fadeOut = out;
            return arrange::replaceArrangement(song, inChanged ? "Fade in" : "Fade out", before, after);
        }
    return nullptr;
}

CommandPtr setChannel(const Song& song, TrackId track, Channel channel, const std::string& name) {
    const Track* t = song.findTrack(track);
    if (!t)
        return nullptr;
    channel.volumeDb = std::clamp(channel.volumeDb, kSilentDb, kMaxFaderDb);
    channel.pan = std::clamp(channel.pan, -1.f, 1.f);
    if (channel == t->channel)
        return nullptr;
    return std::make_unique<SetChannels>(name, std::vector<SetChannels::Change>{{track, t->channel, channel}});
}

CommandPtr nudgeVolumes(const Song& song, const std::vector<SetChannels::Change>& start, float deltaDb) {
    std::vector<SetChannels::Change> list;
    for (const SetChannels::Change& s : start) {
        const Track* t = song.findTrack(s.track);
        if (!t)
            continue;
        Channel after = t->channel;
        if (s.before.volumeDb > kSilentDb)
            after.volumeDb = std::clamp(s.before.volumeDb + deltaDb, kSilentDb, kMaxFaderDb);
        if (after != t->channel)
            list.push_back({s.track, t->channel, after});
    }
    if (list.empty())
        return nullptr;
    std::string name = list.size() == 1 ? "Volume " + dbText(list[0].after.volumeDb)
                                        : std::to_string(list.size()) + " tracks volume";
    return std::make_unique<SetChannels>(name, std::move(list));
}

CommandPtr setDrumInput(const Song& song, TrackId track, int lane, Channel channel, const std::string& name) {
    const Track* t = song.findTrack(track);
    if (!t || lane < 0 || lane >= kDrumInputs)
        return nullptr;
    channel.volumeDb = std::clamp(channel.volumeDb, kSilentDb, kMaxFaderDb);
    channel.pan = std::clamp(channel.pan, -1.f, 1.f);
    std::array<Channel, kDrumInputs> inputs = t->drumInputs;
    inputs[size_t(lane)] = channel;
    return setTrackField(song, track, &Track::drumInputs, inputs, name);
}

CommandPtr setMasterVolume(const Song& song, float db) {
    MasterChannel m = song.master;
    m.volumeDb = std::clamp(db, kSilentDb, kMaxFaderDb);
    return setSongField(song, &Song::master, m, "Master volume " + dbText(m.volumeDb));
}

namespace {

/// The song's groups with `group` changed by `change`; nullopt when it isn't there.
std::optional<std::vector<TrackGroup>> withGroup(const Song& song, GroupId group,
                                                 const std::function<void(TrackGroup&)>& change) {
    std::vector<TrackGroup> groups = song.groups;
    for (TrackGroup& g : groups)
        if (g.id == group) {
            change(g);
            return groups;
        }
    return std::nullopt;
}

} // namespace

CommandPtr setGroupChannel(const Song& song, GroupId group, Channel channel, const std::string& name) {
    channel.volumeDb = std::clamp(channel.volumeDb, kSilentDb, kMaxFaderDb);
    channel.pan = std::clamp(channel.pan, -1.f, 1.f);
    auto groups = withGroup(song, group, [&](TrackGroup& g) { g.channel = channel; });
    return groups ? setSongField(song, &Song::groups, *groups, name) : nullptr;
}

CommandPtr setGroupMute(const Song& song, GroupId group, bool mute) {
    auto groups = withGroup(song, group, [&](TrackGroup& g) { g.mute = mute; });
    return groups ? setSongField(song, &Song::groups, *groups, mute ? "Mute group" : "Unmute group") : nullptr;
}

CommandPtr setSongSource(const Song& song, TrackId track, SourceLevel level, const std::string& name) {
    level.db = std::clamp(level.db, kSilentDb, kMaxFaderDb);
    if (track == song.master.id) {
        MasterChannel m = song.master;
        m.mix = level;
        return setSongField(song, &Song::master, m, name);
    }
    if (!song.findTrack(track))
        return nullptr;
    return setTrackField(song, track, &Track::clips, level, name);
}

std::vector<Tick> hitTicks(const Clip& clip, const ArrangerClip& at, const std::vector<int64_t>& transients,
                           double fileRate, double songTempo) {
    std::vector<Tick> out;
    if (!clip.audio || clip.audio->reverse)
        return out;
    const AudioContent& a = *clip.audio;
    double perTick = framesPerTick(a, fileRate);
    if (perTick <= 0)
        return out;
    double k = songTicksPerClipTick(a, songTempo);
    bool looping = clip.loopMode == LoopMode::Loop && clip.loopEnd > clip.loopStart;
    double start = double(clip.playStart()), period = double(clip.loopEnd - clip.loopStart);
    double from = double(at.offset), to = double(at.offset + at.length);
    for (int64_t f : transients) {
        double t = double(f - a.sourceStart) / perTick; // clip ticks
        if (t < 0 || t >= double(clip.length))
            continue;
        // Play position u (clip ticks since the clip's play start) for each time it comes round.
        if (!looping) {
            if (t >= from && t < to)
                out.push_back(at.position + Tick(std::llround((t - from) * k)));
            continue;
        }
        if (t < double(clip.loopStart) || t >= double(clip.loopEnd))
            continue;
        for (double u = t - start; u < to; u += period)
            if (u >= from)
                out.push_back(at.position + Tick(std::llround((u - from) * k)));
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<AudioSourceId> unusedSources(const Song& song) {
    std::set<AudioSourceId> used;
    for (const auto& [id, clip] : song.clips)
        if (clip.audio)
            used.insert(clip.audio->source);
    std::vector<AudioSourceId> out;
    for (const AudioSource& s : song.audioSources)
        if (!used.count(s.id))
            out.push_back(s.id);
    return out;
}

CommandPtr removeUnusedAudio(const Song& song) {
    std::vector<AudioSource> gone;
    for (AudioSourceId id : unusedSources(song))
        if (const AudioSource* s = song.findSource(id))
            gone.push_back(*s);
    if (gone.empty())
        return nullptr;
    return std::make_unique<RemoveAudioSources>(std::move(gone));
}

// ---- Import -------------------------------------------------------------------------------------------------

std::string audioTrackName(const std::string& fileName) {
    std::string name = fileName;
    size_t slash = name.find_last_of("/\\");
    if (slash != std::string::npos)
        name = name.substr(slash + 1);
    size_t dot = name.find_last_of('.');
    if (dot != std::string::npos && dot > 0)
        name = name.substr(0, dot);
    return name.empty() ? "Audio" : name;
}

Clip makeAudioClip(const ImportedAudio& file, ClipId id) {
    Clip c;
    c.id = id;
    c.name = audioTrackName(file.source.name);
    AudioContent a;
    a.source = file.source.id;
    a.follow = file.follow;
    a.sound = file.sound;
    a.sourceTempo = std::clamp(file.tempo, kMinClipTempo, kMaxClipTempo);
    c.length = std::max<Tick>(1, ticksForFrames(a, file.source.frames, file.source.sampleRate));
    c.loopStart = 0;
    c.loopEnd = c.length;
    c.loopMode = file.loop ? LoopMode::Loop : LoopMode::Off;
    c.audio = a;
    return c;
}

CommandPtr replaceAudioFile(const Song& song, ClipId clip, ImportedAudio file, const NextId& nextId) {
    const Clip* c = song.findClip(clip);
    if (!c || !c->audio)
        return nullptr;
    file.source.id = AudioSourceId(nextId());
    AudioContent after = *c->audio;
    after.source = file.source.id;
    after.sourceStart = 0;
    after.sourceTempo = file.tempo > 0 ? file.tempo : c->audio->sourceTempo;
    auto out = std::make_unique<CompoundCommand>("Replace file with " + file.source.name);
    out->add(std::make_unique<AddAudioSources>(std::vector<AudioSource>{file.source}));
    out->add(std::make_unique<SetAudioContent>(out->name(), clip, *c->audio, after));
    return out;
}

CommandPtr importAudio(const Song& song, std::vector<ImportedAudio> files, const AudioDropTarget& target,
                       const NextId& nextId, ImportResult* result) {
    ImportResult made;
    if (files.empty())
        return nullptr;
    std::string label = files.size() == 1 ? "Import " + files[0].source.name
                                          : "Import " + std::to_string(files.size()) + " audio files";
    auto out = std::make_unique<CompoundCommand>(label);

    std::vector<AudioSource> sources;
    std::vector<Clip> clips;
    for (ImportedAudio& f : files) {
        f.source.id = AudioSourceId(nextId());
        sources.push_back(f.source);
    }
    out->add(std::make_unique<AddAudioSources>(sources));

    switch (target.kind) {
    case AudioDropTarget::Kind::NewTracks: {
        int index = std::clamp(target.index, 0, int(song.tracks.size()));
        for (const ImportedAudio& f : files) {
            if (int(song.tracks.size() + made.tracks.size()) >= kMaxTracks) {
                made.skipped++;
                continue;
            }
            Track t;
            t.id = TrackId(nextId());
            t.type = TrackType::Audio;
            t.name = audioTrackName(f.source.name);
            t.color = trackColorForIndex(int(song.tracks.size() + made.tracks.size()));
            t.slots.resize(song.scenes.size());
            Clip c = makeAudioClip(f, ClipId(nextId()));
            if (target.timeline) {
                ArrangerClip a;
                a.id = ArrangementId(nextId());
                a.clip = c.id;
                a.position = std::max<Tick>(0, target.at);
                a.length = std::max<Tick>(1, c.playEnd() - c.playStart());
                t.arrangement.push_back(a);
                made.instances.push_back(a.id);
            } else if (!t.slots.empty()) {
                t.slots[0].clip = c.id;
            }
            made.tracks.push_back(t.id);
            made.clips.push_back(c.id);
            out->add(std::make_unique<AddTrack>(label, index++, std::move(t), std::vector<Clip>{std::move(c)}));
        }
        break;
    }
    case AudioDropTarget::Kind::Track: {
        const Track* t = song.findTrack(target.track);
        if (!t || t->type != TrackType::Audio)
            return nullptr;
        arrange::Layout before = arrange::layoutOf(song);
        arrange::Layout after = before;
        ClipPool pool = song.clips;
        arrange::IdSet added;
        Tick at = std::max<Tick>(0, target.at);
        for (const ImportedAudio& f : files) {
            Clip c = makeAudioClip(f, ClipId(nextId()));
            ArrangerClip a;
            a.id = ArrangementId(nextId());
            a.clip = c.id;
            a.position = at;
            a.length = std::max<Tick>(1, c.playEnd() - c.playStart());
            at += a.length;
            after.push_back({t->id, a});
            added.insert(a.id);
            made.instances.push_back(a.id);
            made.clips.push_back(c.id);
            pool[c.id] = c;
            out->add(std::make_unique<AddClip>(std::move(c)));
        }
        after = arrange::resolveOverlaps(after, added, pool, [&] { return ArrangementId(nextId()); });
        out->add(arrange::replaceArrangement(song, label, before, after));
        break;
    }
    case AudioDropTarget::Kind::Slot: {
        const Track* t = song.findTrack(target.track);
        if (!t || t->type != TrackType::Audio)
            return nullptr;
        int slot = std::max(0, target.slot);
        for (const ImportedAudio& f : files) {
            if (slot >= int(t->slots.size())) {
                made.skipped++;
                continue;
            }
            Clip c = makeAudioClip(f, ClipId(nextId()));
            made.clips.push_back(c.id);
            out->add(std::make_unique<AddClipToSlot>(t->id, slot++, std::move(c)));
        }
        break;
    }
    }
    if (made.clips.empty())
        return nullptr;
    if (result)
        *result = made;
    return out;
}

} // namespace tessera
