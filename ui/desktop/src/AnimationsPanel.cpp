#include "daw/ui/desktop/AnimationsPanel.h"

namespace daw::ui::desktop
{

namespace
{
using appearance::AnimationEffect;
using appearance::AnimationLevel;
using appearance::ColourRole;
using appearance::Cost;
using appearance::FontRole;

constexpr std::array<AnimationLevel, 3> levels{AnimationLevel::full, AnimationLevel::reduced,
                                               AnimationLevel::off};
constexpr int levelRadioGroup = 4201;

#if JUCE_MAC
constexpr const char* commandKey = "Cmd";
#else
constexpr const char* commandKey = "Ctrl";
#endif

ColourRole roleOf(Cost cost)
{
    return cost == Cost::low      ? ColourRole::textSecondary
           : cost == Cost::medium ? ColourRole::warning
                                  : ColourRole::error;
}
} // namespace

void AnimationsPanel::LoadBar::setLoad(int litPoints, int totalPoints)
{
    lit = litPoints;
    total = std::max(1, totalPoints);
    repaint();
}

void AnimationsPanel::LoadBar::paint(juce::Graphics& g)
{
    const auto& look = ThemedLookAndFeel::get(*this);
    const auto area = getLocalBounds().toFloat();
    const float gapWidth = 3.0f;
    const float width =
        (area.getWidth() - gapWidth * static_cast<float>(total - 1)) / static_cast<float>(total);

    for (int index = 0; index < total; ++index)
    {
        const auto segment =
            area.withX(area.getX() + static_cast<float>(index) * (width + gapWidth))
                .withWidth(width);
        g.setColour(look.colour(index < lit ? ColourRole::accent : ColourRole::raised));
        g.fillPath(look.shapeOf(segment, CornerSize::small));
    }
}

AnimationsPanel::AnimationsPanel(ThemeManager& themeManager) : themes(themeManager)
{
    heading.setText("Animations", juce::dontSendNotification);
    ThemedLookAndFeel::setFontRole(heading, FontRole::title);
    addAndMakeVisible(heading);

    levelLabel.setText("Level", juce::dontSendNotification);
    effectsLabel.setText("Effects", juce::dontSendNotification);
    loadLabel.setText("Load", juce::dontSendNotification);
    for (auto* label : {&levelLabel, &effectsLabel, &loadLabel})
    {
        ThemedLookAndFeel::setFontRole(*label, FontRole::heading);
        addAndMakeVisible(*label);
    }

    for (std::size_t index = 0; index < levels.size(); ++index)
    {
        auto& button = levelButtons[index];
        const auto level = levels[index];
        button.setButtonText(appearance::displayName(level));
        button.setRadioGroupId(levelRadioGroup);
        button.setClickingTogglesState(true);
        button.onClick = [this, level]
        {
            change([level](appearance::AnimationSettings& settings) { settings.setLevel(level); });
        };
        addAndMakeVisible(button);
    }
    levelButtons[1].setTooltip("No moving backgrounds, glitches or panel transitions");

    for (const auto effect : appearance::allAnimationEffects())
    {
        auto& row = rows[static_cast<std::size_t>(effect)];
        row.toggle.setButtonText(appearance::displayName(effect));
        row.toggle.onClick = [this, effect, &row]
        {
            change(
                [effect, on = row.toggle.getToggleState()](appearance::AnimationSettings& settings)
                { settings.setSwitchedOn(effect, on); });
        };
        ThemedLookAndFeel::setFontRole(row.cost, FontRole::caption);
        ThemedLookAndFeel::setFontRole(row.description, FontRole::caption);
        ThemedLookAndFeel::setColourRole(row.description, ColourRole::textSecondary);
        addAndMakeVisible(row.toggle);
        addAndMakeVisible(row.cost);
        addAndMakeVisible(row.description);
    }

    addAndMakeVisible(loadBar);
    ThemedLookAndFeel::setFontRole(loadText, FontRole::numeric);
    addAndMakeVisible(loadText);

    followSystem.onClick = [this]
    {
        change([on = followSystem.getToggleState()](appearance::AnimationSettings& settings)
               { settings.followSystemReduceMotion = on; });
    };
    addAndMakeVisible(followSystem);

    ThemedLookAndFeel::setColourRole(systemNote, ColourRole::warning);
    ThemedLookAndFeel::setFontRole(systemNote, FontRole::caption);
    addAndMakeVisible(systemNote);

    shortcutNote.setText(juce::String("Shift+") + commandKey +
                             "+A switches every animation off, and again back on.",
                         juce::dontSendNotification);
    ThemedLookAndFeel::setColourRole(shortcutNote, ColourRole::textSecondary);
    ThemedLookAndFeel::setFontRole(shortcutNote, FontRole::caption);
    addAndMakeVisible(shortcutNote);

    themes.addListener(*this);
    show();
    setSize(620, 500);
}

AnimationsPanel::~AnimationsPanel()
{
    themes.removeListener(*this);
}

void AnimationsPanel::themeChanged()
{
    show(); // costs and descriptions are the theme's
}

void AnimationsPanel::animationSettingsChanged()
{
    show();
}

void AnimationsPanel::change(const std::function<void(appearance::AnimationSettings&)>& edit)
{
    auto settings = themes.getPreferences().animations;
    edit(settings);
    themes.setAnimationSettings(settings);
}

void AnimationsPanel::show()
{
    const auto& settings = themes.getPreferences().animations;
    const auto& theme = themes.getTheme();
    const bool systemReduces = themes.systemAsksToReduceMotion();
    const auto level = settings.effectiveLevel(systemReduces);

    for (std::size_t index = 0; index < levels.size(); ++index)
        levelButtons[index].setToggleState(settings.level == levels[index],
                                           juce::dontSendNotification);

    for (const auto effect : appearance::allAnimationEffects())
    {
        auto& row = rows[static_cast<std::size_t>(effect)];
        const auto& look = theme.effect(effect);
        const bool allowed = appearance::isAllowedAt(level, effect);

        row.toggle.setToggleState(settings.isSwitchedOn(effect), juce::dontSendNotification);
        row.toggle.setEnabled(look.available && allowed);

        juce::String cost = look.available ? appearance::displayName(look.cost) + " cost"
                                           : juce::String("Not in this theme");
        if (look.available && !allowed)
            cost << ", off at " << appearance::displayName(level);
        row.cost.setText(cost, juce::dontSendNotification);
        ThemedLookAndFeel::setColourRole(row.cost,
                                         allowed ? roleOf(look.cost) : ColourRole::textSecondary);
        row.description.setText(look.description, juce::dontSendNotification);
    }

    const auto load = appearance::animationLoad(settings, theme, systemReduces);
    loadBar.setLoad(load.points, load.maximumPoints);
    loadText.setText((load.rating ? appearance::displayName(*load.rating) : juce::String("None")) +
                         "  (" + juce::String(load.points) + " of " +
                         juce::String(load.maximumPoints) + ")",
                     juce::dontSendNotification);

    followSystem.setToggleState(settings.followSystemReduceMotion, juce::dontSendNotification);
    systemNote.setText(systemReduces && settings.followSystemReduceMotion
                           ? "The system asks apps to reduce motion: animations are at most "
                             "Reduced."
                           : juce::String(),
                       juce::dontSendNotification);
}

void AnimationsPanel::paint(juce::Graphics& g)
{
    ThemedLookAndFeel::get(*this).drawPanel(g, getLocalBounds().toFloat(), ColourRole::window,
                                            false);
}

void AnimationsPanel::resized()
{
    auto area = getLocalBounds().reduced(18);
    heading.setBounds(area.removeFromTop(30));
    area.removeFromTop(10);

    auto levelRow = area.removeFromTop(30);
    levelLabel.setBounds(levelRow.removeFromLeft(90));
    for (auto& button : levelButtons)
    {
        button.setBounds(levelRow.removeFromLeft(110));
        levelRow.removeFromLeft(6);
    }
    area.removeFromTop(16);

    effectsLabel.setBounds(area.removeFromTop(24));
    for (auto& row : rows)
    {
        auto line = area.removeFromTop(40);
        row.toggle.setBounds(line.removeFromLeft(200).withHeight(24));
        auto details = line.withTrimmedLeft(4);
        row.cost.setBounds(details.removeFromTop(18));
        row.description.setBounds(details.removeFromTop(18));
    }
    area.removeFromTop(10);

    auto loadRow = area.removeFromTop(26);
    loadLabel.setBounds(loadRow.removeFromLeft(90));
    loadBar.setBounds(loadRow.removeFromLeft(240).reduced(0, 6));
    loadRow.removeFromLeft(10);
    loadText.setBounds(loadRow);
    area.removeFromTop(14);

    followSystem.setBounds(area.removeFromTop(26));
    systemNote.setBounds(area.removeFromTop(20));
    shortcutNote.setBounds(area.removeFromBottom(20));
}

} // namespace daw::ui::desktop
