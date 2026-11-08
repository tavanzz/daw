#pragma once

#include "daw/appearance/Theme.h"
#include "daw/ui/desktop/AnimationClock.h"
#include "daw/ui/desktop/PrerenderedImages.h"
#include "daw/ui/desktop/ThemeFonts.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <optional>
#include <span>

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

    Components say what they are through properties (setFontRole(), setColourRole(),
    setSurface()): a Label's font role ("label" by default), its text colour role, its
    background (a display on an inset surface); a toggle button's fill when on (a
    status colour such as "mute"). The look and feel turns roles into the theme's
    colours and fonts, so a theme change needs nothing from the components.

    The DAW's own components (lanes, clips, ruler, meters, headers, logo, status bar)
    draw through the methods below the JUCE overrides.

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
    [[nodiscard]] static ThemedLookAndFeel* of(const juce::Component& component) noexcept;

    /** The property of a Label naming its FontRole. */
    static inline const juce::Identifier fontRoleProperty{"fontRole"};
    /** A Label's text colour, or a toggle button's fill when on: a ColourRole key. */
    static inline const juce::Identifier colourRoleProperty{"colourRole"};
    /** A Label's background, drawn as a display in the theme's shape: a ColourRole key. */
    static inline const juce::Identifier surfaceProperty{"surface"};
    /** A toggle button's record pulse, 0 to 1 (see drawRecordPulse()). */
    static inline const juce::Identifier pulseProperty{"pulse"};

    static void setFontRole(juce::Component& component, appearance::FontRole role);
    static void setColourRole(juce::Component& component, appearance::ColourRole role);
    static void setSurface(juce::Component& component, appearance::ColourRole role);

    /** The themed look and feel a component draws with. In the app every component has
        one; a component outside it (a test that made no ThemeManager) gets a fallback in
        the default theme. */
    [[nodiscard]] static ThemedLookAndFeel& get(const juce::Component& component);

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

    /** The theme's glow around a shape (nothing in themes without one), from an image
        painted once per shape, size and colour. */
    void drawGlow(juce::Graphics& g, const juce::Path& shape, juce::Colour glowColour,
                  float opacity = 1.0f) const;

    /** The app's animation clock (set by ThemeManager; none outside the app). */
    void setAnimationClock(AnimationClock* clockToUse) noexcept { animationClock = clockToUse; }
    [[nodiscard]] AnimationClock* getAnimationClock() const noexcept { return animationClock; }

    /** Whether an animated effect is drawn now. */
    [[nodiscard]] bool isAnimating(appearance::AnimationEffect effect) const
    {
        return animationClock != nullptr && animationClock->isActive(effect);
    }

    /** Images costly to paint, kept while this theme is in use. */
    [[nodiscard]] PrerenderedImages& getImages() const noexcept { return images; }

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

    //==============================================================================
    // The DAW's own components.

    /** A panel: its surface, and a separator line along the bottom if asked. */
    void drawPanel(juce::Graphics& g, juce::Rectangle<float> area, appearance::ColourRole surface,
                   bool separatorBelow) const;

    /** A row of the timeline: alternating lanes for tracks, another surface for the
        buses' and the master's automation rows. */
    void drawLane(juce::Graphics& g, juce::Rectangle<float> area, int row, bool isTrackRow,
                  bool translucent = false) const;
    void drawGridLine(juce::Graphics& g, float x, float top, float bottom, bool barLine) const;
    void drawLoopShade(juce::Graphics& g, juce::Rectangle<float> area, bool enabled) const;
    /** How the playhead is drawn now: animated (cursor glow on) or still. */
    struct PlayheadLook
    {
        double seconds = 0.0; ///< on the animation clock
        bool animated = false;
        bool playing = false;
    };

    virtual void drawPlayhead(juce::Graphics& g, float x, float top, float bottom,
                              const PlayheadLook& look) const;

    /** Whether the playhead changes from frame to frame (cursor glow on). */
    [[nodiscard]] virtual bool playheadAnimates(bool playing) const;

    /** How far the playhead paints on either side of its position, glow and trail
        included: what a moving playhead must repaint. */
    [[nodiscard]] int playheadReach() const noexcept;

    /** A moving background in the timeline (scanlines, perspective grid), drawn under
        the lanes, which then let it through, or over them. */
    [[nodiscard]] virtual bool hasTimelineBackground() const { return false; }
    [[nodiscard]] virtual bool timelineBackgroundIsUnder() const { return false; }
    virtual void drawTimelineBackground(juce::Graphics& g, juce::Rectangle<float> area,
                                        double seconds) const;

    /** A band under the transport (Acid Terminal's hazard stripes); 0 for none. */
    [[nodiscard]] virtual int hazardBandHeight() const { return 0; }
    virtual void drawHazardBand(juce::Graphics& g, juce::Rectangle<float> area, double seconds,
                                bool recording) const;

    /** A pulse from 0 to 1 and back, about once a second, for recording. */
    [[nodiscard]] static float pulseAt(double seconds) noexcept;

    /** The record pulse on a control or a recording clip: a ring inside its shape. */
    void drawRecordPulse(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour fill,
                         float pulse) const;
    void drawDropIndicator(juce::Graphics& g, juce::Rectangle<float> area) const;

    enum class ClipState
    {
        ready,
        loading,
        failed,
        recording
    };

    struct ClipStyle
    {
        juce::Colour body;     ///< the clip's area, over its lane
        juce::Colour header;   ///< the name strip
        juce::Colour name;     ///< the name's text, readable on the strip
        juce::Colour waveform; ///< the waveform, on the body
        juce::Colour outline;
    };

    [[nodiscard]] ClipStyle clipStyle(int trackIndex, ClipState state, int row) const;
    [[nodiscard]] float clipHeaderHeight() const;
    virtual void drawClipFrame(juce::Graphics& g, juce::Rectangle<float> bounds,
                               const ClipStyle& style) const;
    virtual void drawClipOutline(juce::Graphics& g, juce::Rectangle<float> bounds,
                                 const ClipStyle& style, bool selected) const;
    void drawClipName(juce::Graphics& g, juce::Rectangle<float> area, const juce::String& name,
                      const ClipStyle& style) const;

    // The piano roll.
    void drawPianoKey(juce::Graphics& g, juce::Rectangle<float> area, bool black,
                      const juce::String& label, bool pressed) const;
    void drawNoteRow(juce::Graphics& g, juce::Rectangle<float> area, bool black, bool octaveLine) const;
    void drawNote(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour, int velocity,
                  bool selected) const;
    void drawVelocityBar(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour,
                         bool selected) const;
    void drawSelectionBand(juce::Graphics& g, juce::Rectangle<float> area) const;
    void drawLaneLabel(juce::Graphics& g, juce::Rectangle<float> area, const juce::String& text) const;
    /** A controller lane: the value as steps, an event as a dot. */
    void drawControllerSteps(juce::Graphics& g, const juce::Path& steps) const;
    void drawControllerEvent(juce::Graphics& g, juce::Point<float> centre, bool selected) const;

    // Instrument editors.
    /** A sample's waveform, a closed outline of its peaks, on a graph background. */
    void drawSampleWaveform(juce::Graphics& g, const juce::Path& outline) const;
    /** A position in a sample (start, loop start or end), labelled at the top. */
    void drawSampleMarker(juce::Graphics& g, float x, float top, float bottom,
                          const juce::String& label, bool loop) const;
    /** A drum pad: its name and what it plays; lit while it sounds. */
    void drawDrumPad(juce::Graphics& g, juce::Rectangle<float> area, const juce::String& title,
                     const juce::String& detail, bool selected, bool lit) const;

    virtual void drawRulerBackground(juce::Graphics& g, juce::Rectangle<float> area) const;
    void drawRulerMark(juce::Graphics& g, float x, float top, float bottom, bool major) const;
    void drawRulerLabel(juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area,
                        bool major) const;
    void drawRulerLoop(juce::Graphics& g, float x0, float x1, float height, bool enabled) const;
    virtual void drawPlayheadMarker(juce::Graphics& g, float x, float bottom) const;

    /** What animated meters add: a glow on the level (themes with a glow), LEDs
        fading out down to the afterglow level (segment meters). */
    struct MeterAnimation
    {
        bool animated = false;
        float afterglowDb = -60.0f;
    };

    /** One channel of a level meter: gradient or LED segments, as the theme says, with
        the unlit track and the peak-hold line. */
    virtual void drawMeterBar(juce::Graphics& g, juce::Rectangle<float> bar, float levelDb,
                              float peakHoldDb, const MeterAnimation& animation) const;
    void drawClipIndicator(juce::Graphics& g, juce::Rectangle<float> area, bool clipping) const;
    void drawMeterScale(juce::Graphics& g, juce::Rectangle<float> area,
                        std::span<const float> marksDb) const;

    void drawTrackHeader(juce::Graphics& g, juce::Rectangle<float> area) const;

    /** A mixer strip: tracks on panels, buses and the master set apart. */
    enum class StripKind
    {
        track,
        bus,
        master
    };

    void drawMixerStrip(juce::Graphics& g, juce::Rectangle<float> area, StripKind kind) const;

    /** The list behind a strip's insert or send slots. */
    void drawSlotList(juce::Graphics& g, juce::Rectangle<float> area) const;
    /** What an effect slot shows besides its name. */
    enum class SlotMark
    {
        none,
        failed, ///< silenced after NaN or infinity
        slow,   ///< a plugin too slow for the audio
        absent  ///< a plugin's placeholder: loading, missing, failed or kept from loading
    };
    void drawEffectSlot(juce::Graphics& g, juce::Rectangle<float> area, const juce::String& name,
                        bool bypassed, SlotMark mark) const;
    /** A send: its bus, and its level as a bar (automation's colour when automated). */
    void drawSendSlot(juce::Graphics& g, juce::Rectangle<float> area, const juce::String& name,
                      float levelProportion, bool automated) const;
    /** "+ Effect", "+ Send": where a click adds one. */
    void drawEmptySlot(juce::Graphics& g, juce::Rectangle<float> area,
                       const juce::String& text) const;

    // Graphs in effect editors: the EQ's response, the compressor's curve and gain
    // reduction.
    void drawGraphBackground(juce::Graphics& g, juce::Rectangle<float> area) const;
    void drawGraphLine(juce::Graphics& g, juce::Line<float> line, bool emphasised) const;
    void drawGraphLabel(juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area,
                        juce::Justification justification) const;
    /** A response curve, with the area between it and the baseline filled if asked. */
    void drawCurve(juce::Graphics& g, const juce::Path& curve, float baselineY, bool filled) const;
    /** Series colours (EQ bands): the theme's clip colours, in turn. */
    [[nodiscard]] juce::Colour seriesColour(int index) const;
    void drawSeriesCurve(juce::Graphics& g, const juce::Path& curve, int series) const;
    void drawCurveHandle(juce::Graphics& g, juce::Point<float> centre, int series, bool on,
                         bool selected, const juce::String& label) const;
    void drawLevelDot(juce::Graphics& g, juce::Point<float> centre) const;
    /** Gain reduction from the top of the bar. */
    void drawGainReductionBar(juce::Graphics& g, juce::Rectangle<float> bar,
                              float proportion) const;
    void drawGainReductionTrace(juce::Graphics& g, const juce::Path& trace) const;

    // Automation over the timeline.
    void drawAutomationShade(juce::Graphics& g, juce::Rectangle<float> row) const;
    void drawAutomationCurve(juce::Graphics& g, const juce::Path& curve, bool reads) const;
    /** The value without points, as a dashed line. */
    void drawAutomationLevel(juce::Graphics& g, const juce::Path& line, bool reads) const;
    void drawAutomationPoint(juce::Graphics& g, juce::Point<float> centre, float radius,
                             bool reads) const;
    void drawAutomationLabel(juce::Graphics& g, const juce::String& text,
                             juce::Rectangle<float> area, bool emphasised) const;

    /** The app's name in the theme's own logo style; glitching: its glitch effect at
        this time on the clock (flicker, glitch or chrome sweep). */
    virtual void drawLogo(juce::Graphics& g, juce::Rectangle<float> area, const juce::String& text,
                          double seconds, bool glitching) const;

    /** What the status bar shows. */
    struct StatusLine
    {
        juce::String message; ///< may be empty
        appearance::ColourRole messageRole = appearance::ColourRole::textSecondary;
        juce::String details;      ///< right-hand side
        juce::String projectName;  ///< for prompt-style bars
        bool cursorVisible = true; ///< the prompt's cursor
    };

    virtual void drawStatusBar(juce::Graphics& g, juce::Rectangle<float> area,
                               const StatusLine& line) const;

protected:
    /** Text as the theme shows labels: capitals for roles that ask for them, and the
        terminal decoration for Acid Terminal's headings. */
    [[nodiscard]] virtual juce::String labelText(appearance::FontRole role,
                                                 const juce::String& text) const;

    /** Draws the border of a control; themes with glows add theirs. */
    /** How far behind the playhead its trail reaches (0 for none). */
    [[nodiscard]] virtual int playheadTrail() const noexcept { return 0; }

    virtual void drawControlOutline(juce::Graphics& g, const juce::Path& outline, bool isActive,
                                    juce::Colour activeColour) const;

    [[nodiscard]] appearance::FontRole roleOf(const juce::Label& label) const;

    /** A button's fill when on, from its colour role, if it has one. */
    [[nodiscard]] std::optional<juce::Colour> buttonOnColour(const juce::Button& button) const;

    const appearance::Theme& theme;
    const ThemeFonts& fonts;

private:
    mutable PrerenderedImages images;
    AnimationClock* animationClock = nullptr;

    void setColoursFromTheme();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ThemedLookAndFeel)
};

} // namespace daw::ui::desktop
