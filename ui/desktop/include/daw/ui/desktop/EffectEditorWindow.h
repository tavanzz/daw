#pragma once

#include "daw/controller/Session.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

namespace daw::ui::desktop
{

/**
    The editor of one effect, in its own window: a header (the effect and where it
    is, Bypass, and a warning with Reactivate when it was silenced for producing NaN
    or infinity), then what suits the effect:
    - equaliser: its response curve with a handle per band, and the selected band's
      controls;
    - compressor: its static curve and gain reduction, and its controls;
    - limiter: its gain reduction and controls;
    - any other: controls made from its parameters (GenericEffectEditor).
*/
class EffectEditorWindow final : public juce::DocumentWindow
{
public:
    EffectEditorWindow(controller::Session& session, model::ObjectId effect);
    ~EffectEditorWindow() override;

    [[nodiscard]] model::ObjectId getEffectId() const noexcept { return effectId; }

    /** At the UI's refresh rate. */
    void refresh(double elapsedSeconds);

    std::function<void()> onClose;
    void closeButtonPressed() override;
    void lookAndFeelChanged() override;

private:
    class Content;

    const model::ObjectId effectId;
    Content* content = nullptr;
};

/** The effect editor windows that are open: at most one per effect. */
class EffectEditors
{
public:
    explicit EffectEditors(controller::Session& sessionToEdit);
    ~EffectEditors();

    /** Opens the editor of an effect, or brings it to the front. */
    void open(model::ObjectId effectId);

    /** Refreshes the open editors; closes those whose effect is gone. */
    void refresh(double elapsedSeconds);

    void closeAll();

private:
    controller::Session& session;
    std::vector<std::unique_ptr<EffectEditorWindow>> windows;
};

} // namespace daw::ui::desktop
