// The Audio Unit (v2) test plugins, written against Apple's Audio Unit SDK as a
// third-party Audio Unit would be. What they do is TestPluginCore.h's.

#include "TestAudioUnits.h"

#include "../core/TestPluginCore.h"

#include <AudioUnitSDK/AUEffectBase.h>
#include <AudioUnitSDK/MusicDeviceBase.h>

#include <array>
#include <mutex>

namespace core = daw::testplugins;

namespace
{

constexpr AudioUnitParameterID parameterId = 0;

// The parameter's description, but its name (AUBase::FillInParameterName(), protected).
void describeParameter(core::Kind kind, AudioUnitParameterInfo& info)
{
    const auto spec = core::parameterOf(kind);
    info.flags = kAudioUnitParameterFlag_IsReadable | kAudioUnitParameterFlag_IsWritable;
    info.unit = kAudioUnitParameterUnit_Generic;
    info.minValue = 0.0f;
    info.maxValue = 1.0f;
    info.defaultValue = static_cast<AudioUnitParameterValue>(spec.defaultValue);
}

template <core::Kind kind>
class TestEffect final : public ausdk::AUEffectBase
{
public:
    explicit TestEffect(AudioComponentInstance instance)
        : AUEffectBase(instance, false)
    {
        Globals()->UseIndexedParameters(1);
        Globals()->SetParameter(parameterId,
                                static_cast<AudioUnitParameterValue>(core::parameterOf(kind).defaultValue));
    }

    OSStatus Initialize() override
    {
        const auto result = AUEffectBase::Initialize();
        processor.prepare(GetSampleRate(), static_cast<int>(GetMaxFramesPerSlice()));
        return result;
    }

    void Cleanup() override { AUEffectBase::Cleanup(); }

    OSStatus Reset(AudioUnitScope scope, AudioUnitElement element) override
    {
        processor.reset();
        return AUEffectBase::Reset(scope, element);
    }

    OSStatus GetParameterInfo(AudioUnitScope scope, AudioUnitParameterID id,
                              AudioUnitParameterInfo& info) override
    {
        if (scope != kAudioUnitScope_Global || id != parameterId)
            return kAudioUnitErr_InvalidParameter;
        describeParameter(kind, info);
        FillInParameterName(info, CFStringCreateWithCString(nullptr, core::parameterOf(kind).name,
                                                            kCFStringEncodingUTF8),
                            true);
        return noErr;
    }

    Float64 GetLatency() override { return processor.getLatency() / GetSampleRate(); }
    Float64 GetTailTime() override { return 0.0; }
    bool SupportsTail() override { return true; }

    UInt32 SupportedNumChannels(const AUChannelInfo** info) override
    {
        static constexpr AUChannelInfo stereo{2, 2};
        if (info != nullptr)
            *info = &stereo;
        return 1;
    }

    OSStatus ProcessBufferLists(AudioUnitRenderActionFlags&, const AudioBufferList& in,
                                AudioBufferList& out, UInt32 frames) override
    {
        if (in.mNumberBuffers < 2 || out.mNumberBuffers < 2)
            return kAudioUnitErr_FormatNotSupported;
        auto* left = static_cast<float*>(out.mBuffers[0].mData);
        auto* right = static_cast<float*>(out.mBuffers[1].mData);
        if (in.mBuffers[0].mData != left)
            std::memcpy(left, in.mBuffers[0].mData, frames * sizeof(float));
        if (in.mBuffers[1].mData != right)
            std::memcpy(right, in.mBuffers[1].mData, frames * sizeof(float));
        processor.setParameter(GetParameter(parameterId));
        processor.process(left, right, 0, static_cast<int>(frames));
        return noErr;
    }

private:
    core::Processor processor{kind};
};

class TestSynth final : public ausdk::MusicDeviceBase
{
public:
    explicit TestSynth(AudioComponentInstance instance)
        : MusicDeviceBase(instance, 0, 1)
    {
        Globals()->UseIndexedParameters(1);
        Globals()->SetParameter(parameterId,
                                static_cast<AudioUnitParameterValue>(core::parameterOf(core::Kind::synth).defaultValue));
    }

    OSStatus Initialize() override
    {
        const auto result = MusicDeviceBase::Initialize();
        processor.prepare(Output(0).GetStreamFormat().mSampleRate,
                          static_cast<int>(GetMaxFramesPerSlice()));
        return result;
    }

    OSStatus Reset(AudioUnitScope scope, AudioUnitElement element) override
    {
        processor.reset();
        pendingCount = 0;
        return MusicDeviceBase::Reset(scope, element);
    }

    bool CanScheduleParameters() const override { return false; }
    bool StreamFormatWritable(AudioUnitScope, AudioUnitElement) override { return !IsInitialized(); }

    OSStatus GetParameterInfo(AudioUnitScope scope, AudioUnitParameterID id,
                              AudioUnitParameterInfo& info) override
    {
        if (scope != kAudioUnitScope_Global || id != parameterId)
            return kAudioUnitErr_InvalidParameter;
        describeParameter(core::Kind::synth, info);
        FillInParameterName(info, CFSTR("Level"), false);
        return noErr;
    }

    OSStatus HandleNoteOn(UInt8 channel, UInt8 key, UInt8 velocity, UInt32 frame) override
    {
        queue({true, channel, key, static_cast<float>(velocity) / 127.0f, frame});
        return noErr;
    }

    OSStatus HandleNoteOff(UInt8 channel, UInt8 key, UInt8, UInt32 frame) override
    {
        queue({false, channel, key, 0.0f, frame});
        return noErr;
    }

    OSStatus HandleAllNotesOff(UInt8) override
    {
        processor.allNotesOff();
        return noErr;
    }

    OSStatus Render(AudioUnitRenderActionFlags&, const AudioTimeStamp&, UInt32 frames) override
    {
        auto& output = Output(0).GetBufferList();
        if (output.mNumberBuffers < 2)
            return kAudioUnitErr_FormatNotSupported;
        auto* left = static_cast<float*>(output.mBuffers[0].mData);
        auto* right = static_cast<float*>(output.mBuffers[1].mData);
        processor.setParameter(Globals()->GetParameter(parameterId));

        UInt32 done = 0;
        int next = 0;
        while (done < frames)
        {
            UInt32 until = frames;
            if (next < pendingCount)
                until = std::min(until, std::max(done, pending[static_cast<std::size_t>(next)].frame));
            processor.process(left, right, static_cast<int>(done), static_cast<int>(until));
            done = until;
            while (next < pendingCount && pending[static_cast<std::size_t>(next)].frame <= done)
            {
                const auto& event = pending[static_cast<std::size_t>(next++)];
                if (event.on)
                    processor.noteOn(event.channel, event.key, event.velocity);
                else
                    processor.noteOff(event.channel, event.key);
            }
        }
        // Events past the end (none, when the host behaves) apply now.
        for (; next < pendingCount; ++next)
        {
            const auto& event = pending[static_cast<std::size_t>(next)];
            if (event.on)
                processor.noteOn(event.channel, event.key, event.velocity);
            else
                processor.noteOff(event.channel, event.key);
        }
        pendingCount = 0;
        return noErr;
    }

private:
    struct Pending
    {
        bool on = false;
        int channel = 0;
        int key = 0;
        float velocity = 0.0f;
        UInt32 frame = 0;
    };

    void queue(const Pending& event)
    {
        if (pendingCount < static_cast<int>(pending.size()))
            pending[static_cast<std::size_t>(pendingCount++)] = event; // in order: frames never go back
    }

    core::Processor processor{core::Kind::synth};
    std::array<Pending, 1024> pending{};
    int pendingCount = 0;
};

using TestGain = TestEffect<core::Kind::gain>;
using TestLatency = TestEffect<core::Kind::latency>;
using TestNaN = TestEffect<core::Kind::nan>;

} // namespace

AUSDK_COMPONENT_ENTRY(ausdk::AUBaseFactory, TestGain)
AUSDK_COMPONENT_ENTRY(ausdk::AUBaseFactory, TestLatency)
AUSDK_COMPONENT_ENTRY(ausdk::AUBaseFactory, TestNaN)
AUSDK_COMPONENT_ENTRY(ausdk::AUMusicDeviceFactory, TestSynth)

namespace daw::testplugins
{

namespace
{

constexpr OSType fourCharacters(const char (&code)[5])
{
    return (static_cast<OSType>(static_cast<unsigned char>(code[0])) << 24) |
           (static_cast<OSType>(static_cast<unsigned char>(code[1])) << 16) |
           (static_cast<OSType>(static_cast<unsigned char>(code[2])) << 8) |
           static_cast<OSType>(static_cast<unsigned char>(code[3]));
}

void registerOne(OSType type, const char (&subtype)[5], CFStringRef name, void* (*factory)(const AudioComponentDescription*))
{
    AudioComponentDescription description{};
    description.componentType = type;
    description.componentSubType = fourCharacters(subtype);
    description.componentManufacturer = fourCharacters("DAWt");
    AudioComponentRegister(&description, name, 0x00010000,
                           reinterpret_cast<AudioComponentFactoryFunction>(factory));
}

} // namespace

void registerTestAudioUnits()
{
    static std::once_flag once;
    std::call_once(once, [] {
        registerOne(kAudioUnitType_Effect, "DtGn", CFSTR("DAW Tests: DAW Test Gain"), TestGainFactory);
        registerOne(kAudioUnitType_Effect, "DtLt", CFSTR("DAW Tests: DAW Test Latency"), TestLatencyFactory);
        registerOne(kAudioUnitType_Effect, "DtNn", CFSTR("DAW Tests: DAW Test NaN"), TestNaNFactory);
        registerOne(kAudioUnitType_MusicDevice, "DtSy", CFSTR("DAW Tests: DAW Test Synth"), TestSynthFactory);
    });
}

} // namespace daw::testplugins
