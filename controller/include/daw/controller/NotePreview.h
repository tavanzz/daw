#pragma once

#include "daw/model/Clip.h"

namespace daw::controller
{

class Session;

/**
    The note one view previews (the piano roll's mouse and keys, a drum pad), played
    through a track's instrument while the mouse is down. It remembers the track and
    key it plays, so that its note-off goes where its note-on went: when it plays
    another key, when the view stops it (mouse up, another clip or track shown) and
    when it is destroyed (the view closes). Message thread.
*/
class NotePreview
{
public:
    explicit NotePreview(Session& sessionToUse) noexcept
        : session(sessionToUse)
    {
    }
    ~NotePreview() { stop(); }

    NotePreview(const NotePreview&) = delete;
    NotePreview& operator=(const NotePreview&) = delete;

    /** Ends the note playing, if any, then plays key on the track (velocity 1 to 127). */
    void play(model::ObjectId trackId, int keyToPlay, int velocity);

    /** Ends the note playing, if any. */
    void stop();

    [[nodiscard]] bool isPlaying() const noexcept { return key >= 0; }
    [[nodiscard]] int getKey() const noexcept { return key; }
    [[nodiscard]] model::ObjectId getTrack() const noexcept { return track; }

private:
    Session& session;
    model::ObjectId track = 0;
    int key = -1;
};

} // namespace daw::controller
