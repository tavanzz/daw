#pragma once

#include "daw/controller/TimelineViewport.h"

namespace daw::controller
{

/**
    What a piano roll shows: the timeline across (a TimelineViewport of its own) and
    the keys up the side, the highest at the top. Key k fills the rows from
    (top - k - 1) * keyHeight to (top - k) * keyHeight, top being the key value at
    y = 0. Conversions, vertical zoom and scrolling live here so that every UI (mouse
    or touch) behaves the same. Plain value type.
*/
class PianoRollViewport
{
public:
    static constexpr double minimumKeyHeight = 5.0;
    static constexpr double maximumKeyHeight = 40.0;
    static constexpr int numKeys = 128;

    TimelineViewport timeline;

    void setHeight(int heightInPixels) noexcept;
    [[nodiscard]] int getHeight() const noexcept { return height; }

    [[nodiscard]] double getKeyHeight() const noexcept { return keyHeight; }
    void setKeyHeight(double newHeight) noexcept; ///< clamped; keeps the top key

    /** The key value at y = 0 (from numKeys down, so that every key can be shown). */
    [[nodiscard]] double getTop() const noexcept { return top; }
    void setTop(double newTop) noexcept;

    /** The top edge of key's row. */
    [[nodiscard]] double keyToY(int key) const noexcept;

    /** The key whose row holds y, within 0..127. */
    [[nodiscard]] int yToKey(double y) const noexcept;

    void scrollByPixels(double pixels) noexcept;

    /** Zooms the keys by factor (above 1: taller), keeping the key under anchorY in place. */
    void zoomAround(double factor, double anchorY) noexcept;

    /** Scrolls so that keys low..high are in view, centred if they fit. */
    void showKeys(int low, int high) noexcept;

private:
    void clampTop() noexcept;

    int height = 400;
    double keyHeight = 12.0;
    double top = 96.0;
};

} // namespace daw::controller
