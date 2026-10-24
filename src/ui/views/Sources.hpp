#pragma once

#include "Ruler.hpp"

namespace tessera {

/// The arranger's timeline: song loop region, markers and the song playhead.
class ArrangerSource : public TimelineSource {
  public:
    explicit ArrangerSource(EditorContext& ctx) : ctx(ctx) {}

    /// Where the song's content ends: the loop end, the last clip or the last marker.
    Tick songEnd();
    /// How far "show the whole song" reaches: the song's end, but at least 16 bars, so a short or empty song
    /// still looks like a song and not a close-up of its first bars.
    Tick fitEnd();

    TimeSig timeSig() override;
    double contentEnd() override;
    Tick loopStart() override;
    Tick loopEnd() override;
    bool loopOn() override;
    void setLoop(Tick start, Tick end) override;
    void toggleLoop() override;
    double playhead() override;
    bool canSeek() override { return true; }
    void seek(double tick, bool play) override;
    bool hasMarkers() override { return true; }
    std::vector<Marker> markers() override;
    void setMarkers(std::vector<Marker> markers, const std::string& undoName) override;
    MarkerId newMarkerId() override;

    /// Adds a marker at `position` as one undo step.
    void addMarker(Tick position);

  private:
    EditorContext& ctx;
};

/// The piano roll's timeline: one clip's loop region and its playback position, in clip time. The playhead
/// shows while the launcher plays this clip on this track, or the arrangement plays an instance of it.
class ClipSource : public TimelineSource {
  public:
    explicit ClipSource(EditorContext& ctx) : ctx(ctx) {}

    /// The clip being shown and its track. Set by the pane every frame; either may be empty.
    ClipId clip;
    TrackId track;

    TimeSig timeSig() override;
    double contentEnd() override;
    Tick loopStart() override;
    /// The clip's loop end, or the end of its lane cycle on a drum track.
    Tick loopEnd() override;
    bool loopOn() override;
    bool loopLocked() override;
    void setLoop(Tick start, Tick end) override;
    void toggleLoop() override;
    double playhead() override;
    double instanceEnd() override;
    double instanceStart() override;
    void previewInstanceEnd(double end) override { endPreview = end; }
    void setInstanceEnd(double end) override;
    /// Seeking from the piano roll: a launched clip (or, with the clips playing, its slot) restarts from that point;
    /// with the timeline playing, the song jumps to where the timeline plays that point of this clip.
    bool canSeek() override;
    void seek(double tick, bool play) override;

    /// The timeline clip the piano roll was opened from: the one selected in the arranger, when it plays this clip.
    const ArrangerClip* instance();

  private:
    const Clip* current();
    /// The track's drum lanes, which can make the clip loop their whole cycle; nullptr on other tracks.
    const std::vector<DrumLane>* lanes();
    /// The slot to restart for a seek: the playing one when it holds this clip, else the first that does; -1 when
    /// the clips play and none does, or when the timeline plays and no launched clip has the track.
    int seekSlot();
    /// The timeline clip a seek maps onto: the selected one, the one under the playhead, or the first on the track.
    const ArrangerClip* seekInstance();
    double endPreview = NAN;
    EditorContext& ctx;
};

} // namespace tessera
