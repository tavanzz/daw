#include "daw/ui/desktop/EditorWindow.h"

namespace daw::ui::desktop
{

EditorWindow::EditorWindow(const juce::String& title)
    : juce::DocumentWindow(title,
                           juce::Desktop::getInstance().getDefaultLookAndFeel().findColour(
                               juce::ResizableWindow::backgroundColourId),
                           juce::DocumentWindow::closeButton)
{
}

juce::String chainOwnerName(const controller::Session& session, const controller::ChainOwner& owner)
{
    const auto& project = session.getProject();
    if (owner.kind == controller::ChainOwner::Kind::track)
        if (const int index = project.indexOfTrack(owner.id); index >= 0)
            return project.getTrack(index).getName();
    if (owner.kind == controller::ChainOwner::Kind::bus)
        if (const int index = project.indexOfBus(owner.id); index >= 0)
            return project.getBus(index).getName();
    return "Master";
}

void EditorWindow::closeButtonPressed()
{
    if (onClose)
        onClose();
}

std::optional<juce::Point<int>> WindowPositions::get(const juce::String& key) const
{
    const auto found = positions.find(key);
    return found != positions.end() ? std::optional<juce::Point<int>>(found->second) : std::nullopt;
}

void WindowPositions::set(const juce::String& key, juce::Point<int> position)
{
    if (const auto found = positions.find(key); found != positions.end() && found->second == position)
        return;
    positions[key] = position;
    if (onChange)
        onChange();
}

juce::var WindowPositions::toJson() const
{
    auto* object = new juce::DynamicObject();
    for (const auto& [key, position] : positions)
        object->setProperty(key, juce::Array<juce::var>{position.x, position.y});
    return juce::var(object);
}

WindowPositions WindowPositions::fromJson(const juce::var& json)
{
    WindowPositions read;
    if (const auto* object = json.getDynamicObject())
        for (const auto& property : object->getProperties())
            if (const auto* xy = property.value.getArray(); xy != nullptr && xy->size() == 2 &&
                                                             (*xy)[0].isInt() && (*xy)[1].isInt())
                read.positions[property.name.toString()] = {static_cast<int>((*xy)[0]), static_cast<int>((*xy)[1])};
    return read;
}

void EditorWindow::showEditedBy(const juce::String& name)
{
    if (baseTitle.isEmpty())
        baseTitle = getName();
    const auto title = name.isEmpty() ? baseTitle : baseTitle + "  (" + name + " is editing this track)";
    if (title != getName())
        setName(title);
}

} // namespace daw::ui::desktop
