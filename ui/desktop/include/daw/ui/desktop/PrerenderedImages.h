#pragma once

#include <juce_graphics/juce_graphics.h>

#include <cstdint>
#include <functional>
#include <map>

namespace daw::ui::desktop
{

/**
    Images too costly to paint every frame (glows, moving-background tiles), painted
    once for a key and a size and reused until either changes. Each theme's look and
    feel has its own set, emptied when another theme takes over, so they are painted
    again only after a change of theme or of size.

    Message thread.
*/
class PrerenderedImages final
{
public:
    using Painter = std::function<void(juce::Graphics& g, juce::Rectangle<int> area)>;

    /** At most this many images are kept; the least recently used go first. */
    static constexpr std::size_t capacity = 96;

    /** The image painted for this key at this size (ARGB, transparent where not
        painted), painting it if there is none yet. */
    [[nodiscard]] juce::Image get(const juce::String& key, juce::Point<int> size,
                                  const Painter& paint);

    void clear();

    [[nodiscard]] std::size_t size() const noexcept { return images.size(); }

    /** How many images have been painted so far (tests). */
    [[nodiscard]] int getPaintCount() const noexcept { return paintCount; }

private:
    struct Entry
    {
        juce::Image image;
        std::uint64_t lastUse = 0;
    };

    std::map<juce::String, Entry> images;
    std::uint64_t useCounter = 0;
    int paintCount = 0;
};

} // namespace daw::ui::desktop
