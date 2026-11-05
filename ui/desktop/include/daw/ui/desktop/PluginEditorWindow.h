#pragma once

#include "daw/controller/PluginLoads.h"
#include "daw/controller/Session.h"
#include "daw/ui/desktop/EditorWindow.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace daw::ui::desktop
{

/**
    A plugin's window: a header in the theme (the plugin and where it is, Bypass, a
    switch between the plugin's own editor and the generic one, its latency and CPU,
    a warning with Reactivate after NaN or infinity, a warning when it is slow), then
    the plugin's own editor (when it has one) or the generic editor of its parameters.
    A plugin that is missing, failed or kept from loading shows why, and "Load" for
    one kept back.

    The window floats over the app's windows while the app is in front, and hides
    while another app is; it opens where this plugin's window was last
    (WindowPositions). It keeps its plugin alive until it closes: the plugin's editor
    must go first.
*/
class PluginEditorWindow final : public EditorWindow
{
public:
    PluginEditorWindow(controller::Session& session, model::ObjectId effect, WindowPositions& positions,
                       const controller::PluginLoads& loads);
    ~PluginEditorWindow() override;

    [[nodiscard]] model::ObjectId getEffectId() const noexcept override { return effectId; }
    void refresh(double elapsedSeconds) override;
    void moved() override;

    /** Whether it shows the plugin's own editor (else the generic one). Tests. */
    [[nodiscard]] bool isShowingPluginEditor() const noexcept;

private:
    class Content;

    const model::ObjectId effectId;
    WindowPositions& positions;
    juce::String positionKey; ///< the plugin's PluginKey
    Content* content = nullptr;
    bool placed = false;
    bool hiddenInBackground = false;
};

} // namespace daw::ui::desktop
