#pragma once

#include "daw/model/ProjectChanges.h"

#include <juce_data_structures/juce_data_structures.h>

namespace daw::model
{

/**
    Records every change made to a project state as ProjectChanges, with what was there
    before, whoever makes it (setters with or without an UndoManager, undo and redo).

    juce::ValueTree tells its listeners what changed but not what the value was, so the
    recorder keeps a copy of the state (its shadow) that follows every change: the value
    before is read there.

    Changes of personal settings (SharedProject.h) are followed but not recorded. While
    recording is off (changes coming from elsewhere), changes are followed and not
    recorded either.

    Message thread only.
*/
class ChangeRecorder : private juce::ValueTree::Listener
{
public:
    explicit ChangeRecorder(juce::ValueTree projectState);
    ~ChangeRecorder() override;

    ChangeRecorder(const ChangeRecorder&) = delete;
    ChangeRecorder& operator=(const ChangeRecorder&) = delete;

    /** The changes recorded since the last call, oldest first. */
    [[nodiscard]] ChangeSet takeChanges();
    [[nodiscard]] bool hasChanges() const noexcept { return !changes.empty(); }

    /** Off: changes are followed but not recorded (changes from other participants). */
    void setRecording(bool shouldRecord) noexcept { recording = shouldRecord; }
    [[nodiscard]] bool isRecording() const noexcept { return recording; }

    /** Turns recording off for its lifetime. */
    class Pause
    {
    public:
        explicit Pause(ChangeRecorder& recorderToPause)
            : recorder(recorderToPause), wasRecording(recorderToPause.isRecording())
        {
            recorder.setRecording(false);
        }
        ~Pause() { recorder.setRecording(wasRecording); }

        Pause(const Pause&) = delete;
        Pause& operator=(const Pause&) = delete;

    private:
        ChangeRecorder& recorder;
        bool wasRecording;
    };

    /** The state as the recorder last saw it, for tests. */
    [[nodiscard]] const juce::ValueTree& getShadow() const noexcept { return shadow; }

private:
    void valueTreePropertyChanged(juce::ValueTree& tree, const juce::Identifier& property) override;
    void valueTreeChildAdded(juce::ValueTree& parent, juce::ValueTree& child) override;
    void valueTreeChildRemoved(juce::ValueTree& parent, juce::ValueTree& child, int index) override;
    void valueTreeChildOrderChanged(juce::ValueTree& parent, int oldIndex, int newIndex) override;
    void valueTreeRedirected(juce::ValueTree& tree) override;

    void record(ProjectChange change);

    juce::ValueTree state;
    juce::ValueTree shadow;
    ChangeSet changes;
    bool recording = true;
};

} // namespace daw::model
