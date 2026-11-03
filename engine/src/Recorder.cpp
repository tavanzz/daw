#include "daw/engine/Recorder.h"

#include <algorithm>
#include <limits>

namespace daw::engine
{

void Recorder::begin(const RecordingSetup& setupToRecord, juce::int64 streamStart) noexcept
{
    setup = &setupToRecord;
    stopped = false;
    captureStart = streamStart + std::max(juce::int64{0}, setup->latency);
    captureEnd = std::numeric_limits<juce::int64>::max();

    firstPosition.store(setup->startPosition - std::max(juce::int64{0}, setup->countInLength),
                        std::memory_order_relaxed);
    captured.store(0, std::memory_order_relaxed);
    stopPosition.store(0, std::memory_order_relaxed);
    overrun.store(false, std::memory_order_relaxed);
    interrupted.store(false, std::memory_order_relaxed);
    state.store(RecordingState::recording, std::memory_order_release);
}

void Recorder::end(juce::int64 streamStop) noexcept
{
    if (setup == nullptr || stopped)
        return;

    stopped = true;
    const auto latency = std::max(juce::int64{0}, setup->latency);
    captureEnd = streamStop + latency;
    stopPosition.store(firstPosition.load(std::memory_order_relaxed) +
                           (streamStop - (captureStart - latency)),
                       std::memory_order_relaxed);
    state.store(RecordingState::finishing, std::memory_order_release);
}

void Recorder::cancel(const RecordingSetup& setupNeverStarted) noexcept
{
    const auto first =
        setupNeverStarted.startPosition - std::max(juce::int64{0}, setupNeverStarted.countInLength);
    firstPosition.store(first, std::memory_order_relaxed);
    captured.store(0, std::memory_order_relaxed);
    stopPosition.store(first, std::memory_order_relaxed);
    overrun.store(false, std::memory_order_relaxed);
    interrupted.store(false, std::memory_order_relaxed);
    state.store(RecordingState::complete, std::memory_order_release);
}

void Recorder::capture(const AudioInputs& inputs, juce::int64 blockStart, int numSamples) noexcept
{
    if (setup == nullptr)
        return;

    const juce::int64 from = std::max(blockStart, captureStart);
    const juce::int64 to = std::min(blockStart + numSamples, captureEnd);

    if (from < to)
    {
        const auto count = static_cast<int>(to - from);
        const auto offset = static_cast<int>(from - blockStart);

        const bool room = std::all_of(setup->targets.begin(), setup->targets.end(),
                                      [count](const RecordingSetup::Target& target) {
                                          return target.fifo->getCapacity() -
                                                     target.fifo->getNumReady() >=
                                                 count;
                                      });
        if (!room)
        {
            overrun.store(true, std::memory_order_relaxed);
            complete();
            return;
        }

        for (const auto& target : setup->targets)
        {
            const float* channels[2] = {};
            for (int channel = 0; channel < std::min(target.numInputChannels, 2); ++channel)
            {
                const float* input = inputs.channel(target.inputChannel + channel);
                channels[channel] = input != nullptr ? input + offset : nullptr;
            }

            [[maybe_unused]] const bool pushed = target.fifo->push(channels, count);
            jassert(pushed); // room was checked; only this thread fills the FIFO
        }

        captured.fetch_add(count, std::memory_order_release);
    }

    if (stopped && blockStart + numSamples >= captureEnd)
        complete();
}

void Recorder::interrupt() noexcept
{
    if (setup == nullptr)
        return;

    // Nothing more will be captured: the recording ends where its capture did.
    if (!stopped)
        stopPosition.store(firstPosition.load(std::memory_order_relaxed) +
                               captured.load(std::memory_order_relaxed),
                           std::memory_order_relaxed);

    interrupted.store(true, std::memory_order_relaxed);
    complete();
}

void Recorder::complete() noexcept
{
    setup = nullptr;
    stopped = false;
    state.store(RecordingState::complete, std::memory_order_release);
}

RecordingStatus Recorder::getStatus() const noexcept
{
    RecordingStatus status;
    status.state = state.load(std::memory_order_acquire);
    status.firstPosition = firstPosition.load(std::memory_order_relaxed);
    status.samplesCaptured = captured.load(std::memory_order_acquire);
    status.stopPosition = stopPosition.load(std::memory_order_relaxed);
    status.overrun = overrun.load(std::memory_order_relaxed);
    status.interrupted = interrupted.load(std::memory_order_relaxed);
    return status;
}

} // namespace daw::engine
