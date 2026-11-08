#include "daw/services/SemanticVersion.h"

#include <algorithm>
#include <tuple>

namespace daw::services
{

namespace
{

bool isNumber(const juce::String& text)
{
    return text.isNotEmpty() && text.containsOnly("0123456789");
}

// A numeric part: digits, no leading zero (semver), small enough for an int.
std::optional<int> numberFrom(const juce::String& text)
{
    if (!isNumber(text) || (text.length() > 1 && text[0] == '0') || text.length() > 9)
        return std::nullopt;
    return text.getIntValue();
}

} // namespace

std::optional<SemanticVersion> SemanticVersion::parse(const juce::String& input)
{
    auto text = input.trim();
    if (text.startsWithChar('v'))
        text = text.substring(1);
    text = text.upToFirstOccurrenceOf("+", false, false);

    const auto core = text.upToFirstOccurrenceOf("-", false, false);
    const auto parts = juce::StringArray::fromTokens(core, ".", {});
    if (parts.size() != 3 || core.endsWithChar('.'))
        return std::nullopt;

    SemanticVersion version;
    const auto majorPart = numberFrom(parts[0]);
    const auto minorPart = numberFrom(parts[1]);
    const auto patchPart = numberFrom(parts[2]);
    if (!majorPart || !minorPart || !patchPart)
        return std::nullopt;
    version.major = *majorPart;
    version.minor = *minorPart;
    version.patch = *patchPart;

    if (text.containsChar('-'))
    {
        const auto suffix = text.fromFirstOccurrenceOf("-", false, false);
        version.preRelease = juce::StringArray::fromTokens(suffix, ".", {});
        if (suffix.isEmpty() || suffix.startsWithChar('.') || suffix.endsWithChar('.') ||
            suffix.contains("..") || version.preRelease.contains({}))
            return std::nullopt;
        for (const auto& identifier : version.preRelease)
            if (!identifier.containsOnly("0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ-") ||
                (isNumber(identifier) && !numberFrom(identifier)))
                return std::nullopt;
    }
    return version;
}

juce::String SemanticVersion::toString() const
{
    juce::String text;
    text << major << "." << minor << "." << patch;
    if (isPreRelease())
        text << "-" << preRelease.joinIntoString(".");
    return text;
}

std::strong_ordering SemanticVersion::operator<=>(const SemanticVersion& other) const
{
    if (const auto order = std::tie(major, minor, patch) <=> std::tie(other.major, other.minor, other.patch);
        order != 0)
        return order;

    // A pre-release comes before its release.
    if (isPreRelease() != other.isPreRelease())
        return isPreRelease() ? std::strong_ordering::less : std::strong_ordering::greater;

    for (int index = 0; index < std::min(preRelease.size(), other.preRelease.size()); ++index)
    {
        const auto& a = preRelease[index];
        const auto& b = other.preRelease[index];
        const bool aNumber = isNumber(a), bNumber = isNumber(b);
        if (aNumber && bNumber)
        {
            if (const auto order = a.getIntValue() <=> b.getIntValue(); order != 0)
                return order;
        }
        else if (aNumber != bNumber)
        {
            // Numbers come before words.
            return aNumber ? std::strong_ordering::less : std::strong_ordering::greater;
        }
        else if (const auto order = a.compare(b) <=> 0; order != 0)
        {
            return order;
        }
    }
    return preRelease.size() <=> other.preRelease.size();
}

} // namespace daw::services
