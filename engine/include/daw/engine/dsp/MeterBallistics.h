#pragma once

namespace daw::engine::dsp
{

/**
    What a peak meter shows, given peak readings over time.

    - The level rises instantly and falls at fallRateDbPerSecond.
    - A peak-hold marker stays at the highest recent level for holdSeconds, then
      falls at the same rate (never below the level).
    - The clip indicator lights when a sample reaches 0 dBFS and stays lit until
      resetClip().

    Everything depends on elapsed time, not on how often update() is called, so a
    meter moves the same way at any refresh rate. Shared by every UI; use it from
    one thread (the UI's).
*/
class MeterBallistics
{
public:
    static constexpr float floorDb = -60.0f; // anything lower shows as an empty meter
    static constexpr float fallRateDbPerSecond = 20.0f;
    static constexpr double holdSeconds = 1.5;

    /** peak: highest absolute sample since the previous update, as a linear gain.
        elapsedSeconds: time since the previous update. */
    void update(float peak, double elapsedSeconds) noexcept;

    [[nodiscard]] float getLevelDb() const noexcept { return levelDb; }
    [[nodiscard]] float getPeakHoldDb() const noexcept { return holdDb; }
    [[nodiscard]] bool isClipping() const noexcept { return clipping; }
    void resetClip() noexcept { clipping = false; }

    /** Where a level sits on the meter scale: 0 at floorDb or below, 1 at 0 dBFS. */
    [[nodiscard]] static float toProportion(float decibels) noexcept;

private:
    float levelDb = floorDb;
    float holdDb = floorDb;
    double timeSinceHold = 0.0;
    bool clipping = false;
};

} // namespace daw::engine::dsp
