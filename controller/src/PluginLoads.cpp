#include "daw/controller/PluginLoads.h"

namespace daw::controller
{

void PluginLoads::add(const std::vector<Reading>& readings, double nowSeconds)
{
    std::map<model::ObjectId, Entry> updated;
    for (const auto& load : readings)
    {
        auto entry = entries.contains(load.effectId) ? entries[load.effectId] : Entry{};
        entry.name = load.name;
        entry.average = load.average;
        entry.peak = load.peak;
        if (load.overruns > 0)
        {
            entry.overruns += load.overruns;
            entry.lastOverrun = nowSeconds;
        }
        entry.slow = entry.average > slowAverage || nowSeconds - entry.lastOverrun < overrunSeconds;
        updated[load.effectId] = entry;
    }
    entries = std::move(updated);
}

const PluginLoads::Entry* PluginLoads::find(model::ObjectId effectId) const
{
    const auto found = entries.find(effectId);
    return found != entries.end() ? &found->second : nullptr;
}

juce::StringArray PluginLoads::getSlowPlugins() const
{
    juce::StringArray names;
    for (const auto& [effectId, entry] : entries)
        if (entry.slow)
            names.addIfNotAlreadyThere(entry.name);
    return names;
}

} // namespace daw::controller
