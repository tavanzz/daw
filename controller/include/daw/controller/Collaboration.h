#pragma once

#include "daw/model/ProjectChanges.h"

#include <juce_core/juce_core.h>

#include <vector>

namespace daw::controller
{

/**
    What a Session asks of the co-op session it is part of (CoopSession): whether an edit
    may happen (the locks), what its changes are (a transaction of the participant's
    personal undo), and where the participant's own audio files go.

    Without one (no co-op session), the Session edits on its own with its UndoManager.
*/
class Collaboration
{
public:
    virtual ~Collaboration() = default;

    /** Whether an edit touching these scopes may happen now; if not, why (for the user). */
    [[nodiscard]] virtual bool mayEdit(const std::vector<model::EditScope>& scopes,
                                       juce::String& why) = 0;
    /** True when this participant holds the scope's lock (not merely free). */
    [[nodiscard]] virtual bool holds(const model::EditScope& scope) const = 0;

    /** The changes made from now on are a transaction of this name: a new undo step, the
        step of the gesture in progress (continuesStep), or none (not undoable). */
    virtual void beginTransaction(const juce::String& name, bool undoable, bool continuesStep) = 0;
    virtual void beginGesture() = 0;
    virtual void endGesture() = 0;

    virtual bool undo() = 0;
    virtual bool redo() = 0;
    [[nodiscard]] virtual bool canUndo() const = 0;
    [[nodiscard]] virtual bool canRedo() const = 0;
    [[nodiscard]] virtual juce::String getUndoName() const = 0;
    [[nodiscard]] virtual juce::String getRedoName() const = 0;

    /** Where this participant's recordings and imported files go, inside the copy. */
    [[nodiscard]] virtual juce::File getOwnFilesFolder() const = 0;
    /** One of this participant's files, inside the copy: it is hashed, then sent. */
    virtual void addOwnFile(const juce::File& file) = 0;
    /** True once an own file is hashed: the project may refer to it. */
    [[nodiscard]] virtual bool isFileReady(const juce::File& file) const = 0;

    /** Keeps these scopes locked (armed tracks while recording, plugin states not sent
        yet); call again regularly while needed. */
    virtual void holdLocks(const std::vector<model::EditScope>& scopes) = 0;
    virtual void setRecording(bool recording) = 0;
};

} // namespace daw::controller
