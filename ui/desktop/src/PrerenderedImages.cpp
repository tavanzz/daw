#include "daw/ui/desktop/PrerenderedImages.h"

#include <algorithm>

namespace daw::ui::desktop
{

juce::Image PrerenderedImages::get(const juce::String& key, juce::Point<int> size,
                                   const Painter& paint)
{
    const auto fullKey = key + "@" + juce::String(size.x) + "x" + juce::String(size.y);

    if (auto found = images.find(fullKey); found != images.end())
    {
        found->second.lastUse = ++useCounter;
        return found->second.image;
    }

    if (images.size() >= capacity)
    {
        const auto oldest =
            std::min_element(images.begin(), images.end(), [](const auto& a, const auto& b)
                             { return a.second.lastUse < b.second.lastUse; });
        images.erase(oldest);
    }

    juce::Image image(juce::Image::ARGB, std::max(1, size.x), std::max(1, size.y), true);
    {
        juce::Graphics g(image);
        paint(g, image.getBounds());
    }

    ++paintCount;
    images[fullKey] = {image, ++useCounter};
    return image;
}

void PrerenderedImages::clear()
{
    images.clear();
}

} // namespace daw::ui::desktop
