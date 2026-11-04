#include "daw/engine/fx/SampleInstrument.h"

#include "daw/engine/dsp/PanLaw.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace daw::engine::fx
{

void SampleInstrument::prepare(const ProcessSetup& setup)
{
    sampleRate = setup.sampleRate;
    interpolator = &dsp::SincInterpolator::get();
    reset();
}

void SampleInstrument::reset() noexcept
{
    for (auto& voice : voices)
    {
        voice.active = false;
        voice.amp.reset();
    }
    clock = 0;
    lastTick = std::numeric_limits<std::uint64_t>::max();
    notesStarted = 0;
    bend = 0.0;
    sustainDown = false;
    volume = 1.0;
    cc7 = 1.0f;
    cc11 = 1.0f;
}

int SampleInstrument::getActiveVoices() const noexcept
{
    return static_cast<int>(std::count_if(voices.begin(), voices.end(),
                                          [](const Voice& voice) { return voice.active; }));
}

void SampleInstrument::process(const ProcessBlock& block) noexcept
{
    blockSamples = block.samples;

    // A sample that is not the one a voice plays any more may be gone: its voices stop.
    for (auto& voice : voices)
    {
        if (!voice.active)
            continue;
        const auto slot = static_cast<std::size_t>(voice.zone.slot);
        const Sample* now = slot < blockSamples.size() ? blockSamples[slot] : nullptr;
        if (now != voice.sample)
            voice.active = false;
    }

    // At each sample: the control tick (if one falls there) first, so that events
    // there see the parameters of their own sample; then the events; then the sound
    // up to the next event or tick.
    std::size_t next = 0;
    int sample = 0;
    while (sample < block.numSamples)
    {
        if (clock % controlInterval == 0 && clock != lastTick)
        {
            tick(block, sample);
            lastTick = clock;
        }

        for (; next < block.events.size() && block.events[next].offset <= sample; ++next)
            handle(block.events[next]);

        const int until = next < block.events.size()
                              ? std::min(block.numSamples, block.events[next].offset)
                              : block.numSamples;
        const auto intoInterval = static_cast<int>(clock % controlInterval);
        const int length = std::min(until - sample, controlInterval - intoInterval);
        if (length <= 0)
            continue;
        render(block.channels, block.numChannels, sample, length);
        sample += length;
        clock += static_cast<std::uint64_t>(length);
    }

    for (; next < block.events.size(); ++next)
        handle(block.events[next]);
}

void SampleInstrument::tick(const ProcessBlock& block, int sample) noexcept
{
    readParameters(block, sample);
    volume += (std::pow(static_cast<double>(cc7) * static_cast<double>(cc11), 2.0) - volume) * 0.25;

    // Voices follow the zone as it is now, except where they started.
    for (auto& voice : voices)
    {
        Zone zone;
        if (!voice.active || !zoneFor(voice.key, zone) || zone.slot != voice.zone.slot)
            continue;
        zone.start = voice.zone.start;
        zone.oneShot = voice.zone.oneShot;
        voice.zone = zone;
        voice.amp.setTimes(zone.attack, zone.decay, zone.sustain, zone.release, sampleRate);
    }
}

void SampleInstrument::render(float* const* channels, int numChannels, int start, int length) noexcept
{
    bool any = false;
    for (auto& voice : voices)
    {
        if (!voice.active)
            continue;
        if (!any)
        {
            std::fill(leftScratch.begin(), leftScratch.end(), 0.0f);
            std::fill(rightScratch.begin(), rightScratch.end(), 0.0f);
        }
        any = true;
        renderVoice(voice, leftScratch.data(), rightScratch.data(), length);
    }
    if (!any)
        return;

    const auto gain = static_cast<float>(levelNow() * volume);
    for (int i = 0; i < length; ++i)
    {
        if (numChannels > 0)
            channels[0][start + i] += leftScratch[static_cast<std::size_t>(i)] * gain;
        if (numChannels > 1)
            channels[1][start + i] += rightScratch[static_cast<std::size_t>(i)] * gain;
    }
}

void SampleInstrument::renderVoice(Voice& voice, float* left, float* right, int length) noexcept
{
    const Sample& sample = *voice.sample;
    const auto& zone = voice.zone;
    const auto length0 = static_cast<double>(sample.getLength());

    // Speed in samples of level 0 per output sample, and the level that keeps it at
    // most 1.
    const double semitones = voice.key - zone.rootKey + zone.tune + bend * bendRangeNow();
    const double speed = std::exp2(semitones / 12.0) * sample.sampleRate / sampleRate;
    int level = 0;
    if (speed > 1.0)
        level = std::min(static_cast<int>(sample.levels.size()) - 1,
                         static_cast<int>(std::ceil(std::log2(speed) - 1e-9)));
    const double scale = static_cast<double>(1 << level);
    const auto& data = sample.levels[static_cast<std::size_t>(level)];
    const juce::int64 lengthAtLevel = data.getNumSamples();
    const float* channel0 = data.getReadPointer(0);
    const float* channel1 = data.getReadPointer(std::min(1, data.getNumChannels() - 1));
    const bool stereo = data.getNumChannels() > 1;

    double loopStart = std::clamp(zone.loopStart, 0.0, 1.0) * length0;
    double loopEnd = std::clamp(zone.loopEnd, 0.0, 1.0) * length0;
    const bool loops = zone.loop != Loop::off && loopEnd - loopStart >= 2.0;
    const double loopLength = loopEnd - loopStart;
    const double crossfade =
        zone.loop == Loop::forward
            ? std::min({zone.crossfadeSeconds * sample.sampleRate, loopStart, loopLength / 2.0})
            : 0.0;

    const auto pan = dsp::panGains(std::clamp(zone.pan, -1.0f, 1.0f));
    const auto leftGain = pan.left * zone.gain * voice.velocityGain;
    const auto rightGain = pan.right * zone.gain * voice.velocityGain;

    const auto read = [&](const float* channel, double position) {
        return interpolator->read(channel, lengthAtLevel, position / scale);
    };

    for (int i = 0; i < length; ++i)
    {
        auto& position = voice.position;
        if (loops && zone.loop == Loop::forward && position >= loopEnd)
            position -= loopLength * std::floor((position - loopStart) / loopLength);
        else if (loops && zone.loop == Loop::pingPong)
        {
            if (voice.direction > 0.0 && position >= loopEnd)
            {
                position = 2.0 * loopEnd - position;
                voice.direction = -1.0;
            }
            else if (voice.direction < 0.0 && position <= loopStart)
            {
                position = 2.0 * loopStart - position;
                voice.direction = 1.0;
            }
        }
        if (!loops && position >= length0)
        {
            voice.active = false;
            return;
        }

        float valueLeft = read(channel0, position);
        float valueRight = stereo ? read(channel1, position) : valueLeft;
        if (crossfade > 0.0 && position > loopEnd - crossfade)
        {
            // Into what comes before the loop's start, which goes on from where the
            // loop starts again: the seam is continuous.
            const auto t = static_cast<float>((position - (loopEnd - crossfade)) / crossfade);
            const double earlier = position - loopLength;
            valueLeft += (read(channel0, earlier) - valueLeft) * t;
            valueRight += ((stereo ? read(channel1, earlier) : read(channel0, earlier)) - valueRight) * t;
        }

        const auto amp = static_cast<float>(voice.amp.next()) * voice.fade;
        left[i] += valueLeft * leftGain * amp;
        right[i] += valueRight * rightGain * amp;

        position += speed * voice.direction;

        if (voice.fadeStep > 0.0f)
        {
            voice.fade -= voice.fadeStep;
            if (voice.fade <= 0.0f)
            {
                voice.active = false;
                return;
            }
        }
    }

    if (!voice.amp.isActive())
        voice.active = false;
}

void SampleInstrument::handle(const midi::Event& event) noexcept
{
    switch (event.type)
    {
    case midi::EventType::noteOn:
        noteOn(event);
        break;
    case midi::EventType::noteOff:
        noteOff(event);
        break;
    case midi::EventType::pitchBend:
        bend = std::clamp(static_cast<double>(event.value), -1.0, 1.0);
        break;
    case midi::EventType::channelPressure:
        break;
    case midi::EventType::controller:
        if (event.number == midi::controllers::volume)
            cc7 = event.value;
        else if (event.number == midi::controllers::expression)
            cc11 = event.value;
        else if (event.number == midi::controllers::sustain)
        {
            sustainDown = event.value >= 0.5f;
            if (!sustainDown)
                for (auto& voice : voices)
                    if (voice.active && voice.sustained)
                    {
                        voice.sustained = false;
                        voice.amp.noteOff();
                    }
        }
        break;
    }
}

void SampleInstrument::fadeOut(Voice& voice, double seconds) noexcept
{
    voice.held = false;
    voice.sustained = false;
    voice.noteId = -1;
    voice.fadeStep = static_cast<float>(1.0 / std::max(1.0, seconds * sampleRate));
}

int SampleInstrument::takeVoice() noexcept
{
    const int count = std::clamp(polyphonyNow(), 1, maximumVoices);
    for (int i = 0; i < count; ++i)
        if (!voices[static_cast<std::size_t>(i)].active)
            return i;

    int quietest = -1;
    int oldest = 0;
    for (int i = 0; i < count; ++i)
    {
        const auto& voice = voices[static_cast<std::size_t>(i)];
        if (!voice.held && !voice.sustained &&
            (quietest < 0 ||
             voice.amp.getLevel() * voice.fade <
                 voices[static_cast<std::size_t>(quietest)].amp.getLevel() *
                     voices[static_cast<std::size_t>(quietest)].fade))
            quietest = i;
        if (voice.order < voices[static_cast<std::size_t>(oldest)].order)
            oldest = i;
    }
    const int stolen = quietest >= 0 ? quietest : oldest;

    // The stolen sound fades out on a spare voice (the one nearest its end if all
    // are busy).
    int spare = maximumVoices;
    for (int i = maximumVoices; i < maximumVoices + spareVoices; ++i)
    {
        const auto& candidate = voices[static_cast<std::size_t>(i)];
        if (!candidate.active)
        {
            spare = i;
            break;
        }
        if (candidate.fade < voices[static_cast<std::size_t>(spare)].fade)
            spare = i;
    }
    auto& fading = voices[static_cast<std::size_t>(spare)];
    fading = voices[static_cast<std::size_t>(stolen)];
    fadeOut(fading, stealFadeSeconds);
    voices[static_cast<std::size_t>(stolen)].active = false;
    return stolen;
}

void SampleInstrument::noteOn(const midi::Event& event) noexcept
{
    Zone zone;
    if (!zoneFor(event.number, zone))
        return;
    const auto slot = static_cast<std::size_t>(zone.slot);
    const Sample* sample = slot < blockSamples.size() ? blockSamples[slot] : nullptr;
    if (sample == nullptr || sample->getLength() == 0)
        return;

    // The same key, or another of its choke group, fades out.
    for (int i = 0; i < maximumVoices; ++i)
    {
        auto& voice = voices[static_cast<std::size_t>(i)];
        if (!voice.active || voice.fadeStep > 0.0f)
            continue;
        if (voice.key == event.number && voice.channel == event.channel)
            fadeOut(voice, stealFadeSeconds);
        else if (zone.chokeGroup > 0 && voice.zone.chokeGroup == zone.chokeGroup)
            fadeOut(voice, chokeFadeSeconds);
    }

    auto& voice = voices[static_cast<std::size_t>(takeVoice())];
    voice = Voice{};
    voice.active = true;
    voice.held = true;
    voice.key = event.number;
    voice.channel = event.channel;
    voice.noteId = event.noteId;
    voice.order = ++notesStarted;
    voice.velocityGain =
        1.0f - zone.velocitySensitivity * (1.0f - std::clamp(event.value, 0.0f, 1.0f));
    voice.zone = zone;
    voice.sample = sample;
    voice.position = std::clamp(zone.start, 0.0, 1.0) * static_cast<double>(sample->getLength());
    voice.amp.setTimes(zone.attack, zone.decay, zone.sustain, zone.release, sampleRate);
    voice.amp.noteOn();
}

void SampleInstrument::noteOff(const midi::Event& event) noexcept
{
    for (int i = 0; i < maximumVoices; ++i)
    {
        auto& voice = voices[static_cast<std::size_t>(i)];
        const bool matches =
            voice.active && voice.held && voice.fadeStep <= 0.0f &&
            (event.noteId >= 0 && voice.noteId >= 0 ? voice.noteId == event.noteId
                                                    : voice.key == event.number &&
                                                          voice.channel == event.channel);
        if (!matches)
            continue;

        voice.held = false;
        if (voice.zone.oneShot)
            return; // plays on to its end
        if (sustainDown)
            voice.sustained = true;
        else
            voice.amp.noteOff();
        return;
    }
}

} // namespace daw::engine::fx
