#pragma once

#include "daw/appearance/Theme.h"
#include "daw/ui/desktop/ThemeFonts.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace daw::ui::desktop
{

[[nodiscard]] inline juce::Colour toJuce(appearance::Colour colour) noexcept
{
    return juce::Colour(colour.toArgb());
}

/** Corner sizes of the theme's shapes (appearance::Shape). */
enum class CornerSize
{
    small,  ///< buttons, slots
    medium, ///< clips, fields, menus
    large   ///< panels, dialogs
};

/**
    Draws JUCE's widgets (buttons, sliders, combo boxes, menus, alerts, tooltips, text
    fields, scroll bars, progress bars, the Windows and Linux menu bar) in a theme:
    its colours, fonts and shapes. Components never use literal colours or fonts: they
    ask the look and feel (ThemedLookAndFeel::of(component)) for roles.

    A Label's font follows its "fontRole" property (a FontRole key such as "numeric";
    "label" by default).

    One subclass per built-in theme adds what data alone cannot say; make() picks it.
    Message thread.
*/
class ThemedLookAndFeel : public juce::LookAndFeel_V4
{
public:
    ThemedLookAndFeel(const appearance::Theme& themeToDraw, const ThemeFonts& fontsToUse);

    /** The look and feel for a theme: its own subclass for the built-in ones. */
    [[nodiscard]] static std::unique_ptr<ThemedLookAndFeel>
    make(const appearance::Theme& themeToDraw, const ThemeFonts& fontsToUse);

    /** The themed look and feel a component draws with, or nullptr if it has another. */
    [[nodiscard]] static ThemedLookAndFeel* of(juce::Component& component) noexcept;

    /** The property of a Label naming its FontRole. */
    static inline const juce::Identifier fontRoleProperty{"fontRole"};

    [[nodiscard]] const appearance::Theme& getTheme() const noexcept { return theme; }
    [[nodiscard]] const ThemeFonts& getFonts() const noexcept { return fonts; }

    [[nodiscard]] juce::Colour colour(appearance::ColourRole role) const noexcept;
    [[nodiscard]] juce::Font font(appearance::FontRole role) const;
    [[nodiscard]] juce::Font font(appearance::FontRole role, float height) const;

    /** The text colour that reads best on this fill (appearance::textRoleOn()). */
    [[nodiscard]] juce::Colour textColourOn(juce::Colour fill) const noexcept;

    [[nodiscard]] float cornerSize(CornerSize size) const noexcept;

    /** The outline of a rectangle in the theme's corner style: rounded, sharp, or cut at
        45 degrees (top left and bottom right). */
    [[nodiscard]] juce::Path shapeOf(juce::Rectangle<float> area, CornerSize size) const;

    //==============================================================================
    void drawButtonBackground(juce::Graphics& g, juce::Button& button,
                              const juce::Colour& backgroundColour, bool isMouseOverButton,
                              bool isButtonDown) override;
    juce::Font getTextButtonFont(juce::TextButton& button, int buttonHeight) override;
    void drawButtonText(juce::Graphics& g, juce::TextButton& button, bool isMouseOverButton,
                        bool isButtonDown) override;
    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& button, bool isMouseOverButton,
                          bool isButtonDown) override;
    void drawTickBox(juce::Graphics& g, juce::Component& component, float x, float y, float w,
                     float h, bool ticked, bool isEnabled, bool isMouseOverButton,
                     bool isButtonDown) override;

    juce::Font getLabelFont(juce::Label& label) override;
    void drawLabel(juce::Graphics& g, juce::Label& label) override;

    void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                          float minSliderPos, float maxSliderPos, juce::Slider::SliderStyle style,
                          juce::Slider& slider) override;
    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPosProportional, float rotaryStartAngle, float rotaryEndAngle,
                          juce::Slider& slider) override;
    juce::Label* createSliderTextBox(juce::Slider& slider) override;
    juce::Font getSliderPopupFont(juce::Slider& slider) override;

    void drawComboBox(juce::Graphics& g, int width, int height, bool isButtonDown, int buttonX,
                      int buttonY, int buttonW, int buttonH, juce::ComboBox& box) override;
    juce::Font getComboBoxFont(juce::ComboBox& box) override;
    void positionComboBoxText(juce::ComboBox& box, juce::Label& label) override;

    void drawPopupMenuBackground(juce::Graphics& g, int width, int height) override;
    void drawPopupMenuItem(juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator,
                           bool isActive, bool isHighlighted, bool isTicked, bool hasSubMenu,
                           const juce::String& text, const juce::String& shortcutKeyText,
                           const juce::Drawable* icon, const juce::Colour* textColour) override;
    juce::Font getPopupMenuFont() override;

    void drawMenuBarBackground(juce::Graphics& g, int width, int height, bool isMouseOverBar,
                               juce::MenuBarComponent& menuBar) override;
    juce::Font getMenuBarFont(juce::MenuBarComponent& menuBar, int itemIndex,
                              const juce::String& itemText) override;

    void drawScrollbar(juce::Graphics& g, juce::ScrollBar& scrollbar, int x, int y, int width,
                       int height, bool isScrollbarVertical, int thumbStartPosition, int thumbSize,
                       bool isMouseOver, bool isMouseDown) override;

    juce::Rectangle<int> getTooltipBounds(const juce::String& tipText, juce::Point<int> screenPos,
                                          juce::Rectangle<int> parentArea) override;
    void drawTooltip(juce::Graphics& g, const juce::String& text, int width, int height) override;

    void fillTextEditorBackground(juce::Graphics& g, int width, int height,
                                  juce::TextEditor& editor) override;
    void drawTextEditorOutline(juce::Graphics& g, int width, int height,
                               juce::TextEditor& editor) override;

    void drawProgressBar(juce::Graphics& g, juce::ProgressBar& bar, int width, int height,
                         double progress, const juce::String& textToShow) override;

    void drawAlertBox(juce::Graphics& g, juce::AlertWindow& alert,
                      const juce::Rectangle<int>& textArea, juce::TextLayout& layout) override;
    juce::Font getAlertWindowTitleFont() override;
    juce::Font getAlertWindowMessageFont() override;
    juce::Font getAlertWindowFont() override;

    void drawGroupComponentOutline(juce::Graphics& g, int width, int height,
                                   const juce::String& text, const juce::Justification& position,
                                   juce::GroupComponent& group) override;

    void drawCallOutBoxBackground(juce::CallOutBox& box, juce::Graphics& g, const juce::Path& path,
                                  juce::Image& cachedImage) override;

protected:
    /** Text as the theme shows labels: capitals for roles that ask for them, and the
        terminal decoration for Acid Terminal's headings. */
    [[nodiscard]] virtual juce::String labelText(appearance::FontRole role,
                                                 const juce::String& text) const;

    /** Draws the border of a control; themes with glows add theirs. */
    virtual void drawControlOutline(juce::Graphics& g, const juce::Path& outline, bool isActive,
                                    juce::Colour activeColour) const;

    [[nodiscard]] appearance::FontRole roleOf(const juce::Label& label) const;

    const appearance::Theme& theme;
    const ThemeFonts& fonts;

private:
    void setColoursFromTheme();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ThemedLookAndFeel)
};

} // namespace daw::ui::desktop
