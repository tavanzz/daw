#pragma once

#include <juce_core/juce_core.h>

#include <compare>
#include <optional>

namespace daw::services
{

/**
    A version as Semantic Versioning 2.0.0 orders it: "1.2.3", "1.2.3-rc.1" (a release
    candidate, older than 1.2.3); build metadata ("+...") is ignored. A leading "v", as in
    release tags, is accepted.
*/
class SemanticVersion
{
public:
    [[nodiscard]] static std::optional<SemanticVersion> parse(const juce::String& text);

    [[nodiscard]] int getMajor() const noexcept { return major; }
    [[nodiscard]] int getMinor() const noexcept { return minor; }
    [[nodiscard]] int getPatch() const noexcept { return patch; }
    [[nodiscard]] bool isPreRelease() const noexcept { return !preRelease.isEmpty(); }

    /** "1.2.3-rc.1" */
    [[nodiscard]] juce::String toString() const;

    [[nodiscard]] std::strong_ordering operator<=>(const SemanticVersion& other) const;
    [[nodiscard]] bool operator==(const SemanticVersion& other) const { return (*this <=> other) == 0; }

private:
    int major = 0, minor = 0, patch = 0;
    juce::StringArray preRelease; // dot-separated identifiers
};

} // namespace daw::services
