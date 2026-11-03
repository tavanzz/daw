#include "daw/engine/io/RecordingWriter.h"

#include "daw/engine/io/Bounce.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>

namespace daw::engine::io
{

namespace
{

class WavSink final : public RecordingSink
{
public:
    WavSink(std::unique_ptr<juce::AudioFormatWriter> writerToUse, int bits)
        : writer(std::move(writerToUse))
        , bitsPerSample(bits)
    {
    }

    bool write(const juce::AudioBuffer<float>& audio, int numSamples) override
    {
        if (bitsPerSample == 32)
            return writer->writeFromAudioSampleBuffer(audio, 0, numSamples);

        // The writer takes 32-bit left-justified integers.
        const int channels = audio.getNumChannels();
        integers.resize(static_cast<std::size_t>(channels * numSamples));
        pointers.resize(static_cast<std::size_t>(channels + 1), nullptr);
        const int shift = 32 - bitsPerSample;

        for (int channel = 0; channel < channels; ++channel)
        {
            int* destination = integers.data() + static_cast<std::size_t>(channel * numSamples);
            const float* source = audio.getReadPointer(channel);
            for (int i = 0; i < numSamples; ++i)
                destination[i] = static_cast<int>(
                    static_cast<unsigned>(quantise(source[i], bitsPerSample)) << shift);
            pointers[static_cast<std::size_t>(channel)] = destination;
        }
        pointers[static_cast<std::size_t>(channels)] = nullptr;

        return writer->write(pointers.data(), numSamples);
    }

    bool flush() override { return writer->flush(); }

private:
    std::unique_ptr<juce::AudioFormatWriter> writer;
    int bitsPerSample;
    std::vector<int> integers;
    std::vector<const int*> pointers;
};

} // namespace

std::unique_ptr<RecordingSink> openWavSink(const juce::File& file, int numChannels,
                                           double sampleRate, int bitsPerSample,
                                           juce::String& error)
{
    const auto quotedName = "\"" + file.getFileName() + "\"";
    if (bitsPerSample != 16 && bitsPerSample != 24 && bitsPerSample != 32)
    {
        error = quotedName + ": only 16-bit, 24-bit and 32-bit float recordings are possible.";
        return nullptr;
    }

    auto fileStream = std::make_unique<juce::FileOutputStream>(file);
    if (!fileStream->openedOk() || !fileStream->setPosition(0) || !fileStream->truncate().wasOk())
    {
        error = quotedName + " could not be created.";
        return nullptr;
    }

    std::unique_ptr<juce::OutputStream> stream = std::move(fileStream);
    using SampleFormat = juce::AudioFormatWriterOptions::SampleFormat;
    const auto options = juce::AudioFormatWriterOptions{}
                             .withSampleRate(sampleRate)
                             .withNumChannels(numChannels)
                             .withBitsPerSample(bitsPerSample)
                             .withSampleFormat(bitsPerSample == 32 ? SampleFormat::floatingPoint
                                                                   : SampleFormat::integral);

    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor(stream, options);
    if (writer == nullptr)
    {
        error = quotedName + ": this format is not supported at this sample rate.";
        return nullptr;
    }

    return std::make_unique<WavSink>(std::move(writer), bitsPerSample);
}

class RecordingWriter::Thread final : public juce::Thread
{
public:
    explicit Thread(RecordingWriter& writerToRun)
        : juce::Thread("Recording writer")
        , writer(writerToRun)
    {
    }

    void run() override { writer.run(); }

private:
    RecordingWriter& writer;
};

RecordingWriter::RecordingWriter(RecordingWriterSettings settingsToUse)
    : settings(std::move(settingsToUse))
{
    jassert(settings.sampleRate > 0.0);

    if (!settings.freeSpace)
        settings.freeSpace = [](const juce::File& file) {
            return file.getParentDirectory().getBytesFreeOnVolume();
        };

    if (!settings.openSink)
        settings.openSink = [rate = settings.sampleRate, bits = settings.bitsPerSample](
                                const juce::File& file, int numChannels, juce::String& error) {
            return openWavSink(file, numChannels, rate, bits, error);
        };
}

RecordingWriter::~RecordingWriter()
{
    if (thread != nullptr)
        thread->stopThread(-1);

    closeFiles();
}

rt::AudioFifo& RecordingWriter::addFile(const juce::File& file, int numChannels)
{
    jassert(thread == nullptr);
    const auto capacity = static_cast<int>(settings.bufferSeconds * settings.sampleRate);

    files.push_back(std::make_unique<OutputFile>(file, numChannels, capacity));
    publishedPeaks.push_back(std::make_shared<const dsp::WaveformPeaks>());
    return files.back()->fifo;
}

juce::Result RecordingWriter::start()
{
    jassert(thread == nullptr);

    int maximumChannels = 1;
    for (auto& output : files)
    {
        juce::String error;
        output->sink = settings.openSink(output->file, output->fifo.getNumChannels(), error);
        if (output->sink == nullptr)
        {
            closeFiles();
            for (auto& created : files)
                created->file.deleteFile();
            return juce::Result::fail(error);
        }
        maximumChannels = std::max(maximumChannels, output->fifo.getNumChannels());
    }

    // A tenth of a second at a time: small enough to keep up, large enough to be cheap.
    scratch.setSize(maximumChannels,
                    std::max(1, static_cast<int>(settings.sampleRate / 10.0)));
    scratchChannels.clear();
    for (int channel = 0; channel < maximumChannels; ++channel)
        scratchChannels.push_back(scratch.getWritePointer(channel));

    thread = std::make_unique<Thread>(*this);
    thread->startThread();
    return juce::Result::ok();
}

void RecordingWriter::finish() noexcept
{
    finishing.store(true);
}

bool RecordingWriter::isFinished() const noexcept
{
    return finished.load();
}

void RecordingWriter::discard()
{
    if (thread != nullptr)
        thread->stopThread(-1);

    closeFiles();
    for (auto& output : files)
        output->file.deleteFile();
}

const juce::File& RecordingWriter::getFile(int index) const
{
    return files[static_cast<std::size_t>(index)]->file;
}

juce::int64 RecordingWriter::getSamplesWritten(int index) const noexcept
{
    return files[static_cast<std::size_t>(index)]->samplesWritten.load();
}

std::shared_ptr<const dsp::WaveformPeaks> RecordingWriter::getPeaks(int index) const
{
    const std::scoped_lock lock(peaksLock);
    return publishedPeaks[static_cast<std::size_t>(index)];
}

double RecordingWriter::bytesPerSecond(double sampleRate, int bitsPerSample,
                                       int numChannels) noexcept
{
    return sampleRate * static_cast<double>(bitsPerSample / 8 * numChannels);
}

void RecordingWriter::run()
{
    check();
    auto lastCheck = juce::Time::getMillisecondCounter();

    while (!thread->threadShouldExit())
    {
        // Read the flag before emptying the FIFOs: whatever was pushed before
        // finish() is then certainly taken.
        const bool lastRound = finishing.load();
        const bool moved = moveAudio();

        if (juce::Time::getMillisecondCounter() - lastCheck >=
            static_cast<juce::uint32>(settings.checkIntervalMilliseconds))
        {
            check();
            lastCheck = juce::Time::getMillisecondCounter();
        }

        if (lastRound && !moved)
        {
            closeFiles();
            publishPeaks();
            finished.store(true);
            return;
        }

        if (!moved)
            thread->wait(2);
    }
}

bool RecordingWriter::moveAudio()
{
    bool moved = false;

    for (auto& output : files)
    {
        float* const* channels = scratchChannels.data();
        const int numChannels = output->fifo.getNumChannels();
        const int count = output->fifo.pop(channels, scratch.getNumSamples());
        if (count <= 0)
            continue;

        moved = true;
        if (output->failed)
            continue;

        const juce::AudioBuffer<float> block(channels, numChannels, count);
        if (!output->sink->write(block, count))
        {
            output->failed = true;
            problem.store(Problem::writeFailed);
            continue;
        }

        output->peaks.append(channels, count);
        output->samplesWritten.fetch_add(count);
    }

    return moved;
}

void RecordingWriter::check()
{
    int totalChannels = 0;
    for (auto& output : files)
    {
        totalChannels += output->fifo.getNumChannels();
        if (!output->failed && !output->sink->flush())
        {
            output->failed = true;
            problem.store(Problem::writeFailed);
        }
    }

    publishPeaks();

    if (files.empty())
        return;

    const auto rate = bytesPerSecond(settings.sampleRate, settings.bitsPerSample, totalChannels);
    const auto freeBytes = static_cast<double>(settings.freeSpace(files.front()->file));
    secondsLeft.store(freeBytes / rate);

    if (freeBytes / rate < settings.minimumSecondsLeft && problem.load() == Problem::none)
        problem.store(Problem::lowDiskSpace);
}

void RecordingWriter::publishPeaks()
{
    std::vector<std::shared_ptr<const dsp::WaveformPeaks>> latest;
    latest.reserve(files.size());
    for (auto& output : files)
        latest.push_back(std::make_shared<const dsp::WaveformPeaks>(output->peaks.build()));

    const std::scoped_lock lock(peaksLock);
    publishedPeaks.swap(latest);
}

void RecordingWriter::closeFiles()
{
    for (auto& output : files)
        output->sink.reset();
}

} // namespace daw::engine::io
