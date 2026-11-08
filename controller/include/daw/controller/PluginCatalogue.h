#pragma once

#include <juce_audio_processors_headless/juce_audio_processors_headless.h>
#include <juce_core/juce_core.h>

#include <map>
#include <optional>
#include <set>
#include <vector>

namespace daw::controller
{

/** A plugin as the scanner found it: JUCE's description (what loading it needs) and
    whether the app can host it. */
struct PluginInfo
{
    juce::PluginDescription description;
    /** Why the app cannot host it (a MIDI effect, more than two main channels...);
        empty when it can. Such plugins stay listed in the Plugin Manager, not in the
        browser. */
    juce::String unsupported;

    [[nodiscard]] bool isSupported() const noexcept { return unsupported.isEmpty(); }
};

/** Identifies a plugin across scans and machines: its format and JUCE's unique id,
    which each format derives from what the plugin declares (VST3 class id, AU
    component codes, LV2 URI), not from where it is installed. */
struct PluginKey
{
    juce::String format;
    int uniqueId = 0;

    [[nodiscard]] static PluginKey of(const juce::PluginDescription& description);
    [[nodiscard]] juce::String toString() const;
    [[nodiscard]] static std::optional<PluginKey> fromString(const juce::String& text);

    bool operator==(const PluginKey&) const = default;
    bool operator<(const PluginKey& other) const
    {
        return format != other.format ? format < other.format : uniqueId < other.uniqueId;
    }
};

/** Why a file (or AU component, LV2 plugin) is on the blacklist. */
enum class ScanFailure
{
    crashed,     ///< the scanner process died while loading it
    timedOut,    ///< it did not answer in time (hung)
    noPlugins,   ///< loaded, but declared no plugin
    failedToLoad ///< the format could not load it
};

[[nodiscard]] juce::String toString(ScanFailure failure);
[[nodiscard]] std::optional<ScanFailure> scanFailureFromString(const juce::String& text);

/** One thing to scan: a VST3 bundle, an AU component's identifier, an LV2 URI. */
struct ScanItem
{
    juce::String format;
    juce::String item;

    bool operator==(const ScanItem&) const = default;
    bool operator<(const ScanItem& other) const
    {
        return format != other.format ? format < other.format : item < other.item;
    }
};

/** What the browser asks for (see PluginCatalogue::search()). */
struct PluginQuery
{
    enum class Kind
    {
        any,
        effects,
        instruments
    };

    juce::String text;              ///< words, each found in the name, manufacturer, category or format
    std::set<juce::String> formats; ///< empty: every format
    Kind kind = Kind::any;
    juce::String manufacturer;      ///< empty: every manufacturer
    bool favouritesOnly = false;
};

/**
    The plugins the scanner found, with what scanning needs to stay incremental and
    safe: which files (or components) were scanned, the blacklist, the folders to
    search per format, and the user's favourites. Pure data and rules, no plugin is
    ever loaded here (the scanner does that in another process, see PluginScanner),
    so every UI and the tests use it alike. Saved as JSON (toJson(), fromJson()).

    Incremental scanning (planScan()): items the formats find now are compared with
    what was scanned before. New items are scanned; items whose format says they
    changed (a VST3 bundle's date, an AU's version, an LV2 plugin's data) are
    scanned again; items gone are forgotten; blacklisted items are left alone until
    the user retries them, except files that changed since they failed.

    Message thread.
*/
class PluginCatalogue
{
public:
    /** The formats the app hosts on this platform, in the order the UI lists them. */
    [[nodiscard]] static juce::StringArray hostedFormats();

    /** Where a format's plugins are searched by default (none for AU, which macOS
        lists itself). */
    [[nodiscard]] static juce::FileSearchPath defaultFolders(juce::AudioPluginFormat& format);

    // Folders -------------------------------------------------------------------------

    /** The user's own folders for a format, searched after the default ones. */
    void setCustomFolders(const juce::String& format, const juce::StringArray& folders);
    [[nodiscard]] juce::StringArray getCustomFolders(const juce::String& format) const;

    /** Every folder searched for a format: the defaults, then the custom ones. */
    [[nodiscard]] juce::FileSearchPath foldersFor(juce::AudioPluginFormat& format) const;

    // Scanning ------------------------------------------------------------------------

    /** What to scan now, given the items the formats find (format manager with the
        hosted formats). Forgets plugins whose items are gone. everything: scan every
        item again, except the blacklisted ones. */
    [[nodiscard]] std::vector<ScanItem> planScan(juce::AudioPluginFormatManager& formats,
                                                 bool everything = false);

    /** The scanner's result for an item: the plugins it holds (replacing the old). */
    void addScanned(const ScanItem& item, std::vector<PluginInfo> plugins);

    /** Puts an item on the blacklist (its plugins are forgotten). */
    void block(const ScanItem& item, ScanFailure failure, const juce::String& detail = {});

    /** Takes an item off the blacklist, so that the next scan tries it again. */
    void unblock(const ScanItem& item);

    struct Blocked
    {
        ScanItem item;
        ScanFailure failure = ScanFailure::crashed;
        juce::String detail;
        juce::int64 modified = 0; ///< a file's modification time when it failed (ms), else 0
    };
    [[nodiscard]] std::vector<Blocked> getBlocked() const;
    [[nodiscard]] bool isBlocked(const ScanItem& item) const;

    // Plugins -------------------------------------------------------------------------

    /** Every plugin found, sorted by name, then manufacturer, then format. */
    [[nodiscard]] std::vector<PluginInfo> getPlugins() const;

    /** The supported plugins matching the query, sorted as getPlugins(). */
    [[nodiscard]] std::vector<PluginInfo> search(const PluginQuery& query) const;

    /** The manufacturers of the supported plugins, sorted, each once. */
    [[nodiscard]] juce::StringArray getManufacturers() const;

    [[nodiscard]] std::optional<PluginInfo> find(const PluginKey& key) const;

    /** How many items were scanned (each may hold several plugins). */
    [[nodiscard]] int getNumScannedItems() const noexcept { return static_cast<int>(scanned.size()); }

    // Favourites ----------------------------------------------------------------------

    void setFavourite(const PluginKey& key, bool favourite);
    [[nodiscard]] bool isFavourite(const PluginKey& key) const;

    // Persistence ---------------------------------------------------------------------

    [[nodiscard]] juce::var toJson() const;

    /** Never fails: what cannot be read is left out. */
    [[nodiscard]] static PluginCatalogue fromJson(const juce::var& json);

    bool saveTo(const juce::File& file) const;
    [[nodiscard]] static PluginCatalogue loadFrom(const juce::File& file);

    /** Whether anything changed since the last call (so the UI and the settings
        know when to refresh). */
    [[nodiscard]] bool takeChanged() noexcept { return std::exchange(changed, false); }

private:
    std::map<ScanItem, std::vector<PluginInfo>> scanned;
    std::map<ScanItem, Blocked> blocked;
    std::map<juce::String, juce::StringArray> customFolders;
    std::set<PluginKey> favourites;
    bool changed = false;
};

/** A plugin description as JSON, and back (the fields JUCE needs to load it). */
[[nodiscard]] juce::var descriptionToJson(const juce::PluginDescription& description);
[[nodiscard]] std::optional<juce::PluginDescription> descriptionFromJson(const juce::var& json);

} // namespace daw::controller
