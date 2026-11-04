#include "daw/controller/MidiTakes.h"

#include <algorithm>

namespace daw::controller
{

MidiTakeContent midiTakeContent(const RecordedMidiTake& take, juce::int64 stopPosition,
                                double sampleRate)
{
    namespace midi = engine::midi;
    MidiTakeContent content;
    const auto origin = model::samplesToTicks(take.firstPosition, sampleRate);
    const auto toContent = [&](juce::int64 position) {
        return std::max(model::Ticks{0}, model::samplesToTicks(position, sampleRate) - origin);
    };

    struct Open
    {
        int channel = 0;
        int key = 0;
        model::Ticks start = 0;
        int velocity = 0;
    };
    std::vector<Open> open;

    const auto close = [&](std::size_t index, model::Ticks end) {
        const auto& note = open[index];
        content.notes.push_back({0, note.start, std::max(model::Ticks{1}, end - note.start), note.key,
                                 note.velocity});
        open.erase(open.begin() + static_cast<std::ptrdiff_t>(index));
    };

    for (const auto& recorded : take.events)
    {
        const auto& event = recorded.event;
        const auto at = toContent(recorded.position);
        switch (event.type)
        {
        case midi::EventType::noteOn:
            open.push_back({event.channel, event.number, at,
                            std::clamp(midi::toSevenBit(event.value), model::Note::minimumVelocity,
                                       model::Note::maximumVelocity)});
            break;
        case midi::EventType::noteOff:
            for (std::size_t i = 0; i < open.size(); ++i)
                if (open[i].channel == event.channel && open[i].key == event.number)
                {
                    close(i, at);
                    break;
                }
            break;
        case midi::EventType::controller:
            content.controllers.push_back(
                {model::ControllerType::controlChange(event.number), at, midi::toSevenBit(event.value)});
            break;
        case midi::EventType::pitchBend:
            content.controllers.push_back(
                {model::ControllerType::pitchBend(), at, midi::toPitchBend(event.value)});
            break;
        case midi::EventType::channelPressure:
            content.controllers.push_back(
                {model::ControllerType::channelPressure(), at, midi::toSevenBit(event.value)});
            break;
        }
    }

    const auto stop = toContent(stopPosition);
    while (!open.empty())
        close(0, std::max(stop, open.front().start + 1));

    model::normalise(content.notes);
    model::normalise(content.controllers);
    return content;
}

} // namespace daw::controller
