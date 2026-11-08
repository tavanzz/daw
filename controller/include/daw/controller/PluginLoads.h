#pragma once

#include "daw/model/Clip.h"

#include <juce_core/juce_core.h>

#include <map>
#include <vector>

namespace daw::controller
{

/**
    What the UI shows of what plugins cost: per plugin effect, the share of each
    block's duration it takes (the average over about a second, the most of one
    block lately) and whether it is slow: its average above half the block's
    duration, or a block it overran in the last few seconds. A slow plugin is only
    flagged, never bypassed. Message thread; the session feeds it a few times a
    second (Session::getPluginLoads()).
*/
class PluginLoads
{
public:
    static constexpr float slowAverage = 0.5f;
    /** How long an overrun keeps a plugin flagged. */
    static constexpr double overrunSeconds = 5.0;

    /** What a plugin took since the last reading (engine::plugins::PluginEffect::Load). */
    struct Reading
    {
        model::ObjectId effectId = 0;
        juce::String name;
        float average = 0.0f; ///< over about a second, of each block's duration
        float peak = 0.0f;    ///< the most of one block
        int overruns = 0;     ///< blocks it took longer than
    };

    struct Entry
    {
        juce::String name;
        float average = 0.0f;
        float peak = 0.0f;
        int overruns = 0; ///< since it started playing
        double lastOverrun = -1.0e9;
        bool slow = false;
    };

    /** The loads taken now; plugins not among them are forgotten (gone, or not
        playing any more). */
    void add(const std::vector<Reading>& readings, double nowSeconds);

    [[nodiscard]] const Entry* find(model::ObjectId effectId) const;

    /** The names of the slow plugins, for the status bar. */
    [[nodiscard]] juce::StringArray getSlowPlugins() const;

private:
    std::map<model::ObjectId, Entry> entries;
};

} // namespace daw::controller
