#include "daw/controller/FadeOutKeeper.h"

#include <algorithm>
#include <set>

namespace daw::controller
{

namespace
{

std::set<const void*> instancesIn(const engine::Arrangement& arrangement)
{
    std::set<const void*> found;
    const auto add = [&found](const engine::EffectChain& chain) {
        for (const auto& slot : chain.slots)
            found.insert(slot.instance.get());
    };
    for (const auto& track : arrangement.tracks)
        add(track.inserts);
    for (const auto& bus : arrangement.buses)
        add(bus.inserts);
    add(arrangement.masterInserts);
    return found;
}

// Keys for sends and buses, which have no object of their own that persists: their
// ids, as pointer-sized values out of reach of real pointers (odd values).
const void* keyOf(juce::int64 id, int kind)
{
    return reinterpret_cast<const void*>(static_cast<std::uintptr_t>(id) * 4 + 1 +
                                         static_cast<std::uintptr_t>(kind) * 2);
}

} // namespace

bool FadeOutKeeper::keeps(const void* key, double now)
{
    const auto [entry, inserted] = since.try_emplace(key, now);
    juce::ignoreUnused(inserted);
    return now - entry->second < keepSeconds;
}

void FadeOutKeeper::keep(engine::Arrangement& arrangement, const engine::Arrangement* previous,
                         double now)
{
    std::map<const void*, double> stillKept;
    const auto keepUntilDue = [&](const void* key) {
        if (!keeps(key, now))
            return false;
        stillKept[key] = since[key];
        return true;
    };

    if (previous != nullptr)
    {
        const auto present = instancesIn(arrangement);

        // Effects: switched off at their old place in the chain they were in.
        const auto keepEffects = [&](const engine::EffectChain& before,
                                     engine::EffectChain& after) {
            for (std::size_t i = 0; i < before.slots.size(); ++i)
            {
                const auto& slot = before.slots[i];
                if (slot.instance == nullptr || present.count(slot.instance.get()) > 0 ||
                    !keepUntilDue(slot.instance.get()))
                    continue;

                auto fading = slot;
                fading.enabled = false;
                fading.automation.clear();
                const auto at = std::min(i, after.slots.size());
                after.slots.insert(after.slots.begin() + static_cast<std::ptrdiff_t>(at), fading);
            }
        };

        for (const auto& old : previous->tracks)
        {
            const auto track = std::find_if(arrangement.tracks.begin(), arrangement.tracks.end(),
                                            [&old](const auto& t) { return t.id == old.id; });
            if (track == arrangement.tracks.end())
                continue;

            keepEffects(old.inserts, track->inserts);

            // Sends: at gain 0.
            for (const auto& send : old.sends)
            {
                const bool gone = std::none_of(track->sends.begin(), track->sends.end(),
                                               [&send](const auto& s) { return s.id == send.id; });
                if (gone && keepUntilDue(keyOf(send.id, 0)))
                {
                    auto fading = send;
                    fading.gain = 0.0f;
                    fading.levelCurve.reset();
                    track->sends.push_back(fading);
                }
            }
        }

        for (const auto& old : previous->buses)
        {
            const auto bus = std::find_if(arrangement.buses.begin(), arrangement.buses.end(),
                                          [&old](const auto& b) { return b.id == old.id; });
            if (bus != arrangement.buses.end())
            {
                keepEffects(old.inserts, bus->inserts);
            }
            else if (keepUntilDue(keyOf(old.id, 1)))
            {
                // A bus: silent, effects and all.
                auto fading = old;
                fading.leftGain = 0.0f;
                fading.rightGain = 0.0f;
                fading.automation = {};
                arrangement.buses.push_back(fading);
            }
        }

        keepEffects(previous->masterInserts, arrangement.masterInserts);

        // Sends keep pointing at their buses where they are now.
        for (auto& track : arrangement.tracks)
            for (auto& send : track.sends)
                for (std::size_t b = 0; b < arrangement.buses.size(); ++b)
                    if (arrangement.buses[b].id == send.bus)
                        send.busIndex = static_cast<int>(b);
    }

    since = std::move(stillKept);
}

bool FadeOutKeeper::isDue(double now) const
{
    return std::any_of(since.begin(), since.end(),
                       [now](const auto& entry) { return now - entry.second >= keepSeconds; });
}

} // namespace daw::controller
