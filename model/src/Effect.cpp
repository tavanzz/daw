#include "daw/model/Effect.h"

#include "daw/model/Automation.h"

#include "Identifiers.h"
#include "ObjectIds.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace daw::model
{

Effect::Effect(juce::ValueTree effectState)
    : state(std::move(effectState))
{
    jassert(state.hasType(ids::effect));
}

ObjectId Effect::getId() const
{
    return static_cast<ObjectId>(state[ids::id]);
}

juce::String Effect::getType() const
{
    return state[ids::type].toString();
}

bool Effect::isBypassed() const
{
    return static_cast<bool>(state[ids::bypass]);
}

void Effect::setBypassed(bool shouldBeBypassed, juce::UndoManager* undoManager)
{
    state.setProperty(ids::bypass, shouldBeBypassed, undoManager);
}

std::optional<double> Effect::getParameter(const juce::String& parameterId) const
{
    const auto parameters = state.getChildWithName(ids::parameters);
    if (!parameters.isValid() || parameterId.isEmpty())
        return std::nullopt;

    const auto& value = parameters[juce::Identifier(parameterId)];
    if (value.isVoid())
        return std::nullopt;
    return static_cast<double>(value);
}

void Effect::setParameter(const juce::String& parameterId, double value,
                          juce::UndoManager* undoManager)
{
    if (parameterId.isEmpty() || !std::isfinite(value))
        return;

    // At the precision of a float, what effects work with: project files then hold
    // the value exactly (JUCE writes doubles with 16 significant digits, enough
    // for any float but not for every double).
    value = static_cast<double>(static_cast<float>(
        std::clamp(value, -static_cast<double>(std::numeric_limits<float>::max()),
                   static_cast<double>(std::numeric_limits<float>::max()))));

    auto parameters = state.getChildWithName(ids::parameters);
    if (!parameters.isValid())
    {
        parameters = juce::ValueTree(ids::parameters);
        state.appendChild(parameters, undoManager);
    }
    parameters.setProperty(juce::Identifier(parameterId), value, undoManager);
}

std::vector<std::pair<juce::String, double>> Effect::getParameters() const
{
    std::vector<std::pair<juce::String, double>> values;
    const auto parameters = state.getChildWithName(ids::parameters);
    for (int i = 0; i < parameters.getNumProperties(); ++i)
    {
        const auto name = parameters.getPropertyName(i);
        values.emplace_back(name.toString(), static_cast<double>(parameters[name]));
    }
    return values;
}

std::optional<Effect> Effect::ofParameters(const juce::ValueTree& tree)
{
    if (!tree.hasType(ids::parameters))
        return std::nullopt;
    const auto parent = tree.getParent();
    if (!parent.hasType(ids::effect))
        return std::nullopt;
    return Effect(parent);
}

std::optional<PluginReference> Effect::getPlugin() const
{
    const auto plugin = state.getChildWithName(ids::plugin);
    if (!plugin.isValid())
        return std::nullopt;
    PluginReference reference;
    reference.format = plugin[ids::pluginFormat].toString();
    reference.uniqueId = static_cast<int>(plugin[ids::pluginUniqueId]);
    reference.name = plugin[ids::name].toString();
    reference.manufacturer = plugin[ids::pluginManufacturer].toString();
    reference.version = plugin[ids::pluginVersion].toString();
    reference.category = plugin[ids::pluginCategory].toString();
    reference.identifier = plugin[ids::pluginIdentifier].toString();
    reference.isInstrument = static_cast<bool>(plugin[ids::pluginInstrument]);
    return reference;
}

void Effect::setPlugin(const PluginReference& reference, juce::UndoManager* undoManager)
{
    juce::ValueTree plugin(ids::plugin);
    plugin.setProperty(ids::pluginFormat, reference.format, nullptr);
    plugin.setProperty(ids::pluginUniqueId, reference.uniqueId, nullptr);
    plugin.setProperty(ids::name, reference.name, nullptr);
    plugin.setProperty(ids::pluginManufacturer, reference.manufacturer, nullptr);
    plugin.setProperty(ids::pluginVersion, reference.version, nullptr);
    plugin.setProperty(ids::pluginCategory, reference.category, nullptr);
    plugin.setProperty(ids::pluginIdentifier, reference.identifier, nullptr);
    plugin.setProperty(ids::pluginInstrument, reference.isInstrument, nullptr);
    if (const auto old = state.getChildWithName(ids::plugin); old.isValid())
        state.removeChild(old, undoManager);
    state.appendChild(plugin, undoManager);
}

juce::MemoryBlock Effect::getExtraState() const
{
    // Standard Base64 (RFC 4648), not JUCE's own variant: the file format is not tied
    // to JUCE.
    juce::MemoryOutputStream decoded;
    if (!juce::Base64::convertFromBase64(decoded, state[ids::data].toString()))
        return {};
    return decoded.getMemoryBlock();
}

void Effect::setExtraState(const juce::MemoryBlock& extra, juce::UndoManager* undoManager)
{
    if (extra.isEmpty())
        state.removeProperty(ids::data, undoManager);
    else
        state.setProperty(ids::data, juce::Base64::toBase64(extra.getData(), extra.getSize()),
                          undoManager);
}

std::vector<std::pair<juce::String, juce::File>> Effect::getFiles() const
{
    std::vector<std::pair<juce::String, juce::File>> files;
    const auto container = state.getChildWithName(ids::files);
    for (int i = 0; i < container.getNumProperties(); ++i)
    {
        const auto key = container.getPropertyName(i);
        files.emplace_back(key.toString(), juce::File(container[key].toString()));
    }
    return files;
}

juce::File Effect::getFile(const juce::String& key) const
{
    const auto container = state.getChildWithName(ids::files);
    const auto path = container.isValid() && key.isNotEmpty()
                          ? container[juce::Identifier(key)].toString()
                          : juce::String();
    return path.isEmpty() ? juce::File() : juce::File(path);
}

void Effect::setFile(const juce::String& key, const juce::File& file, juce::UndoManager* undoManager)
{
    if (key.isEmpty() || !key.containsOnly("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"))
    {
        jassertfalse;
        return;
    }

    auto container = state.getChildWithName(ids::files);
    if (file == juce::File())
    {
        if (container.isValid())
            container.removeProperty(juce::Identifier(key), undoManager);
        return;
    }

    if (!container.isValid())
    {
        container = juce::ValueTree(ids::files);
        state.appendChild(container, undoManager);
    }
    container.setProperty(juce::Identifier(key), file.getFullPathName(), undoManager);
}

EffectList::EffectList(juce::ValueTree listState)
    : state(std::move(listState))
{
    jassert(state.hasType(ids::effects));
}

int EffectList::getNumEffects() const
{
    return state.getNumChildren();
}

Effect EffectList::getEffect(int index) const
{
    jassert(index >= 0 && index < getNumEffects());
    return Effect(state.getChild(index));
}

int EffectList::indexOf(ObjectId effectId) const
{
    for (int index = 0; index < getNumEffects(); ++index)
        if (static_cast<ObjectId>(state.getChild(index)[ids::id]) == effectId)
            return index;
    return -1;
}

Effect EffectList::add(const juce::String& type, int index,
                       const std::vector<std::pair<juce::String, double>>& parameters,
                       juce::UndoManager* undoManager)
{
    juce::ValueTree effectState(ids::effect);
    effectState.setProperty(ids::id, detail::nextObjectId(state), nullptr);
    effectState.setProperty(ids::type, type, nullptr);
    effectState.setProperty(ids::bypass, false, nullptr);

    Effect effect(effectState);
    for (const auto& [id, value] : parameters)
        effect.setParameter(id, value);

    const int at = index < 0 ? getNumEffects() : std::clamp(index, 0, getNumEffects());
    state.addChild(effectState, at, undoManager);
    return effect;
}

void EffectList::remove(int index, juce::UndoManager* undoManager)
{
    if (index < 0 || index >= getNumEffects())
        return;

    // Its automation goes with it: the lanes of its parameters, in its owner.
    const auto effectId = getEffect(index).getId();
    const auto owner = state.getParent();
    if (const auto lanes = owner.getChildWithName(ids::automation); lanes.isValid())
        Automation(lanes).removeLanesOfEffect(effectId, undoManager);

    state.removeChild(index, undoManager);
}

void EffectList::move(int from, int to, juce::UndoManager* undoManager)
{
    if (from < 0 || from >= getNumEffects() || to < 0 || to >= getNumEffects() || from == to)
        return;
    state.moveChild(from, to, undoManager);
}

} // namespace daw::model
