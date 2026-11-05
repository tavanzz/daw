// A test plugin in the LV2 format, written against the LV2 headers only, as a
// third-party plugin would be. What it does is TestPluginCore.h's;
// DAW_TEST_PLUGIN_KIND picks the kind and DAW_TEST_PLUGIN_URI its URI. Its ports are
// described in plugin.ttl (made from effect.ttl.in or instrument.ttl.in).

#include "../core/TestPluginCore.h"

#include <lv2/atom/atom.h>
#include <lv2/atom/util.h>
#include <lv2/core/lv2.h>
#include <lv2/midi/midi.h>
#include <lv2/urid/urid.h>

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <thread>

namespace core = daw::testplugins;

namespace
{

constexpr core::Kind kind = core::Kind::DAW_TEST_PLUGIN_KIND;

// Effects: 0, 1 audio in; 2, 3 audio out; 4 the parameter; 5 the latency (output).
// The instrument: 0 MIDI in (atom sequence); 1, 2 audio out; 3 the parameter.
struct Plugin
{
    core::Processor processor{kind};
    const float* input[2] = {nullptr, nullptr};
    float* output[2] = {nullptr, nullptr};
    const float* parameter = nullptr;
    float* latency = nullptr;
    const LV2_Atom_Sequence* midi = nullptr;
    LV2_URID midiEvent = 0;
};

[[maybe_unused]] LV2_Handle instantiatePlugin(double rate, const LV2_Feature* const* features);

LV2_Handle instantiate(const LV2_Descriptor*, double rate, const char*, const LV2_Feature* const* features)
{
    // The faulty kinds misbehave when instantiated (LV2 hosts scan the Turtle files
    // without loading the library; the scanner instantiates every plugin).
    if constexpr (kind == core::Kind::crash)
    {
        std::abort();
    }
    else if constexpr (kind == core::Kind::hang)
    {
        for (;;)
            std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    else
    {
        return instantiatePlugin(rate, features);
    }
}

[[maybe_unused]] LV2_Handle instantiatePlugin(double rate, const LV2_Feature* const* features)
{
    auto* plugin = new Plugin();
    plugin->processor.prepare(rate, 8192);
    for (int i = 0; features != nullptr && features[i] != nullptr; ++i)
        if (std::strcmp(features[i]->URI, LV2_URID__map) == 0)
        {
            auto* map = static_cast<LV2_URID_Map*>(features[i]->data);
            plugin->midiEvent = map->map(map->handle, LV2_MIDI__MidiEvent);
        }
    if (core::isInstrument(kind) && plugin->midiEvent == 0)
    {
        delete plugin;
        return nullptr; // urid:map is a required feature
    }
    return plugin;
}

void connectPort(LV2_Handle handle, uint32_t port, void* data)
{
    auto& plugin = *static_cast<Plugin*>(handle);
    if constexpr (core::isInstrument(kind))
    {
        switch (port)
        {
        case 0: plugin.midi = static_cast<const LV2_Atom_Sequence*>(data); break;
        case 1: plugin.output[0] = static_cast<float*>(data); break;
        case 2: plugin.output[1] = static_cast<float*>(data); break;
        case 3: plugin.parameter = static_cast<const float*>(data); break;
        default: break;
        }
    }
    else
    {
        switch (port)
        {
        case 0: plugin.input[0] = static_cast<const float*>(data); break;
        case 1: plugin.input[1] = static_cast<const float*>(data); break;
        case 2: plugin.output[0] = static_cast<float*>(data); break;
        case 3: plugin.output[1] = static_cast<float*>(data); break;
        case 4: plugin.parameter = static_cast<const float*>(data); break;
        case 5: plugin.latency = static_cast<float*>(data); break;
        default: break;
        }
    }
}

void activate(LV2_Handle handle)
{
    static_cast<Plugin*>(handle)->processor.reset();
}

void run(LV2_Handle handle, uint32_t samples)
{
    auto& plugin = *static_cast<Plugin*>(handle);
    const int length = static_cast<int>(samples);
    if (plugin.parameter != nullptr)
        plugin.processor.setParameter(static_cast<double>(*plugin.parameter));
    if (plugin.latency != nullptr)
        *plugin.latency = static_cast<float>(plugin.processor.getLatency());

    if constexpr (!core::isInstrument(kind))
    {
        for (int channel = 0; channel < 2; ++channel)
            if (plugin.output[channel] != plugin.input[channel])
                std::memmove(plugin.output[channel], plugin.input[channel], sizeof(float) * samples);
        plugin.processor.process(plugin.output[0], plugin.output[1], 0, length);
        return;
    }

    int done = 0;
    if (plugin.midi != nullptr)
    {
        for (auto* event = lv2_atom_sequence_begin(&plugin.midi->body);
             !lv2_atom_sequence_is_end(&plugin.midi->body, plugin.midi->atom.size, event);
             event = lv2_atom_sequence_next(event))
        {
            if (event->body.type != plugin.midiEvent || event->body.size < 3)
                continue;
            const int at = std::min(length, std::max(done, static_cast<int>(event->time.frames)));
            plugin.processor.process(plugin.output[0], plugin.output[1], done, at);
            done = at;
            const auto* bytes = reinterpret_cast<const uint8_t*>(event + 1);
            const int channel = bytes[0] & 0x0f;
            switch (lv2_midi_message_type(bytes))
            {
            case LV2_MIDI_MSG_NOTE_ON:
                plugin.processor.noteOn(channel, bytes[1], static_cast<float>(bytes[2]) / 127.0f);
                break;
            case LV2_MIDI_MSG_NOTE_OFF:
                plugin.processor.noteOff(channel, bytes[1]);
                break;
            default:
                break;
            }
        }
    }
    plugin.processor.process(plugin.output[0], plugin.output[1], done, length);
}

void deactivate(LV2_Handle) {}

void cleanup(LV2_Handle handle)
{
    delete static_cast<Plugin*>(handle);
}

const void* extensionData(const char*)
{
    return nullptr;
}

const LV2_Descriptor descriptor = {DAW_TEST_PLUGIN_URI, instantiate, connectPort, activate,
                                   run, deactivate, cleanup, extensionData};

} // namespace

LV2_SYMBOL_EXPORT const LV2_Descriptor* lv2_descriptor(uint32_t index)
{
    return index == 0 ? &descriptor : nullptr;
}
