#include "daw/controller/PianoRollViewport.h"

#include <algorithm>
#include <cmath>

namespace daw::controller
{

void PianoRollViewport::setHeight(int heightInPixels) noexcept
{
    height = std::max(1, heightInPixels);
    clampTop();
}

void PianoRollViewport::setKeyHeight(double newHeight) noexcept
{
    keyHeight = std::clamp(newHeight, minimumKeyHeight, maximumKeyHeight);
    clampTop();
}

void PianoRollViewport::setTop(double newTop) noexcept
{
    top = newTop;
    clampTop();
}

double PianoRollViewport::keyToY(int key) const noexcept
{
    return (top - static_cast<double>(key) - 1.0) * keyHeight;
}

int PianoRollViewport::yToKey(double y) const noexcept
{
    // Rows are [top - k - 1, top - k) in key units from the top: the key is the one
    // whose row's lower edge is below y.
    return std::clamp(static_cast<int>(std::ceil(top - y / keyHeight)) - 1, 0, numKeys - 1);
}

void PianoRollViewport::scrollByPixels(double pixels) noexcept
{
    setTop(top + pixels / keyHeight);
}

void PianoRollViewport::zoomAround(double factor, double anchorY) noexcept
{
    const double anchorKey = top - anchorY / keyHeight;
    keyHeight = std::clamp(keyHeight * factor, minimumKeyHeight, maximumKeyHeight);
    setTop(anchorKey + anchorY / keyHeight);
}

void PianoRollViewport::showKeys(int low, int high) noexcept
{
    if (high < low)
        std::swap(low, high);
    const double visible = height / keyHeight;
    const double span = high - low + 1;
    setTop(span <= visible ? high + 1 + (visible - span) / 2.0 : high + 1);
}

void PianoRollViewport::clampTop() noexcept
{
    const double visible = height / keyHeight;
    top = std::clamp(top, std::min(visible, static_cast<double>(numKeys)),
                     static_cast<double>(numKeys));
}

} // namespace daw::controller
