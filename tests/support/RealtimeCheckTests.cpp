// Tests of the test tool itself: a check that never fires would make every
// "no allocation on the audio thread" test pass for the wrong reason.

#include "support/RealtimeCheck.h"

#include <juce_core/juce_core.h>

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <thread>
#include <vector>

using daw::test::heapOperationsDuring;
using daw::test::heapOperationsOnThisThread;

TEST_CASE("Heap operations are counted for new, delete and growing containers", "[test-support]")
{
    const auto beforeNew = heapOperationsOnThisThread();
    auto* value = new int(42);
    delete value;
    const auto newAndDelete = heapOperationsOnThisThread() - beforeNew;

    const auto beforeVector = heapOperationsOnThisThread();
    {
        std::vector<float> samples;
        samples.push_back(1.0f);
    }
    const auto vectorGrowth = heapOperationsOnThisThread() - beforeVector;

    const auto beforeString = heapOperationsOnThisThread();
    {
        const juce::String text = juce::String("level ") + juce::String(42);
    }
    const auto stringBuilding = heapOperationsOnThisThread() - beforeString;

    CHECK(newAndDelete == 2);
    CHECK(vectorGrowth >= 2);
    CHECK(stringBuilding >= 2);
}

TEST_CASE("Heap operations on other threads are not counted", "[test-support]")
{
    // Starting the thread allocates on this thread, so measure only from start to
    // join. The worker's vector (one allocation, one deallocation) must not show up.
    std::thread worker([] { std::vector<int> values(1000, 7); });
    const auto afterStart = heapOperationsOnThisThread();
    worker.join();
    const auto afterJoin = heapOperationsOnThisThread();

    CHECK(afterJoin - afterStart < 2);
}

TEST_CASE("heapOperationsDuring returns zero for code that does not allocate", "[test-support]")
{
    int counter = 0;
    CHECK(heapOperationsDuring([&] { ++counter; }) == 0);
    CHECK(counter == 1);
}

TEST_CASE("heapOperationsDuring reports code that allocates", "[test-support]")
{
    if constexpr (daw::test::realtimeSanitizerEnabled)
        SKIP("RealtimeSanitizer stops the process on the first allocation instead");

    const auto count = heapOperationsDuring([] { std::vector<double> values(64); });
    CHECK(count == 2);
}

// Not discovered as a normal test (the "." tag hides it). In RealtimeSanitizer
// builds CTest runs it on its own and expects RTSan's report: malloc() is invisible
// to the operator new counter, but not to the sanitizer.
TEST_CASE("RealtimeSanitizer stops audio-thread code that calls malloc", "[.rtsan-self-test]")
{
    static void* volatile sink = nullptr;
    const auto count = heapOperationsDuring([] {
        sink = std::malloc(64);
        std::free(sink);
    });
    CHECK(count == 0);
}
