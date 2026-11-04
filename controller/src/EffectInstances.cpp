#include "daw/controller/EffectInstances.h"

#include "daw/engine/fx/BuiltInEffects.h"

#include <algorithm>
#include <set>

namespace daw::controller
{

namespace
{

// The ids of new that keep their order from old: a longest increasing run of their
// old positions (among those not known to have moved). The others moved.
std::set<model::ObjectId> keptInOrder(const std::vector<model::ObjectId>& old,
                                      const std::vector<model::ObjectId>& now,
                                      const std::set<model::ObjectId>& movedForSure)
{
    std::vector<std::pair<int, model::ObjectId>> common; // old position, id
    for (const auto id : now)
        if (const auto at = std::find(old.begin(), old.end(), id);
            at != old.end() && movedForSure.count(id) == 0)
            common.emplace_back(static_cast<int>(at - old.begin()), id);

    const auto count = common.size();
    std::vector<int> length(count, 1);
    std::vector<int> previous(count, -1);
    for (std::size_t i = 0; i < count; ++i)
        for (std::size_t j = 0; j < i; ++j)
            if (common[j].first < common[i].first && length[j] + 1 > length[i])
            {
                length[i] = length[j] + 1;
                previous[i] = static_cast<int>(j);
            }

    std::set<model::ObjectId> kept;
    if (count == 0)
        return kept;
    auto index = static_cast<int>(std::max_element(length.begin(), length.end()) - length.begin());
    while (index >= 0)
    {
        kept.insert(common[static_cast<std::size_t>(index)].second);
        index = previous[static_cast<std::size_t>(index)];
    }
    return kept;
}

} // namespace

void EffectInstances::prepare(double newSampleRate, int maximumBlockSize)
{
    if (juce::exactlyEqual(newSampleRate, sampleRate) && maximumBlockSize == blockSize)
        return;

    sampleRate = newSampleRate;
    blockSize = maximumBlockSize;
    instances.clear();
    orders.clear();
}

void EffectInstances::clear()
{
    instances.clear();
    orders.clear();
    revivals.clear();
    used.clear();
}

std::shared_ptr<engine::fx::EffectInstance>
EffectInstances::makeInstance(const juce::String& type, double rate, int maximumBlockSize,
                              bool fadeIn, const EffectFactory& factory)
{
    auto effect = engine::fx::createBuiltInEffect(type);
    if (effect == nullptr && factory)
        effect = factory(type);
    if (effect == nullptr)
        return nullptr;

    engine::fx::ProcessSetup setup;
    setup.sampleRate = rate;
    setup.maximumBlockSize = maximumBlockSize;
    return std::make_shared<engine::fx::EffectInstance>(std::move(effect), setup, fadeIn);
}

std::vector<engine::ChainSlot> EffectInstances::chainFor(const ChainOwner& owner,
                                                         const std::vector<model::Effect>& effects,
                                                         bool fadeIn)
{
    std::vector<model::ObjectId> order;
    for (const auto& effect : effects)
        order.push_back(effect.getId());

    const auto& old = orders[owner];
    const auto kept = keptInOrder(old, order, moved);

    std::vector<engine::ChainSlot> slots;
    for (const auto& effect : effects)
    {
        const auto id = effect.getId();
        used.push_back(id);

        auto& instance = instances[id];
        const bool wasHere = std::find(old.begin(), old.end(), id) != old.end();
        const bool sameType =
            instance != nullptr && instance->getDescription().type == effect.getType();

        // Moved within the chain, or come from another one: it starts anew where it
        // is now, while the old instance fades out where it was.
        const bool movedHere = wasHere && kept.count(id) == 0;
        const bool cameFromElsewhere = !wasHere && instance != nullptr;
        if (!sameType || movedHere || cameFromElsewhere)
            instance = makeInstance(effect.getType(), sampleRate, blockSize, fadeIn, factory);

        engine::ChainSlot slot;
        slot.instance = instance;
        slot.revival = revivals[id];
        slots.push_back(std::move(slot));
    }

    for (const auto id : order)
        moved.erase(id);
    orders[owner] = std::move(order);
    return slots;
}

std::shared_ptr<engine::fx::EffectInstance> EffectInstances::find(model::ObjectId effectId) const
{
    const auto found = instances.find(effectId);
    return found != instances.end() ? found->second : nullptr;
}

void EffectInstances::forgetUnused()
{
    const std::set<model::ObjectId> asked(used.begin(), used.end());
    for (auto entry = instances.begin(); entry != instances.end();)
        entry = asked.count(entry->first) > 0 ? std::next(entry) : instances.erase(entry);
    used.clear();
}

} // namespace daw::controller
