#include "daw/controller/PluginLoads.h"

#include <catch2/catch_test_macros.hpp>

using daw::controller::PluginLoads;

namespace
{

PluginLoads::Reading loadOf(daw::model::ObjectId effectId, float average, float peak = 0.0f, int overruns = 0)
{
    return {effectId, "Plugin " + juce::String(effectId), average, peak, overruns};
}

} // namespace

TEST_CASE("A plugin is slow above half the block on average, or for a while after an overrun",
          "[controller][plugins]")
{
    PluginLoads loads;
    loads.add({loadOf(1, 0.1f, 0.2f), loadOf(2, 0.6f, 0.7f)}, 10.0);
    REQUIRE(loads.find(1) != nullptr);
    CHECK_FALSE(loads.find(1)->slow);
    CHECK(loads.find(1)->peak == 0.2f);
    CHECK(loads.find(2)->slow);
    CHECK(loads.getSlowPlugins() == juce::StringArray{"Plugin 2"});

    // An overrun flags it for a few seconds, even at a low average.
    loads.add({loadOf(1, 0.1f, 1.3f, 1), loadOf(2, 0.4f)}, 11.0);
    CHECK(loads.find(1)->slow);
    CHECK(loads.find(1)->overruns == 1);
    CHECK_FALSE(loads.find(2)->slow);
    loads.add({loadOf(1, 0.1f), loadOf(2, 0.4f)}, 11.0 + PluginLoads::overrunSeconds - 0.1);
    CHECK(loads.find(1)->slow);
    loads.add({loadOf(1, 0.1f), loadOf(2, 0.4f)}, 11.0 + PluginLoads::overrunSeconds + 0.1);
    CHECK_FALSE(loads.find(1)->slow);
    CHECK(loads.find(1)->overruns == 1);
    CHECK(loads.getSlowPlugins().isEmpty());

    // A plugin that no longer plays is forgotten.
    loads.add({loadOf(2, 0.4f)}, 20.0);
    CHECK(loads.find(1) == nullptr);
    CHECK(loads.find(2) != nullptr);
}
