// A test plugin in the VST3 format, written against the VST 3 SDK's interfaces only
// (pluginterfaces/), as a third-party plugin would be: one class that is the
// component, the audio processor and the edit controller. What it does is
// TestPluginCore.h's; DAW_TEST_PLUGIN_KIND picks the kind, DAW_TEST_PLUGIN_NAME its
// name and DAW_TEST_PLUGIN_UID{1..4} its class id.

#include "../core/TestPluginCore.h"

#include <pluginterfaces/base/ibstream.h>
#include <pluginterfaces/base/ipluginbase.h>
#include <pluginterfaces/base/ustring.h>
#include <pluginterfaces/vst/ivstaudioprocessor.h>
#include <pluginterfaces/vst/ivstcomponent.h>
#include <pluginterfaces/vst/ivsteditcontroller.h>
#include <pluginterfaces/vst/ivstevents.h>
#include <pluginterfaces/vst/ivstparameterchanges.h>
#include <pluginterfaces/vst/vsttypes.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <thread>

#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#endif

// The ids of the VST interfaces used here (the core ones come from coreiids.cpp).
namespace Steinberg::Vst
{
DEF_CLASS_IID(IComponent)
DEF_CLASS_IID(IAudioProcessor)
DEF_CLASS_IID(IEditController)
} // namespace Steinberg::Vst

using namespace Steinberg;
using namespace Steinberg::Vst;
namespace core = daw::testplugins;

namespace
{

constexpr core::Kind kind = core::Kind::DAW_TEST_PLUGIN_KIND;
constexpr ParamID parameterId = 1;

const FUID classId(DAW_TEST_PLUGIN_UID1, DAW_TEST_PLUGIN_UID2, DAW_TEST_PLUGIN_UID3,
                   DAW_TEST_PLUGIN_UID4);

void copyName(String128 target, const char* source)
{
    int i = 0;
    for (; source[i] != 0 && i < 127; ++i)
        target[i] = static_cast<char16>(source[i]);
    target[i] = 0;
}

bool sameId(const TUID a, const TUID b)
{
    return std::memcmp(a, b, sizeof(TUID)) == 0;
}

class TestPlugin final : public IComponent, public IAudioProcessor, public IEditController
{
public:
    TestPlugin()
        : processor(kind)
    {
    }

    // FUnknown
    tresult PLUGIN_API queryInterface(const TUID interfaceId, void** object) override
    {
        if (sameId(interfaceId, FUnknown::iid) || sameId(interfaceId, IPluginBase::iid) || sameId(interfaceId, IComponent::iid))
            *object = static_cast<IComponent*>(this);
        else if (sameId(interfaceId, IAudioProcessor::iid))
            *object = static_cast<IAudioProcessor*>(this);
        else if (sameId(interfaceId, IEditController::iid))
            *object = static_cast<IEditController*>(this);
        else
        {
            *object = nullptr;
            return kNoInterface;
        }
        addRef();
        return kResultOk;
    }
    uint32 PLUGIN_API addRef() override { return ++references; }
    uint32 PLUGIN_API release() override
    {
        const auto left = --references;
        if (left == 0)
            delete this;
        return left;
    }

    // IPluginBase
    tresult PLUGIN_API initialize(FUnknown*) override { return kResultOk; }
    tresult PLUGIN_API terminate() override { return kResultOk; }

    // IComponent
    tresult PLUGIN_API getControllerClassId(TUID) override { return kNotImplemented; }
    tresult PLUGIN_API setIoMode(IoMode) override { return kResultOk; }
    int32 PLUGIN_API getBusCount(MediaType type, BusDirection direction) override
    {
        if (type == kAudio)
            return direction == kOutput || !core::isInstrument(kind) ? 1 : 0;
        return direction == kInput && core::isInstrument(kind) ? 1 : 0;
    }
    tresult PLUGIN_API getBusInfo(MediaType type, BusDirection direction, int32 index, BusInfo& bus) override
    {
        if (index != 0 || getBusCount(type, direction) == 0)
            return kInvalidArgument;
        bus.mediaType = type;
        bus.direction = direction;
        bus.channelCount = type == kAudio ? 2 : 16;
        copyName(bus.name, type == kAudio ? (direction == kInput ? "Input" : "Output") : "MIDI");
        bus.busType = kMain;
        bus.flags = BusInfo::kDefaultActive;
        return kResultOk;
    }
    tresult PLUGIN_API getRoutingInfo(RoutingInfo&, RoutingInfo&) override { return kNotImplemented; }
    tresult PLUGIN_API activateBus(MediaType, BusDirection, int32, TBool) override { return kResultOk; }
    tresult PLUGIN_API setActive(TBool state) override
    {
        if (state)
            processor.reset();
        return kResultOk;
    }
    tresult PLUGIN_API setState(IBStream* state) override
    {
        unsigned char bytes[8] = {};
        int32 read = 0;
        if (state == nullptr || state->read(bytes, 8, &read) != kResultOk || read != 8)
            return kResultFalse;
        processor.setParameter(core::readState(bytes));
        controllerValue = core::readState(bytes);
        return kResultOk;
    }
    tresult PLUGIN_API getState(IBStream* state) override
    {
        // The controller's value: the host's last change, even if no audio has been
        // processed since (the processor hears of it in process()).
        unsigned char bytes[8] = {};
        core::writeState(controllerValue.load(), bytes);
        int32 written = 0;
        return state != nullptr && state->write(bytes, 8, &written) == kResultOk && written == 8
                   ? kResultOk
                   : kResultFalse;
    }

    // IAudioProcessor
    tresult PLUGIN_API setBusArrangements(SpeakerArrangement* inputs, int32 numInputs,
                                          SpeakerArrangement* outputs, int32 numOutputs) override
    {
        const int32 expectedInputs = core::isInstrument(kind) ? 0 : 1;
        if (numInputs != expectedInputs || numOutputs != 1 || outputs[0] != SpeakerArr::kStereo ||
            (numInputs == 1 && inputs[0] != SpeakerArr::kStereo))
            return kResultFalse;
        return kResultOk;
    }
    tresult PLUGIN_API getBusArrangement(BusDirection direction, int32 index,
                                         SpeakerArrangement& arrangement) override
    {
        if (index != 0 || getBusCount(kAudio, direction) == 0)
            return kInvalidArgument;
        arrangement = SpeakerArr::kStereo;
        return kResultOk;
    }
    tresult PLUGIN_API canProcessSampleSize(int32 size) override
    {
        return size == kSample32 ? kResultOk : kResultFalse;
    }
    uint32 PLUGIN_API getLatencySamples() override { return static_cast<uint32>(processor.getLatency()); }
    tresult PLUGIN_API setupProcessing(ProcessSetup& setup) override
    {
        processor.prepare(setup.sampleRate, setup.maxSamplesPerBlock);
        return kResultOk;
    }
    tresult PLUGIN_API setProcessing(TBool) override { return kResultOk; }
    uint32 PLUGIN_API getTailSamples() override { return 0; }

    tresult PLUGIN_API process(ProcessData& data) override
    {
        // Parameter changes and events at their sample offsets, the audio in between.
        IParamValueQueue* queue = nullptr;
        int32 point = 0;
        if (data.inputParameterChanges != nullptr)
            for (int32 i = 0; i < data.inputParameterChanges->getParameterCount(); ++i)
                if (auto* candidate = data.inputParameterChanges->getParameterData(i);
                    candidate != nullptr && candidate->getParameterId() == parameterId)
                    queue = candidate;
        int32 event = 0;
        const int32 numEvents = data.inputEvents != nullptr ? data.inputEvents->getEventCount() : 0;

        float* left = nullptr;
        float* right = nullptr;
        if (data.numOutputs > 0 && data.outputs[0].numChannels >= 2)
        {
            left = data.outputs[0].channelBuffers32[0];
            right = data.outputs[0].channelBuffers32[1];
            if (data.numInputs > 0 && data.inputs[0].numChannels >= 2)
            {
                if (data.inputs[0].channelBuffers32[0] != left)
                    std::memcpy(left, data.inputs[0].channelBuffers32[0], sizeof(float) * static_cast<std::size_t>(data.numSamples));
                if (data.inputs[0].channelBuffers32[1] != right)
                    std::memcpy(right, data.inputs[0].channelBuffers32[1], sizeof(float) * static_cast<std::size_t>(data.numSamples));
            }
        }

        int32 done = 0;
        while (true)
        {
            int32 next = data.numSamples;
            int32 offset = 0;
            ParamValue value = 0.0;
            if (queue != nullptr && point < queue->getPointCount() &&
                queue->getPoint(point, offset, value) == kResultOk)
                next = std::min(next, offset);
            Event e{};
            if (event < numEvents && data.inputEvents->getEvent(event, e) == kResultOk)
                next = std::min(next, e.sampleOffset);
            next = std::max(next, done);

            if (left != nullptr)
                processor.process(left, right, done, next);
            done = next;
            if (done >= data.numSamples)
                break;

            while (queue != nullptr && point < queue->getPointCount() &&
                   queue->getPoint(point, offset, value) == kResultOk && offset <= done)
            {
                processor.setParameter(value);
                ++point;
            }
            while (event < numEvents && data.inputEvents->getEvent(event, e) == kResultOk &&
                   e.sampleOffset <= done)
            {
                if (e.type == Event::kNoteOnEvent)
                    processor.noteOn(e.noteOn.channel, e.noteOn.pitch, e.noteOn.velocity);
                else if (e.type == Event::kNoteOffEvent)
                    processor.noteOff(e.noteOff.channel, e.noteOff.pitch);
                ++event;
            }
        }
        // What is left after the last sample (points or events at numSamples).
        while (queue != nullptr && point < queue->getPointCount())
        {
            int32 offset = 0;
            ParamValue value = 0.0;
            if (queue->getPoint(point++, offset, value) == kResultOk)
                processor.setParameter(value);
        }
        if (data.numOutputs > 0)
            data.outputs[0].silenceFlags = 0;
        return kResultOk;
    }

    // IEditController
    // The controller follows the component's state (as two-part plugins do).
    tresult PLUGIN_API setComponentState(IBStream* state) override
    {
        unsigned char bytes[8] = {};
        int32 read = 0;
        if (state == nullptr || state->read(bytes, 8, &read) != kResultOk || read != 8)
            return kResultFalse;
        controllerValue = core::readState(bytes);
        return kResultOk;
    }
    int32 PLUGIN_API getParameterCount() override { return 1; }
    tresult PLUGIN_API getParameterInfo(int32 index, ParameterInfo& info) override
    {
        if (index != 0)
            return kInvalidArgument;
        const auto spec = core::parameterOf(kind);
        info = {};
        info.id = parameterId;
        copyName(info.title, spec.name);
        copyName(info.shortTitle, spec.name);
        copyName(info.units, "");
        info.stepCount = 0;
        info.defaultNormalizedValue = spec.defaultValue;
        info.unitId = 0; // the root unit
        info.flags = ParameterInfo::kCanAutomate;
        return kResultOk;
    }
    tresult PLUGIN_API getParamStringByValue(ParamID id, ParamValue value, String128 text) override
    {
        if (id != parameterId)
            return kInvalidArgument;
        char buffer[32];
        std::snprintf(buffer, sizeof buffer, "%.3f", 2.0 * value);
        copyName(text, buffer);
        return kResultOk;
    }
    tresult PLUGIN_API getParamValueByString(ParamID, TChar*, ParamValue&) override { return kNotImplemented; }
    ParamValue PLUGIN_API normalizedParamToPlain(ParamID, ParamValue value) override { return 2.0 * value; }
    ParamValue PLUGIN_API plainParamToNormalized(ParamID, ParamValue plain) override { return plain / 2.0; }
    ParamValue PLUGIN_API getParamNormalized(ParamID id) override
    {
        return id == parameterId ? controllerValue.load() : 0.0;
    }
    tresult PLUGIN_API setParamNormalized(ParamID id, ParamValue value) override
    {
        if (id != parameterId)
            return kInvalidArgument;
        controllerValue = value;
        return kResultOk;
    }
    tresult PLUGIN_API setComponentHandler(IComponentHandler*) override { return kResultOk; }
    IPlugView* PLUGIN_API createView(FIDString) override { return nullptr; }

private:
    std::atomic<uint32> references{1};
    core::Processor processor;
    std::atomic<double> controllerValue{core::parameterOf(kind).defaultValue};
};

class Factory final : public IPluginFactory2
{
public:
    tresult PLUGIN_API queryInterface(const TUID interfaceId, void** object) override
    {
        if (sameId(interfaceId, FUnknown::iid) || sameId(interfaceId, IPluginFactory::iid) ||
            sameId(interfaceId, IPluginFactory2::iid))
        {
            *object = static_cast<IPluginFactory2*>(this);
            return kResultOk;
        }
        *object = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }

    tresult PLUGIN_API getFactoryInfo(PFactoryInfo* info) override
    {
        *info = PFactoryInfo("DAW Tests", "https://github.com/tavanzz/daw", "", PFactoryInfo::kUnicode);
        return kResultOk;
    }
    int32 PLUGIN_API countClasses() override { return 1; }
    tresult PLUGIN_API getClassInfo(int32 index, PClassInfo* info) override
    {
        if (index != 0)
            return kInvalidArgument;
        TUID cid;
        classId.toTUID(cid);
        *info = PClassInfo(cid, PClassInfo::kManyInstances, kVstAudioEffectClass, DAW_TEST_PLUGIN_NAME);
        return kResultOk;
    }
    tresult PLUGIN_API getClassInfo2(int32 index, PClassInfo2* info) override
    {
        if (index != 0)
            return kInvalidArgument;
        TUID cid;
        classId.toTUID(cid);
        *info = PClassInfo2(cid, PClassInfo::kManyInstances, kVstAudioEffectClass, DAW_TEST_PLUGIN_NAME,
                            kDistributable, core::isInstrument(kind) ? "Instrument|Synth" : "Fx",
                            "DAW Tests", "1.0.0", kVstVersionString);
        return kResultOk;
    }
    tresult PLUGIN_API createInstance(FIDString cid, FIDString interfaceId, void** object) override
    {
        TUID ours;
        classId.toTUID(ours);
        if (std::memcmp(cid, ours, sizeof(TUID)) != 0)
        {
            *object = nullptr;
            return kNoInterface;
        }
        auto* plugin = new TestPlugin();
        const auto result = plugin->queryInterface(interfaceId, object);
        plugin->release();
        return result;
    }
};

} // namespace

// The entry points every VST3 module exports.
extern "C"
{
#if defined(_WIN32)
#define DAW_TEST_EXPORT __declspec(dllexport)
#else
#define DAW_TEST_EXPORT __attribute__((visibility("default")))
#endif

DAW_TEST_EXPORT IPluginFactory* PLUGIN_API GetPluginFactory()
{
    // The faulty kinds misbehave when the host asks for the factory: when it scans.
    if constexpr (kind == core::Kind::crash)
    {
        // A write through a null pointer, not abort(): with its own C runtime (static on
        // Windows), a plugin's abort() would open the debug runtime's dialog instead of
        // ending the scanner, whose handlers belong to its own runtime.
        static volatile int* volatile nowhere = nullptr;
        *nowhere = 1;
        return nullptr; // not reached
    }
    else if constexpr (kind == core::Kind::hang)
    {
        for (;;)
            std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    else
    {
        static Factory factory;
        return &factory;
    }
}

#if defined(__APPLE__)
DAW_TEST_EXPORT bool bundleEntry(CFBundleRef) { return true; }
DAW_TEST_EXPORT bool bundleExit() { return true; }
#elif defined(_WIN32)
DAW_TEST_EXPORT bool InitDll() { return true; }
DAW_TEST_EXPORT bool ExitDll() { return true; }
#else
DAW_TEST_EXPORT bool ModuleEntry(void*) { return true; }
DAW_TEST_EXPORT bool ModuleExit() { return true; }
#endif
}
