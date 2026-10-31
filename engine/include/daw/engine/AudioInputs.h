#pragma once

namespace daw::engine
{

/**
    The input channels of one audio block, as the device delivers them.

    Indexed by the device's channel number (0 = its first input), whatever subset of
    them is switched on: a channel that is off, or that the device does not have, is
    null. Each non-null channel holds the block's samples. The pointers are only
    valid during the block.
*/
struct AudioInputs
{
    /** Devices with more inputs offer only their first maxChannels. */
    static constexpr int maxChannels = 64;

    const float* const* channels = nullptr;
    int numChannels = 0;

    /** Null if the channel is off or does not exist. */
    [[nodiscard]] const float* channel(int index) const noexcept
    {
        return index >= 0 && index < numChannels ? channels[index] : nullptr;
    }
};

} // namespace daw::engine
