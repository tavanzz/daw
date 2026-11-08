#pragma once

#include "daw/controller/Session.h"
#include "daw/model/Clip.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <map>
#include <optional>

namespace daw::ui::desktop
{

/** A window editing one effect or instrument (built-in or plugin), which the
    EffectEditors refresh at the UI's rate and close when the effect goes. */
class EditorWindow : public juce::DocumentWindow
{
public:
    explicit EditorWindow(const juce::String& title);

    [[nodiscard]] virtual model::ObjectId getEffectId() const noexcept = 0;

    /** At the UI's refresh rate. */
    virtual void refresh(double elapsedSeconds) = 0;

    std::function<void()> onClose;
    void closeButtonPressed() override;
};

/** The name of a track, a bus or "Master", for editor titles. */
[[nodiscard]] juce::String chainOwnerName(const controller::Session& session, const controller::ChainOwner& owner);

/** Where plugin editor windows were last, by plugin (its PluginKey as text), so that
    a plugin's editor opens where it was. The app keeps it in the user settings. */
class WindowPositions
{
public:
    [[nodiscard]] std::optional<juce::Point<int>> get(const juce::String& key) const;
    void set(const juce::String& key, juce::Point<int> position);

    [[nodiscard]] juce::var toJson() const;
    /** Never fails: what cannot be read is left out. */
    [[nodiscard]] static WindowPositions fromJson(const juce::var& json);

    /** Called after every change. */
    std::function<void()> onChange;

private:
    std::map<juce::String, juce::Point<int>> positions;
};

} // namespace daw::ui::desktop
