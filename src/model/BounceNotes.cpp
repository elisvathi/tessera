#include "BounceNotes.hpp"

#include "Render.hpp"
#include "TrackColors.hpp"

#include <algorithm>
#include <cmath>

namespace tessera {

CommandPtr bounceNotes(const Song& song, const NoteBounce& what, double tempo, const NextId& nextId,
                       NoteBounceMade* made) {
    const Track* source = song.findTrack(what.track);
    if (!source || source->type == TrackType::Audio || int(song.tracks.size()) >= kMaxTracks)
        return nullptr;

    render::Options options;
    options.timing = what.afterDevices ? render::Timing::Played : render::Timing::Written;
    options.operators = what.afterDevices ? render::OperatorMode::Rendered : render::OperatorMode::Written;
    options.devices = what.afterDevices ? render::DeviceMode::Applied : render::DeviceMode::Bypassed;
    render::Context context{tempo, ""};

    std::vector<render::ExportNote> played;
    Tick from = 0, length = 0;
    if (what.clip.valid()) {
        const Clip* clip = song.findClip(what.clip);
        if (!clip || clip->audio)
            return nullptr;
        played = render::clipNotes(song, what.track, what.clip, options, context);
        length = clip->playEnd() - clip->playStart();
    } else {
        if (what.to <= what.from)
            return nullptr;
        Song work = song;
        if (!what.only.empty())
            for (ArrangerClip& a : work.findTrack(what.track)->arrangement)
                if (std::find(what.only.begin(), what.only.end(), a.id) == what.only.end())
                    a.muted = true;
        played = render::trackNotes(work, what.track, options, context);
        from = what.from;
        length = what.to - what.from;
    }

    std::string name = source->name + " (bounce)";
    Clip clip;
    clip.id = ClipId(nextId());
    clip.name = name;
    clip.length = length;
    clip.loopStart = 0;
    clip.loopEnd = length;
    clip.loopMode = LoopMode::Loop;
    for (const render::ExportNote& p : played) {
        Tick start = Tick(std::llround(p.start)) - from;
        if (start < 0 || start >= length || (what.channel > 0 && int(p.channel) != what.channel))
            continue;
        Note n;
        n.id = NoteId(nextId());
        n.start = start;
        n.length = std::clamp<Tick>(Tick(std::llround(p.end - p.start)), 1, length - start);
        n.pitch = int8_t(std::clamp(p.pitch, 0, 127));
        n.velocity = p.velocity;
        n.releaseVelocity = p.release;
        n.channel = p.channel;
        clip.notes.push_back(n);
    }
    if (clip.notes.empty())
        return nullptr;
    clip.sortNotes();

    Track track;
    track.id = TrackId(nextId());
    track.type = source->type;
    track.name = name;
    track.color = trackColorForIndex(int(song.tracks.size()));
    track.drumLanes = source->drumLanes;
    track.slots.resize(song.scenes.size());
    if (what.clip.valid()) {
        if (what.slot >= 0 && what.slot < int(track.slots.size()))
            track.slots[size_t(what.slot)].clip = clip.id;
    } else {
        ArrangerClip a;
        a.id = ArrangementId(nextId());
        a.clip = clip.id;
        a.position = from;
        a.length = length;
        track.arrangement.push_back(a);
    }
    if (made)
        *made = {track.id, clip.id};
    int index = song.trackIndex(what.track) + 1;
    return std::make_unique<AddTrack>("Bounce " + source->name, index, std::move(track),
                                      std::vector<Clip>{std::move(clip)});
}

} // namespace tessera
