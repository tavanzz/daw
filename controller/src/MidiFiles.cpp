#include "daw/controller/MidiFiles.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace daw::controller
{

namespace
{

// One event of a track being written: its pulse, and its order within a pulse
// (note-offs, then controllers, then note-ons).
struct Outgoing
{
    juce::int64 pulse = 0;
    int order = 0;
    juce::MidiMessage message;
};

struct Interval
{
    model::Ticks start = 0;
    model::Ticks end = 0;
};

std::vector<Interval> uncovered(model::Ticks start, model::Ticks end,
                                const std::vector<Interval>& covered)
{
    std::vector<Interval> parts;
    model::Ticks position = start;
    for (const auto& interval : covered)
    {
        if (interval.end <= position)
            continue;
        if (interval.start >= end)
            break;
        if (interval.start > position)
            parts.push_back({position, interval.start});
        position = std::max(position, interval.end);
        if (position >= end)
            break;
    }
    if (position < end)
        parts.push_back({position, end});
    return parts;
}

void cover(std::vector<Interval>& covered, model::Ticks start, model::Ticks end)
{
    covered.push_back({start, end});
    std::sort(covered.begin(), covered.end(),
              [](const Interval& a, const Interval& b) { return a.start < b.start; });
    std::vector<Interval> merged;
    for (const auto& interval : covered)
    {
        if (!merged.empty() && interval.start <= merged.back().end)
            merged.back().end = std::max(merged.back().end, interval.end);
        else
            merged.push_back(interval);
    }
    covered = std::move(merged);
}

juce::MidiMessage controllerMessage(int channel, const model::ControllerType& type, int value)
{
    switch (type.kind)
    {
    case model::ControllerType::Kind::pitchBend:
        return juce::MidiMessage::pitchWheel(channel, value);
    case model::ControllerType::Kind::channelPressure:
        return juce::MidiMessage::channelPressureChange(channel, value);
    case model::ControllerType::Kind::controlChange:
        break;
    }
    return juce::MidiMessage::controllerEvent(channel, type.number, value);
}

// What a MIDI track plays, as file events (as the arrangement builder does, in ticks).
std::vector<Outgoing> eventsOf(const model::Track& track, double ticksPerQuarter)
{
    const auto toPulse = [ticksPerQuarter](model::Ticks ticks) {
        return static_cast<juce::int64>(
            std::llround(static_cast<double>(ticks) * midiFileResolution / ticksPerQuarter));
    };
    const int channel = track.getMidiChannel();
    std::vector<Outgoing> events;

    struct PlayedNote
    {
        model::Ticks start = 0;
        model::Ticks end = 0;
        int key = 60;
        int velocity = 100;
    };
    std::vector<PlayedNote> played;

    struct Step
    {
        model::Ticks position = 0;
        int order = 0; // 0: going back to rest at a clip's end
        int value = 0;
    };
    std::map<model::ControllerType, std::vector<Step>> steps;

    std::vector<Interval> covered;
    for (int c = track.getNumClips() - 1; c >= 0; --c)
    {
        const auto clip = track.getClip(c);
        const auto end = clip.getEnd();
        if (!clip.isMidi() || !end)
            continue;

        const auto parts = uncovered(clip.getPosition(), *end, covered);
        cover(covered, clip.getPosition(), *end);
        const model::MidiClip midi(clip.getState());
        const auto origin = clip.getPosition() - clip.getOffset();
        const auto notes = midi.getNotes();
        const auto controllers = midi.getControllerEvents();

        for (const auto& part : parts)
        {
            for (const auto& note : notes)
            {
                const auto start = origin + note.start;
                if (start < part.start || start >= part.end)
                    continue;
                played.push_back({start, std::min(origin + note.getEnd(), part.end), note.key,
                                  note.velocity});
            }

            for (const auto& type : midi.getControllerTypes())
            {
                auto& lane = steps[type];
                int value = type.restValue();
                for (const auto& event : controllers)
                {
                    if (event.type != type)
                        continue;
                    const auto at = origin + event.position;
                    if (at <= part.start)
                    {
                        value = event.value;
                        continue;
                    }
                    if (at >= part.end)
                        break;
                    lane.push_back({at, 1, event.value});
                }
                lane.push_back({part.start, 1, value});
                lane.push_back({part.end, 0, type.restValue()});
            }
        }
    }

    // As the engine plays them: a key struck again ends the note sounding on it, so the
    // file never holds two notes on one key at once (which MIDI 1.0 could not tell
    // apart).
    std::stable_sort(played.begin(), played.end(),
                     [](const PlayedNote& a, const PlayedNote& b) { return a.start < b.start; });
    for (std::size_t i = 0; i < played.size(); ++i)
        for (std::size_t j = i + 1; j < played.size(); ++j)
            if (played[j].key == played[i].key && played[j].start < played[i].end)
            {
                played[i].end = played[j].start;
                break;
            }
    for (const auto& note : played)
    {
        const auto on = toPulse(note.start);
        const auto off = toPulse(note.end);
        if (off <= on)
            continue; // struck again at once: it never sounds
        events.push_back({on, 2,
                          juce::MidiMessage::noteOn(channel, note.key,
                                                    static_cast<juce::uint8>(note.velocity))});
        events.push_back({off, 0,
                          juce::MidiMessage::noteOff(channel, note.key, static_cast<juce::uint8>(64))});
    }

    for (auto& [type, lane] : steps)
    {
        std::stable_sort(lane.begin(), lane.end(), [](const Step& a, const Step& b) {
            return a.position != b.position ? a.position < b.position : a.order < b.order;
        });
        int current = type.restValue();
        for (std::size_t i = 0; i < lane.size(); ++i)
        {
            if (i + 1 < lane.size() && lane[i + 1].position == lane[i].position)
                continue;
            if (lane[i].value == current)
                continue;
            current = lane[i].value;
            events.push_back({toPulse(lane[i].position), 1, controllerMessage(channel, type, current)});
        }
    }

    std::stable_sort(events.begin(), events.end(), [](const Outgoing& a, const Outgoing& b) {
        return a.pulse != b.pulse ? a.pulse < b.pulse : a.order < b.order;
    });
    return events;
}

void add(juce::MidiMessageSequence& sequence, const Outgoing& event)
{
    auto message = event.message;
    message.setTimeStamp(static_cast<double>(event.pulse));
    sequence.addEvent(message);
}

juce::MidiMessageSequence conductor(const model::Project& project)
{
    const auto tempoMap = project.getTempoMap();
    juce::MidiMessageSequence sequence;
    auto tempo = juce::MidiMessage::tempoMetaEvent(
        static_cast<int>(std::lround(60'000'000.0 / tempoMap.getTempo())));
    tempo.setTimeStamp(0.0);
    sequence.addEvent(tempo);
    const auto signature = tempoMap.getTimeSignature();
    auto metre = juce::MidiMessage::timeSignatureMetaEvent(signature.numerator, signature.denominator);
    metre.setTimeStamp(0.0);
    sequence.addEvent(metre);
    return sequence;
}

void endTrack(juce::MidiMessageSequence& sequence)
{
    auto end = juce::MidiMessage::endOfTrack();
    end.setTimeStamp(sequence.getEndTime());
    sequence.addEvent(end);
}

} // namespace

juce::MemoryBlock writeMidiFile(const model::Project& project, int type)
{
    const double ticksPerQuarter = project.getTempoMap().ticksPerQuarterNote();
    juce::MidiFile file;
    file.setTicksPerQuarterNote(midiFileResolution);

    if (type == 0)
    {
        auto sequence = conductor(project);
        std::vector<Outgoing> all;
        for (int t = 0; t < project.getNumTracks(); ++t)
            if (const auto track = project.getTrack(t); track.isMidi())
            {
                const auto events = eventsOf(track, ticksPerQuarter);
                all.insert(all.end(), events.begin(), events.end());
            }
        std::stable_sort(all.begin(), all.end(), [](const Outgoing& a, const Outgoing& b) {
            return a.pulse != b.pulse ? a.pulse < b.pulse : a.order < b.order;
        });
        for (const auto& event : all)
            add(sequence, event);
        endTrack(sequence);
        file.addTrack(sequence);
    }
    else
    {
        auto tempoTrack = conductor(project);
        endTrack(tempoTrack);
        file.addTrack(tempoTrack);

        for (int t = 0; t < project.getNumTracks(); ++t)
        {
            const auto track = project.getTrack(t);
            if (!track.isMidi())
                continue;
            juce::MidiMessageSequence sequence;
            auto name = juce::MidiMessage::textMetaEvent(3, track.getName());
            name.setTimeStamp(0.0);
            sequence.addEvent(name);
            for (const auto& event : eventsOf(track, ticksPerQuarter))
                add(sequence, event);
            endTrack(sequence);
            file.addTrack(sequence);
        }
    }

    juce::MemoryOutputStream stream;
    file.writeTo(stream, type == 0 ? 0 : 1);
    return stream.getMemoryBlock();
}

juce::Result readMidiFile(const juce::File& file, const model::TempoMap& tempoMap,
                          MidiFileContent& content)
{
    juce::MemoryBlock bytes;
    if (!file.loadFileAsData(bytes))
        return juce::Result::fail("\"" + file.getFileName() + "\" could not be read.");
    return readMidiFile(bytes, tempoMap, content);
}

juce::Result readMidiFile(const juce::MemoryBlock& bytes, const model::TempoMap& tempoMap,
                          MidiFileContent& content)
{
    juce::MemoryInputStream stream(bytes, false);
    juce::MidiFile midi;
    int fileType = 1;
    if (!midi.readFrom(stream, true, &fileType))
        return juce::Result::fail("It is not a standard MIDI file.");

    content = MidiFileContent{};
    content.type = fileType;

    // Positions: beats at the project's tempo (pulses), or real time (SMPTE files).
    const short timeFormat = midi.getTimeFormat();
    const bool beats = timeFormat > 0;
    if (!beats)
        midi.convertTimestampTicksToSeconds();
    const double ticksPerQuarter = tempoMap.ticksPerQuarterNote();
    const auto toTicks = [&](double timestamp) {
        return beats ? static_cast<model::Ticks>(std::llround(timestamp * ticksPerQuarter / timeFormat))
                     : model::secondsToTicks(timestamp);
    };

    int tempoChanges = 0;
    int metreChanges = 0;
    std::map<juce::String, int> leftOut;
    double firstTempoAt = 1e300;
    double firstMetreAt = 1e300;

    for (int t = 0; t < midi.getNumTracks(); ++t)
    {
        const auto* sequence = midi.getTrack(t);
        juce::String trackName;
        std::map<int, MidiFileContent::Part> byChannel;

        for (int i = 0; i < sequence->getNumEvents(); ++i)
        {
            const auto* holder = sequence->getEventPointer(i);
            const auto& message = holder->message;
            const double time = message.getTimeStamp();

            if (message.isMetaEvent())
            {
                if (message.isTrackNameEvent() && trackName.isEmpty())
                    trackName = message.getTextFromTextMetaEvent();
                else if (message.isTempoMetaEvent())
                {
                    const double tempo = 60.0 / message.getTempoSecondsPerQuarterNote();
                    if (time < firstTempoAt)
                    {
                        if (content.hasTempo && !juce::approximatelyEqual(tempo, content.tempo))
                            ++tempoChanges;
                        firstTempoAt = time;
                        content.tempo = tempo;
                        content.hasTempo = true;
                    }
                    else if (!juce::approximatelyEqual(tempo, content.tempo))
                        ++tempoChanges;
                }
                else if (message.isTimeSignatureMetaEvent())
                {
                    int numerator = 4;
                    int denominator = 4;
                    message.getTimeSignatureInfo(numerator, denominator);
                    const model::TimeSignature signature{numerator, denominator};
                    if (time < firstMetreAt)
                    {
                        firstMetreAt = time;
                        content.timeSignature = signature;
                        content.hasTimeSignature = signature.isValid();
                    }
                    else if (signature != content.timeSignature)
                        ++metreChanges;
                }
                else if (!message.isEndOfTrackMetaEvent() && !message.isKeySignatureMetaEvent() &&
                         !message.isTextMetaEvent())
                    ++leftOut["other meta events"];
                continue;
            }

            if (message.isSysEx())
            {
                ++leftOut["system exclusive messages"];
                continue;
            }

            const int channel = message.getChannel();
            if (channel < 1 || channel > 16)
                continue;
            auto& part = byChannel[channel];
            part.channel = channel;

            if (message.isNoteOn())
            {
                const auto start = toTicks(time);
                const double offTime = holder->noteOffObject != nullptr
                                           ? holder->noteOffObject->message.getTimeStamp()
                                           : sequence->getEndTime();
                const auto end = std::max(start + 1, toTicks(offTime));
                part.notes.push_back({0, start, end - start, message.getNoteNumber(),
                                      std::clamp(static_cast<int>(message.getVelocity()),
                                                 model::Note::minimumVelocity,
                                                 model::Note::maximumVelocity)});
                part.end = std::max(part.end, end);
            }
            else if (message.isController())
            {
                if (message.getControllerNumber() >= 120)
                    continue; // channel mode messages: not part of the music
                part.controllers.push_back({model::ControllerType::controlChange(message.getControllerNumber()),
                                            toTicks(time), message.getControllerValue()});
                part.end = std::max(part.end, toTicks(time) + 1);
            }
            else if (message.isPitchWheel())
            {
                part.controllers.push_back(
                    {model::ControllerType::pitchBend(), toTicks(time), message.getPitchWheelValue()});
                part.end = std::max(part.end, toTicks(time) + 1);
            }
            else if (message.isChannelPressure())
            {
                part.controllers.push_back({model::ControllerType::channelPressure(), toTicks(time),
                                            message.getChannelPressureValue()});
                part.end = std::max(part.end, toTicks(time) + 1);
            }
            else if (message.isProgramChange())
                ++leftOut["program changes"];
            else if (message.isAftertouch())
                ++leftOut["polyphonic aftertouch messages"];
        }

        const bool several = byChannel.size() > 1;
        for (auto& [channel, part] : byChannel)
        {
            if (part.notes.empty() && part.controllers.empty())
                continue;
            auto name = trackName.isNotEmpty() ? trackName : "Channel " + juce::String(channel);
            if (several && trackName.isNotEmpty())
                name << " (Ch " << channel << ")";
            part.name = name;
            model::normalise(part.notes);
            model::normalise(part.controllers);
            content.parts.push_back(std::move(part));
        }
    }

    if (tempoChanges > 0)
        content.leftOut.add(juce::String(tempoChanges) + " tempo change" + (tempoChanges > 1 ? "s" : "") +
                            " (the project has one tempo)");
    if (metreChanges > 0)
        content.leftOut.add(juce::String(metreChanges) + " time signature change" +
                            (metreChanges > 1 ? "s" : ""));
    for (const auto& [what, count] : leftOut)
        content.leftOut.add(juce::String(count) + " " + what);
    return juce::Result::ok();
}

} // namespace daw::controller
