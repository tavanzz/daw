#include "daw/ui/desktop/AutomationLanes.h"

#include "daw/controller/AutomationEditing.h"
#include "daw/engine/dsp/FaderLaw.h"
#include "daw/engine/fx/BuiltInEffects.h"
#include "daw/engine/plugins/PluginEffect.h"
#include "daw/ui/desktop/EffectSlots.h"
#include "daw/ui/desktop/ThemedLookAndFeel.h"

#include <cmath>
#include <functional>
#include <vector>

namespace daw::ui::desktop
{

namespace
{

constexpr float margin = 6.0f;
constexpr float pointRadius = 4.0f;

// The description of an effect's type, for its parameters' names and mappings: a
// plugin's own while it is loaded.
std::optional<engine::fx::EffectDescription> describe(const controller::Session& session,
                                                      model::ObjectId effectId)
{
    const auto effect = session.getEffect(effectId);
    if (!effect)
        return std::nullopt;
    if (effect->getType() == model::pluginEffectType)
    {
        if (const auto* plugin = session.findPlugin(effectId))
            return plugin->getDescription();
        return std::nullopt;
    }
    if (auto made = engine::fx::createBuiltInEffect(effect->getType()))
        return made->getDescription();
    return std::nullopt;
}

// An effect's automatable parameters; a plugin's many in submenus of a few dozen.
void addParameters(juce::PopupMenu& into, const engine::fx::EffectDescription& description,
                   const std::function<void(juce::PopupMenu&, const engine::fx::ParameterInfo&)>& add)
{
    constexpr int perMenu = 40;
    std::vector<const engine::fx::ParameterInfo*> shown;
    for (const auto& info : description.parameters)
        if (info.automatable)
            shown.push_back(&info);
    if (static_cast<int>(shown.size()) <= perMenu)
    {
        for (const auto* info : shown)
            add(into, *info);
        return;
    }
    for (std::size_t first = 0; first < shown.size(); first += perMenu)
    {
        const auto last = std::min(shown.size(), first + perMenu);
        juce::PopupMenu part;
        for (auto i = first; i < last; ++i)
            add(part, *shown[i]);
        into.addSubMenu(shown[first]->name + " ... " + shown[last - 1]->name, part);
    }
}

} // namespace

AutomationLanes::AutomationLanes(ArrangeState& sharedState)
    : state(sharedState)
{
}

juce::String AutomationLanes::targetName(const controller::Session& session,
                                         const controller::ChainOwner& owner,
                                         const model::AutomationTarget& target)
{
    using Kind = model::AutomationTarget::Kind;
    switch (target.kind)
    {
    case Kind::volume:
        return "Volume";
    case Kind::pan:
        return "Pan";
    case Kind::sendLevel:
    {
        const auto& project = session.getProject();
        if (owner.kind == controller::ChainOwner::Kind::track)
            if (const int t = project.indexOfTrack(owner.id); t >= 0)
                if (const int s = project.getTrack(t).indexOfSend(target.object); s >= 0)
                    if (const int b = project.indexOfBus(project.getTrack(t).getSend(s).getBus()); b >= 0)
                        return "Send: " + project.getBus(b).getName();
        return "Send";
    }
    case Kind::effectParameter:
    {
        const auto effect = session.getEffect(target.object);
        const auto description = describe(session, target.object);
        if (!effect || !description)
            return target.parameter;
        const int index = description->indexOf(target.parameter);
        return effectDisplayName(*effect) + ": " +
               (index >= 0 ? description->parameters[static_cast<std::size_t>(index)].name
                           : target.parameter);
    }
    }
    return {};
}

juce::String AutomationLanes::valueText(const controller::Session& session,
                                        const model::AutomationTarget& target, double normalised)
{
    using Kind = model::AutomationTarget::Kind;
    if (target.kind == Kind::volume || target.kind == Kind::sendLevel)
    {
        const float gain = engine::dsp::faderGain(static_cast<float>(normalised));
        return gain <= 0.0f ? juce::String("-inf dB")
                            : juce::String(20.0 * std::log10(gain), 1) + " dB";
    }
    if (target.kind == Kind::pan)
    {
        const auto percent = static_cast<int>(std::lround(std::abs(normalised * 2.0 - 1.0) * 100.0));
        return percent == 0 ? juce::String("C")
                            : (normalised < 0.5 ? "L" : "R") + juce::String(percent);
    }
    if (const auto description = describe(session, target.object))
        if (const int index = description->indexOf(target.parameter); index >= 0)
        {
            if (const auto* plugin = session.findPlugin(target.object))
                return plugin->getParameterText(index, static_cast<float>(normalised));
            const auto& info = description->parameters[static_cast<std::size_t>(index)];
            return info.toText(info.fromNormalised(static_cast<float>(normalised)));
        }
    return juce::String(normalised, 3);
}

void AutomationLanes::showTargetMenu(controller::Session& session, const controller::ChainOwner& owner,
                                     juce::Component* near,
                                     std::function<void(const model::AutomationTarget&)> chosen)
{
    juce::PopupMenu menu;
    const auto automation = session.getAutomation(owner);
    const auto add = [&](juce::PopupMenu& into, const juce::String& name,
                         const model::AutomationTarget& target) {
        const int lane = automation.indexOfLane(target);
        const bool hasPoints = lane >= 0 && automation.getLane(lane).getNumPoints() > 0;
        into.addItem(name + (hasPoints ? "  *" : ""), [chosen, target] { chosen(target); });
    };

    add(menu, "Volume", model::AutomationTarget::volume());
    if (owner.kind != controller::ChainOwner::Kind::master)
        add(menu, "Pan", model::AutomationTarget::pan());

    const auto& project = session.getProject();
    if (owner.kind == controller::ChainOwner::Kind::track)
        if (const int t = project.indexOfTrack(owner.id); t >= 0)
        {
            const auto track = project.getTrack(t);
            for (int s = 0; s < track.getNumSends(); ++s)
            {
                const auto send = track.getSend(s);
                const int b = project.indexOfBus(send.getBus());
                add(menu, "Send: " + (b >= 0 ? project.getBus(b).getName() : juce::String("?")),
                    model::AutomationTarget::sendLevel(send.getId()));
            }
        }

    // A MIDI track's instrument: its parameters by section (the synth has many).
    if (owner.kind == controller::ChainOwner::Kind::track)
        if (const int t = project.indexOfTrack(owner.id); t >= 0)
            if (const auto instrument = project.getTrack(t).getInstrument())
                if (const auto description = describe(session, instrument->getId()))
                {
                    juce::PopupMenu sub;
                    juce::StringArray groups;
                    for (const auto& info : description->parameters)
                        if (info.automatable)
                            groups.addIfNotAlreadyThere(info.group);
                    const auto addParameter = [&](juce::PopupMenu& into, const engine::fx::ParameterInfo& info) {
                        add(into, info.name, model::AutomationTarget::effectParameter(instrument->getId(), info.id));
                    };
                    if (groups.size() <= 1)
                    {
                        addParameters(sub, *description, addParameter);
                    }
                    else
                    {
                        for (const auto& group : groups)
                        {
                            juce::PopupMenu section;
                            for (const auto& info : description->parameters)
                                if (info.automatable && info.group == group)
                                    addParameter(section, info);
                            sub.addSubMenu(group.isEmpty() ? juce::String("Other") : group, section);
                        }
                    }
                    menu.addSubMenu("Instrument: " + effectDisplayName(*instrument), sub);
                }

    const auto effects = session.getEffects(owner);
    for (int e = 0; e < effects.getNumEffects(); ++e)
    {
        const auto effect = effects.getEffect(e);
        const auto description = describe(session, effect.getId());
        if (!description)
            continue;
        juce::PopupMenu sub;
        addParameters(sub, *description, [&](juce::PopupMenu& into, const engine::fx::ParameterInfo& info) {
            add(into, info.name, model::AutomationTarget::effectParameter(effect.getId(), info.id));
        });
        menu.addSubMenu(juce::String(e + 1) + ". " + effectDisplayName(effect), sub);
    }

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(near));
}

float AutomationLanes::yOf(juce::Rectangle<float> row, double value) const
{
    return row.getBottom() - margin - static_cast<float>(value) * (row.getHeight() - 2.0f * margin);
}

double AutomationLanes::valueAt(juce::Rectangle<float> row, float y) const
{
    return juce::jlimit(0.0, 1.0,
                        static_cast<double>((row.getBottom() - margin - y) / (row.getHeight() - 2.0f * margin)));
}

std::optional<model::AutomationLane> AutomationLanes::laneOf(const controller::ChainOwner& owner,
                                                              const model::AutomationTarget& target) const
{
    const auto automation = state.session.getAutomation(owner);
    const int index = automation.indexOfLane(target);
    return index >= 0 ? std::optional<model::AutomationLane>(automation.getLane(index)) : std::nullopt;
}

double AutomationLanes::fixedValue(const controller::ChainOwner& owner,
                                   const model::AutomationTarget& target) const
{
    using Kind = model::AutomationTarget::Kind;
    const auto& project = state.session.getProject();
    const auto track = owner.kind == controller::ChainOwner::Kind::track ? project.indexOfTrack(owner.id) : -1;
    const auto bus = owner.kind == controller::ChainOwner::Kind::bus ? project.indexOfBus(owner.id) : -1;

    switch (target.kind)
    {
    case Kind::volume:
        return engine::dsp::faderPosition(track >= 0 ? project.getTrack(track).getVolume()
                                          : bus >= 0 ? project.getBus(bus).getVolume()
                                                     : project.getMasterVolume());
    case Kind::pan:
        return 0.5 * ((track >= 0 ? project.getTrack(track).getPan()
                                  : bus >= 0 ? project.getBus(bus).getPan() : 0.0) + 1.0);
    case Kind::sendLevel:
        if (track >= 0)
            if (const int s = project.getTrack(track).indexOfSend(target.object); s >= 0)
                return engine::dsp::faderPosition(project.getTrack(track).getSend(s).getLevel());
        return 0.0;
    case Kind::effectParameter:
        if (const auto description = describe(state.session, target.object))
            if (const int index = description->indexOf(target.parameter); index >= 0)
            {
                const auto& info = description->parameters[static_cast<std::size_t>(index)];
                const auto effect = state.session.getEffect(target.object);
                // A plugin's parameter the model holds no value for: the plugin's own.
                const auto* plugin = state.session.findPlugin(target.object);
                const auto fallback = plugin != nullptr ? plugin->getParameter(index) : info.defaultValue;
                return info.toNormalised(static_cast<float>(
                    effect->getParameter(info.id).value_or(static_cast<double>(fallback))));
            }
        return 0.0;
    }
    return 0.0;
}

void AutomationLanes::paintRow(juce::Graphics& g, juce::Rectangle<float> row,
                               const controller::ChainOwner& owner,
                               const ThemedLookAndFeel& look) const
{
    const auto target = state.targetOf(owner);
    const auto& viewport = state.viewport;
    const bool reads = state.session.getAutomation(owner).getMode() == model::AutomationMode::read;
    look.drawAutomationShade(g, row);

    const auto lane = laneOf(owner, target);
    if (!lane || lane->getNumPoints() == 0)
    {
        // The value as set, until there are points.
        const float y = yOf(row, fixedValue(owner, target));
        juce::Path line;
        line.startNewSubPath(row.getX(), y);
        line.lineTo(row.getRight(), y);
        look.drawAutomationLevel(g, line, reads);
        look.drawAutomationLabel(g, targetName(state.session, owner, target) +
                                               ": click to add points",
                                        row.reduced(8.0f, 4.0f), false);
        return;
    }

    const auto curve = controller::automationCurveInTicks(*lane);
    juce::Path path;
    bool started = false; // (a path with only its start point still counts as empty)
    for (int x = static_cast<int>(row.getX()); x <= static_cast<int>(row.getRight()); x += 2)
    {
        const auto ticks = viewport.xToTicks(static_cast<double>(x));
        const float y = yOf(row, curve.valueAt(ticks));
        if (!started)
            path.startNewSubPath(static_cast<float>(x), y);
        else
            path.lineTo(static_cast<float>(x), y);
        started = true;
    }
    look.drawAutomationCurve(g, path, reads);

    for (int i = 0; i < lane->getNumPoints(); ++i)
    {
        const auto point = lane->getPoint(i);
        const auto x = static_cast<float>(viewport.ticksToX(point.position));
        if (x < row.getX() - pointRadius || x > row.getRight() + pointRadius)
            continue;
        look.drawAutomationPoint(g, {x, yOf(row, point.value)}, pointRadius, reads);
    }

    look.drawAutomationLabel(g, targetName(state.session, owner, target),
                                    row.reduced(8.0f, 4.0f), true);

    if (drag && drag->owner == owner && drag->index >= 0 && drag->index < lane->getNumPoints())
    {
        const auto point = lane->getPoint(drag->index);
        const auto x = static_cast<float>(viewport.ticksToX(point.position));
        look.drawAutomationLabel(g, valueText(state.session, target, point.value),
                                        {x + 8.0f, yOf(row, point.value) - 16.0f, 90.0f, 14.0f},
                                        true);
    }
}

void AutomationLanes::mouseDown(const juce::MouseEvent& event, juce::Rectangle<float> row,
                                const controller::ChainOwner& owner)
{
    auto& session = state.session;
    const auto target = state.targetOf(owner);

    if (event.mods.isPopupMenu())
    {
        juce::PopupMenu menu;
        const auto automation = session.getAutomation(owner);
        const bool reads = automation.getMode() == model::AutomationMode::read;
        menu.addItem("Clear " + targetName(session, owner, target), [&session, owner, target] {
            session.clearAutomation(owner, target);
        });
        menu.addItem(reads ? "Turn Automation Off" : "Read Automation", [&session, owner, reads] {
            session.setAutomationMode(owner, reads ? model::AutomationMode::off : model::AutomationMode::read);
        });
        menu.showMenuAsync(juce::PopupMenu::Options());
        return;
    }

    const auto& viewport = state.viewport;
    const auto lane = laneOf(owner, target);
    const auto ticks = viewport.xToTicks(event.position.x);
    const auto tolerance = static_cast<model::Ticks>(viewport.getTicksPerPixel() * (pointRadius + 2.0));
    const double valueTolerance = (pointRadius + 2.0) / (row.getHeight() - 2.0f * margin);

    Drag next{owner, target, row};
    if (lane)
        next.index = controller::findAutomationPoint(*lane, ticks, valueAt(row, event.position.y),
                                                     tolerance, valueTolerance);

    if (next.index < 0 && lane && event.mods.isAltDown())
    {
        // Bend the segment under the pointer.
        next.index = controller::automationSegmentAt(*lane, ticks);
        if (next.index < 0)
            return;
        next.bending = true;
        next.startBend = lane->getPoint(next.index).curve;
        next.startY = event.position.y;
        session.beginGesture("Bend Automation");
    }
    else if (next.index < 0)
    {
        session.beginGesture("Add Automation Point");
        next.index = session.addAutomationPoint(
            owner, target, {state.snap(ticks, event.mods), valueAt(row, event.position.y), 0.0});
    }
    else
    {
        session.beginGesture("Move Automation Point");
    }

    drag = next;
}

void AutomationLanes::mouseDrag(const juce::MouseEvent& event)
{
    if (!drag)
        return;

    auto& session = state.session;
    const auto lane = laneOf(drag->owner, drag->target);
    if (!lane || drag->index < 0 || drag->index >= lane->getNumPoints())
        return;

    auto point = lane->getPoint(drag->index);
    if (drag->bending)
    {
        if (drag->index + 1 >= lane->getNumPoints())
            return;
        const auto next = lane->getPoint(drag->index + 1);
        const double delta = (drag->startY - event.position.y) / (drag->row.getHeight() - 2.0f * margin);
        point.curve = controller::bendForDrag(drag->startBend, point.value, next.value, delta);
    }
    else
    {
        point.position = std::max<model::Ticks>(0, state.snap(state.viewport.xToTicks(event.position.x), event.mods));
        point.value = valueAt(drag->row, event.position.y);
    }

    drag->index = session.setAutomationPoint(drag->owner, drag->target, drag->index, point);
}

void AutomationLanes::mouseUp()
{
    if (drag)
        state.session.endGesture();
    drag.reset();
}

void AutomationLanes::mouseDoubleClick(const juce::MouseEvent& event, juce::Rectangle<float> row,
                                       const controller::ChainOwner& owner)
{
    const auto target = state.targetOf(owner);
    const auto lane = laneOf(owner, target);
    if (!lane)
        return;

    const auto& viewport = state.viewport;
    const auto tolerance = static_cast<model::Ticks>(viewport.getTicksPerPixel() * (pointRadius + 2.0));
    const double valueTolerance = (pointRadius + 2.0) / (row.getHeight() - 2.0f * margin);
    const int index = controller::findAutomationPoint(*lane, viewport.xToTicks(event.position.x),
                                                      valueAt(row, event.position.y), tolerance,
                                                      valueTolerance);
    if (index >= 0)
        state.session.removeAutomationPoint(owner, target, index);
}

} // namespace daw::ui::desktop
