#include "daw/ui/desktop/AnimationsPanel.h"
#include "daw/ui/desktop/ThemePreviewsPanel.h"

#include <catch2/catch_test_macros.hpp>

using namespace daw;
using appearance::AnimationEffect;
using appearance::AnimationLevel;
using ui::desktop::ThemeManager;

namespace
{
// Writes a picture of the component when DAW_UI_SNAPSHOTS names a folder, so the panels
// can be looked at without opening the app.
void snapshot(juce::Component& component, const juce::String& name)
{
    const auto folder = juce::SystemStats::getEnvironmentVariable("DAW_UI_SNAPSHOTS", {});
    if (folder.isEmpty())
        return;

    const auto image = component.createComponentSnapshot(component.getLocalBounds(), true, 2.0f);
    const juce::File file = juce::File(folder).getChildFile(name + ".png");
    file.deleteFile();
    juce::FileOutputStream stream(file);
    juce::PNGImageFormat().writeImageToStream(image, stream);
}

// What a click does to a toggling button, without the event loop.
void click(juce::Button& button)
{
    button.setToggleState(button.getRadioGroupId() != 0 || !button.getToggleState(),
                          juce::dontSendNotification);
    if (button.onClick)
        button.onClick();
}

template <typename Predicate>
juce::Component* findChild(juce::Component& parent, Predicate predicate)
{
    for (auto* child : parent.getChildren())
    {
        if (predicate(*child))
            return child;
        if (auto* found = findChild(*child, predicate))
            return found;
    }
    return nullptr;
}

juce::Button* buttonNamed(juce::Component& parent, const juce::String& text)
{
    return dynamic_cast<juce::Button*>(
        findChild(parent,
                  [&text](juce::Component& child)
                  {
                      auto* button = dynamic_cast<juce::Button*>(&child);
                      return button != nullptr && button->getButtonText() == text;
                  }));
}
} // namespace

TEST_CASE("The theme previews show every theme and clicking one uses it", "[ui]")
{
    ThemeManager themes({});
    ui::desktop::ThemePreviewsPanel panel(themes);
    panel.setBounds(panel.getLocalBounds());

    int cards = 0;
    juce::Component* acidCard = nullptr;
    for (auto* child : panel.getChildren())
        if (child->getTitle().isNotEmpty())
        {
            ++cards;
            if (child->getTitle() == "Acid Terminal")
                acidCard = child;
        }

    CHECK(cards == 3);
    REQUIRE(acidCard != nullptr);

    for (const auto& theme : themes.getLibrary().getThemes())
    {
        themes.setTheme(theme.id);
        snapshot(panel, "previews-" + theme.id);
    }

    themes.setTheme("neon-grid");
    const auto centre = acidCard->getLocalBounds().getCentre().toFloat();
    const juce::MouseEvent click(juce::Desktop::getInstance().getMainMouseSource(), centre, {},
                                 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, acidCard, acidCard,
                                 juce::Time::getCurrentTime(), centre, juce::Time::getCurrentTime(),
                                 1, false);
    acidCard->mouseUp(click);

    CHECK(themes.getTheme().id == "acid-terminal");
}

TEST_CASE("The animations panel changes the settings for every theme", "[ui]")
{
    ThemeManager themes({});
    themes.setSystemReducesMotionForTesting(false);
    ui::desktop::AnimationsPanel panel(themes);
    panel.setBounds(panel.getLocalBounds());
    snapshot(panel, "animations-full");

    auto* reduced = buttonNamed(panel, "Reduced");
    auto* glitch = buttonNamed(panel, "Glitch");
    auto* glow = buttonNamed(panel, "Cursor glow");
    REQUIRE(reduced != nullptr);
    REQUIRE(glitch != nullptr);
    REQUIRE(glow != nullptr);

    click(*reduced);
    CHECK(themes.getPreferences().animations.level == AnimationLevel::reduced);
    CHECK_FALSE(glitch->isEnabled()); // reduced turns glitches off
    CHECK(glow->isEnabled());
    snapshot(panel, "animations-reduced");

    click(*glow);
    CHECK_FALSE(themes.getPreferences().animations.isSwitchedOn(AnimationEffect::cursorGlow));

    // Switched all off elsewhere (Shift+Cmd+A): the panel follows.
    auto settings = themes.getPreferences().animations;
    settings.toggleAllOff();
    themes.setAnimationSettings(settings);
    CHECK_FALSE(glow->isEnabled());
    CHECK(buttonNamed(panel, "Off")->getToggleState());
}
