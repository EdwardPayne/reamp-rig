#include "BatchController.h"
#include "SyncPlan.h"
#include "../Engine/Resampler.h"
#include "../UI/Format.h"
#include "../UI/LookAndFeel.h"

#include <cstdio>

namespace rf::app
{
    namespace format = ui::format;
    using model::FileStatus;
    using Tone = ui::StatusBar::Tone;
    namespace Warning = model::Warning;

    namespace
    {
        constexpr double resampledErrorLimitDb = -80.0;   // documented Resampler threshold

        juce::String dash()     { return ui::utf8 (" \xe2\x80\x94 "); }
        juce::String dot()      { return ui::utf8 (" \xc2\xb7 "); }
        juce::String arrow()    { return ui::utf8 (" \xe2\x86\x92 "); }
        juce::String ellipsis() { return ui::utf8 ("\xe2\x80\xa6"); }

        int channelIndex (const model::FileItem& item)
        {
            return item.hasChannelChoice() && item.channel == model::Channel::right ? 1 : 0;
        }

        std::optional<model::Channel> channelTag (const model::FileItem& item)
        {
            return item.hasChannelChoice() ? std::optional<model::Channel> (item.channel) : std::nullopt;
        }

        juce::String channelText (const model::FileItem& item)
        {
            return item.hasChannelChoice() ? (item.channel == model::Channel::left ? "L" : "R") : "mono";
        }

        /** "00:12", "01:03", "1:02:03" (whole seconds elapsed, like the waveform readout). */
        juce::String clock (double seconds)
        {
            const auto total = (juce::int64) std::floor (juce::jmax (0.0, seconds) + 1.0e-6);
            const auto h = total / 3600, m = (total / 60) % 60, s = total % 60;
            const auto mmss = juce::String (m).paddedLeft ('0', 2) + ":" + juce::String (s).paddedLeft ('0', 2);
            return h > 0 ? juce::String (h) + ":" + mmss : mmss;
        }

        juce::String formatDb (float db)
        {
            return (db > 0.0f ? "+" : "") + juce::String (db, 1);
        }

        juce::String describePolicy (model::CollisionPolicy p)
        {
            switch (p)
            {
                case model::CollisionPolicy::overwrite: return "overwrite";
                case model::CollisionPolicy::skip:      return "skip";
                case model::CollisionPolicy::autoNumber: break;
            }

            return "auto-number";
        }

        bool sameRequest (const engine::LoadRequest& a, const engine::LoadRequest& b)
        {
            return a.file == b.file && a.channel == b.channel && juce::approximatelyEqual (a.targetSampleRate, b.targetSampleRate);
        }

        juce::String appVersion()
        {
            if (auto* app = juce::JUCEApplicationBase::getInstance())
                return app->getApplicationVersion();

            return {};
        }
    }

    //==============================================================================
    BatchController::BatchController (AudioController& a, model::FileTree& t, OutputOptions& o, Views v)
        : audio (a), tree (t), options (o), views (v)
    {
        audio.onSnapshot = [this] (const engine::EngineSnapshot& snap) { handleSnapshot (snap); };
        audio.onDeviceStopped = [this]
        {
            if (queue.getState() == model::BatchQueue::State::running)
                pauseBecause ("Paused: the audio device stopped. The interrupted file is recorded again on Resume.",
                              Tone::warning);
        };

        views.topBar.onStart = [this] { start(); };
        views.topBar.onPauseResume = [this] { pauseResume(); };
        views.topBar.onSkip = [this] { skipCurrent(); };
        views.topBar.onStop = [this] { stop(); };
        views.fileList.onSkipCurrent = [this] { skipCurrent(); };
        views.fileList.onRedoWarnings = [this] { redoFilesWithWarnings(); };

        tree.addListener (this);
        updateTransport();
    }

    BatchController::~BatchController()
    {
        tree.removeListener (this);
        cancelPendingUpdate();

        audio.onSnapshot = nullptr;
        audio.onDeviceStopped = nullptr;

        for (auto* f : { &views.topBar.onStart, &views.topBar.onPauseResume, &views.topBar.onSkip, &views.topBar.onStop,
                         &views.fileList.onSkipCurrent, &views.fileList.onRedoWarnings })
            *f = nullptr;

        if (take != nullptr)
            take->cancel();

        take.reset();
        writer.cancel();
        loader.cancel();
    }

    //==============================================================================
    void BatchController::start (bool confirmedUnsynced)
    {
        if (queue.isActive() || views.confirm.isShowing())
            return;

        if (audio.isSyncActive())
        {
            views.statusBar.setMessage ("Cannot start while Sync measures", Tone::warning);
            return;
        }

        if (! audio.checkCanRecord())
            return;

        naming = options.getNamingOptions();

        if (const auto problem = model::OutputNaming::validate (naming); problem.isNotEmpty())
        {
            views.statusBar.setMessage ("Cannot start: " + problem, Tone::warning);
            return;
        }

        // 3.6.4: every configuration this batch runs in needs a measurement, or a confirmation.
        plannedKeys = getPlannedKeys();

        if (! confirmedUnsynced)
        {
            if (const auto unsynced = describeUnsynced (plannedKeys); ! unsynced.isEmpty() && tree.countWithStatus (FileStatus::queued) > 0)
            {
                askToStartUnsynced (unsynced);
                return;
            }
        }

        if (queue.start (tree) == 0)
        {
            views.statusBar.setMessage (tree.getNumFiles() == 0 ? "Nothing to process: add files first"
                                                                : "Nothing queued: every file is done. Use Reset status to process files again.",
                                        Tone::warning);
            return;
        }

        bitsPerSample = options.getBitsPerSample();
        tailMs = options.getTailMs();

        const auto status = audio.getDeviceStatus();
        startConfig = audio.getSelectedConfig();
        startConfig.sampleRate = status.config.sampleRate;
        startConfig.bufferSize = status.config.bufferSize;

        log = {};
        logTried = false;
        startedMs = juce::Time::getMillisecondCounterHiRes();
        pausedTotalMs = 0.0;
        processedSeconds = 0.0;
        numDone = numSkipped = numErrors = numWithWarnings = 0;
        preloaded.reset();
        preloadedRequest = {};

        audio.setBatchActive (true);
        options.setEnabled (false);
        updateTransport();

        juce::Logger::writeToLog ("Batch: " + juce::String (queue.getTotal()) + " files queued on "
                                  + audio.describeDevice (status));
        next();
    }

    std::vector<engine::SyncKey> BatchController::getPlannedKeys()
    {
        model::BatchQueue probe;
        std::vector<double> rates;

        if (probe.start (tree) > 0)
            for (const auto& e : probe.getEntries())
                rates.push_back (e.sampleRate);

        return syncplan::keysForBatch (audio.getDeviceStatus(), rates);
    }

    juce::StringArray BatchController::describeUnsynced (const std::vector<engine::SyncKey>& keys) const
    {
        juce::StringArray result;

        for (const auto& k : syncplan::missing (keys, [this] (const engine::SyncKey& key) { return audio.findSync (key); }))
            result.add (audio.describeKey (k));

        return result;
    }

    void BatchController::askToStartUnsynced (const juce::StringArray& unsynced)
    {
        const auto several = unsynced.size() > 1;

        ui::ConfirmDialog::Content c;
        c.title = "Not synced for this configuration";
        c.intro = juce::String ("No latency measurement is stored for ")
                + (several ? "these device configurations" : "this device configuration") + " used by the batch:";
        c.items = unsynced;
        c.note = "Start anyway uses the driver-reported latency as an estimate: those files are marked NC (not calibrated) "
                 "in the list and the log and may be off by a few milliseconds. To measure instead, bypass the amp, connect "
                 "the output directly to the input and press Sync"
               + juce::String (several ? " at each sample rate." : ".");
        c.confirmText = "Start anyway";
        c.cancelText = "Cancel";

        views.statusBar.setMessage ("Not synced for this configuration: " + unsynced.joinIntoString (", "), Tone::warning);

        views.confirm.show (c, [this] (bool confirmed)
        {
            if (confirmed)
                start (true);
            else
                views.statusBar.setMessage ("Start cancelled: not synced. Bypass the amp and press Sync to measure.", Tone::warning);
        });
    }

    void BatchController::pauseResume()
    {
        if (queue.getState() == model::BatchQueue::State::paused)
        {
            if (! audio.checkCanRecord())
                return;

            pausedTotalMs += juce::Time::getMillisecondCounterHiRes() - pausedSinceMs;
            queue.resume();
            phase = Phase::idle;
            updateTransport();
            beginFile();
            return;
        }

        if (queue.getState() == model::BatchQueue::State::running)
        {
            const auto* item = tree.find (currentId);
            pauseBecause ("Paused" + (item != nullptr ? ": " + item->file.getFileName() + " is recorded again from its start on Resume"
                                                      : juce::String()),
                          Tone::normal);
        }
    }

    void BatchController::pauseBecause (const juce::String& message, Tone tone)
    {
        cancelCurrent();

        if (currentId != 0)
            tree.setResult (currentId, FileStatus::queued, 0.0, 0, {}, {});

        queue.pause();
        phase = Phase::paused;
        pausedSinceMs = juce::Time::getMillisecondCounterHiRes();
        views.waveform.setPlayhead (std::nullopt);
        updateTransport();
        views.statusBar.setMessage (message, tone);
    }

    void BatchController::skipCurrent()
    {
        if (queue.getState() != model::BatchQueue::State::running || currentId == 0)
            return;

        cancelCurrent();
        finishFile (FileStatus::skipped, 0, {}, "skipped by the user");
        next();
    }

    void BatchController::stop()
    {
        if (! queue.isActive())
            return;

        cancelCurrent();

        if (currentId != 0 && tree.find (currentId) != nullptr)
            tree.setResult (currentId, FileStatus::queued, 0.0, 0, {}, {});

        queue.stop();
        finishBatch (true);
    }

    void BatchController::redoFilesWithWarnings()
    {
        if (queue.isActive())
            return;

        const auto ids = tree.getIdsWithWarnings (Warning::redoable);

        if (ids.empty())
        {
            views.statusBar.setMessage ("No files with dropout, silence or clipping warnings");
            return;
        }

        tree.resetStatus (ids);
        views.statusBar.setMessage (format::fileCount ((int) ids.size()) + " with warnings queued again; press Start");
    }

    void BatchController::cancelCurrent()
    {
        if (take != nullptr)
            take->cancel();

        take.reset();
        loader.cancel();        // also drops a pending preload
        preloaded.reset();
        preloadedRequest = {};
        pendingRequest = {};
    }

    //==============================================================================
    void BatchController::next()
    {
        take.reset();
        phase = Phase::idle;
        currentId = queue.advance();

        if (currentId == 0)
        {
            if (queue.getState() == model::BatchQueue::State::finished)
                finishBatch (false);

            return;
        }

        beginFile();
    }

    engine::LoadRequest BatchController::makeRequest (const model::FileItem& item, double deviceRate) const
    {
        return { item.file, channelIndex (item), deviceRate };
    }

    void BatchController::beginFile()
    {
        currentId = queue.getCurrent();
        const auto* item = tree.find (currentId);

        if (item == nullptr)
        {
            queue.finishCurrent (FileStatus::skipped);   // removed from the list meanwhile
            next();
            return;
        }

        target = model::OutputNaming::resolve (item->file, item->root, channelTag (*item), naming);

        if (! logTried)
        {
            logTried = true;
            openLog (naming.mode == model::DestinationMode::singleFolder
                         ? naming.outputFolder
                         : model::OutputNaming::folder (item->file, item->root, naming));
        }

        if (target.skip)
        {
            finishFile (FileStatus::skipped, 0, {}, target.reason);
            next();
            return;
        }

        // Sample rate (4.4): switch the device when it supports the file's rate. Consecutive
        // files of one rate need no switch, so each group of them costs one reopen at most.
        auto status = audio.getDeviceStatus();
        const auto fileRate = item->info.sampleRate;

        if (status.isOpen && ! juce::approximatelyEqual (status.config.sampleRate, fileRate)
            && status.sampleRates.contains (fileRate))
        {
            views.statusBar.setMessage ("Switching the device to " + format::sampleRate (fileRate) + ellipsis());
            audio.switchSampleRate (fileRate);
            status = audio.getDeviceStatus();
        }

        if (! status.isOpen || ! audio.checkCanRecord())
        {
            pauseBecause ("Paused: the audio device is not ready (" + views.statusBar.getMessage() + ")", Tone::warning);
            return;
        }

        tree.setResult (currentId, FileStatus::recording, 0.0, 0, {}, {});
        views.fileList.setBatchState (currentId, true);
        latencyNote = {};

        const auto channel = channelIndex (*item);
        views.waveform.setSource (item->file, item->info.numChannels, fileRate, item->info.lengthInSamples, channel);
        views.waveform.clearRecorded();
        views.waveform.setPlayhead (0.0);
        lastProgress = -1.0;

        currentRequest = makeRequest (*item, status.config.sampleRate);
        phase = Phase::loading;
        updateStatusLine();

        if (preloaded != nullptr && sameRequest (preloadedRequest, currentRequest))
        {
            auto source = std::move (preloaded);
            preloadedRequest = {};
            startTake (std::move (source));
            return;
        }

        if (loader.isLoading() && sameRequest (pendingRequest, currentRequest))
            return;   // the preload of this very file is still running; loaded() starts the take

        pendingRequest = currentRequest;
        loader.loadAsync (currentRequest, [this, request = currentRequest] (std::shared_ptr<const engine::LoadedSource> s, juce::String e)
        {
            loaded (request, std::move (s), e);
        });
    }

    void BatchController::loaded (const engine::LoadRequest& request, std::shared_ptr<const engine::LoadedSource> source,
                                  const juce::String& error)
    {
        pendingRequest = {};

        if (phase == Phase::loading && sameRequest (request, currentRequest))
        {
            if (source == nullptr)
            {
                finishFile (FileStatus::error, 0, {}, "could not load the source: " + error);
                next();
                return;
            }

            startTake (std::move (source));
            return;
        }

        // A preload for a later file.
        if (source != nullptr)
        {
            preloaded = std::move (source);
            preloadedRequest = request;
        }
    }

    void BatchController::preloadNext()
    {
        const auto* nextEntry = queue.peekNext();
        const auto* item = nextEntry != nullptr ? tree.find (nextEntry->id) : nullptr;

        if (item == nullptr)
            return;

        // Predict the device rate for it: its own rate if the device can run at it.
        const auto status = audio.getDeviceStatus();
        const auto request = makeRequest (*item, syncplan::deviceRateFor (item->info.sampleRate, status));

        if (preloaded != nullptr && sameRequest (request, preloadedRequest))
            return;

        pendingRequest = request;
        loader.loadAsync (request, [this, request] (std::shared_ptr<const engine::LoadedSource> s, juce::String e)
        {
            loaded (request, std::move (s), e);
        });
    }

    void BatchController::startTake (std::shared_ptr<const engine::LoadedSource> source)
    {
        const auto* item = tree.find (currentId);

        if (item == nullptr)
        {
            queue.finishCurrent (FileStatus::skipped);
            next();
            return;
        }

        takeStatus = audio.getDeviceStatus();

        // Latency (4.3, 3.6.4): the sync measurement stored for exactly the configuration this
        // take runs in (the batch may have switched the rate for this file: every rate is its
        // own key); without one, the driver-reported input + output latency as an estimate,
        // and the file says so (NC).
        takeSync = audio.findSync (takeStatus);
        const auto latency = syncplan::chooseLatency (takeStatus, takeSync);

        engine::TakeSpec spec;
        spec.source = std::move (source);
        spec.deviceRate = takeStatus.config.sampleRate;
        spec.latencySamples = latency.samples;
        spec.latencyMeasured = latency.measured;
        spec.tailSamples = (juce::int64) std::llround (tailMs * item->info.sampleRate / 1000.0);
        spec.outputFile = target.file;
        spec.bitsPerSample = bitsPerSample;
        spec.replaceExisting = target.overwrites;
        spec.onWritten = [&waveform = views.waveform] (juce::int64 start, const float* data, int n)
        {
            waveform.addRecordedSamples (start, data, n);   // writer thread
        };

        views.waveform.beginRecording (item->file, spec.source->fileSampleRate, engine::Take::getOutputLength (spec));
        xrunsAtStart = takeStatus.xrunCount;

        take = std::make_unique<engine::Take>();

        if (const auto error = take->start (spec, audio.getEngine(), writer); error.isNotEmpty())
        {
            take.reset();
            views.waveform.clearRecorded();
            finishFile (FileStatus::error, 0, {}, error);
            next();
            return;
        }

        phase = Phase::recording;
        preloadNext();
        updateStatusLine();
    }

    void BatchController::handleSnapshot (const engine::EngineSnapshot& snap)
    {
        if (check.active && checkTransport && exerciseTransport())
            return;   // the batch changed state; the snapshot belongs to the previous take

        if (phase != Phase::recording || take == nullptr)
        {
            if (phase == Phase::loading)
                updateStatusLine();

            return;
        }

        const auto state = take->update (snap);
        const auto progress = take->getProgress();

        if (progress - lastProgress >= 0.004 || state == engine::Take::Phase::done)
        {
            lastProgress = progress;
            tree.setProgress (currentId, progress);
        }

        views.waveform.setPlayhead (take->getPlayheadSeconds());
        updateStatusLine();

        if (state == engine::Take::Phase::done)
            takeDone();
    }

    void BatchController::takeDone()
    {
        const auto r = take->getResult();
        const auto spec = take->getSpec();
        const auto now = audio.getDeviceStatus();
        const auto xruns = (xrunsAtStart >= 0 && now.xrunCount > xrunsAtStart) ? now.xrunCount - xrunsAtStart : 0;

        juce::uint32 warnings = 0;

        if (r.notCalibrated)                     warnings |= Warning::notCalibrated;
        if (r.resampled)                         warnings |= Warning::resampled;
        if (r.hadDropout() || xruns > 0)         warnings |= Warning::dropout;
        if (r.ok && r.silent)                    warnings |= Warning::silence;
        if (r.ok && r.clipped)                   warnings |= Warning::clipped;

        const auto fileRate = spec.source->fileSampleRate;

        juce::StringArray details;
        details.add ((r.ok ? juce::String ("Done") : "Error: " + r.error)
                     + dot() + juce::String (r.length) + " samples at " + format::sampleRate (fileRate)
                     + (spec.tailSamples > 0 ? " (tail " + juce::String (spec.tailSamples) + ")" : juce::String())
                     + dot() + "device " + format::sampleRate (spec.deviceRate) + ", buffer " + juce::String (takeStatus.config.bufferSize)
                     + dot() + "gain " + formatDb (audio.getEngine().getGainDb()) + " dB"
                     + dot() + "latency " + juce::String (spec.latencySamples) + " smp ("
                     + (spec.latencyMeasured && takeSync.has_value()
                            ? "measured: " + format::milliseconds (takeSync->ms) + ", confidence " + engine::toString (takeSync->confidence)
                              + ", synced " + format::dateTime (takeSync->date) + "; driver reports in "
                              + juce::String (takeStatus.inputLatencySamples) + " + out " + juce::String (takeStatus.outputLatencySamples)
                            : "estimated: driver in " + juce::String (takeStatus.inputLatencySamples) + " + out "
                              + juce::String (takeStatus.outputLatencySamples) + ", not calibrated")
                     + ")" + dot() + "peak " + juce::String (juce::Decibels::gainToDecibels (r.peak, -120.0f), 1) + " dBFS");

        latencyNote = "latency " + juce::String (spec.latencySamples) + " smp " + (spec.latencyMeasured ? "measured" : "estimated");

        if (r.hadDropout() || xruns > 0)
            details.add ("Dropouts: " + juce::String (xruns) + " driver xruns, " + juce::String (r.callbackGaps) + " callback gaps, "
                         + juce::String (r.droppedSamples) + " samples lost to a full record buffer, "
                         + juce::String (r.paddedSamples) + " samples padded");

        processedSeconds += (double) r.length / fileRate;
        check.deviceRates[currentId] = spec.deviceRate;
        take.reset();
        phase = Phase::idle;

        if (r.ok)
            finishFile (FileStatus::done, warnings, r.file, {}, details);
        else
            finishFile (FileStatus::error, warnings, {}, r.error, details);

        next();
    }

    void BatchController::finishFile (FileStatus status, juce::uint32 warnings, const juce::File& output,
                                      const juce::String& note, const juce::StringArray& details)
    {
        const auto* item = tree.find (currentId);
        const auto index = queue.getCurrentIndex() + 1;

        queue.finishCurrent (status);

        switch (status)
        {
            case FileStatus::done:    ++numDone; break;
            case FileStatus::skipped: ++numSkipped; break;
            case FileStatus::error:   ++numErrors; break;
            case FileStatus::queued:
            case FileStatus::recording: break;
        }

        if (warnings != 0)
            ++numWithWarnings;

        if (item == nullptr)
            return;

        const auto sourceFile = item->file;
        const auto channel = channelText (*item);
        const auto rate = item->info.sampleRate;

        tree.setResult (currentId, status, status == FileStatus::done ? 1.0 : 0.0, warnings, output, note);
        check.handled.push_back (currentId);

        juce::StringArray lines;
        lines.add ("[" + juce::String (index) + "/" + juce::String (queue.getTotal()) + "] " + sourceFile.getFullPathName()
                   + " (" + channel + ")" + arrow()
                   + (output != juce::File() ? output.getFullPathName() : model::toString (status) + (note.isNotEmpty() ? ": " + note : "")));

        for (const auto& d : details)
            lines.add ("      " + d);

        if (details.isEmpty() && note.isNotEmpty())
            lines.add ("      " + model::toString (status) + ": " + note);

        if (warnings != 0)
            lines.add ("      Warnings: " + Warning::describeAll (warnings, "; "));

        log.append (lines);

        if (check.active)
        {
            juce::String codes;

            for (auto flag : Warning::all)
                if ((warnings & flag) != 0)
                    codes << (codes.isEmpty() ? "" : ",") << Warning::code (flag);

            printCheck (juce::String (index) + "/" + juce::String (queue.getTotal()) + " " + sourceFile.getFileName()
                        + " (" + channel + ", " + format::sampleRate (rate) + "): " + model::toString (status)
                        + (output != juce::File() ? arrow() + output.getFullPathName() : juce::String())
                        + (note.isNotEmpty() ? " (" + note + ")" : juce::String())
                        + (codes.isNotEmpty() ? " [" + codes + "]" : juce::String())
                        + (latencyNote.isNotEmpty() ? ", " + latencyNote : juce::String()));
        }
    }

    //==============================================================================
    void BatchController::openLog (const juce::File& folder)
    {
        const auto status = audio.getDeviceStatus();
        const auto& c = audio.getSelectedConfig();

        juce::StringArray header;
        header.add ("Reamp Rig " + appVersion() + " batch log");
        header.add ("Started      " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H:%M:%S"));
        header.add ("Device       " + c.typeName + dot() + audio.describeDevice (status)
                    + (c.isSplit() ? " (separate input and output devices: not sample-synchronized)" : juce::String()));
        header.add ("Output       " + c.outputChannelName + " (channel " + juce::String (c.outputChannel + 1) + ")" + dot()
                    + "level " + formatDb (audio.getEngine().getGainDb()) + " dB");
        header.add ("Input        " + c.inputChannelName + " (channel " + juce::String (c.inputChannel + 1) + ")");
        // One line per configuration the batch runs in: measured by Sync, or estimated.
        for (size_t i = 0; i < plannedKeys.size(); ++i)
        {
            const auto& k = plannedKeys[i];
            const auto m = audio.findSync (k);
            const auto text = m.has_value()
                ? "measured " + format::latency (m->samples, m->ms) + " at " + audio.describeKey (k) + " (Sync, confidence "
                  + engine::toString (m->confidence) + ", returned " + juce::String (m->returnedPeakDb, 1) + " dBFS, "
                  + format::dateTime (m->date) + ")"
                : "estimated at " + audio.describeKey (k) + ": driver-reported input + output latency, not calibrated "
                  "(no sync measurement; files marked NC)";

            header.add ((i == 0 ? "Latency      " : "             ") + text);
        }

        header.add ("             driver reports in " + juce::String (status.inputLatencySamples) + " + out "
                    + juce::String (status.outputLatencySamples) + " smp at the start; latency used per file below");
        header.add ("Format       WAV " + (bitsPerSample == 32 ? juce::String ("32-bit float") : juce::String (bitsPerSample) + "-bit")
                    + " at each source's sample rate" + dot() + "tail " + juce::String (tailMs) + " ms");
        header.add ("Destination  " + (naming.mode == model::DestinationMode::besideSource
                                          ? "subfolder \"" + naming.subfolderName + "\" next to each source"
                                          : naming.outputFolder.getFullPathName() + (naming.mirrorStructure ? " (mirrored folders)" : ""))
                    + dot() + "collisions: " + describePolicy (naming.collision));
        header.add ("Naming       prefix \"" + naming.prefix + "\", suffix \"" + naming.suffix + "\", channel tag "
                    + (naming.channelTag ? "on" : "off"));

        if (log.open (folder, header))
            juce::Logger::writeToLog ("Batch log: " + log.getFile().getFullPathName());
    }

    void BatchController::finishBatch (bool stopped)
    {
        take.reset();
        loader.cancel();
        preloaded.reset();
        phase = Phase::idle;
        currentId = 0;

        views.fileList.setBatchState (0, false);
        views.waveform.setPlayhead (std::nullopt);

        audio.restoreConfig (startConfig);
        audio.setBatchActive (false);
        options.setEnabled (true);
        updateTransport();

        const auto summary = juce::String (numDone) + " done, " + juce::String (numSkipped) + " skipped, "
                           + juce::String (numErrors) + (numErrors == 1 ? " error" : " errors")
                           + (numWithWarnings > 0 ? dot() + juce::String (numWithWarnings) + " with warnings" : juce::String());

        log.close ({ juce::String (stopped ? "Stopped      " : "Finished     ")
                     + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H:%M:%S") + dot() + summary });

        const auto hasProblems = numErrors > 0 || numWithWarnings > 0;
        views.statusBar.setMessage ((stopped ? "Batch stopped: " : "Batch finished: ") + summary
                                        + (log.isOpen() ? dot() + "log: " + log.getFile().getFileName() : juce::String()),
                                    numErrors > 0 ? Tone::error : (hasProblems ? Tone::warning : Tone::normal),
                                    log.isOpen() ? log.getFile().getFullPathName() : juce::String());

        juce::Logger::writeToLog (juce::String ("Batch ") + (stopped ? "stopped: " : "finished: ") + summary);

        if (onFinished != nullptr)
            onFinished();

        if (check.active)
        {
            check.active = false;
            auto ok = ! stopped && numErrors == 0 && numSkipped == 0 && numDone > 0;

            if (checkTransport)
            {
                const auto ids = check.handled;
                auto statusOf = [this] (int listIndex)
                {
                    const auto all = queue.getEntries();
                    const auto* item = juce::isPositiveAndBelow (listIndex, (int) all.size()) ? tree.find (all[(size_t) listIndex].id) : nullptr;
                    return item != nullptr ? item->status : FileStatus::error;
                };

                auto tempsLeft = 0;

                for (const auto& f : naming.outputFolder.findChildFiles (juce::File::findFiles, true, "*.reamprig-part.wav",
                                                                         juce::File::FollowSymlinks::no))
                    tempsLeft += f.exists() ? 1 : 0;

                ok = stopped && transportStage == 4 && numDone == 3 && numSkipped == 1 && numErrors == 0
                  && statusOf (0) == FileStatus::done && statusOf (1) == FileStatus::done && statusOf (2) == FileStatus::skipped
                  && statusOf (3) == FileStatus::done && statusOf (4) == FileStatus::queued && tempsLeft == 0;

                printCheck ("transport: stopped " + juce::String (stopped ? "yes" : "no") + ", " + juce::String (numDone) + " done, "
                            + juce::String (numSkipped) + " skipped, file 5 " + model::toString (statusOf (4))
                            + ", temp files left " + juce::String (tempsLeft));
            }

            if (log.isOpen())
                printCheck ("sidecar log: " + log.getFile().getFullPathName());

            ok = verifyOutputs() && ok;
            printCheck (ok ? "PASS" : "FAIL");

            if (auto done = std::move (check.onDone); done != nullptr)
                done (ok);
        }
    }

    //==============================================================================
    double BatchController::getElapsedActiveSeconds() const
    {
        const auto now = juce::Time::getMillisecondCounterHiRes();
        const auto pausedNow = phase == Phase::paused ? now - pausedSinceMs : 0.0;
        return juce::jmax (0.0, (now - startedMs - pausedTotalMs - pausedNow) / 1000.0);
    }

    void BatchController::updateStatusLine()
    {
        const auto* item = tree.find (currentId);

        if (item == nullptr || ! queue.isActive() || phase == Phase::paused)
            return;

        const auto position = "File " + juce::String (queue.getCurrentIndex() + 1) + " of " + juce::String (queue.getTotal());

        if (phase == Phase::loading || take == nullptr)
        {
            views.statusBar.setMessage (position + dash() + "loading " + item->file.getFileName() + ellipsis());
            return;
        }

        const auto elapsed = take->getPlayheadSeconds();
        const auto duration = item->info.getDurationSeconds();

        // ETA: the audio still to record, at the pace measured so far (includes loading,
        // rate switches and writing); before the first file finishes, real time.
        const auto spec = take->getSpec();
        const auto extra = tailMs / 1000.0 + (double) spec.latencySamples / juce::jmax (1.0, spec.deviceRate);
        const auto remainingAudio = queue.getRemainingSeconds (extra, take->getProgress() * (duration + extra));
        const auto activeSeconds = getElapsedActiveSeconds();
        const auto pace = processedSeconds > 0.5 && activeSeconds > 0.5 ? processedSeconds / activeSeconds : 1.0;

        views.statusBar.setMessage (position + dash() + clock (elapsed) + " / " + clock (duration)
                                    + dash() + "ETA " + clock (remainingAudio / juce::jmax (0.01, pace)),
                                    Tone::normal,
                                    item->file.getFullPathName() + arrow() + target.file.getFullPathName());
    }

    void BatchController::updateTransport()
    {
        using T = ui::TopBar::Transport;
        const auto state = queue.getState();
        views.topBar.setTransport (state == model::BatchQueue::State::running ? T::running
                                 : state == model::BatchQueue::State::paused  ? T::paused
                                                                               : T::idle);
        views.fileList.setBatchState (currentId, queue.isActive());
    }

    //==============================================================================
    void BatchController::fileTreeChanged()
    {
        // The current file may have been removed from the list; handled outside this
        // notification (the batch changes the list itself).
        if (queue.isActive() && currentId != 0)
            triggerAsyncUpdate();
    }

    void BatchController::handleAsyncUpdate()
    {
        if (! queue.isActive() || currentId == 0 || tree.find (currentId) != nullptr)
            return;

        cancelCurrent();
        queue.finishCurrent (FileStatus::skipped);
        ++numSkipped;
        log.append ({ "[" + juce::String (queue.getCurrentIndex() + 1) + "/" + juce::String (queue.getTotal())
                      + "] removed from the list during the batch; skipped" });

        if (queue.getState() == model::BatchQueue::State::running)
            next();
    }

    //==============================================================================
    void BatchController::runCheck (bool snapshotMidway, bool verifyContent, std::function<void (bool)> onDone)
    {
        check = {};
        check.active = true;
        check.snapshotMidway = snapshotMidway;
        check.verifyContent = verifyContent;
        check.onDone = std::move (onDone);

        printCheck ("device: " + audio.describeDevice (audio.getDeviceStatus()) + " | output " + audio.getSelectedConfig().outputChannelName
                    + " | input " + audio.getSelectedConfig().inputChannelName + " | level "
                    + formatDb (audio.getEngine().getGainDb()) + " dB | destination "
                    + options.getNamingOptions().outputFolder.getFullPathName());

        // The check never waits for a click: an unsynced configuration counts as confirmed.
        const auto keys = getPlannedKeys();

        for (const auto& k : keys)
        {
            const auto m = audio.findSync (k);
            printCheck ("sync " + audio.describeKey (k) + ": "
                        + (m.has_value() ? "measured " + format::latency (m->samples, m->ms) + ", confidence " + engine::toString (m->confidence)
                                         : juce::String ("not synced (started anyway: driver estimate, files marked NC)")));
        }

        start (true);

        if (! queue.isActive() && check.active)
        {
            printCheck ("FAIL: the batch did not start: " + views.statusBar.getMessage());
            check.active = false;

            if (auto done = std::move (check.onDone); done != nullptr)
                done (false);
        }
    }

    bool BatchController::exerciseTransport()
    {
        const auto index = queue.getCurrentIndex();   // 0-based
        const auto midTake = phase == Phase::recording && take != nullptr && take->getProgress() >= 0.3;

        if (transportStage == 0 && index == 1 && midTake)
        {
            printCheck ("transport: Pause during file 2");
            pauseResume();
            resumeAtMs = juce::Time::getMillisecondCounterHiRes() + 1000.0;
            transportStage = 1;
            return true;
        }
        else if (transportStage == 1 && juce::Time::getMillisecondCounterHiRes() >= resumeAtMs)
        {
            printCheck ("transport: Resume (file 2 is recorded again from its start)");
            pauseResume();
            transportStage = 2;
            return true;
        }
        else if (transportStage == 2 && index == 2 && midTake)
        {
            printCheck ("transport: Skip file 3");
            skipCurrent();
            transportStage = 3;
            return true;
        }
        else if (transportStage == 3 && index == 4 && midTake)
        {
            printCheck ("transport: Stop during file 5");
            transportStage = 4;
            stop();
            return true;
        }

        return false;
    }

    bool BatchController::isWaitingForSnapshot() const
    {
        if (! check.active || ! check.snapshotMidway)
            return false;

        const auto middle = juce::jmax (1, queue.getTotal() / 2);
        return ! (queue.getCurrentIndex() >= middle && take != nullptr && take->getProgress() >= 0.45);
    }

    void BatchController::printCheck (const juce::String& text) const
    {
        std::fprintf (stderr, "[batch-check] %s\n", text.toRawUTF8());
        std::fflush (stderr);
    }

    bool BatchController::verifyOutputs()
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        const auto gain = juce::Decibels::decibelsToGain (audio.getEngine().getGainDb());
        auto allOk = true;

        for (const auto id : check.handled)
        {
            const auto* item = tree.find (id);

            if (item == nullptr || item->status != FileStatus::done)
                continue;

            std::unique_ptr<juce::AudioFormatReader> src (formats.createReaderFor (item->file));
            std::unique_ptr<juce::AudioFormatReader> out (formats.createReaderFor (item->outputFile));

            if (src == nullptr || out == nullptr)
            {
                printCheck ("verify " + item->file.getFileName() + ": cannot read the source or the output");
                allOk = false;
                continue;
            }

            const auto tail = (juce::int64) std::llround (tailMs * src->sampleRate / 1000.0);
            const auto expectedLength = src->lengthInSamples + tail;
            const auto lengthOk = out->lengthInSamples == expectedLength && out->numChannels == 1
                                && juce::approximatelyEqual (out->sampleRate, src->sampleRate);

            auto line = "verify " + item->outputFile.getFileName() + ": " + juce::String (out->lengthInSamples) + " samples (expected "
                      + juce::String (expectedLength) + "), " + format::sampleRate (out->sampleRate) + ", "
                      + juce::String ((int) out->bitsPerSample) + "-bit";

            auto ok = lengthOk;

            if (check.verifyContent && src->lengthInSamples < std::numeric_limits<int>::max())
            {
                const auto n = (int) src->lengthInSamples;
                juce::AudioBuffer<float> a ((int) src->numChannels, n), b (1, n);
                src->read (&a, 0, n, 0, true, true);
                out->read (&b, 0, (int) juce::jmin ((juce::int64) n, out->lengthInSamples), 0, true, false);

                const auto ch = channelIndex (*item);
                const auto resampled = (item->warnings & Warning::resampled) != 0;
                const auto oneLsb = std::ldexp (1.0f, -(int) (out->bitsPerSample - 1));

                auto maxError = [&] (const float* reference)
                {
                    auto worst = 0.0f;

                    for (int i = 0; i < n; ++i)
                        worst = juce::jmax (worst, std::abs (b.getSample (0, i) - reference[i] * gain));

                    return worst;
                };

                auto describe = [] (float e)
                {
                    return juce::exactlyEqual (e, 0.0f) ? juce::String ("bit-exact")
                                                        : "max error " + juce::String (juce::Decibels::gainToDecibels (e, -400.0f), 1) + " dBFS";
                };

                const auto fromSource = maxError (a.getReadPointer (ch));

                if (! resampled)
                {
                    line << ", " << describe (fromSource) << " vs the source channel";
                    ok = ok && fromSource <= oneLsb;
                }
                else
                {
                    // The take went file rate -> device rate -> file rate. Its exact expectation
                    // is the source through the same two conversions offline; the difference to
                    // the raw source is what band-limiting removed (hard edges in the file).
                    const auto devRate = check.deviceRates.count (id) > 0 ? check.deviceRates[id] : 0.0;
                    const auto midLength = engine::Resampler::getOutputLength (n, src->sampleRate, devRate);
                    std::vector<float> mid ((size_t) midLength), back ((size_t) n);
                    engine::Resampler (src->sampleRate, devRate).process (a.getReadPointer (ch), n, mid.data(), 0, midLength);
                    engine::Resampler (devRate, src->sampleRate).process (mid.data(), midLength, back.data(), 0, n);

                    const auto fromReference = maxError (back.data());
                    line << ", resampled via " << format::sampleRate (devRate) << ": " << describe (fromReference)
                         << " vs the offline resampled reference (" << describe (fromSource) << " vs the raw source)";
                    ok = ok && juce::Decibels::gainToDecibels (fromReference, -400.0f) < resampledErrorLimitDb;
                }
            }

            printCheck (line + (ok ? "" : "  <-- FAIL"));
            allOk = allOk && ok;
        }

        return allOk;
    }
}
