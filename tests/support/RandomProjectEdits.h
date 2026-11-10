#pragma once

#include "daw/model/Project.h"
#include "daw/model/ProjectChanges.h"

#include <juce_core/juce_core.h>

#include <functional>

namespace daw::test
{

/**
    Makes random edits to a project through the model's setters, as the controller would:
    tracks, clips, notes, mixer settings, effects, instruments, plugins, buses, sends,
    automation, tempo, the loop, personal settings, and undo or redo when it has an undo
    manager. Deterministic for a seed.

    mayEdit, when set, limits the edits to what it allows (the scopes of a co-op
    participant's locks); an edit that needs several scopes needs them all.
*/
class RandomProjectEditor
{
public:
    explicit RandomProjectEditor(juce::int64 seed, juce::File audioFolder = {});

    /** One edit (sometimes none, when nothing allowed fits); says what it did. */
    juce::String edit(model::Project& project, juce::UndoManager* undoManager);

    std::function<bool(const model::EditScope&)> mayEdit;

    /** Undo and redo of the undo manager, one in this many edits (0: never). */
    int undoOneIn = 8;

    [[nodiscard]] juce::Random& getRandom() noexcept { return random; }

private:
    [[nodiscard]] bool allowed(const model::EditScope& scope) const;
    [[nodiscard]] juce::File randomFile();
    [[nodiscard]] double randomUnit() { return random.nextDouble(); }
    [[nodiscard]] int randomInt(int limit) { return random.nextInt(juce::jmax(1, limit)); }

    juce::String editProject(model::Project& project, juce::UndoManager* undoManager);
    juce::String editTrack(model::Project& project, int trackIndex, juce::UndoManager* undoManager);
    juce::String editBus(model::Project& project, int busIndex, juce::UndoManager* undoManager);
    juce::String editMaster(model::Project& project, juce::UndoManager* undoManager);

    juce::Random random;
    juce::File folder;
};

/** Strict equality including personal settings and the order of properties, for tests. */
[[nodiscard]] bool isExactlyTheSame(const juce::ValueTree& a, const juce::ValueTree& b);

} // namespace daw::test
