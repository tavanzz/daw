#pragma once

#include "daw/controller/Session.h"
#include "daw/engine/fx/Effect.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

namespace daw::ui::desktop
{

/**
    Controls for some of an effect's parameters, made from their descriptions: a
    slider for a range (in its scale: logarithmic ones evenly in ratios), a menu for
    a choice, a button for a switch, each with its value as text. A drag is one undo
    step. A parameter whose automation reads follows it and cannot be moved.
*/
class GenericEffectEditor final : public juce::Component
{
public:
    static constexpr int rowHeight = 26;

    /** For a plugin: its value where the model holds none (the plugin's state has
        it), and its value as the plugin writes it. */
    struct PluginHooks
    {
        std::function<float(int index)> value;
        std::function<juce::String(int index, float normalised)> text;
    };

    /** parameters: indices into description's parameters, in the order shown. */
    GenericEffectEditor(controller::Session& sessionToEdit, model::ObjectId effect,
                        const engine::fx::EffectDescription& description,
                        std::vector<int> parameters, int columns = 1, PluginHooks hooks = {});
    ~GenericEffectEditor() override;

    /** Shows the model's values (and automation's). */
    void refresh();

    [[nodiscard]] int getPreferredHeight() const noexcept;

    void resized() override;

private:
    class Row;

    controller::Session& session;
    const model::ObjectId effectId;
    std::vector<std::unique_ptr<Row>> rows;
    int numColumns = 1;
    PluginHooks plugin;
};

} // namespace daw::ui::desktop
