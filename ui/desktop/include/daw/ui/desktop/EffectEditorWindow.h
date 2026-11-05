#pragma once

#include "daw/controller/Session.h"
#include "daw/ui/desktop/EditorWindow.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

namespace daw::ui::desktop
{

/**
    The editor of one effect or instrument, in its own window: a header (the effect and where it
    is, Bypass, and a warning with Reactivate when it was silenced for producing NaN
    or infinity), then what suits the effect:
    - equaliser: its response curve with a handle per band, and the selected band's
      controls;
    - compressor: its static curve and gain reduction, and its controls;
    - limiter: its gain reduction and controls;
    - sampler: its waveform with start and loop, its file (SamplerView);
    - drum sampler: the pads, the selected pad's controls and the kit's (DrumPadsView);
    - synth (any other instrument): controls in sections (SectionedEffectEditor);
    - any other effect: controls made from its parameters (GenericEffectEditor).
*/
class EffectEditorWindow final : public EditorWindow
{
public:
    EffectEditorWindow(controller::Session& session, model::ObjectId effect);
    ~EffectEditorWindow() override;

    [[nodiscard]] model::ObjectId getEffectId() const noexcept override { return effectId; }

    /** At the UI's refresh rate. */
    void refresh(double elapsedSeconds) override;

private:
    class Content;

    const model::ObjectId effectId;
    Content* content = nullptr;
};

/** The effect editor windows that are open: at most one per effect; a plugin's is a
    PluginEditorWindow. */
class EffectEditors
{
public:
    EffectEditors(controller::Session& sessionToEdit, WindowPositions& pluginWindowPositions);
    ~EffectEditors();

    /** Opens the editor of an effect, or brings it to the front. */
    void open(model::ObjectId effectId);

    /** Refreshes the open editors; closes those whose effect is gone. */
    void refresh(double elapsedSeconds);

    void closeAll();

    /** The open window of an effect; null if none. */
    [[nodiscard]] EditorWindow* find(model::ObjectId effectId) const noexcept;

private:
    controller::Session& session;
    WindowPositions& positions;
    std::vector<std::unique_ptr<EditorWindow>> windows;
};

} // namespace daw::ui::desktop
