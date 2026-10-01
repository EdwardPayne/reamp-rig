#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <map>

#include "../Engine/FileWriter.h"
#include "../Engine/SourceLoader.h"
#include "../Engine/Take.h"
#include "../Model/BatchQueue.h"
#include "../Model/FileTree.h"
#include "../UI/FileTreeView.h"
#include "../UI/StatusBar.h"
#include "../UI/TopBar.h"
#include "../UI/WaveformPanel.h"
#include "AudioController.h"
#include "BatchLog.h"
#include "OutputOptions.h"

namespace rf::app
{
    /*  Runs a batch (PROMPT.md 3.3, 3.4, 4.3, 4.4) on the message thread:

        - Start: refuses without an open input (the same status-bar message as elsewhere, e.g.
          microphone access denied), with invalid destination options, or with nothing
          queued. Stops audition and locks the AUDIO, DESTINATION and OPTIONS controls.
        - Per file, in list order (model::BatchQueue): resolves the output name
          (model::OutputNaming, collision policy), switches the device to the file's sample
          rate when the device supports it (only when the rate changes, so consecutive files
          of one rate form a group) or else plays a resampled source and resamples the
          recording back ("resampled" warning), loads the played channel on the batch's own
          SourceLoader (the next file is preloaded there while the current one records), and
          runs an engine::Take with the latency estimate (driver-reported input + output
          latency, "not calibrated" until phase 5 measures it).
        - Progress: row status and progress bar, the highlighted current row, the batch
          playhead and the live recorded lane in the waveform panel, and the status line
          "File 7 of 23 — 00:12 / 01:03 — ETA 14:20" (ETA = remaining audio at the speed
          measured so far).
        - After each take: Done / Error with warnings (not calibrated, resampled, dropout from
          xruns, callback gaps or FIFO overflow, silence, clipping), one sidecar-log entry.
        - Pause discards the current take and redoes that file from its start on Resume; Skip
          marks it Skipped; Stop discards it and leaves it Queued. A device that stops pauses
          the batch. At the end the device's original sample rate is restored.

        It drives the engine only through AudioController and engine::Take.
    */
    class BatchController final : private model::FileTree::Listener,
                                  private juce::AsyncUpdater
    {
    public:
        struct Views
        {
            ui::TopBar& topBar;
            ui::StatusBar& statusBar;
            ui::WaveformPanel& waveform;
            ui::FileTreeView& fileList;
        };

        BatchController (AudioController&, model::FileTree&, OutputOptions&, Views);
        ~BatchController() override;

        void start();
        void pauseResume();
        void skipCurrent();
        void stop();

        /** Queues again every Done file with a dropout, silence or clipping warning. */
        void redoFilesWithWarnings();

        bool isActive() const noexcept                  { return queue.isActive(); }
        model::ItemId getCurrentItem() const noexcept   { return currentId; }

        /** Called when a batch ends (finished or stopped), after the controls are unlocked. */
        std::function<void()> onFinished;

        //==============================================================================
        /** Development aid (--batch-check): starts the batch, prints one line per file and a
            verification of every output (length, and content against the source) to
            stderr, and reports success. With `snapshotMidway` it only runs until
            isWaitingForSnapshot() turns false (a file in the middle half recorded). */
        void runCheck (bool snapshotMidway, bool verifyContent, std::function<void (bool ok)> onDone);

        /** --batch-check-transport: during the check, pause file 2 at 30 % and resume it a
            second later (it must be redone from its start), skip file 3 at 30 % and stop
            during file 5 at 30 %. Expected: 1, 2, 4 Done and exact, 3 Skipped, 5 and later
            Queued, no temp files left. */
        void setCheckTransport (bool shouldExercise)     { checkTransport = shouldExercise; }

        /** --batch-check with --snapshot: true until the batch is mid-way. */
        bool isWaitingForSnapshot() const;

    private:
        enum class Phase { idle, loading, recording, paused };

        void fileTreeChanged() override;
        void handleAsyncUpdate() override;

        void next();
        void beginFile();
        void loaded (const engine::LoadRequest&, std::shared_ptr<const engine::LoadedSource>, const juce::String& error);
        void startTake (std::shared_ptr<const engine::LoadedSource>);
        void takeDone();
        void finishFile (model::FileStatus, juce::uint32 warnings, const juce::File& output, const juce::String& note,
                         const juce::StringArray& details = {});
        void preloadNext();
        void cancelCurrent();
        void pauseBecause (const juce::String& message, ui::StatusBar::Tone);
        void finishBatch (bool stopped);
        void handleSnapshot (const engine::EngineSnapshot&);
        void updateStatusLine();
        void updateTransport();
        void openLog (const juce::File& folder);
        double getElapsedActiveSeconds() const;
        void printCheck (const juce::String&) const;
        bool exerciseTransport();
        bool verifyOutputs();

        engine::LoadRequest makeRequest (const model::FileItem&, double deviceRate) const;

        AudioController& audio;
        model::FileTree& tree;
        OutputOptions& options;
        Views views;

        model::BatchQueue queue;
        engine::SourceLoader loader;        // the batch's own loader: current file + preloading
        engine::FileWriter writer;
        std::unique_ptr<engine::Take> take;

        Phase phase = Phase::idle;
        model::ItemId currentId = 0;
        engine::LoadRequest currentRequest, pendingRequest, preloadedRequest;
        std::shared_ptr<const engine::LoadedSource> preloaded;
        model::OutputNaming::Target target;
        engine::DeviceStatus takeStatus;    // device state when the take started
        int xrunsAtStart = -1;
        double lastProgress = -1.0;

        // Batch-wide
        model::NamingOptions naming;
        int bitsPerSample = 24, tailMs = 0;
        engine::DeviceConfig startConfig;
        BatchLog log;
        bool logTried = false;
        double startedMs = 0.0, pausedSinceMs = 0.0, pausedTotalMs = 0.0;
        double processedSeconds = 0.0;      // audio of the files finished so far
        int numDone = 0, numSkipped = 0, numErrors = 0, numWithWarnings = 0;

        struct Check
        {
            bool active = false, snapshotMidway = false, verifyContent = true;
            std::function<void (bool)> onDone;
            std::vector<model::ItemId> handled;
            std::map<model::ItemId, double> deviceRates;   // device rate each take ran at
        };

        Check check;
        bool checkTransport = false;
        int transportStage = 0;
        double resumeAtMs = 0.0;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BatchController)
    };
}
