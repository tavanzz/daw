#pragma once

#include "daw/engine/Arrangement.h"
#include "daw/engine/Engine.h"
#include "daw/engine/fx/EffectInstance.h"

#include "support/RealtimeCheck.h"
#include "support/TestEffects.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <memory>
#include <vector>

namespace daw::test
{

/** A test instrument hosted as the mixer hosts one, and the instrument itself. */
struct HostedInstrument
{
    std::shared_ptr<engine::fx::EffectInstance> instance;
    TestInstrument* probe = nullptr;
};

inline HostedInstrument makeTestInstrument(double sampleRate = 48000.0, int maximumBlock = 512)
{
    engine::fx::ProcessSetup setup;
    setup.sampleRate = sampleRate;
    setup.maximumBlockSize = maximumBlock;
    auto* probe = new TestInstrument();
    return {std::make_shared<engine::fx::EffectInstance>(
                std::unique_ptr<engine::fx::Effect>(probe), setup),
            probe};
}

/** A note with an identity of its own (its index + 1 unless given). */
inline engine::ArrangementNote makeNote(juce::int64 start, juce::int64 end, int key,
                                        float velocity, std::uint64_t identity = 0)
{
    engine::ArrangementNote note;
    note.start = start;
    note.end = end;
    note.key = static_cast<std::uint8_t>(key);
    note.velocity = velocity;
    note.identity = identity;
    return note;
}

/** One MIDI track (id trackId) playing notes through instrument. */
inline std::unique_ptr<engine::Arrangement>
makeMidiArrangement(const HostedInstrument& instrument, std::vector<engine::ArrangementNote> notes,
                    juce::int64 length, double sampleRate = 48000.0, juce::int64 trackId = 1)
{
    for (std::size_t i = 0; i < notes.size(); ++i)
        if (notes[i].identity == 0)
            notes[i].identity = i + 1;
    std::stable_sort(notes.begin(), notes.end(),
                     [](const auto& a, const auto& b) { return a.start < b.start; });

    auto arrangement = std::make_unique<engine::Arrangement>();
    arrangement->sampleRate = sampleRate;
    arrangement->length = length;

    engine::ArrangementTrack track;
    track.id = trackId;
    track.playsNotes = true;
    track.notes = std::move(notes);
    engine::ChainSlot slot;
    slot.instance = instrument.instance;
    slot.parameters = {0.5f}; // level 1 (0..2)
    track.inserts.slots.push_back(std::move(slot));
    arrangement->tracks.push_back(std::move(track));
    arrangement->updateLayoutHash();
    return arrangement;
}

/** Plays an engine in blocks whose sizes cycle through blockSizes and returns the
    left output channel, counting heap operations inside process(). */
struct EngineRun
{
    std::vector<float> left;
    std::size_t heapOperations = 0;
};

inline EngineRun runEngine(engine::Engine& engine, int numSamples, const std::vector<int>& blockSizes)
{
    EngineRun run;
    int largest = 1;
    for (int size : blockSizes)
        largest = std::max(largest, size);
    juce::AudioBuffer<float> block(2, largest);

    std::size_t next = 0;
    for (int done = 0; done < numSamples;)
    {
        const int size = std::min(blockSizes[next++ % blockSizes.size()], numSamples - done);
        juce::AudioBuffer<float> view(block.getArrayOfWritePointers(), 2, size);
        run.heapOperations += heapOperationsDuring([&] { engine.process(view); });
        run.left.insert(run.left.end(), view.getReadPointer(0), view.getReadPointer(0) + size);
        done += size;
    }
    return run;
}

} // namespace daw::test
