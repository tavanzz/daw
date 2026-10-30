#pragma once

#include <cstddef>
#include <memory>
#include <type_traits>

namespace daw::test
{

#if defined(__has_feature)
#if __has_feature(realtime_sanitizer)
#define DAW_TEST_HAS_RTSAN 1
#endif
#endif

/** True in RealtimeSanitizer builds (preset linux-rtsan, run in CI). */
#if defined(DAW_TEST_HAS_RTSAN)
inline constexpr bool realtimeSanitizerEnabled = true;
#else
inline constexpr bool realtimeSanitizerEnabled = false;
#endif

namespace detail
{
std::size_t runAsAudioThread(void (*function)(void*), void* context);
}

/**
    Runs function the way the audio thread would and returns how many heap
    operations (operator new and operator delete calls) it made on this thread.
    Code that runs on the audio thread must return 0.

    In RealtimeSanitizer builds the function also runs in a real-time context: any
    malloc, lock, blocking system call or file I/O aborts the test with a report.
    That catches what counting operator new cannot, such as JUCE's HeapBlock,
    which calls malloc directly.

    Keep assertions outside function: Catch2 itself allocates.
*/
template <typename Function>
std::size_t heapOperationsDuring(Function&& function)
{
    using Callable = std::remove_reference_t<Function>;
    Callable* callable = std::addressof(function);

    return detail::runAsAudioThread([](void* context) { (*static_cast<Callable*>(context))(); },
                                    const_cast<void*>(static_cast<const void*>(callable)));
}

/** Total operator new and operator delete calls made so far by the calling thread. */
[[nodiscard]] std::size_t heapOperationsOnThisThread() noexcept;

} // namespace daw::test
