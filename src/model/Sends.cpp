#include "Sends.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <set>

namespace tessera {

bool sendsReach(const Song& song, TrackId from, TrackId to) {
    std::vector<TrackId> stack{from};
    std::set<uint64_t> seen;
    while (!stack.empty()) {
        TrackId at = stack.back();
        stack.pop_back();
        if (!seen.insert(at.value).second)
            continue;
        const Track* t = song.findTrack(at);
        if (!t)
            continue;
        for (const Send& s : t->sends) {
            if (s.to == to)
                return true;
            stack.push_back(s.to);
        }
    }
    return false;
}

std::string whyNoSend(const Song& song, TrackId from, TrackId to) {
    const Track* a = song.findTrack(from);
    const Track* b = song.findTrack(to);
    if (!a || !b)
        return "No such track";
    if (from == to)
        return "A track can't send to itself";
    if (findSend(song, from, to))
        return "Already sends to " + b->name;
    if (sendsReach(song, to, from))
        return "Would feed back into " + a->name;
    return "";
}

std::vector<TrackId> sendersTo(const Song& song, TrackId to) {
    std::vector<TrackId> out;
    for (const Track& t : song.tracks)
        for (const Send& s : t.sends)
            if (s.to == to)
                out.push_back(t.id);
    return out;
}

std::vector<TrackId> sendOrder(const Song& song) {
    // Depth-first: a track goes in once everything that sends to it is in.
    std::vector<TrackId> out;
    std::set<uint64_t> placed, visiting;
    std::function<void(TrackId)> place = [&](TrackId id) {
        if (placed.count(id.value) || !visiting.insert(id.value).second)
            return;
        for (TrackId sender : sendersTo(song, id))
            place(sender);
        visiting.erase(id.value);
        if (placed.insert(id.value).second)
            out.push_back(id);
    };
    for (const Track& t : song.tracks)
        place(t.id);
    return out;
}

const Send* findSend(const Song& song, TrackId from, TrackId to) {
    const Track* t = song.findTrack(from);
    if (!t)
        return nullptr;
    for (const Send& s : t->sends)
        if (s.to == to)
            return &s;
    return nullptr;
}

CommandPtr addSend(const Song& song, TrackId from, TrackId to, float db) {
    if (!whyNoSend(song, from, to).empty())
        return nullptr;
    std::vector<Send> sends = song.findTrack(from)->sends;
    sends.push_back({to, std::clamp(db, kSilentDb, kMaxFaderDb), false});
    return setTrackField(song, from, &Track::sends, sends, "Send to " + song.findTrack(to)->name);
}

CommandPtr setSend(const Song& song, TrackId from, Send send, const std::string& name) {
    const Track* t = song.findTrack(from);
    if (!t)
        return nullptr;
    send.db = std::clamp(send.db, kSilentDb, kMaxFaderDb);
    std::vector<Send> sends = t->sends;
    auto it = std::find_if(sends.begin(), sends.end(), [&](const Send& s) { return s.to == send.to; });
    if (it == sends.end())
        return nullptr;
    *it = send;
    return setTrackField(song, from, &Track::sends, sends, name);
}

CommandPtr removeSend(const Song& song, TrackId from, TrackId to) {
    const Track* t = song.findTrack(from);
    const Track* target = song.findTrack(to);
    if (!t)
        return nullptr;
    std::vector<Send> sends = t->sends;
    sends.erase(std::remove_if(sends.begin(), sends.end(), [&](const Send& s) { return s.to == to; }), sends.end());
    return setTrackField(song, from, &Track::sends, sends,
                         "Remove send to " + (target ? target->name : std::string("track")));
}

void normalizeSends(Song& song) {
    // Rebuild the graph one send at a time, keeping only sends that point somewhere new and close no loop.
    std::vector<std::vector<Send>> wanted;
    for (Track& t : song.tracks) {
        wanted.push_back(std::move(t.sends));
        t.sends.clear();
    }
    for (size_t i = 0; i < song.tracks.size(); i++) {
        Track& t = song.tracks[i];
        for (Send s : wanted[i]) {
            if (!whyNoSend(song, t.id, s.to).empty())
                continue;
            s.db = std::isfinite(s.db) ? std::clamp(s.db, kSilentDb, kMaxFaderDb) : kSilentDb;
            t.sends.push_back(s);
        }
    }
}

} // namespace tessera
