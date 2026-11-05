#pragma once

namespace daw::engine::rt
{

/**
    Marks code that is not ours running on the audio thread: a plugin's process(),
    reset() and parameter calls, through JUCE's format code. We cannot promise what
    a plugin does (it may allocate or lock), so the real-time checks leave it out:
    the tests' heap counter does not count inside, and RealtimeSanitizer is switched
    off inside. Everything around it (the adapter) is still checked.

    RAII; nests; real-time safe.
*/
class ForeignCode
{
public:
    ForeignCode() noexcept;
    ~ForeignCode();

    ForeignCode(const ForeignCode&) = delete;
    ForeignCode& operator=(const ForeignCode&) = delete;

    /** Whether foreign code runs on the calling thread now. */
    [[nodiscard]] static bool isRunning() noexcept;
};

} // namespace daw::engine::rt
