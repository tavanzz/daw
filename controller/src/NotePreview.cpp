#include "daw/controller/NotePreview.h"

#include "daw/controller/Session.h"

namespace daw::controller
{

void NotePreview::play(model::ObjectId trackId, int keyToPlay, int velocity)
{
    stop();
    if (session.startNotePreview(trackId, keyToPlay, velocity))
    {
        track = trackId;
        key = keyToPlay;
    }
}

void NotePreview::stop()
{
    if (key >= 0)
        session.stopNotePreview(track, key);
    track = 0;
    key = -1;
}

} // namespace daw::controller
