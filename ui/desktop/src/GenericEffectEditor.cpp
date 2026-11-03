#include "daw/ui/desktop/GenericEffectEditor.h"

namespace daw::ui::desktop
{

// One parameter: its name and a control. Continuous parameters are shown on a
// slider over 0..1 (the parameter's own mapping), so logarithmic ones feel even.
class GenericEffectEditor::Row final : public juce::Component
{
public:
    Row(controller::Session& sessionToEdit, model::ObjectId effect,
        const engine::fx::ParameterInfo& parameterInfo)
        : session(sessionToEdit)
        , effectId(effect)
        , info(parameterInfo)
    {
        label.setText(info.name, juce::dontSendNotification);
        addAndMakeVisible(label);

        if (info.isChoice() && info.choices.size() == 2 && info.choices[0] == "Off")
        {
            toggle.setButtonText(info.choices[1]);
            toggle.onClick = [this] { set(toggle.getToggleState() ? 1.0f : 0.0f); };
            addAndMakeVisible(toggle);
        }
        else if (info.isChoice())
        {
            for (int i = 0; i < info.choices.size(); ++i)
                menu.addItem(info.choices[i], i + 1);
            menu.onChange = [this] { set(static_cast<float>(menu.getSelectedId() - 1)); };
            addAndMakeVisible(menu);
        }
        else
        {
            slider.setSliderStyle(juce::Slider::LinearHorizontal);
            slider.setRange(0.0, 1.0, 0.0);
            slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 76, 18);
            slider.textFromValueFunction = [this](double position) {
                return info.toText(info.fromNormalised(static_cast<float>(position)));
            };
            slider.valueFromTextFunction = [this](const juce::String& text) {
                // Typed in the parameter's units ("1.2k" is not understood: plain numbers).
                auto value = static_cast<float>(text.getDoubleValue());
                if (text.containsIgnoreCase("k") && (info.unit == "Hz"))
                    value *= 1000.0f;
                if (text.containsIgnoreCase(" s") && info.unit == "ms")
                    value *= 1000.0f;
                return static_cast<double>(info.toNormalised(value));
            };
            slider.setDoubleClickReturnValue(true, info.toNormalised(info.defaultValue));
            slider.onDragStart = [this] { session.beginGesture("Change " + info.name); };
            slider.onDragEnd = [this] { session.endGesture(); };
            slider.onValueChange = [this] {
                set(info.fromNormalised(static_cast<float>(slider.getValue())));
            };
            slider.setWantsKeyboardFocus(false);
            slider.updateText(); // at 0 the value never changes, so the text needs telling
            addAndMakeVisible(slider);
        }
    }

    void refresh()
    {
        const auto location = session.findEffect(effectId);
        const auto effect = session.getEffect(effectId);
        if (!location || !effect)
            return;

        float value = info.clamp(static_cast<float>(
            effect->getParameter(info.id).value_or(static_cast<double>(info.defaultValue))));
        const auto automated = session.getAutomatedValue(
            location->owner, model::AutomationTarget::effectParameter(effectId, info.id));
        if (automated)
            value = info.fromNormalised(static_cast<float>(*automated));

        for (auto* control : std::initializer_list<juce::Component*>{&slider, &menu, &toggle})
            control->setEnabled(!automated);

        if (!slider.isMouseButtonDown())
            slider.setValue(info.toNormalised(value), juce::dontSendNotification);
        menu.setSelectedId(static_cast<int>(value) + 1, juce::dontSendNotification);
        toggle.setToggleState(value >= 0.5f, juce::dontSendNotification);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(2);
        label.setBounds(area.removeFromLeft(std::min(120, area.getWidth() / 3)));
        for (auto* control : std::initializer_list<juce::Component*>{&slider, &menu, &toggle})
            control->setBounds(area);
    }

private:
    void set(float value) { session.setEffectParameter(effectId, info.id, value); }

    controller::Session& session;
    const model::ObjectId effectId;
    const engine::fx::ParameterInfo info;
    juce::Label label;
    juce::Slider slider;
    juce::ComboBox menu;
    juce::ToggleButton toggle;
};

GenericEffectEditor::GenericEffectEditor(controller::Session& sessionToEdit, model::ObjectId effect,
                                         const engine::fx::EffectDescription& description,
                                         std::vector<int> parameters, int columns)
    : session(sessionToEdit)
    , effectId(effect)
    , numColumns(std::max(1, columns))
{
    for (const int index : parameters)
    {
        if (index < 0 || static_cast<std::size_t>(index) >= description.parameters.size())
            continue;
        rows.push_back(std::make_unique<Row>(session, effectId,
                                             description.parameters[static_cast<std::size_t>(index)]));
        addAndMakeVisible(*rows.back());
    }
    refresh();
}

GenericEffectEditor::~GenericEffectEditor() = default;

void GenericEffectEditor::refresh()
{
    for (auto& row : rows)
        row->refresh();
}

int GenericEffectEditor::getPreferredHeight() const noexcept
{
    const auto perColumn = (static_cast<int>(rows.size()) + numColumns - 1) / numColumns;
    return perColumn * rowHeight + 8;
}

void GenericEffectEditor::resized()
{
    const auto perColumn = std::max(1, (static_cast<int>(rows.size()) + numColumns - 1) / numColumns);
    const int columnWidth = (getWidth() - 8) / numColumns;
    for (std::size_t i = 0; i < rows.size(); ++i)
    {
        const int column = static_cast<int>(i) / perColumn;
        const int line = static_cast<int>(i) % perColumn;
        rows[i]->setBounds(4 + column * columnWidth, 4 + line * rowHeight, columnWidth - 4, rowHeight);
    }
}

} // namespace daw::ui::desktop
