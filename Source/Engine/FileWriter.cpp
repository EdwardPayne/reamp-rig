#include "FileWriter.h"
#include "Resampler.h"

#include <cmath>

namespace rf::engine
{
    namespace
    {
        constexpr int chunkSize = 8192;
        constexpr int pollMs = 2;
    }

    struct FileWriter::Work
    {
        std::vector<float> in, out, converted;
        std::vector<int> pcm;
        std::unique_ptr<ResamplerStream> resampler;
        juce::int64 discarded = 0;
        float peak = 0.0f;
    };

    FileWriter::FileWriter()
        : juce::Thread ("Take writer")
    {
    }

    FileWriter::~FileWriter()
    {
        cancel();
    }

    juce::File FileWriter::getTempFile (const juce::File& finalFile)
    {
        return finalFile.getSiblingFile ("." + finalFile.getFileNameWithoutExtension() + ".reamprig-part"
                                         + finalFile.getFileExtension());
    }

    void FileWriter::floatToPcm (const float* in, int* out, int numSamples, int bits) noexcept
    {
        const auto fullScale = (double) (1 << (bits - 1));
        const auto maxValue = (int) fullScale - 1;
        const auto shift = 32 - bits;

        for (int i = 0; i < numSamples; ++i)
        {
            const auto v = (int) juce::jlimit (-fullScale, (double) maxValue, std::round ((double) in[i] * fullScale));
            out[i] = (int) ((juce::uint32) v << shift);
        }
    }

    juce::String FileWriter::start (WriteJob newJob)
    {
        JUCE_ASSERT_MESSAGE_THREAD
        cancel();

        jassert (newJob.stream != nullptr && newJob.deviceRate > 0.0 && newJob.fileRate > 0.0);

        const auto folder = newJob.file.getParentDirectory();

        if (! folder.isDirectory())
            if (const auto r = folder.createDirectory(); r.failed())
                return "cannot create " + folder.getFullPathName() + " (" + r.getErrorMessage() + ")";

        tempFile = getTempFile (newJob.file);
        tempFile.deleteFile();

        std::unique_ptr<juce::OutputStream> stream = tempFile.createOutputStream();

        if (stream == nullptr)
            return "cannot write to " + folder.getFullPathName();

        const auto isFloat = newJob.bitsPerSample == 32;
        juce::WavAudioFormat wav;
        writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                  .withSampleRate (newJob.fileRate)
                                                  .withNumChannels (1)
                                                  .withBitsPerSample (newJob.bitsPerSample)
                                                  .withSampleFormat (isFloat ? juce::AudioFormatWriterOptions::SampleFormat::floatingPoint
                                                                             : juce::AudioFormatWriterOptions::SampleFormat::integral));
        if (writer == nullptr)
        {
            tempFile.deleteFile();
            return "cannot create a " + juce::String (newJob.bitsPerSample) + "-bit WAV writer";
        }

        job = std::move (newJob);

        work = std::make_unique<Work>();
        work->in.resize (chunkSize);
        work->out.resize (chunkSize);
        work->pcm.resize (chunkSize);

        if (! juce::approximatelyEqual (job.deviceRate, job.fileRate))
            work->resampler = std::make_unique<ResamplerStream> (job.deviceRate, job.fileRate);

        {
            const std::scoped_lock lock (resultLock);
            result.reset();
        }

        written.store (0);
        busy.store (true);
        startThread (juce::Thread::Priority::high);
        return {};
    }

    void FileWriter::cancel()
    {
        JUCE_ASSERT_MESSAGE_THREAD

        if (isThreadRunning())
            stopThread (5000);

        if (writer != nullptr || busy.load())
        {
            writer.reset();
            tempFile.deleteFile();

            const std::scoped_lock lock (resultLock);

            if (! result.has_value())
            {
                WriteResult r;
                r.cancelled = true;
                r.error = "cancelled";
                result = r;
            }
        }

        work.reset();
        busy.store (false);
    }

    std::optional<WriteResult> FileWriter::getResult()
    {
        const std::scoped_lock lock (resultLock);
        return result;
    }

    //==============================================================================
    void FileWriter::writeSamples (const float* data, int numSamples)
    {
        const auto already = written.load (std::memory_order_relaxed);
        const auto n = (int) juce::jmin ((juce::int64) numSamples, job.outputLength - already);

        if (n <= 0)
            return;

        const auto range = juce::FloatVectorOperations::findMinAndMax (data, n);
        work->peak = juce::jmax (work->peak, -range.getStart(), range.getEnd());

        if (job.bitsPerSample == 32)
        {
            const float* channels[] = { data, nullptr };
            writer->write (reinterpret_cast<const int**> (channels), n);
        }
        else
        {
            floatToPcm (data, work->pcm.data(), n, job.bitsPerSample);
            const int* channels[] = { work->pcm.data(), nullptr };
            writer->write (channels, n);
        }

        if (job.onWritten != nullptr)
            job.onWritten (already, data, n);

        written.store (already + n, std::memory_order_relaxed);
    }

    void FileWriter::run()
    {
        auto& w = *work;
        auto& stream = *job.stream;

        while (! threadShouldExit())
        {
            const auto n = stream.read (w.in.data(), chunkSize);

            if (n == 0)
            {
                if (stream.isComplete() && stream.getNumReady() == 0)
                    break;

                juce::Thread::sleep (pollMs);
                continue;
            }

            // Latency compensation: the first `discardSamples` recorded samples are dropped.
            auto offset = 0;

            if (w.discarded < job.discardSamples)
            {
                offset = (int) juce::jmin ((juce::int64) n, job.discardSamples - w.discarded);
                w.discarded += offset;
            }

            if (offset == n)
                continue;

            if (w.resampler == nullptr)
            {
                writeSamples (w.in.data() + offset, n - offset);
                continue;
            }

            w.resampler->push (w.in.data() + offset, n - offset);

            for (int produced; (produced = w.resampler->pull (w.out.data(), chunkSize)) > 0;)
                writeSamples (w.out.data(), produced);
        }

        if (threadShouldExit())
            return;   // cancel() cleans up

        const auto ok = finish();
        juce::ignoreUnused (ok);
        busy.store (false);
    }

    bool FileWriter::finish()
    {
        auto& w = *work;
        WriteResult r;
        r.file = job.file;
        r.resampled = w.resampler != nullptr;

        // The rest of the resampled signal (input beyond the recording counts as silence).
        if (w.resampler != nullptr)
        {
            w.resampler->finish();

            while (written.load() < job.outputLength)
            {
                const auto want = (int) juce::jmin ((juce::int64) chunkSize, job.outputLength - written.load());
                const auto produced = w.resampler->pull (w.out.data(), want);

                if (produced <= 0)
                    break;

                writeSamples (w.out.data(), produced);
            }
        }

        // Samples lost to a dropout: keep the length exact with silence (the file is marked).
        std::fill (w.out.begin(), w.out.end(), 0.0f);

        while (written.load() < job.outputLength)
        {
            const auto n = (int) juce::jmin ((juce::int64) chunkSize, job.outputLength - written.load());
            r.paddedSamples += n;
            writeSamples (w.out.data(), n);
        }

        r.samplesWritten = written.load();
        r.peak = w.peak;

        writer->flush();
        writer.reset();   // closes the stream and finalises the header

        if (job.file.exists() && ! job.replaceExisting)
        {
            r.error = job.file.getFileName() + " appeared while recording; the take was kept as "
                    + tempFile.getFileName();
        }
        else if (! tempFile.moveFileTo (job.file))
        {
            r.error = "could not rename " + tempFile.getFileName() + " to " + job.file.getFileName();
        }
        else
        {
            r.ok = true;
        }

        const std::scoped_lock lock (resultLock);
        result = r;
        return r.ok;
    }
}
