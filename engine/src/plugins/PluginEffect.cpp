#include "daw/engine/plugins/PluginEffect.h"

#include "daw/engine/midi/EventBuffer.h"
#include "daw/engine/rt/ForeignCode.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace daw::engine::plugins
{

namespace
{

// Room in the MIDI buffer for the most events one call gets, each as JUCE stores it
// (a sample position, a size and up to three bytes, rounded up).
constexpr int midiBytesPerEvent = 16;

juce::String parameterIdOf(juce::AudioProcessorParameter& parameter)
{
    if (auto* hosted = dynamic_cast<juce::HostedAudioProcessorParameter*>(&parameter))
        if (const auto id = hosted->getParameterID(); id.isNotEmpty())
            return id;
    return juce::String(parameter.getParameterIndex());
}

float clampNormalised(float value) noexcept
{
    return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 0.0f;
}

} // namespace

std::variant<std::unique_ptr<PluginEffect>, juce::String>
PluginEffect::create(std::unique_ptr<juce::AudioPluginInstance> instance)
{
    if (instance == nullptr)
        return juce::String("no plugin");
    const auto chosen = setUpStereoLayout(*instance);
    if (const auto* reason = std::get_if<juce::String>(&chosen))
        return *reason;
    return std::unique_ptr<PluginEffect>(new PluginEffect(std::move(instance), std::get<PluginLayout>(chosen)));
}

PluginEffect::PluginEffect(std::unique_ptr<juce::AudioPluginInstance> pluginToHost, PluginLayout chosenLayout)
    : plugin(std::move(pluginToHost))
    , pluginDescription(plugin->getPluginDescription())
    , layout(chosenLayout)
    , instrument(pluginDescription.isInstrument || chosenLayout.inputs == 0)
{
    description.type = "plugin";
    description.name = pluginDescription.name;
    description.isInstrument = instrument;

    parameters = plugin->getParameters();
    watches = std::make_unique<ParameterWatch[]>(static_cast<std::size_t>(parameters.size()));
    for (int i = 0; i < parameters.size(); ++i)
    {
        auto& parameter = *parameters[i];
        fx::ParameterInfo info;
        info.id = parameterIdOf(parameter);
        info.name = parameter.getName(64);
        info.unit = parameter.getLabel();
        info.minimum = 0.0f;
        info.maximum = 1.0f;
        info.defaultValue = clampNormalised(parameter.getDefaultValue());
        info.automatable = parameter.isAutomatable();
        info.smoothed = false;
        description.parameters.push_back(info);
        watches[static_cast<std::size_t>(i)].value = parameter.getValue();
    }
    seenChanges.assign(static_cast<std::size_t>(parameters.size()), 0);
    seenGestures.assign(static_cast<std::size_t>(parameters.size()), 0);
    lastSent.assign(static_cast<std::size_t>(parameters.size()), std::numeric_limits<float>::quiet_NaN());
    ticksPerSecond = static_cast<double>(juce::Time::getHighResolutionTicksPerSecond());

    plugin->setPlayHead(&playHead);
    plugin->addListener(this);
}

PluginEffect::~PluginEffect()
{
    plugin->removeListener(this);
    plugin->setPlayHead(nullptr);
    if (prepared)
        plugin->releaseResources();
}

void PluginEffect::prepare(const fx::ProcessSetup& setup)
{
    hostIsLoading = true;
    if (prepared)
        plugin->releaseResources();

    sampleRate = setup.sampleRate;
    maximumBlockSize = std::max(1, setup.maximumBlockSize);
    plugin->setNonRealtime(setup.offline);
    plugin->setRateAndBufferSizeDetails(sampleRate, maximumBlockSize);
    plugin->prepareToPlay(sampleRate, maximumBlockSize);
    prepared = true;
    scratch.setSize(2, maximumBlockSize);

    // Some formats know a plugin's latency only once it has run (LV2's latency port):
    // one short block of silence, here, before any audio thread uses the plugin.
    {
        scratch.clear();
        float* pointers[] = {scratch.getWritePointer(0), scratch.getWritePointer(1)};
        juce::AudioBuffer<float> silence(pointers, std::max({1, layout.inputs, layout.outputs}),
                                         std::min(maximumBlockSize, 64));
        juce::MidiBuffer none;
        plugin->processBlock(silence, none);
        plugin->reset();
    }
    latency = std::max(0, plugin->getLatencySamples());
    latencyChanged = false;
    for (int i = 0; i < parameters.size(); ++i)
        watches[static_cast<std::size_t>(i)].value = parameters[i]->getValue();
    hostIsLoading = false;

    midiBuffer.ensureSize(static_cast<std::size_t>((midi::EventBuffer::defaultCapacity +
                                                    midi::EventBuffer::maximumHeldNotes + 2 * 16 + 16 * 128) *
                                                   midiBytesPerEvent));
    playHead.sampleRate = sampleRate;
    std::fill(lastSent.begin(), lastSent.end(), std::numeric_limits<float>::quiet_NaN());
    sounding.reset();
    panicPending = false;
}

void PluginEffect::reset() noexcept
{
    sounding.reset();
    panicPending = false;
    const rt::ForeignCode foreign;
    plugin->reset();
}

void PluginEffect::panic() noexcept
{
    // Ends what sounds at the start of the next call (all notes off and all sound
    // off, after the voices were reset), so that the plugin gets it in its order.
    panicPending = true;
}

double PluginEffect::getTailSeconds(std::span<const float>) const
{
    return plugin->getTailLengthSeconds();
}

juce::MemoryBlock PluginEffect::getExtraState() const
{
    juce::MemoryBlock state;
    plugin->getStateInformation(state);
    return state;
}

bool PluginEffect::setExtraState(const juce::MemoryBlock& state)
{
    if (state.isEmpty())
        return true;
    hostIsLoading = true;
    plugin->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    for (int i = 0; i < parameters.size(); ++i)
        watches[static_cast<std::size_t>(i)].value = parameters[i]->getValue();
    hostIsLoading = false;
    return true;
}

void PluginEffect::setParameter(int index, float normalised)
{
    if (index < 0 || index >= parameters.size())
        return;
    const float value = clampNormalised(normalised);
    hostIsSetting = index;
    parameters[index]->setValue(value);
    hostIsSetting = -1;
    watches[static_cast<std::size_t>(index)].value = value;
}

float PluginEffect::getParameter(int index) const
{
    return index >= 0 && index < parameters.size() ? parameters[index]->getValue() : 0.0f;
}

juce::String PluginEffect::getParameterText(int index, float normalised) const
{
    if (index < 0 || index >= parameters.size())
        return {};
    auto& parameter = *parameters[index];
    auto text = parameter.getText(clampNormalised(normalised), 64);
    if (const auto unit = parameter.getLabel(); unit.isNotEmpty() && !text.endsWith(unit))
        text << " " << unit;
    return text;
}

void PluginEffect::takeParameterReports(std::vector<ParameterReport>& reports)
{
    reports.clear();
    if (!parametersChanged.exchange(false))
        return;
    for (int i = 0; i < parameters.size(); ++i)
    {
        const auto index = static_cast<std::size_t>(i);
        auto& watch = watches[index];
        const auto changes = watch.changes.load();
        const auto gestures = watch.gestures.load();
        if (changes == seenChanges[index] && gestures == seenGestures[index])
            continue;
        ParameterReport report;
        report.index = i;
        report.value = watch.value.load();
        report.inGesture = (gestures & 1u) != 0;
        // An end since the last report: the count passed an even number.
        report.gestureEnded = !report.inGesture ? gestures != seenGestures[index]
                                                : gestures - seenGestures[index] >= 2;
        seenChanges[index] = changes;
        seenGestures[index] = gestures;
        reports.push_back(report);
    }
}

PluginEffect::Load PluginEffect::takeLoad() noexcept
{
    return {loadAverage.load(std::memory_order_relaxed), loadPeak.exchange(0.0f, std::memory_order_relaxed),
            overruns.exchange(0, std::memory_order_relaxed)};
}

void PluginEffect::audioProcessorParameterChanged(juce::AudioProcessor*, int index, float value)
{
    // Not the plugin's own change: the host setting it, restoring a state or preparing
    // (formats tell their parameters what the ports hold then), or no change at all.
    if (index < 0 || index >= parameters.size() || hostIsSetting.load() == index || hostIsLoading.load())
        return;
    auto& watch = watches[static_cast<std::size_t>(index)];
    if (juce::exactlyEqual(watch.value.load(), value))
        return;
    watch.value.store(value);
    watch.changes.fetch_add(1);
    parametersChanged = true;
}

void PluginEffect::audioProcessorChanged(juce::AudioProcessor*, const ChangeDetails& details)
{
    if (details.latencyChanged)
        latencyChanged = true;
    if (details.nonParameterStateChanged || details.programChanged)
        stateChanged = true;
}

void PluginEffect::audioProcessorParameterChangeGestureBegin(juce::AudioProcessor*, int index)
{
    if (index < 0 || index >= parameters.size())
        return;
    auto& watch = watches[static_cast<std::size_t>(index)];
    if ((watch.gestures.load() & 1u) == 0)
        watch.gestures.fetch_add(1);
    parametersChanged = true;
}

void PluginEffect::audioProcessorParameterChangeGestureEnd(juce::AudioProcessor*, int index)
{
    if (index < 0 || index >= parameters.size())
        return;
    auto& watch = watches[static_cast<std::size_t>(index)];
    if ((watch.gestures.load() & 1u) != 0)
        watch.gestures.fetch_add(1);
    parametersChanged = true;
}

juce::Optional<juce::AudioPlayHead::PositionInfo> PluginEffect::PlayHead::getPosition() const
{
    PositionInfo info;
    const auto seconds = static_cast<double>(time.position) / sampleRate;
    const auto quarters = seconds * time.tempo / 60.0;
    const auto quartersPerBar = 4.0 * time.numerator / std::max(1, time.denominator);
    info.setTimeInSamples(time.position);
    info.setTimeInSeconds(seconds);
    info.setBpm(time.tempo);
    info.setTimeSignature(juce::AudioPlayHead::TimeSignature{time.numerator, time.denominator});
    info.setPpqPosition(quarters);
    info.setPpqPositionOfLastBarStart(std::floor(quarters / quartersPerBar) * quartersPerBar);
    info.setBarCount(static_cast<juce::int64>(std::floor(quarters / quartersPerBar)));
    info.setIsPlaying(time.playing);
    info.setIsRecording(false);
    info.setIsLooping(false);
    return info;
}

void PluginEffect::addMidi(const midi::Event& event, int offset) noexcept
{
    const auto channel = static_cast<juce::uint8>(event.channel & 0x0f);
    const auto key = static_cast<juce::uint8>(event.number & 0x7f);
    const auto bit = static_cast<std::size_t>(channel) * 128u + static_cast<std::size_t>(key);
    juce::uint8 bytes[3] = {};
    int size = 3;
    switch (event.type)
    {
    case midi::EventType::noteOn:
        bytes[0] = static_cast<juce::uint8>(0x90 | channel);
        bytes[1] = key;
        bytes[2] = static_cast<juce::uint8>(std::max(1, midi::toSevenBit(event.value)));
        sounding.set(bit);
        break;
    case midi::EventType::noteOff:
        bytes[0] = static_cast<juce::uint8>(0x80 | channel);
        bytes[1] = key;
        bytes[2] = static_cast<juce::uint8>(midi::toSevenBit(event.value));
        sounding.reset(bit);
        break;
    case midi::EventType::controller:
        bytes[0] = static_cast<juce::uint8>(0xb0 | channel);
        bytes[1] = key;
        bytes[2] = static_cast<juce::uint8>(midi::toSevenBit(event.value));
        break;
    case midi::EventType::pitchBend:
    {
        const int bend = midi::toPitchBend(event.value);
        bytes[0] = static_cast<juce::uint8>(0xe0 | channel);
        bytes[1] = static_cast<juce::uint8>(bend & 0x7f);
        bytes[2] = static_cast<juce::uint8>((bend >> 7) & 0x7f);
        break;
    }
    case midi::EventType::channelPressure:
        bytes[0] = static_cast<juce::uint8>(0xd0 | channel);
        bytes[1] = static_cast<juce::uint8>(midi::toSevenBit(event.value));
        size = 2;
        break;
    }
    midiBuffer.addEvent(bytes, size, offset);
}

void PluginEffect::sendAutomation(const fx::ProcessBlock& block, int sample) noexcept
{
    const int count = std::min(block.parameters.size(), parameters.size());
    for (int i = 0; i < count; ++i)
    {
        const float* values = block.parameters.getChanging(i);
        if (values == nullptr)
            continue;
        const float value = clampNormalised(values[sample]);
        auto& last = lastSent[static_cast<std::size_t>(i)];
        if (juce::exactlyEqual(value, last))
            continue;
        last = value;
        // A parameter the user is dragging in the plugin's editor is the editor's.
        if ((watches[static_cast<std::size_t>(i)].gestures.load(std::memory_order_relaxed) & 1u) != 0)
            continue;
        const rt::ForeignCode foreign;
        parameters[i]->setValue(value);
    }
}

void PluginEffect::process(const fx::ProcessBlock& block) noexcept
{
    const int numSamples = block.numSamples;
    if (numSamples <= 0)
        return;
    const auto started = juce::Time::getHighResolutionTicks();

    // Where to cut the call: the breakpoints, and the grid while automation moves.
    bool automated = false;
    for (int i = 0; i < block.parameters.size() && !automated; ++i)
        automated = block.parameters.getChanging(i) != nullptr;
    int cutCount = 0;
    if (automated)
    {
        const auto addCut = [&](int at) noexcept {
            if (at > 0 && at < numSamples && cutCount < static_cast<int>(cuts.size()) - 1)
                cuts[static_cast<std::size_t>(cutCount++)] = at;
        };
        for (const int at : block.breakpoints)
            addCut(at);
        const auto position = block.time.position;
        const auto phase = static_cast<int>(((position % automationGrid) + automationGrid) % automationGrid);
        for (int at = (automationGrid - phase) % automationGrid; at < numSamples; at += automationGrid)
            addCut(at);
        std::sort(cuts.begin(), cuts.begin() + cutCount);
        cutCount = static_cast<int>(std::unique(cuts.begin(), cuts.begin() + cutCount) - cuts.begin());
    }
    cuts[static_cast<std::size_t>(cutCount++)] = numSamples;

    playHead.time = block.time;
    std::size_t nextEvent = 0;
    int start = 0;
    for (int c = 0; c < cutCount; ++c)
    {
        const int end = cuts[static_cast<std::size_t>(c)];
        if (end <= start)
            continue;
        if (automated)
            sendAutomation(block, start);
        playHead.time.position = block.time.position + (block.time.playing ? start : 0);
        runPlugin(block.channels, block.numChannels, start, end, end == numSamples, block.events, nextEvent);
        start = end;
    }

    // The load: this call's time against its share of real time.
    const auto elapsed = static_cast<double>(juce::Time::getHighResolutionTicks() - started) / ticksPerSecond;
    const auto load = static_cast<float>(elapsed * sampleRate / numSamples);
    const auto weight = static_cast<float>(std::min(1.0, numSamples / sampleRate)); // about a second
    const auto average = loadAverage.load(std::memory_order_relaxed);
    loadAverage.store(average + (load - average) * weight, std::memory_order_relaxed);
    if (load > loadPeak.load(std::memory_order_relaxed))
        loadPeak.store(load, std::memory_order_relaxed);
    if (load > 1.0f)
        overruns.fetch_add(1, std::memory_order_relaxed);
}

void PluginEffect::runPlugin(float* const* channels, int numChannels, int start, int end, bool lastPiece,
                             midi::EventSpan events, std::size_t& nextEvent) noexcept
{
    const int length = end - start;
    midiBuffer.clear();

    if (panicPending)
    {
        panicPending = false;
        {
            const rt::ForeignCode foreign;
            plugin->reset();
        }
        for (int channel = 0; channel < 16; ++channel)
            for (int key = 0; key < 128; ++key)
                if (sounding.test(static_cast<std::size_t>(channel * 128 + key)))
                    addMidi(midi::noteOff(0, key, channel), 0);
        for (int channel = 0; channel < 16; ++channel)
        {
            addMidi(midi::controller(0, midi::controllers::allNotesOff, 0.0f, channel), 0);
            addMidi(midi::controller(0, midi::controllers::allSoundOff, 0.0f, channel), 0);
        }
    }

    // The events of [start, end); the last piece takes any left over.
    for (; nextEvent < events.size(); ++nextEvent)
    {
        const auto& event = events[nextEvent];
        if (!lastPiece && event.offset >= end)
            break;
        addMidi(event, std::clamp(event.offset - start, 0, length - 1));
    }

    float* left = channels[0] + start;
    float* right = numChannels > 1 ? channels[1] + start : left;

    // Straight on the track's channels: an effect in stereo.
    if (!instrument && layout.inputs == 2 && layout.outputs == 2)
    {
        float* direct[] = {left, right};
        juce::AudioBuffer<float> buffer(direct, 2, length);
        const rt::ForeignCode foreign;
        plugin->processBlock(buffer, midiBuffer);
        return;
    }

    // Otherwise through the scratch: (L + R) / 2 into a mono input, silence into an
    // instrument; a mono output to both sides; an instrument adds its sound.
    const int pluginChannels = std::max({1, layout.inputs, layout.outputs});
    float* const first = scratch.getWritePointer(0);
    float* const second = scratch.getWritePointer(1);
    if (instrument)
    {
        juce::FloatVectorOperations::clear(first, length);
        juce::FloatVectorOperations::clear(second, length);
    }
    else
    {
        for (int i = 0; i < length; ++i)
            first[i] = (left[i] + right[i]) * 0.5f;
        juce::FloatVectorOperations::clear(second, length);
    }

    float* pointers[] = {first, second};
    juce::AudioBuffer<float> buffer(pointers, pluginChannels, length);
    {
        const rt::ForeignCode foreign;
        plugin->processBlock(buffer, midiBuffer);
    }

    const float* outLeft = first;
    const float* outRight = layout.outputs >= 2 ? second : first;
    if (instrument)
    {
        juce::FloatVectorOperations::add(left, outLeft, length);
        if (right != left)
            juce::FloatVectorOperations::add(right, outRight, length);
    }
    else
    {
        juce::FloatVectorOperations::copy(left, outLeft, length);
        if (right != left)
            juce::FloatVectorOperations::copy(right, outRight, length);
    }
}

} // namespace daw::engine::plugins
