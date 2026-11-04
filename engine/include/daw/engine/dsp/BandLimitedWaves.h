#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace daw::engine::dsp
{

/**
    Single-cycle tables of the classic waves, band-limited: each table holds the
    harmonics up to a count, and an oscillator reads the tables whose harmonics all
    stay below 0.45 of the sample rate at its frequency, so no harmonic ever folds
    back (aliasing). Tables come every third of an octave of harmonic counts (up to
    4096: below 5 Hz the top harmonics would be above hearing anyway); reading
    crossfades the two whose counts are just below the limit, so the timbre moves
    smoothly with the pitch. Samples are read with cubic (Hermite) interpolation from
    tables at least eight samples per cycle of their highest harmonic.

    Built once for the whole app (independent of the sample rate), on first use off
    the audio thread; read-only afterwards, so any thread may read it.
*/
class BandLimitedWaves
{
public:
    enum class Wave : std::uint8_t
    {
        sine,
        triangle,
        saw ///< rising from -1 to 1 (a pulse is the difference of two saws)
    };

    static constexpr int tablesPerOctave = 3;
    static constexpr int maximumHarmonics = 4096;
    static constexpr double highestFraction = 0.45; ///< of the sample rate

    /** The tables to read for one frequency, and how much of the second. */
    struct Selection
    {
        int lower = 0;
        int upper = 0;
        float upperWeight = 0.0f;
    };

    /** The shared set, built the first time; thread safe, but not for the audio thread
        the first time. */
    static const BandLimitedWaves& get();

    /** The tables for a fundamental of increment cycles per sample. Real-time safe. */
    [[nodiscard]] Selection select(double increment) const noexcept;

    /** The wave at phase (0 to 1). Real-time safe. */
    [[nodiscard]] float read(Wave wave, double phase, const Selection& selection) const noexcept;

    /** How many harmonics table index holds. */
    [[nodiscard]] int harmonicsOf(int index) const noexcept;

private:
    BandLimitedWaves();

    struct Table
    {
        int harmonics = 1;
        int size = 0;               ///< a power of two
        std::vector<float> samples; ///< size + 3: one before, two after, for interpolation
    };

    [[nodiscard]] static float readTable(const Table& table, double phase) noexcept;

    static constexpr int numTables = 37; // 2^(36 / 3) = 4096 harmonics
    std::array<std::vector<Table>, 3> tables;
};

} // namespace daw::engine::dsp
