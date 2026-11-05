#pragma once

#include "daw/controller/ChainOwner.h"
#include "daw/controller/PluginCatalogue.h"
#include "daw/model/Clip.h"

#include <optional>
#include <vector>

namespace daw::controller
{

class Session;

/** The format the browser lists the built-in effects and instruments under. */
inline const juce::String builtInFormat{"Built-in"};

/** One row of the plugin browser: a built-in effect or instrument, or a plugin. */
struct BrowserItem
{
    juce::String builtInType; ///< "equaliser", "synth"...; empty for a plugin
    std::optional<juce::PluginDescription> plugin;
    juce::String name;
    juce::String manufacturer;
    juce::String format; ///< builtInFormat, "VST3", "AudioUnit", "LV2"
    juce::String category;
    bool isInstrument = false;
    bool favourite = false;

    /** What favourites are kept by (a built-in one: its type under builtInFormat). */
    [[nodiscard]] PluginKey key() const;

    /** What a drag carries: "builtin:<type>" or "plugin:<format>:<id>". */
    [[nodiscard]] juce::String dragDescription() const;
};

/** The built-in effects and instruments, then the plugins, that match the query
    (the query's formats may name builtInFormat), sorted by name. */
[[nodiscard]] std::vector<BrowserItem> browserItems(const PluginCatalogue& catalogue, const PluginQuery& query);

/** The item a drag description names; nothing if it names none. */
[[nodiscard]] std::optional<BrowserItem> browserItemFor(const PluginCatalogue& catalogue,
                                                        const juce::String& dragDescription);

/** Where a browser item is dropped (or double-clicked to). */
struct BrowserTarget
{
    enum class Kind
    {
        track,   ///< a track: an effect goes last in its chain, an instrument replaces its instrument
        chain,   ///< a slot of a chain (a mixer strip): an effect goes in there
        newTrack ///< empty space: a new track for it (a MIDI track for an instrument)
    };

    Kind kind = Kind::newTrack;
    ChainOwner owner;
    int index = -1; ///< for chain: where in it (-1: the end)
};

/**
    Puts a browser item where it was dropped, one undo step: an effect into a chain
    (a track's end, or a slot), an instrument as a MIDI track's instrument (on an
    audio track or empty space, a new MIDI track with it). Returns the new effect's
    id; 0 if it does not go there (an instrument on a bus or the master, an effect
    into a MIDI track's instrument slot) or the session refused it.
*/
model::ObjectId addFromBrowser(Session& session, const BrowserItem& item, const BrowserTarget& target);

} // namespace daw::controller
