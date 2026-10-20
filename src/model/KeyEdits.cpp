#include "KeyEdits.hpp"

#include "NoteEdits.hpp"

#include <algorithm>

namespace tessera {

const ScaleRef& clipKeyRef(const Song& song, const Clip& clip) {
    return clip.scaleOverride ? *clip.scaleOverride : song.scale;
}

Scale clipScale(const Song& song, const Clip& clip) { return resolveScale(clipKeyRef(song, clip), song.customScales); }

bool usedByDrumTrack(const Song& song, ClipId clip) {
    for (const Track& t : song.tracks) {
        if (t.type != TrackType::Drum)
            continue;
        for (const ClipSlot& s : t.slots)
            if (s.clip == clip)
                return true;
        for (const ArrangerClip& a : t.arrangement)
            if (a.clip == clip)
                return true;
    }
    return false;
}

std::vector<ClipId> clipsFollowingSongKey(const Song& song) {
    std::vector<ClipId> out;
    for (const auto& [id, clip] : song.clips)
        if (!clip.scaleOverride && !usedByDrumTrack(song, id))
            out.push_back(id);
    return out;
}

bool keyChangeMovesNotes(const Song& song, const std::vector<ClipId>& clips, const Scale& from, const Scale& to) {
    if (from == to)
        return false;
    for (ClipId id : clips)
        if (const Clip* c = song.findClip(id))
            for (const Note& n : c->notes)
                if (moveToKey(n.pitch, from, to) != n.pitch)
                    return true;
    return false;
}

namespace {

void addMovedNotes(CompoundCommand& out, const Song& song, ClipId id, const Scale& from, const Scale& to) {
    const Clip* c = song.findClip(id);
    if (!c || from == to)
        return;
    edits::NoteList after = edits::moveToKey(c->notes, edits::idsOf(c->notes), from, to);
    out.add(replaceNotes("Move notes to new key", id, c->notes, after));
}

} // namespace

CommandPtr changeSongKey(const Song& song, const ScaleRef& key, bool moveNotes) {
    if (key == song.scale)
        return nullptr;
    auto out = std::make_unique<CompoundCommand>("Key " + keyName(key, song.customScales));
    out->add(setSongField(song, &Song::scale, key, "Key"));
    if (moveNotes) {
        Scale from = resolveScale(song.scale, song.customScales);
        Scale to = resolveScale(key, song.customScales);
        for (ClipId id : clipsFollowingSongKey(song))
            addMovedNotes(*out, song, id, from, to);
    }
    return out;
}

CommandPtr changeClipKey(const Song& song, ClipId id, const std::optional<ScaleRef>& key, bool moveNotes) {
    const Clip* c = song.findClip(id);
    if (!c || c->scaleOverride == key)
        return nullptr;
    std::string name = key ? "Clip key " + keyName(*key, song.customScales) : "Clip follows song key";
    auto out = std::make_unique<CompoundCommand>(name);
    out->add(setClipField(song, id, &Clip::scaleOverride, key, name));
    if (moveNotes && !usedByDrumTrack(song, id)) {
        Scale from = clipScale(song, *c);
        Scale to = resolveScale(key ? *key : song.scale, song.customScales);
        addMovedNotes(*out, song, id, from, to);
    }
    return out;
}

CommandPtr putCustomScale(const Song& song, const CustomScale& scale) {
    std::vector<CustomScale> list = song.customScales;
    auto it = std::find_if(list.begin(), list.end(), [&](const CustomScale& c) { return c.id == scale.id; });
    bool added = it == list.end();
    if (added)
        list.push_back(scale);
    else
        *it = scale;
    return setSongField(song, &Song::customScales, list,
                        added ? "Add scale " + scale.name : "Edit scale " + scale.name);
}

CommandPtr deleteCustomScale(const Song& song, const std::string& id) {
    std::vector<CustomScale> list = song.customScales;
    auto it = std::find_if(list.begin(), list.end(), [&](const CustomScale& c) { return c.id == id; });
    if (it == list.end())
        return nullptr;
    std::string name = "Delete scale " + it->name;
    list.erase(it);
    return setSongField(song, &Song::customScales, list, name);
}

} // namespace tessera
